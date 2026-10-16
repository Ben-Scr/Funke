#include "pch.hpp"
#include "Physics/PhysicsDebugDraw2D.hpp"

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Graphics/Gizmo.hpp"
#include "Math/VectorMath.hpp"
#include "Scene/Scene.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace Funke::PhysicsDebugDraw2D {

	namespace {
		const Color k_ColliderColor{ 0.20f, 1.0f, 0.35f, 1.0f };
		const Color k_TriggerColor{ 0.35f, 0.85f, 1.0f, 1.0f };
		const Color k_FastColliderColor{ 0.10f, 0.85f, 0.85f, 1.0f };
		const Color k_ContactColor{ 1.0f, 0.35f, 0.20f, 1.0f };

		Color ColorOf(const Collider2D& collider) {
			return collider.IsSensor() ? k_TriggerColor : k_ColliderColor;
		}

		// 2 px outlines: Gizmo's wire shapes are 1 px and vanish on a sprite edge of the same size.
		struct Outline {
			float Width = 0.02f;

			void Line(const Vec2& a, const Vec2& b) const { Gizmo::DrawThickLine(a, b, Width); }

			void Box(const Vec2& center, const Vec2& size, float radians) const {
				const Vec2 half = size * 0.5f;
				const Vec2 corners[4] = {
					center + Vector::Rotated(Vec2{ -half.x, -half.y }, radians),
					center + Vector::Rotated(Vec2{  half.x, -half.y }, radians),
					center + Vector::Rotated(Vec2{  half.x,  half.y }, radians),
					center + Vector::Rotated(Vec2{ -half.x,  half.y }, radians),
				};
				for (int i = 0; i < 4; ++i) Line(corners[i], corners[(i + 1) % 4]);
			}

			void Circle(const Vec2& center, float radius) const {
				constexpr int k_Segments = 32;
				Vec2 previous = center + Vec2{ radius, 0.0f };
				for (int i = 1; i <= k_Segments; ++i) {
					const float angle = static_cast<float>(i) / k_Segments * 6.28318530718f;
					const Vec2 point = center + Vec2{ std::cos(angle), std::sin(angle) } * radius;
					Line(previous, point);
					previous = point;
				}
			}
		};

		void DrawBodyContacts(b2BodyId body, float worldPerPixel) {
			const int capacity = b2Body_GetContactCapacity(body);
			if (capacity <= 0) return;
			std::vector<b2ContactData> contacts(static_cast<size_t>(capacity));
			const int count = b2Body_GetContactData(body, contacts.data(), capacity);
			const float markerRadius = 3.0f * worldPerPixel;
			const float normalLength = 24.0f * worldPerPixel;
			Gizmo::SetColor(k_ContactColor);
			for (int i = 0; i < count; ++i) {
				const b2ContactData& data = contacts[static_cast<size_t>(i)];
				// Each pair is listed by both bodies; draw it from shape A's side only.
				if (!b2Shape_IsValid(data.shapeIdA)) continue;
				const b2BodyId bodyA = b2Shape_GetBody(data.shapeIdA);
				if (!B2_ID_EQUALS(bodyA, body)) continue;
				const b2Manifold& manifold = data.manifold;
				for (int p = 0; p < manifold.pointCount; ++p) {
					const Vec2 point{ manifold.points[p].clipPoint.x, manifold.points[p].clipPoint.y };
					Gizmo::DrawCircle(point, markerRadius, 12);
					Gizmo::DrawThickLine(point, point + Vec2{ manifold.normal.x, manifold.normal.y } * normalLength, 2.0f * worldPerPixel);
				}
			}
		}
	}

	void DrawScene(Scene& scene, bool contacts, float worldPerPixel) {
		const Color previousColor = Gizmo::GetColor();
		const GizmoLayer previousLayer = Gizmo::GetLayer();
		Gizmo::SetLayer(GizmoLayer::Debug);
		const Outline outline{ 2.0f * worldPerPixel };

		auto& registry = scene.GetRegistry();
		std::unordered_set<uint64_t> contactBodies;
		const auto rememberBody = [&](const Collider2D& collider) {
			if (contacts && b2Body_IsValid(collider.m_BodyId)) contactBodies.insert(b2StoreBodyId(collider.m_BodyId));
		};

		for (const EntityHandle entity : registry.view<Transform2DComponent, BoxCollider2DComponent>()) {
			const auto& transform = registry.get<Transform2DComponent>(entity);
			const auto& collider = registry.get<BoxCollider2DComponent>(entity);
			if (!collider.IsValid()) continue;
			const Vec2 size = collider.GetScale();
			if (size.x == 0.0f || size.y == 0.0f) continue;
			Gizmo::SetColor(ColorOf(collider));
			outline.Box(transform.Position + Vector::Rotated(collider.GetCenter(), transform.Rotation), size, transform.Rotation);
			rememberBody(collider);
		}
		for (const EntityHandle entity : registry.view<Transform2DComponent, CircleCollider2DComponent>()) {
			const auto& transform = registry.get<Transform2DComponent>(entity);
			const auto& collider = registry.get<CircleCollider2DComponent>(entity);
			if (!collider.IsValid() || collider.GetRadius() <= 0.0f) continue;
			Gizmo::SetColor(ColorOf(collider));
			outline.Circle(transform.Position + Vector::Rotated(collider.GetCenter(), transform.Rotation), collider.GetRadius());
			rememberBody(collider);
		}
		for (const EntityHandle entity : registry.view<Transform2DComponent, PolygonCollider2DComponent>()) {
			const auto& transform = registry.get<Transform2DComponent>(entity);
			const auto& collider = registry.get<PolygonCollider2DComponent>(entity);
			if (!collider.IsValid()) continue;
			const std::vector<Vec2> points = collider.GetWorldPoints();
			if (points.size() < 3) continue;
			Gizmo::SetColor(ColorOf(collider));
			for (size_t i = 0; i < points.size(); ++i) {
				outline.Line(transform.Position + Vector::Rotated(points[i], transform.Rotation),
					transform.Position + Vector::Rotated(points[(i + 1) % points.size()], transform.Rotation));
			}
			rememberBody(collider);
		}
		Gizmo::SetColor(k_FastColliderColor);
		for (const EntityHandle entity : registry.view<Transform2DComponent, FastBoxCollider2DComponent>()) {
			const auto& transform = registry.get<Transform2DComponent>(entity);
			const auto& collider = registry.get<FastBoxCollider2DComponent>(entity);
			if (!collider.IsValid()) continue;
			const Vec2 he = collider.GetHalfExtents();
			outline.Box(transform.Position, Vec2{ std::abs(he.x) * 2.0f, std::abs(he.y) * 2.0f }, transform.Rotation);
		}
		for (const EntityHandle entity : registry.view<Transform2DComponent, FastCircleCollider2DComponent>()) {
			const auto& transform = registry.get<Transform2DComponent>(entity);
			const auto& collider = registry.get<FastCircleCollider2DComponent>(entity);
			if (!collider.IsValid()) continue;
			outline.Circle(transform.Position, collider.GetRadius());
		}

		for (const uint64_t body : contactBodies) {
			DrawBodyContacts(b2LoadBodyId(body), worldPerPixel);
		}

		Gizmo::SetLayer(previousLayer);
		Gizmo::SetColor(previousColor);
	}

} // namespace Funke::PhysicsDebugDraw2D
