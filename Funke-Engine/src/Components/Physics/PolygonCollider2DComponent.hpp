#pragma once
#include "Collider2D.hpp"
#include "Rigidbody2DComponent.hpp"
#include "Physics/PhysicsSystem2D.hpp"

#include <box2d/box2d.h>

#include <vector>

namespace Funke {
	// Polygon collider made of one or more paths. A path of up to 8 points that is convex, or not a simple
	// polygon, becomes its convex hull (the classic behavior); any other path may be concave and have any number
	// of points: it is triangulated and merged into convex pieces of at most 8 vertices, one Box2D shape each,
	// all on the entity's body.
	// In Edge mode every path is an open line instead (ground, walls): a Box2D chain of segments, smooth across its
	// points, that collides on the left of the walking direction, i.e. on top of a line drawn left to right.
	class FUNKE_API PolygonCollider2DComponent : public Collider2D {
		friend class PhysicsSystem2D;
	public:
		// Box2D limit for a single convex piece.
		static constexpr int k_MaxVertices = 8;
		static constexpr int k_MinVertices = 3;
		static constexpr int k_MinEdgePoints = 2;
		static constexpr int k_DefaultSides = 5;
		// Upper bound per path and in total, so a corrupt file can't allocate without limit.
		static constexpr int k_MaxPathPoints = 16384;
		static constexpr int k_MaxPaths = 1024;

		PolygonCollider2DComponent();
		explicit PolygonCollider2DComponent(EntityHandle entity);

		// Replaces path 0 and keeps the other paths.
		void SetPoints(const std::vector<Vec2>& localPoints, const Scene& scene);
		const std::vector<Vec2>& GetLocalPoints() const { return m_LocalPoints; }
		// Vertices of the first convex piece, in the entity's frame (rotation not applied).
		std::vector<Vec2> GetWorldPoints() const;
		int GetVertexCount() const { return static_cast<int>(m_LocalPoints.size()); }

		// Replaces every path; an empty list restores the default pentagon.
		void SetPaths(const std::vector<std::vector<Vec2>>& paths, const Scene& scene);
		int GetPathCount() const { return 1 + static_cast<int>(m_ExtraPaths.size()); }
		const std::vector<Vec2>& GetPath(int index) const;
		// Every convex piece Box2D collides with, in the entity's frame (rotation not applied). Empty without a live shape.
		std::vector<std::vector<Vec2>> GetShapePolygons() const;
		// The same pieces computed from the authored paths, for drawing without a live body (prefab editing).
		std::vector<std::vector<Vec2>> ComputeLocalPieces(const Vec2& transformScale) const;

		// Regenerate as a regular n-gon with sides in [k_MinVertices, k_MaxVertices].
		// Resets any custom vertex list (and extra paths) back to a uniform polygon.
		void SetSides(int sides, const Scene& scene);
		int GetSides() const { return static_cast<int>(m_LocalPoints.size()); }

		void SetSize(const Vec2& size, const Scene& scene);
		Vec2 GetSize() const { return m_LocalSize; }
		Vec2 GetLocalSize(const Scene& scene) const { (void)scene; return m_LocalSize; }

		void SetCenter(const Vec2& center, const Scene& scene);
		Vec2 GetCenter() const;

		void SetEnabled(bool enabled);
		void SetSensor(bool sensor, Scene& scene);

		// Paths become open lines (no area, no mass, never a sensor) instead of filled polygons.
		void SetEdge(bool edge, Scene& scene);
		bool IsEdge() const { return m_Edge; }

		void SyncWithTransform(const Scene& scene);
		void UpdatePolygon(const Scene& scene) { SyncWithTransform(scene); }

		void Destroy() override;
		void DestroyShape(bool updateBodyMass = true) override;

		// Build a regular convex polygon with the given side count, centered
		// at origin, fitting in a unit square. Used as the default vertex set.
		static std::vector<Vec2> MakeRegularPolygon(int sides);

		// Splits one path into convex pieces of at most k_MaxVertices (see the class comment). Exposed for tests.
		static std::vector<std::vector<Vec2>> DecomposePath(const std::vector<Vec2>& path);

	protected:
		int GetExtraShapes(const b2ShapeId*& outShapes) const override;

	private:
		std::vector<Vec2> m_LocalPoints;
		std::vector<std::vector<Vec2>> m_ExtraPaths;
		std::vector<b2ShapeId> m_ExtraShapeIds;
		// Edge mode: one chain per path; their segments are m_ShapeId + m_ExtraShapeIds.
		std::vector<b2ChainId> m_ChainIds;
		bool m_Edge{ false };
		Vec2 m_LocalSize{ 1.0f, 1.0f };
		Vec2 m_Center{ 0.0f, 0.0f };

		Vec2 m_LastAppliedScale{ 0.0f, 0.0f };
		Vec2 m_LastAppliedLocalSize{ 0.0f, 0.0f };
		Vec2 m_LastAppliedCenter{ 0.0f, 0.0f };
		std::vector<Vec2> m_LastAppliedPoints;
		std::vector<std::vector<Vec2>> m_LastAppliedExtraPaths;

		void RebuildPolygon(const Transform2DComponent& tr);
		void RebuildEdges(const Transform2DComponent& tr);
		void RebuildAndRefreshMass(const Scene& scene, const char* context);
		void DestroyExtraShapes(bool updateBodyMass);
		void DestroyChains();
		std::vector<Vec2> LocalPath(const std::vector<Vec2>& path, const Vec2& transformScale) const;
	};
}
