#include "pch.hpp"
#include "Physics3D/PhysicsDebugDraw3D.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/PrimitiveMesh.hpp"
#include "Graphics/Renderer3D.hpp"
#include "Physics3D/PhysicsSystem3D.hpp"
#include "Scene/Scene.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace Index::PhysicsDebugDraw3D {

	namespace {
		const Color k_ColliderColor{ 0.20f, 1.0f, 0.35f, 1.0f };
		const Color k_TriggerColor{ 0.35f, 0.85f, 1.0f, 1.0f };
		const Color k_ContactColor{ 1.0f, 0.35f, 0.20f, 1.0f };
		const Color k_TriggerContactColor{ 1.0f, 0.85f, 0.25f, 1.0f };
		// Larger meshes draw their bounds instead of every edge.
		constexpr size_t k_MaxMeshGizmoTriangles = 20000;

		// World pose of a collider the way PhysicsWorld3D builds it: rotation and positive scale of the entity, Center in local space.
		struct ColliderFrame {
			glm::vec3 Origin{ 0.0f };
			glm::vec3 Axes[3]{};
			glm::vec3 Scale{ 1.0f };
		};

		bool MakeColliderFrame(const glm::mat4& world, const Vec3& center, ColliderFrame& out) {
			for (int i = 0; i < 3; ++i) {
				const glm::vec3 axis(world[i]);
				out.Scale[i] = glm::length(axis);
				if (out.Scale[i] < 1e-8f) return false;
				out.Axes[i] = axis / out.Scale[i];
			}
			out.Origin = glm::vec3(world * glm::vec4(center, 1.0f));
			return true;
		}

		glm::mat4 FrameMatrix(const ColliderFrame& frame) {
			glm::mat4 m(1.0f);
			for (int i = 0; i < 3; ++i) m[i] = glm::vec4(frame.Axes[i], 0.0f);
			m[3] = glm::vec4(frame.Origin, 1.0f);
			return m;
		}

		// Half circle from axisA through axisB to -axisA.
		void DrawWireArc(Renderer3D& renderer, const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB, float radius, const Color& color) {
			constexpr int k_Segments = 24;
			glm::vec3 previous = center + axisA * radius;
			for (int i = 1; i <= k_Segments; ++i) {
				const float angle = static_cast<float>(i) / k_Segments * glm::pi<float>();
				const glm::vec3 point = center + (axisA * std::cos(angle) + axisB * std::sin(angle)) * radius;
				renderer.DrawLine(previous, point, color);
				previous = point;
			}
		}

		void DrawCapsule(Renderer3D& renderer, const ColliderFrame& frame, const CapsuleCollider3DComponent& c, const Color& color) {
			const int axis = static_cast<int>(c.Direction);
			const glm::vec3 up = frame.Axes[axis];
			const glm::vec3 side = frame.Axes[(axis + 1) % 3];
			const glm::vec3 front = frame.Axes[(axis + 2) % 3];
			const float radius = std::max(std::abs(c.Radius) * std::max(frame.Scale[(axis + 1) % 3], frame.Scale[(axis + 2) % 3]), 1e-3f);
			const float halfCylinder = std::max(std::abs(c.Height) * frame.Scale[axis] * 0.5f - radius, 0.0f);
			const glm::vec3 top = frame.Origin + up * halfCylinder;
			const glm::vec3 bottom = frame.Origin - up * halfCylinder;
			renderer.DrawWireCircle(top, side, front, radius, color);
			renderer.DrawWireCircle(bottom, side, front, radius, color);
			for (const glm::vec3& offset : { side, -side, front, -front }) {
				renderer.DrawLine(top + offset * radius, bottom + offset * radius, color);
			}
			DrawWireArc(renderer, top, side, up, radius, color);
			DrawWireArc(renderer, top, front, up, radius, color);
			DrawWireArc(renderer, bottom, side, -up, radius, color);
			DrawWireArc(renderer, bottom, front, -up, radius, color);
		}

		const PrimitiveMeshData& PrimitiveMeshFor(PrimitiveMeshType type) {
			static std::array<PrimitiveMeshData, k_PrimitiveMeshTypeCount> s_Meshes;
			const size_t index = std::min(static_cast<size_t>(type), k_PrimitiveMeshTypeCount - 1);
			if (s_Meshes[index].Indices.empty()) s_Meshes[index] = BuildPrimitiveMesh(static_cast<PrimitiveMeshType>(index));
			return s_Meshes[index];
		}

		void DrawMeshCollider(Renderer3D& renderer, const Scene& scene, EntityHandle entity, const glm::mat4& world,
			const MeshCollider3DComponent& c, const Color& color) {
			const auto* meshRenderer = c.UseMeshRenderer ? scene.GetRegistry().try_get<MeshRendererComponent>(entity) : nullptr;
			const uint64_t customMesh = meshRenderer ? meshRenderer->CustomMesh : c.CustomMesh;
			const PrimitiveMeshType primitive = meshRenderer ? meshRenderer->Mesh : c.Mesh;
			const PrimitiveMeshData* mesh = nullptr;
			glm::vec3 boundsMin(-0.5f), boundsMax(0.5f);
			if (customMesh != 0) {
				const MeshAsset* asset = MeshLibrary::Get(customMesh);
				if (!asset) return;
				mesh = &asset->Data;
				boundsMin = asset->BoundsMin;
				boundsMax = asset->BoundsMax;
			}
			else {
				mesh = &PrimitiveMeshFor(primitive);
			}

			// Mesh points live in the entity's space shifted by Center, matching the physics shape.
			const glm::mat4 meshWorld = world * glm::translate(glm::mat4(1.0f), glm::vec3(c.Center));
			// A convex Plane collides as a thin box (it has no hull).
			if (c.Convex && customMesh == 0 && primitive == PrimitiveMeshType::Plane) {
				renderer.DrawWireBox(meshWorld, { 0.5f, 0.01f, 0.5f }, color);
				return;
			}
			// Jolt builds a custom mesh's convex hull at play time; its bounds are the preview of it.
			if ((c.Convex && customMesh != 0) || mesh->Indices.size() / 3 > k_MaxMeshGizmoTriangles) {
				const glm::vec3 half = glm::max((boundsMax - boundsMin) * 0.5f, glm::vec3(0.005f));
				renderer.DrawWireBox(meshWorld * glm::translate(glm::mat4(1.0f), (boundsMin + boundsMax) * 0.5f), half, color);
				return;
			}

			std::unordered_set<uint64_t> drawn;
			drawn.reserve(mesh->Indices.size());
			auto point = [&](uint32_t index) {
				const MeshVertex& v = mesh->Vertices[index];
				return glm::vec3(meshWorld * glm::vec4(v.Position[0], v.Position[1], v.Position[2], 1.0f));
			};
			for (size_t i = 0; i + 2 < mesh->Indices.size(); i += 3) {
				for (int edge = 0; edge < 3; ++edge) {
					uint32_t a = mesh->Indices[i + edge];
					uint32_t b = mesh->Indices[i + (edge + 1) % 3];
					if (a > b) std::swap(a, b);
					if (!drawn.insert((static_cast<uint64_t>(a) << 32) | b).second) continue;
					renderer.DrawLine(point(a), point(b), color);
				}
			}
		}

		void DrawContact(Renderer3D& renderer, const Vec3& point, const Vec3& normal, bool isTrigger) {
			const Color color = isTrigger ? k_TriggerContactColor : k_ContactColor;
			const glm::vec3 p(point);
			constexpr float k_Cross = 0.08f;
			renderer.DrawLine(p - glm::vec3(k_Cross, 0, 0), p + glm::vec3(k_Cross, 0, 0), color);
			renderer.DrawLine(p - glm::vec3(0, k_Cross, 0), p + glm::vec3(0, k_Cross, 0), color);
			renderer.DrawLine(p - glm::vec3(0, 0, k_Cross), p + glm::vec3(0, 0, k_Cross), color);
			const glm::vec3 n(normal);
			// Both directions: whether it points into the ground depends on which body Jolt lists first.
			if (glm::dot(n, n) > 1e-8f) renderer.DrawLine(p - glm::normalize(n) * 0.4f, p + glm::normalize(n) * 0.4f, color);
		}
	}

	void DrawColliders(Renderer3D& renderer, const Scene& scene, EntityHandle entity, const glm::mat4& world) {
		const auto& registry = scene.GetRegistry();
		auto colorOf = [](const Collider3DBase& c) { return c.IsTrigger ? k_TriggerColor : k_ColliderColor; };
		renderer.SetLinesSeeThrough(true);
		ColliderFrame frame;
		if (const auto* box = registry.try_get<BoxCollider3DComponent>(entity); box && MakeColliderFrame(world, box->Center, frame)) {
			renderer.DrawWireBox(FrameMatrix(frame), glm::abs(glm::vec3(box->Size)) * frame.Scale * 0.5f, colorOf(*box));
		}
		if (const auto* sphere = registry.try_get<SphereCollider3DComponent>(entity); sphere && MakeColliderFrame(world, sphere->Center, frame)) {
			const float radius = std::max(std::abs(sphere->Radius) * std::max({ frame.Scale.x, frame.Scale.y, frame.Scale.z }), 1e-3f);
			for (int i = 0; i < 3; ++i) {
				renderer.DrawWireCircle(frame.Origin, frame.Axes[i], frame.Axes[(i + 1) % 3], radius, colorOf(*sphere));
			}
		}
		if (const auto* capsule = registry.try_get<CapsuleCollider3DComponent>(entity); capsule && MakeColliderFrame(world, capsule->Center, frame)) {
			DrawCapsule(renderer, frame, *capsule, colorOf(*capsule));
		}
		if (const auto* mesh = registry.try_get<MeshCollider3DComponent>(entity)) {
			// Every edge of a mesh is drawn, so it is lighter than the simple shapes.
			Color color = colorOf(*mesh);
			color.a = 0.55f;
			DrawMeshCollider(renderer, scene, entity, world, *mesh, color);
		}
		renderer.SetLinesSeeThrough(false);
	}

	void DrawScene(Renderer3D& renderer, Scene& scene, bool contacts) {
		const auto& registry = scene.GetRegistry();
		std::unordered_set<EntityHandle> drawn;
		const auto drawAll = [&](auto view) {
			for (const EntityHandle entity : view) {
				if (!drawn.insert(entity).second) continue;
				DrawColliders(renderer, scene, entity, view.template get<Transform3DComponent>(entity).WorldMatrix);
			}
		};
		drawAll(registry.view<Transform3DComponent, BoxCollider3DComponent>());
		drawAll(registry.view<Transform3DComponent, SphereCollider3DComponent>());
		drawAll(registry.view<Transform3DComponent, CapsuleCollider3DComponent>());
		drawAll(registry.view<Transform3DComponent, MeshCollider3DComponent>());

		if (!contacts) return;
		if (PhysicsWorld3D* world = PhysicsSystem3D::FindWorld(scene, false)) {
			renderer.SetLinesSeeThrough(true);
			world->ForEachActiveContact([&renderer](const Vec3& point, const Vec3& normal, bool isTrigger) {
				DrawContact(renderer, point, normal, isTrigger);
			});
			renderer.SetLinesSeeThrough(false);
		}
	}

} // namespace Index::PhysicsDebugDraw3D
