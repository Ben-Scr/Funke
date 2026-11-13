#include "pch.hpp"
#include "Gizmo.hpp"
#include <glm/gtc/quaternion.hpp>

namespace Funke {
	std::vector<Square> Gizmo::s_Squares;
	std::vector<Circle> Gizmo::s_Circles;
	std::vector<Line> Gizmo::s_Lines;
	std::vector<Line3D> Gizmo::s_Lines3D;
	std::vector<ThickLine> Gizmo::s_ThickLines;
	std::vector<Square> Gizmo::s_FilledSquares;
	std::vector<Circle> Gizmo::s_FilledCircles;
	std::vector<GizmoText> Gizmo::s_Texts;
	// Per-frame RENDER budget (cost: line=1, square=4, circle=segments). NOT enforced here at
	// draw time — it is applied at BUILD time (GizmoRenderer) to the VISIBLE gizmos only, so
	// off-screen gizmos (frustum-culled before counting) don't consume it. Each view is
	// budgeted independently. Accumulation has only the hard OOM guard k_GizmoAccumCap below.
	size_t Gizmo::s_MaxVertices = 100000;
	size_t Gizmo::s_RegisteredVertices = 0;
	float Gizmo::s_LineWidth = 1.0f;
	AABB Gizmo::s_CamViewportAABB{};
	bool Gizmo::s_HasViewportOverride = false;

	bool Gizmo::s_IsEnabled = true;
	bool Gizmo::s_ShowInRuntime = true;
	GizmoLayer Gizmo::s_Layer = GizmoLayer::Shared;
	Color Gizmo::s_Color = { 0.f, 1.f, 0.f, 1.f };

	namespace {
		struct RefreshBase {
			size_t Squares = 0;
			size_t Circles = 0;
			size_t Lines = 0;
			size_t Lines3D = 0;
			size_t ThickLines = 0;
			size_t FilledSquares = 0;
			size_t FilledCircles = 0;
			size_t Texts = 0;
			size_t RegisteredVertices = 0;
		};

		RefreshBase s_RefreshBase;

		template<typename T>
		void DropRefreshBase(std::vector<T>& commands, size_t count) {
			count = Min(count, commands.size());
			if (count == commands.size()) {
				commands.clear();
				return;
			}
			commands.erase(commands.begin(), commands.begin() + count);
		}

		template<typename T>
		void KeepRefreshBase(std::vector<T>& commands, size_t count) {
			commands.resize(Min(count, commands.size()));
		}

		// Hard per-frame accumulation guard (OOM protection only — NOT the render budget,
		// which is applied per-view at build time). Reachable only by a pathological script
		// drawing an extreme number of gizmos in a single frame.
		constexpr size_t k_GizmoAccumCap = 8000000;
		bool IsFinite(const Vec3& value) {
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
		bool ExceedsGizmoAccumCap(size_t registered, size_t cost) {
			if (registered + cost < k_GizmoAccumCap) return false;
			static bool s_Warned = false;
			if (!s_Warned) {
				s_Warned = true;
				FNK_CORE_WARN_TAG("Gizmo",
					"Per-frame gizmo accumulation cap ({}) hit — an extreme number of gizmos were "
					"drawn this frame; further draws were dropped.", k_GizmoAccumCap);
			}
			return true;
		}
	}

	void Gizmo::DrawWireCircle(const Vec2& center, float radius, int segments) {
		// Script-supplied: a negative count added to the size_t vertex counter wraps it and drops every later gizmo.
		segments = std::max(segments, 3);
		if (!IsCurrentLayerEnabled() || ExceedsGizmoAccumCap(s_RegisteredVertices, segments))
			return;

		s_RegisteredVertices += segments;
		s_Circles.emplace_back(Circle{ center, radius, segments, s_Color, s_Layer });
	}

	void Gizmo::DrawWireSquare(const Vec2& center, const Vec2& scale, float degrees) {
		if (!IsCurrentLayerEnabled() || ExceedsGizmoAccumCap(s_RegisteredVertices, k_BoxVertices))
			return;

		s_RegisteredVertices += k_BoxVertices;
		s_Squares.emplace_back(Square{ center, scale / 2.f, Radians<float>(degrees), s_Color, s_Layer });
	}

	void Gizmo::DrawCircle(const Vec2& center, float radius, int segments) {
		// Script-supplied: a negative count added to the size_t vertex counter wraps it and drops every later gizmo.
		segments = std::max(segments, 3);
		if (!IsCurrentLayerEnabled() || ExceedsGizmoAccumCap(s_RegisteredVertices, segments))
			return;

		s_RegisteredVertices += segments;
		s_FilledCircles.emplace_back(Circle{ center, radius, segments, s_Color, s_Layer });
	}

	void Gizmo::DrawSquare(const Vec2& center, const Vec2& scale, float degrees) {
		if (!IsCurrentLayerEnabled() || ExceedsGizmoAccumCap(s_RegisteredVertices, k_BoxVertices))
			return;

		s_RegisteredVertices += k_BoxVertices;
		s_FilledSquares.emplace_back(Square{ center, scale / 2.f, Radians<float>(degrees), s_Color, s_Layer });
	}

	void Gizmo::DrawText(const std::string& text, const Vec2& position, float rotation, float size) {
		if (!IsCurrentLayerEnabled() || text.empty())
			return;
		// Budget proxy: one "vertex" per character (a glyph is a quad; this just bounds runaway text).
		const size_t cost = text.size();
		if (ExceedsGizmoAccumCap(s_RegisteredVertices, cost))
			return;

		s_RegisteredVertices += cost;
		s_Texts.emplace_back(GizmoText{ position, Radians<float>(rotation), size, text, s_Color, s_Layer });
	}

	void Gizmo::DrawLine(const Vec2& start, const Vec2& end) {
		if (!IsCurrentLayerEnabled() || ExceedsGizmoAccumCap(s_RegisteredVertices, k_LineVertices))
			return;

		s_RegisteredVertices += k_LineVertices;
		s_Lines.emplace_back(Line{ start, end, s_Color, s_Layer });
	}

	void Gizmo::DrawLine(const Vec3& start, const Vec3& end) {
		if (!IsCurrentLayerEnabled() || !IsFinite(start) || !IsFinite(end)
			|| ExceedsGizmoAccumCap(s_RegisteredVertices, 1)) return;
		++s_RegisteredVertices;
		s_Lines3D.push_back({ start, end, s_Color, s_Layer });
	}

	void Gizmo::DrawBox(const Vec3& center, const Vec3& size, const Vec3& rotation) {
		if (!IsCurrentLayerEnabled() || !IsFinite(center) || !IsFinite(size) || !IsFinite(rotation)
			|| ExceedsGizmoAccumCap(s_RegisteredVertices, 12)) return;
		const glm::quat orientation(glm::radians(rotation));
		Vec3 corners[8];
		for (int i = 0; i < 8; ++i) {
			const Vec3 local((i & 1) ? size.x : -size.x,
				(i & 2) ? size.y : -size.y, (i & 4) ? size.z : -size.z);
			corners[i] = center + orientation * (local * 0.5f);
		}
		for (int i = 0; i < 8; ++i)
			for (int axis = 1; axis <= 4; axis <<= 1)
				if (!(i & axis)) DrawLine(corners[i], corners[i | axis]);
	}

	void Gizmo::DrawSphere(const Vec3& center, float radius, int segments) {
		segments = std::max(segments, 3);
		if (!IsCurrentLayerEnabled() || !IsFinite(center) || !std::isfinite(radius) || radius <= 0.0f
			|| ExceedsGizmoAccumCap(s_RegisteredVertices, std::min(size_t(segments), k_GizmoAccumCap) * 3)) return;
		for (int plane = 0; plane < 3; ++plane) {
			const int a = plane, b = (plane + 1) % 3;
			Vec3 first = center;
			first[a] += radius;
			Vec3 previous = first;
			for (int i = 1; i <= segments; ++i) {
				const float angle = float(i) / float(segments) * 6.28318530717958647692f;
				Vec3 point = center;
				point[a] += std::cos(angle) * radius;
				point[b] += std::sin(angle) * radius;
				if (i == segments) point = first;
				DrawLine(previous, point);
				previous = point;
			}
		}
	}

	void Gizmo::DrawThickLine(const Vec2& start, const Vec2& end, float width) {
		if (!IsCurrentLayerEnabled() || ExceedsGizmoAccumCap(s_RegisteredVertices, k_ThickLineVertices))
			return;

		s_RegisteredVertices += k_ThickLineVertices;
		s_ThickLines.emplace_back(ThickLine{ start, end, Max(0.0001f, width * 0.5f), s_Color, s_Layer });
	}

	void Gizmo::BeginCommandFrame() {
		DropRefreshBase(s_Squares, s_RefreshBase.Squares);
		DropRefreshBase(s_Circles, s_RefreshBase.Circles);
		DropRefreshBase(s_Lines, s_RefreshBase.Lines);
		DropRefreshBase(s_Lines3D, s_RefreshBase.Lines3D);
		DropRefreshBase(s_ThickLines, s_RefreshBase.ThickLines);
		DropRefreshBase(s_FilledSquares, s_RefreshBase.FilledSquares);
		DropRefreshBase(s_FilledCircles, s_RefreshBase.FilledCircles);
		DropRefreshBase(s_Texts, s_RefreshBase.Texts);
		s_RegisteredVertices = s_RegisteredVertices >= s_RefreshBase.RegisteredVertices
			? s_RegisteredVertices - s_RefreshBase.RegisteredVertices
			: 0;
		s_RefreshBase = {};
	}

	void Gizmo::CaptureRefreshBase() {
		s_RefreshBase.Squares = s_Squares.size();
		s_RefreshBase.Circles = s_Circles.size();
		s_RefreshBase.Lines = s_Lines.size();
		s_RefreshBase.Lines3D = s_Lines3D.size();
		s_RefreshBase.ThickLines = s_ThickLines.size();
		s_RefreshBase.FilledSquares = s_FilledSquares.size();
		s_RefreshBase.FilledCircles = s_FilledCircles.size();
		s_RefreshBase.Texts = s_Texts.size();
		s_RefreshBase.RegisteredVertices = s_RegisteredVertices;
	}

	void Gizmo::EndCommandFrame() {
		KeepRefreshBase(s_Squares, s_RefreshBase.Squares);
		KeepRefreshBase(s_Circles, s_RefreshBase.Circles);
		KeepRefreshBase(s_Lines, s_RefreshBase.Lines);
		KeepRefreshBase(s_Lines3D, s_RefreshBase.Lines3D);
		KeepRefreshBase(s_ThickLines, s_RefreshBase.ThickLines);
		KeepRefreshBase(s_FilledSquares, s_RefreshBase.FilledSquares);
		KeepRefreshBase(s_FilledCircles, s_RefreshBase.FilledCircles);
		KeepRefreshBase(s_Texts, s_RefreshBase.Texts);
		s_RegisteredVertices = Min(s_RegisteredVertices, s_RefreshBase.RegisteredVertices);
	}

	void Gizmo::Clear() {
		s_Squares.clear();
		s_Lines.clear();
		s_Lines3D.clear();
		s_ThickLines.clear();
		s_Circles.clear();
		s_FilledSquares.clear();
		s_FilledCircles.clear();
		s_Texts.clear();
		s_RegisteredVertices = 0;
		s_RefreshBase = {};
	}
}
