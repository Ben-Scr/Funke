#include <pch.hpp>
#include <Components/Physics/PolygonCollider2DComponent.hpp>
#include <Components/Physics/Rigidbody2DComponent.hpp>
#include <Components/General/Transform2DComponent.hpp>

#include <Scene/Scene.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace Funke {
	namespace {
		const Transform2DComponent* TryGetTransform(const Scene& scene, EntityHandle entity, const char* context) {
			if (entity == entt::null || !scene.IsValid(entity) || !scene.HasComponent<Transform2DComponent>(entity)) {
				FNK_CORE_WARN_TAG("PolygonCollider2D", "{} skipped because entity {} has no Transform2DComponent", context, static_cast<uint32_t>(entity));
				return nullptr;
			}

			return &scene.GetComponent<Transform2DComponent>(entity);
		}

		double Cross(const Vec2& o, const Vec2& a, const Vec2& b) {
			return (static_cast<double>(a.x) - o.x) * (static_cast<double>(b.y) - o.y)
				- (static_cast<double>(a.y) - o.y) * (static_cast<double>(b.x) - o.x);
		}

		double SignedArea(const std::vector<Vec2>& p) {
			double area = 0.0;
			for (size_t i = 0, n = p.size(); i < n; ++i) {
				const Vec2& a = p[i];
				const Vec2& b = p[(i + 1) % n];
				area += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
			}
			return area * 0.5;
		}

		// Tolerance for "on the line" tests, relative to the path's size.
		double AreaEpsilon(const std::vector<Vec2>& p) {
			float minX = p[0].x, maxX = p[0].x, minY = p[0].y, maxY = p[0].y;
			for (const Vec2& v : p) {
				minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
				minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
			}
			const double extent = std::max(static_cast<double>(maxX - minX), static_cast<double>(maxY - minY));
			return std::max(extent * extent * 1e-9, 1e-12);
		}

		// Drops repeated and collinear points; the result is counter-clockwise.
		std::vector<Vec2> CleanPath(const std::vector<Vec2>& in) {
			std::vector<Vec2> p;
			p.reserve(in.size());
			for (const Vec2& v : in) {
				if (!std::isfinite(v.x) || !std::isfinite(v.y)) continue;
				if (!p.empty() && std::abs(v.x - p.back().x) < 1e-6f && std::abs(v.y - p.back().y) < 1e-6f) continue;
				p.push_back(v);
			}
			while (p.size() > 1 && std::abs(p.front().x - p.back().x) < 1e-6f && std::abs(p.front().y - p.back().y) < 1e-6f) {
				p.pop_back();
			}
			if (p.size() < 3) return {};

			const double eps = AreaEpsilon(p);
			bool removed = true;
			while (removed && p.size() >= 3) {
				removed = false;
				for (size_t i = 0; i < p.size() && p.size() >= 3; ++i) {
					const size_t n = p.size();
					if (std::abs(Cross(p[(i + n - 1) % n], p[i], p[(i + 1) % n])) <= eps) {
						p.erase(p.begin() + static_cast<std::ptrdiff_t>(i));
						removed = true;
						break;
					}
				}
			}
			if (p.size() < 3) return {};
			if (SignedArea(p) < 0.0) std::reverse(p.begin(), p.end());
			return p;
		}

		bool IsConvex(const std::vector<Vec2>& p) {
			const double eps = AreaEpsilon(p);
			for (size_t i = 0, n = p.size(); i < n; ++i) {
				if (Cross(p[(i + n - 1) % n], p[i], p[(i + 1) % n]) < -eps) return false;
			}
			return true;
		}

		bool SegmentsCross(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
			const double d1 = Cross(c, d, a), d2 = Cross(c, d, b), d3 = Cross(a, b, c), d4 = Cross(a, b, d);
			return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
		}

		bool IsSimple(const std::vector<Vec2>& p) {
			const size_t n = p.size();
			for (size_t i = 0; i < n; ++i) {
				for (size_t j = i + 2; j < n; ++j) {
					if (i == 0 && j == n - 1) continue;
					if (SegmentsCross(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) return false;
				}
			}
			return true;
		}

		bool PointInTriangle(const Vec2& pt, const Vec2& a, const Vec2& b, const Vec2& c) {
			return Cross(a, b, pt) >= 0.0 && Cross(b, c, pt) >= 0.0 && Cross(c, a, pt) >= 0.0;
		}

		// Ear clipping of a counter-clockwise simple polygon.
		bool Triangulate(const std::vector<Vec2>& p, std::vector<std::array<int, 3>>& tris) {
			std::vector<int> v(p.size());
			for (size_t i = 0; i < p.size(); ++i) v[i] = static_cast<int>(i);
			const double eps = AreaEpsilon(p);

			while (v.size() > 3) {
				const size_t n = v.size();
				bool clipped = false;
				for (size_t i = 0; i < n; ++i) {
					const int a = v[(i + n - 1) % n], b = v[i], c = v[(i + 1) % n];
					if (Cross(p[a], p[b], p[c]) <= eps) continue;
					bool ear = true;
					for (size_t k = 0; k < n; ++k) {
						const int q = v[k];
						if (q == a || q == b || q == c) continue;
						if (PointInTriangle(p[q], p[a], p[b], p[c])) { ear = false; break; }
					}
					if (!ear) continue;
					tris.push_back({ a, b, c });
					v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
					clipped = true;
					break;
				}
				if (clipped) continue;

				// No ear: drop a degenerate (flat) vertex to make progress, else the path is not simple.
				bool dropped = false;
				for (size_t i = 0; i < n; ++i) {
					if (std::abs(Cross(p[v[(i + n - 1) % n]], p[v[i]], p[v[(i + 1) % n]])) <= eps) {
						v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
						dropped = true;
						break;
					}
				}
				if (!dropped) return false;
			}
			if (v.size() == 3 && Cross(p[v[0]], p[v[1]], p[v[2]]) > eps) {
				tris.push_back({ v[0], v[1], v[2] });
			}
			return true;
		}

		uint64_t EdgeKey(int a, int b) {
			return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
		}

		bool IsConvexIndexed(const std::vector<Vec2>& p, const std::vector<int>& idx, double eps) {
			for (size_t i = 0, n = idx.size(); i < n; ++i) {
				if (Cross(p[idx[(i + n - 1) % n]], p[idx[i]], p[idx[(i + 1) % n]]) < -eps) return false;
			}
			return true;
		}

		// Greedily merges triangles that share an edge while the result stays convex and within the vertex limit.
		std::vector<std::vector<int>> MergeConvex(const std::vector<Vec2>& p, const std::vector<std::array<int, 3>>& tris, int maxVertices) {
			std::vector<std::vector<int>> pieces;
			pieces.reserve(tris.size());
			for (const auto& t : tris) pieces.push_back({ t[0], t[1], t[2] });
			std::vector<bool> alive(pieces.size(), true);
			std::unordered_map<uint64_t, int> owner;
			owner.reserve(tris.size() * 3);
			for (int i = 0; i < static_cast<int>(pieces.size()); ++i) {
				const auto& piece = pieces[i];
				for (size_t k = 0; k < piece.size(); ++k) owner[EdgeKey(piece[k], piece[(k + 1) % piece.size()])] = i;
			}

			const double eps = AreaEpsilon(p);
			for (int i = 0; i < static_cast<int>(pieces.size()); ++i) {
				bool merged = true;
				while (alive[i] && merged) {
					merged = false;
					const std::vector<int> a = pieces[i];
					for (size_t k = 0; k < a.size() && !merged; ++k) {
						const int from = a[k], to = a[(k + 1) % a.size()];
						const auto it = owner.find(EdgeKey(to, from));
						if (it == owner.end()) continue;
						const int j = it->second;
						if (j == i || !alive[j]) continue;
						const std::vector<int>& b = pieces[j];
						if (static_cast<int>(a.size() + b.size()) - 2 > maxVertices) continue;

						// a = [.. from, to ..], b = [.. to, from ..]; walk a from `to` round to `from`, then b from `from` round to `to`.
						std::vector<int> joined;
						joined.reserve(a.size() + b.size() - 2);
						for (size_t s = 0; s + 1 < a.size(); ++s) joined.push_back(a[(k + 1 + s) % a.size()]);
						const auto bFrom = std::find(b.begin(), b.end(), from);
						const size_t bk = static_cast<size_t>(bFrom - b.begin());
						for (size_t s = 0; s + 1 < b.size(); ++s) joined.push_back(b[(bk + s) % b.size()]);
						if (!IsConvexIndexed(p, joined, eps)) continue;

						for (size_t e = 0; e < b.size(); ++e) owner.erase(EdgeKey(b[e], b[(e + 1) % b.size()]));
						for (size_t e = 0; e < a.size(); ++e) owner.erase(EdgeKey(a[e], a[(e + 1) % a.size()]));
						pieces[i] = std::move(joined);
						alive[j] = false;
						for (size_t e = 0; e < pieces[i].size(); ++e) owner[EdgeKey(pieces[i][e], pieces[i][(e + 1) % pieces[i].size()])] = i;
						merged = true;
					}
				}
			}

			std::vector<std::vector<int>> result;
			for (size_t i = 0; i < pieces.size(); ++i) {
				if (alive[i]) result.push_back(std::move(pieces[i]));
			}
			return result;
		}

		bool MakePolygon(const std::vector<Vec2>& piece, b2Polygon& out) {
			if (piece.size() < 3 || piece.size() > static_cast<size_t>(B2_MAX_POLYGON_VERTICES)) return false;
			b2Vec2 points[B2_MAX_POLYGON_VERTICES];
			for (size_t i = 0; i < piece.size(); ++i) points[i] = b2Vec2{ piece[i].x, piece[i].y };
			const b2Hull hull = b2ComputeHull(points, static_cast<int>(piece.size()));
			if (hull.count < 3) return false;
			out = b2MakePolygon(&hull, 0.0f);
			return true;
		}
	}

	std::vector<Vec2> PolygonCollider2DComponent::MakeRegularPolygon(int sides) {
		const int clamped = std::clamp(sides, k_MinVertices, k_MaxVertices);
		constexpr float kPi = 3.14159265358979323846f;

		std::vector<Vec2> points;
		points.reserve(static_cast<size_t>(clamped));
		for (int i = 0; i < clamped; ++i) {
			// Starts at the top; small convex paths go through b2ComputeHull, which sorts the points itself.
			const float angle = -static_cast<float>(i) * (2.0f * kPi) / static_cast<float>(clamped) + kPi * 0.5f;
			points.push_back(Vec2{ std::cos(angle) * 0.5f, std::sin(angle) * 0.5f });
		}
		return points;
	}

	std::vector<std::vector<Vec2>> PolygonCollider2DComponent::DecomposePath(const std::vector<Vec2>& path) {
		if (path.size() < static_cast<size_t>(k_MinVertices)) return {};
		const std::vector<Vec2> clean = CleanPath(path);
		if (clean.size() < 3) return {};

		// Small convex paths and unordered point sets keep the convex-hull behavior.
		if (path.size() <= static_cast<size_t>(k_MaxVertices) && (IsConvex(clean) || !IsSimple(clean))) {
			return { clean };
		}

		std::vector<std::array<int, 3>> tris;
		tris.reserve(clean.size());
		if (!Triangulate(clean, tris) || tris.empty()) {
			FNK_CORE_WARN_TAG("PolygonCollider2D", "A path with {} points intersects itself and was skipped", path.size());
			return {};
		}

		std::vector<std::vector<Vec2>> pieces;
		for (const std::vector<int>& indices : MergeConvex(clean, tris, k_MaxVertices)) {
			std::vector<Vec2> piece;
			piece.reserve(indices.size());
			for (int i : indices) piece.push_back(clean[static_cast<size_t>(i)]);
			pieces.push_back(std::move(piece));
		}
		return pieces;
	}

	PolygonCollider2DComponent::PolygonCollider2DComponent()
		: m_LocalPoints(MakeRegularPolygon(k_DefaultSides)) {}

	PolygonCollider2DComponent::PolygonCollider2DComponent(EntityHandle entity)
		: Collider2D(entity), m_LocalPoints(MakeRegularPolygon(k_DefaultSides)) {}

	int PolygonCollider2DComponent::GetExtraShapes(const b2ShapeId*& outShapes) const {
		outShapes = m_ExtraShapeIds.empty() ? nullptr : m_ExtraShapeIds.data();
		return static_cast<int>(m_ExtraShapeIds.size());
	}

	const std::vector<Vec2>& PolygonCollider2DComponent::GetPath(int index) const {
		if (index <= 0 || index > static_cast<int>(m_ExtraPaths.size())) return m_LocalPoints;
		return m_ExtraPaths[static_cast<size_t>(index - 1)];
	}

	std::vector<Vec2> PolygonCollider2DComponent::LocalPath(const std::vector<Vec2>& path, const Vec2& transformScale) const {
		std::vector<Vec2> local;
		local.reserve(path.size());
		for (const Vec2& p : path) {
			local.push_back(Vec2{
				p.x * transformScale.x * m_LocalSize.x + m_Center.x,
				p.y * transformScale.y * m_LocalSize.y + m_Center.y });
		}
		return local;
	}

	std::vector<std::vector<Vec2>> PolygonCollider2DComponent::ComputeLocalPieces(const Vec2& transformScale) const {
		std::vector<std::vector<Vec2>> pieces;
		const auto addPath = [&](const std::vector<Vec2>& path) {
			const std::vector<Vec2> local = LocalPath(path, transformScale);
			if (m_Edge) {
				// One two-point piece per segment, so outline drawing shows the open line.
				for (size_t i = 1; i < local.size(); ++i) pieces.push_back({ local[i - 1], local[i] });
				return;
			}
			for (std::vector<Vec2>& piece : DecomposePath(local)) pieces.push_back(std::move(piece));
		};
		addPath(m_LocalPoints);
		for (const std::vector<Vec2>& path : m_ExtraPaths) addPath(path);
		return pieces;
	}

	void PolygonCollider2DComponent::DestroyChains() {
		const bool hasWorld = PhysicsSystem2D::IsInitialized();
		if (hasWorld) {
			auto& dispatcher = PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher();
			if (b2StoreShapeId(m_ShapeId) != b2StoreShapeId(b2_nullShapeId)) dispatcher.UnregisterShape(m_ShapeId);
			for (const b2ShapeId shape : m_ExtraShapeIds) dispatcher.UnregisterShape(shape);
		}
		for (const b2ChainId chain : m_ChainIds) {
			if (b2Chain_IsValid(chain)) b2DestroyChain(chain);
		}
		m_ChainIds.clear();
		m_ExtraShapeIds.clear();
		m_ShapeId = b2_nullShapeId;
	}

	void PolygonCollider2DComponent::RebuildEdges(const Transform2DComponent& tr) {
		std::vector<std::vector<b2Vec2>> chains;
		const auto addPath = [&](const std::vector<Vec2>& path) {
			std::vector<Vec2> local = LocalPath(path, tr.Scale);
			local.erase(std::unique(local.begin(), local.end()), local.end());
			if (local.size() < static_cast<size_t>(k_MinEdgePoints)) return;
			// Box2D chains collide on the right of their direction: reverse, so a line drawn left to right is solid
			// from above, and add straight ghost points so the end segments are real ones.
			std::reverse(local.begin(), local.end());
			std::vector<b2Vec2> points;
			points.reserve(local.size() + 2);
			const Vec2 first = local.front() * 2.0f - local[1];
			const Vec2 last = local.back() * 2.0f - local[local.size() - 2];
			points.push_back(b2Vec2{ first.x, first.y });
			for (const Vec2& p : local) points.push_back(b2Vec2{ p.x, p.y });
			points.push_back(b2Vec2{ last.x, last.y });
			chains.push_back(std::move(points));
		};
		addPath(m_LocalPoints);
		for (const std::vector<Vec2>& path : m_ExtraPaths) addPath(path);
		if (chains.empty()) {
			FNK_CORE_WARN_TAG("PolygonCollider2D", "Entity {} has no edge path with {} or more points; keeping the previous shape",
				static_cast<uint32_t>(m_EntityHandle), k_MinEdgePoints);
			return;
		}
		if (m_Sensor) {
			FNK_CORE_WARN_TAG("PolygonCollider2D", "Entity {}: edge colliders can't be sensors; it collides normally", static_cast<uint32_t>(m_EntityHandle));
		}

		// The live state (polygon shape or previous segments) carries material, filter and event flags.
		const float friction = GetFriction();
		const float bounciness = GetBounciness();
		const bool registerContacts = CanRegisterContacts();
		b2Filter filter = b2DefaultFilter();
		filter.categoryBits = GetLayer();
		filter.maskBits = GetCollisionMask();
		filter.groupIndex = GetGroupIndex();

		auto& dispatcher = PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher();
		auto savedCallbacks = dispatcher.SnapshotCallbacks(m_ShapeId);
		if (!m_ChainIds.empty()) {
			DestroyChains();
		}
		else {
			DestroyExtraShapes(false);
			Collider2D::DestroyShape(false);
		}

		b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
		material.friction = friction;
		material.restitution = bounciness;
		for (const std::vector<b2Vec2>& points : chains) {
			b2ChainDef def = b2DefaultChainDef();
			def.points = points.data();
			def.count = static_cast<int>(points.size());
			def.materials = &material;
			def.materialCount = 1;
			def.filter = filter;
			def.isLoop = false;
			def.enableSensorEvents = true;
			const b2ChainId chain = b2CreateChain(m_BodyId, &def);
			m_ChainIds.push_back(chain);

			std::vector<b2ShapeId> segments(static_cast<size_t>(b2Chain_GetSegmentCount(chain)));
			b2Chain_GetSegments(chain, segments.data(), static_cast<int>(segments.size()));
			for (const b2ShapeId segment : segments) {
				b2Shape_EnableContactEvents(segment, registerContacts);
				if (b2StoreShapeId(m_ShapeId) == b2StoreShapeId(b2_nullShapeId)) m_ShapeId = segment;
				else m_ExtraShapeIds.push_back(segment);
			}
		}
		dispatcher.RestoreCallbacks(m_ShapeId, std::move(savedCallbacks));

		m_LastAppliedScale = tr.Scale;
		m_LastAppliedLocalSize = m_LocalSize;
		m_LastAppliedCenter = m_Center;
		m_LastAppliedPoints = m_LocalPoints;
		m_LastAppliedExtraPaths = m_ExtraPaths;
	}

	void PolygonCollider2DComponent::SetEdge(bool edge, Scene& scene) {
		if (m_Edge == edge) return;
		m_Edge = edge;
		if (!b2Body_IsValid(m_BodyId)) return;     // no live body (prefab editing): the flag is enough
		if (!edge && !m_ChainIds.empty()) {
			// Back to filled polygons: a fresh polygon shape takes over the segments' settings.
			const float friction = GetFriction();
			const float bounciness = GetBounciness();
			const uint64_t layer = GetLayer();
			const uint64_t mask = GetCollisionMask();
			const int group = GetGroupIndex();
			const bool registerContacts = CanRegisterContacts();
			auto& dispatcher = PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher();
			auto savedCallbacks = dispatcher.SnapshotCallbacks(m_ShapeId);
			DestroyChains();
			m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(m_EntityHandle, scene, m_BodyId, ShapeType::Polygon, m_Sensor);
			dispatcher.RestoreCallbacks(m_ShapeId, std::move(savedCallbacks));
			SetFriction(friction);
			SetBounciness(bounciness);
			SetDensity(m_Density);
			SetGroupIndex(group);
			SetCollisionMask(mask);
			SetLayer(layer);
			SetRegisterContacts(registerContacts);
		}
		RebuildAndRefreshMass(scene, "SetEdge");
	}

	void PolygonCollider2DComponent::DestroyExtraShapes(bool updateBodyMass) {
		if (!m_ChainIds.empty()) return;            // chain segments go with their chain (DestroyChains)
		if (m_ExtraShapeIds.empty()) return;
		const bool hasWorld = PhysicsSystem2D::IsInitialized();
		for (const b2ShapeId shape : m_ExtraShapeIds) {
			if (hasWorld) PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().UnregisterShape(shape);
			if (b2Shape_IsValid(shape)) b2DestroyShape(shape, false);
		}
		m_ExtraShapeIds.clear();
		if (updateBodyMass && b2Body_IsValid(m_BodyId)) b2Body_ApplyMassFromShapes(m_BodyId);
	}

	void PolygonCollider2DComponent::RebuildPolygon(const Transform2DComponent& tr) {
		if (!b2Shape_IsValid(m_ShapeId)) return;
		if (m_Edge) {
			RebuildEdges(tr);
			return;
		}

		std::vector<b2Polygon> polygons;
		for (const std::vector<Vec2>& piece : ComputeLocalPieces(tr.Scale)) {
			b2Polygon polygon;
			if (MakePolygon(piece, polygon)) polygons.push_back(polygon);
		}
		if (polygons.empty()) {
			FNK_CORE_WARN_TAG("PolygonCollider2D", "Entity {} has no valid polygon (points: {}, paths: {}); keeping the previous shape",
				static_cast<uint32_t>(m_EntityHandle), m_LocalPoints.size(), GetPathCount());
			return;
		}

		b2Shape_SetPolygon(m_ShapeId, &polygons[0]);

		const size_t extraCount = polygons.size() - 1;
		while (m_ExtraShapeIds.size() > extraCount) {
			const b2ShapeId shape = m_ExtraShapeIds.back();
			m_ExtraShapeIds.pop_back();
			if (PhysicsSystem2D::IsInitialized()) PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().UnregisterShape(shape);
			if (b2Shape_IsValid(shape)) b2DestroyShape(shape, false);
		}
		if (extraCount > 0) {
			// New pieces copy the first shape's material, filter and event flags.
			b2ShapeDef def = b2DefaultShapeDef();
			def.density = b2Shape_GetDensity(m_ShapeId);
			def.material.friction = b2Shape_GetFriction(m_ShapeId);
			def.material.restitution = b2Shape_GetRestitution(m_ShapeId);
			def.filter = b2Shape_GetFilter(m_ShapeId);
			def.isSensor = b2Shape_IsSensor(m_ShapeId);
			def.enableSensorEvents = true;
			def.enableContactEvents = b2Shape_AreContactEventsEnabled(m_ShapeId);
			def.updateBodyMass = false;
			for (size_t i = 0; i < extraCount; ++i) {
				if (i < m_ExtraShapeIds.size() && b2Shape_IsValid(m_ExtraShapeIds[i])) {
					b2Shape_SetPolygon(m_ExtraShapeIds[i], &polygons[i + 1]);
				}
				else if (i < m_ExtraShapeIds.size()) {
					m_ExtraShapeIds[i] = b2CreatePolygonShape(m_BodyId, &def, &polygons[i + 1]);
				}
				else {
					m_ExtraShapeIds.push_back(b2CreatePolygonShape(m_BodyId, &def, &polygons[i + 1]));
				}
			}
		}
		// SetPolygon keeps the old mass; recompute it from the new pieces.
		b2Body_ApplyMassFromShapes(m_BodyId);

		m_LastAppliedScale = tr.Scale;
		m_LastAppliedLocalSize = m_LocalSize;
		m_LastAppliedCenter = m_Center;
		m_LastAppliedPoints = m_LocalPoints;
		m_LastAppliedExtraPaths = m_ExtraPaths;
	}

	void PolygonCollider2DComponent::RebuildAndRefreshMass(const Scene& scene, const char* context) {
		if (!b2Shape_IsValid(m_ShapeId)) return; // no live shape (prefab edit): the stored data is enough
		const Transform2DComponent* tr = TryGetTransform(scene, m_EntityHandle, context);
		if (!tr) return;
		RebuildPolygon(*tr);
		if (scene.HasComponent<Rigidbody2DComponent>(m_EntityHandle)) {
			const_cast<Rigidbody2DComponent&>(scene.GetComponent<Rigidbody2DComponent>(m_EntityHandle)).ApplyMassFromState();
		}
	}

	void PolygonCollider2DComponent::SetPoints(const std::vector<Vec2>& localPoints, const Scene& scene) {
		const int count = static_cast<int>(localPoints.size());
		const int minPoints = m_Edge ? k_MinEdgePoints : k_MinVertices;
		if (count < minPoints || count > k_MaxPathPoints) {
			FNK_CORE_WARN_TAG("PolygonCollider2D",
				"SetPoints: vertex count {} out of range [{}, {}]; ignoring",
				count, minPoints, k_MaxPathPoints);
			return;
		}

		m_LocalPoints = localPoints;
		RebuildAndRefreshMass(scene, "SetPoints");
	}

	void PolygonCollider2DComponent::SetPaths(const std::vector<std::vector<Vec2>>& paths, const Scene& scene) {
		std::vector<std::vector<Vec2>> kept;
		const size_t minPoints = static_cast<size_t>(m_Edge ? k_MinEdgePoints : k_MinVertices);
		for (const std::vector<Vec2>& path : paths) {
			if (static_cast<int>(kept.size()) >= k_MaxPaths) break;
			if (path.size() < minPoints || path.size() > static_cast<size_t>(k_MaxPathPoints)) {
				FNK_CORE_WARN_TAG("PolygonCollider2D", "SetPaths: skipped a path with {} points", path.size());
				continue;
			}
			kept.push_back(path);
		}

		if (kept.empty()) {
			m_LocalPoints = MakeRegularPolygon(k_DefaultSides);
			m_ExtraPaths.clear();
		}
		else {
			m_LocalPoints = std::move(kept.front());
			m_ExtraPaths.assign(std::make_move_iterator(kept.begin() + 1), std::make_move_iterator(kept.end()));
		}
		RebuildAndRefreshMass(scene, "SetPaths");
	}

	std::vector<Vec2> PolygonCollider2DComponent::GetWorldPoints() const {
		std::vector<Vec2> result;
		if (!b2Shape_IsValid(m_ShapeId)) return result;
		if (b2Shape_GetType(m_ShapeId) != b2_polygonShape) {
			// Edge mode: the line of path 0 as the segments hold it.
			for (const std::vector<Vec2>& piece : GetShapePolygons()) {
				if (result.empty()) result.push_back(piece[0]);
				result.push_back(piece[1]);
			}
			return result;
		}

		b2Polygon polygon = b2Shape_GetPolygon(m_ShapeId);
		result.reserve(static_cast<size_t>(polygon.count));
		for (int i = 0; i < polygon.count; ++i) {
			result.push_back(Vec2{ polygon.vertices[i].x, polygon.vertices[i].y });
		}
		return result;
	}

	std::vector<std::vector<Vec2>> PolygonCollider2DComponent::GetShapePolygons() const {
		std::vector<std::vector<Vec2>> result;
		const auto add = [&](b2ShapeId shape) {
			if (!b2Shape_IsValid(shape)) return;
			if (b2Shape_GetType(shape) == b2_chainSegmentShape) {
				// Stored reversed (see RebuildEdges): report each segment in the authored direction.
				const b2Segment segment = b2Shape_GetChainSegment(shape).segment;
				result.push_back({ Vec2{ segment.point2.x, segment.point2.y }, Vec2{ segment.point1.x, segment.point1.y } });
				return;
			}
			const b2Polygon polygon = b2Shape_GetPolygon(shape);
			std::vector<Vec2> points;
			points.reserve(static_cast<size_t>(polygon.count));
			for (int i = 0; i < polygon.count; ++i) points.push_back(Vec2{ polygon.vertices[i].x, polygon.vertices[i].y });
			result.push_back(std::move(points));
		};
		add(m_ShapeId);
		for (const b2ShapeId shape : m_ExtraShapeIds) add(shape);
		return result;
	}

	void PolygonCollider2DComponent::SetSides(int sides, const Scene& scene) {
		m_LocalPoints = MakeRegularPolygon(sides);
		m_ExtraPaths.clear();
		RebuildAndRefreshMass(scene, "SetSides");
	}

	void PolygonCollider2DComponent::SetSize(const Vec2& size, const Scene& scene) {
		m_LocalSize = size;
		RebuildAndRefreshMass(scene, "SetSize");
	}

	void PolygonCollider2DComponent::SetCenter(const Vec2& center, const Scene& scene) {
		m_Center = center;
		RebuildAndRefreshMass(scene, "SetCenter");
	}

	Vec2 PolygonCollider2DComponent::GetCenter() const {
		// The authored offset, not the shape's centroid: it is what the paths are placed by and what gets saved.
		return m_Center;
	}

	void PolygonCollider2DComponent::SetEnabled(bool enabled) {
		if (!b2Body_IsValid(m_BodyId)) return;
		if (enabled)
			b2Body_Enable(m_BodyId);
		else
			b2Body_Disable(m_BodyId);
	}

	void PolygonCollider2DComponent::SetSensor(bool sensor, Scene& scene) {
		m_Sensor = sensor;
		if (!IsValid() || IsSensor() == sensor) {
			return;
		}

		const float friction = GetFriction();
		const float bounciness = GetBounciness();
		const uint64_t layer = GetLayer();
		const bool enabled = IsEnabled();
		const bool registerContacts = CanRegisterContacts();

		// See BoxCollider2DComponent::SetSensor for callback-snapshot rationale —
		// recreating the b2Shape mints a fresh id, so dispatcher entries keyed
		// on the old id must be migrated explicitly or they silently disappear.
		auto& dispatcher = PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher();
		auto savedCallbacks = dispatcher.SnapshotCallbacks(m_ShapeId);
		DestroyShape(false);
		m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(m_EntityHandle, scene, m_BodyId, ShapeType::Polygon, sensor);
		dispatcher.RestoreCallbacks(m_ShapeId, std::move(savedCallbacks));
		SetFriction(friction);
		SetBounciness(bounciness);
		SetDensity(m_Density);
		SetLayer(layer);
		SetRegisterContacts(registerContacts);
		SetEnabled(enabled);
		// Rebuilds the paths (the fresh shape is a pentagon) and recreates the extra pieces as sensors too.
		RebuildAndRefreshMass(scene, "SetSensor");
	}

	void PolygonCollider2DComponent::SyncWithTransform(const Scene& scene) {
		const Transform2DComponent* tr = TryGetTransform(scene, m_EntityHandle, "SyncWithTransform");
		if (!tr) return;
		if (tr->Scale == m_LastAppliedScale
			&& m_LocalSize == m_LastAppliedLocalSize
			&& m_Center == m_LastAppliedCenter
			&& m_LocalPoints == m_LastAppliedPoints
			&& m_ExtraPaths == m_LastAppliedExtraPaths) {
			return;
		}
		if (!b2Shape_IsValid(m_ShapeId)) return;

		RebuildAndRefreshMass(scene, "SyncWithTransform");
	}

	void PolygonCollider2DComponent::DestroyShape(bool updateBodyMass) {
		if (!m_ChainIds.empty()) {
			DestroyChains();
			return;
		}
		DestroyExtraShapes(false);
		Collider2D::DestroyShape(updateBodyMass);
	}

	// Idempotent: destroy hooks fire from both Rigidbody2D and this component.
	void PolygonCollider2DComponent::Destroy() {
		if (!m_ChainIds.empty()) DestroyChains();
		DestroyExtraShapes(false);
		if (b2Shape_IsValid(m_ShapeId)) {
			Collider2D::DestroyShape(true);
		}
		if (b2Body_IsValid(m_BodyId)) {
			if (PhysicsSystem2D::IsInitialized()) {
				PhysicsSystem2D::GetMainPhysicsWorld().UnregisterBodyBinding(m_BodyId);
			}
			b2DestroyBody(m_BodyId);
		}
		m_ShapeId = b2_nullShapeId;
		m_BodyId = b2_nullBodyId;
	}
}
