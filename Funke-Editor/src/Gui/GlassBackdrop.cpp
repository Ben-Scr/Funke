#include <pch.hpp>
#include "Gui/GlassBackdrop.hpp"

#include "Core/Platform/Win32Titlebar.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Funke::GlassBackdrop {

	namespace {
		struct Blob {
			ImVec2 Center; // main-viewport UV
			ImVec2 Drift;  // UV amplitude
			ImVec2 Speed;  // rad/s; periods of one to two minutes keep the motion out of the way
			float Phase;
			float Radius;  // fraction of the main viewport's longer edge
			ImVec4 Color;
		};

		// Kept to the edges: the Editor View covers the middle of the main window.
		constexpr Blob k_Blobs[] = {
			{ { 0.16f, 0.20f }, { 0.10f, 0.08f }, { 0.071f, 0.053f }, 0.0f, 0.50f, { 0.23f, 0.42f, 1.00f, 0.82f } },
			{ { 0.84f, 0.18f }, { 0.08f, 0.10f }, { 0.047f, 0.083f }, 1.7f, 0.44f, { 0.54f, 0.30f, 1.00f, 0.72f } },
			{ { 0.66f, 0.88f }, { 0.12f, 0.06f }, { 0.061f, 0.039f }, 3.1f, 0.46f, { 0.10f, 0.76f, 0.79f, 0.58f } },
			{ { 0.22f, 0.84f }, { 0.09f, 0.07f }, { 0.043f, 0.067f }, 4.4f, 0.38f, { 0.91f, 0.31f, 0.56f, 0.48f } },
			// Funke amber, kept faint: the warm logo colours are for branding, not for anything interactive.
			{ { 0.97f, 0.98f }, { 0.05f, 0.05f }, { 0.057f, 0.049f }, 5.6f, 0.30f, { 1.00f, 0.59f, 0.34f, 0.32f } },
		};

		// Base wash corners: top-left, top-right, bottom-right, bottom-left.
		constexpr ImVec4 k_BaseCorners[4] = {
			{ 0.045f, 0.060f, 0.125f, 1.0f },
			{ 0.075f, 0.050f, 0.140f, 1.0f },
			{ 0.060f, 0.040f, 0.110f, 1.0f },
			{ 0.035f, 0.070f, 0.095f, 1.0f },
		};

		constexpr int k_Segments = 64;
		constexpr int k_Rings = 8;
		constexpr float k_TwoPi = 6.28318530718f;

		// Ringed fan with a Gaussian alpha falloff; a plain fan's linear falloff shows a cone tip in the middle.
		void DrawBlob(ImDrawList* dl, ImVec2 center, ImVec2 radius, const ImVec4& color) {
			constexpr int vtxCount = 1 + k_Rings * k_Segments;
			constexpr int idxCount = k_Segments * 3 + (k_Rings - 1) * k_Segments * 6;
			dl->PrimReserve(idxCount, vtxCount);

			const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
			const unsigned int base = dl->_VtxCurrentIdx;
			dl->PrimWriteVtx(center, uv, ImGui::ColorConvertFloat4ToU32(color));
			for (int ring = 1; ring <= k_Rings; ++ring) {
				const float t = static_cast<float>(ring) / k_Rings;
				const float alpha = ring == k_Rings ? 0.0f : color.w * std::exp(-3.5f * t * t);
				const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, alpha));
				for (int s = 0; s < k_Segments; ++s) {
					const float angle = k_TwoPi * static_cast<float>(s) / k_Segments;
					dl->PrimWriteVtx(ImVec2(center.x + std::cos(angle) * radius.x * t, center.y + std::sin(angle) * radius.y * t), uv, col);
				}
			}

			auto ringVtx = [base](int ring, int segment) {
				return static_cast<ImDrawIdx>(base + 1 + (ring - 1) * k_Segments + segment % k_Segments);
			};
			for (int s = 0; s < k_Segments; ++s) {
				dl->PrimWriteIdx(static_cast<ImDrawIdx>(base));
				dl->PrimWriteIdx(ringVtx(1, s));
				dl->PrimWriteIdx(ringVtx(1, s + 1));
			}
			for (int ring = 1; ring < k_Rings; ++ring) {
				for (int s = 0; s < k_Segments; ++s) {
					const ImDrawIdx a = ringVtx(ring, s);
					const ImDrawIdx b = ringVtx(ring, s + 1);
					const ImDrawIdx c = ringVtx(ring + 1, s);
					const ImDrawIdx d = ringVtx(ring + 1, s + 1);
					dl->PrimWriteIdx(a); dl->PrimWriteIdx(b); dl->PrimWriteIdx(d);
					dl->PrimWriteIdx(a); dl->PrimWriteIdx(d); dl->PrimWriteIdx(c);
				}
			}
		}
	}

	ImVec4 TitlebarColor(bool focused) {
		return focused ? ImVec4(0.055f, 0.075f, 0.170f, 1.0f) : ImVec4(0.050f, 0.060f, 0.120f, 1.0f);
	}

	void SyncViewportCaptions(bool glass) {
		// Viewport ID -> HWND it was tinted on; a recreated platform window needs the colour again.
		static std::unordered_map<ImGuiID, void*> s_Tinted;
		if (!glass && s_Tinted.empty()) return;

		const ImVec4 c = TitlebarColor(true);
		const Color caption(c.x, c.y, c.z, glass ? 1.0f : 0.0f);
		const Color none(0.0f, 0.0f, 0.0f, 0.0f);
		const ImGuiViewport* main = ImGui::GetMainViewport();

		std::unordered_map<ImGuiID, void*> live;
		for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
			if (viewport == main || !viewport->PlatformHandleRaw || (viewport->Flags & ImGuiViewportFlags_NoDecoration)) {
				continue;
			}
			const auto it = s_Tinted.find(viewport->ID);
			const bool tinted = it != s_Tinted.end() && it->second == viewport->PlatformHandleRaw;
			if (glass != tinted) {
				Win32Titlebar::SetColors(viewport->PlatformHandleRaw, caption, none, caption);
			}
			if (glass) {
				live.emplace(viewport->ID, viewport->PlatformHandleRaw);
			}
		}
		s_Tinted = std::move(live);
	}

	void Draw() {
		ImGuiViewport* main = ImGui::GetMainViewport();
		if (!main || main->Size.x <= 0.0f || main->Size.y <= 0.0f || (main->Flags & ImGuiViewportFlags_IsMinimized)) {
			return;
		}

		ImDrawList* dl = ImGui::GetBackgroundDrawList(main);
		const ImVec2 min = main->Pos;
		const ImVec2 max(min.x + main->Size.x, min.y + main->Size.y);
		dl->AddRectFilledMultiColor(min, max,
			ImGui::ColorConvertFloat4ToU32(k_BaseCorners[0]),
			ImGui::ColorConvertFloat4ToU32(k_BaseCorners[1]),
			ImGui::ColorConvertFloat4ToU32(k_BaseCorners[2]),
			ImGui::ColorConvertFloat4ToU32(k_BaseCorners[3]));

		const double time = ImGui::GetTime();
		const float extent = std::max(main->Size.x, main->Size.y);
		for (const Blob& blob : k_Blobs) {
			const float sx = static_cast<float>(std::sin(time * blob.Speed.x + blob.Phase));
			const float sy = static_cast<float>(std::sin(time * blob.Speed.y + blob.Phase * 1.3));
			const ImVec2 center(
				min.x + (blob.Center.x + blob.Drift.x * sx) * main->Size.x,
				min.y + (blob.Center.y + blob.Drift.y * sy) * main->Size.y);
			// Radii breathe out of phase, so the blobs morph instead of only sliding.
			const float r = blob.Radius * extent;
			const ImVec2 radius(
				r * (1.0f + 0.14f * static_cast<float>(std::sin(time * blob.Speed.y * 1.7 + blob.Phase))),
				r * (1.0f + 0.14f * static_cast<float>(std::cos(time * blob.Speed.x * 1.3 + blob.Phase))));
			DrawBlob(dl, center, radius, blob.Color);
		}
	}

}
