#include "pch.hpp"
#include "Animation/AnimationViewer.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Window.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#if FUNKE_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#endif
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Graphics/DynamicRenderData.hpp"
#endif

#include <limits>
#include <vector>

namespace Funke {
	namespace {
		// Bounds come from the last drawn pose and are refreshed every few frames; limbs swing past them in between.
		constexpr float k_MotionScale = 1.5f;
		constexpr float k_MotionPadding = 0.25f;

#if FUNKE_WITH_3D
		// The surface the game camera renders to: the editor's Game View or aspect-locked region, else the window.
		float GameAspect() {
			const Window::UIRegion region = Window::GetUIRegion();
			if (region.IsActive()) return static_cast<float>(region.Width) / static_cast<float>(region.Height);
			const Viewport* viewport = Window::GetMainViewport();
			if (viewport && viewport->GetWidth() > 0 && viewport->GetHeight() > 0) return viewport->GetAspect();
			return 16.0f / 9.0f;
		}

		// Object-space box of what the entity draws in 3D; bind pose for skinned models.
		bool LocalRenderBox(const entt::registry& registry, EntityHandle entity, glm::vec3& outMin, glm::vec3& outMax) {
			if (const auto* mesh = registry.try_get<MeshRendererComponent>(entity)) {
				if (mesh->CustomMesh == 0) {
					// Built-in primitives are one unit in size.
					outMin = glm::vec3(-0.5f);
					outMax = glm::vec3(0.5f);
					return true;
				}
				const MeshAsset* asset = MeshLibrary::Get(mesh->CustomMesh);
				if (!asset) return false;
				outMin = asset->BoundsMin;
				outMax = asset->BoundsMax;
				return true;
			}
			if (const auto* skinned = registry.try_get<SkinnedMeshRendererComponent>(entity)) {
				const MeshAsset* asset = MeshLibrary::Get(skinned->Model);
				if (!asset) return false;
				outMin = asset->BoundsMin;
				outMax = asset->BoundsMax;
				return true;
			}
			return false;
		}
#endif

		void TransformBox(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& world, glm::vec3& outMin, glm::vec3& outMax) {
			// Arvo: the world box of a transformed local box.
			const glm::vec3 center(world * glm::vec4((localMin + localMax) * 0.5f, 1.0f));
			const glm::vec3 half = (localMax - localMin) * 0.5f;
			const glm::vec3 extent = glm::abs(glm::vec3(world[0])) * half.x + glm::abs(glm::vec3(world[1])) * half.y
				+ glm::abs(glm::vec3(world[2])) * half.z;
			outMin = center - extent;
			outMax = center + extent;
		}

		AnimationBounds SphereAround(const glm::vec3& worldMin, const glm::vec3& worldMax) {
			AnimationBounds bounds;
			bounds.Kind = AnimationBounds::Space::World3D;
			bounds.Center = (worldMin + worldMax) * 0.5f;
			bounds.Radius = glm::length(worldMax - worldMin) * 0.5f * k_MotionScale + k_MotionPadding;
			return bounds;
		}
	}

	AnimationViewer AnimationViewer::Find(Scene& scene) {
		AnimationViewer viewer;
#if FUNKE_WITH_3D
		// Renderer3D draws every loaded scene through the first loaded scene's camera.
		Scene* cameraScene = nullptr;
		EntityHandle camera = entt::null;
		bool sceneLoaded = false;
		SceneManager::ForeachLoadedScene([&](Scene& loaded) {
			sceneLoaded |= &loaded == &scene;
			if (cameraScene) return;
			const EntityHandle found = Camera3DComponent::FindMain(loaded);
			if (found != entt::null) {
				cameraScene = &loaded;
				camera = found;
			}
		});
		// Detached scenes (previews, tests) are not drawn through it.
		if (!sceneLoaded) {
			camera = Camera3DComponent::FindMain(scene);
			cameraScene = camera != entt::null ? &scene : nullptr;
		}
		if (cameraScene) {
			const entt::registry& registry = cameraScene->GetRegistry();
			// Scripts may have moved the camera since the hierarchy pass.
			const glm::mat4 world = Transform3DHierarchySystem::ComputeWorldMatrix(registry, camera);
			const glm::mat4 projection = registry.get<Camera3DComponent>(camera).GetProjectionMatrix(GameAspect());
			viewer.Set3D(projection * Camera3DComponent::ViewFromWorld(world), glm::vec3(world[3]));
		}
#endif
#if FUNKE_WITH_2D
		const Camera2DComponent* camera2D = scene.GetMainCamera();
		if (!camera2D) camera2D = Camera2DComponent::GetMain();
		// Without a viewport (headless) the camera has no view rectangle yet.
		if (camera2D && camera2D->GetViewport()) {
			const AABB view = camera2D->GetViewportAABB();
			if (view.Max.x > view.Min.x && view.Max.y > view.Min.y) viewer.Set2D(view, camera2D->GetPosition());
		}
#endif
		return viewer;
	}

	void AnimationViewer::Set3D(const glm::mat4& viewProjection, const glm::vec3& position) {
		Has3D = true;
		Position3D = position;
		const auto row = [&](int i) { return glm::vec4(viewProjection[0][i], viewProjection[1][i], viewProjection[2][i], viewProjection[3][i]); };
		const glm::vec4 x = row(0), y = row(1), z = row(2), w = row(3);
		Planes = { w + x, w - x, w + y, w - y, z, w - z };
		for (glm::vec4& plane : Planes) {
			const float length = glm::length(glm::vec3(plane));
			if (length > 0.0f) plane /= length;
		}
	}

	void AnimationViewer::Set2D(const AABB& view, const Vec2& position) {
		Has2D = true;
		View2D = view;
		Position2D = position;
	}

	float AnimationViewer::DistanceTo(const Scene& scene, EntityHandle entity) const {
		const entt::registry& registry = scene.GetRegistry();
#if FUNKE_WITH_3D
		if (const auto* transform = registry.try_get<Transform3DComponent>(entity)) {
			return Has3D ? glm::distance(Position3D, glm::vec3(transform->WorldMatrix[3])) : 0.0f;
		}
#endif
#if FUNKE_WITH_2D
		if (const auto* transform = registry.try_get<Transform2DComponent>(entity)) {
			return Has2D ? glm::distance(Position2D, transform->Position) : 0.0f;
		}
#endif
		(void)registry;
		(void)entity;
		return 0.0f;
	}

	AnimationBounds AnimationBounds::OfHierarchy(const Scene& scene, EntityHandle entity) {
		const entt::registry& registry = scene.GetRegistry();
		thread_local std::vector<EntityHandle> t_Pending;
		t_Pending.clear();
		t_Pending.push_back(entity);
		glm::vec3 min3(std::numeric_limits<float>::max());
		glm::vec3 max3(std::numeric_limits<float>::lowest());
		bool any3 = false;
		Vec2 min2(std::numeric_limits<float>::max());
		Vec2 max2(std::numeric_limits<float>::lowest());
		bool any2 = false;
		while (!t_Pending.empty()) {
			const EntityHandle current = t_Pending.back();
			t_Pending.pop_back();
			if (!registry.valid(current) || (current != entity && registry.all_of<DisabledTag>(current))) continue;
			if (const auto* hierarchy = registry.try_get<HierarchyComponent>(current)) {
				t_Pending.insert(t_Pending.end(), hierarchy->Children.begin(), hierarchy->Children.end());
			}
#if FUNKE_WITH_3D
			if (const auto* transform = registry.try_get<Transform3DComponent>(current)) {
				glm::vec3 localMin, localMax;
				if (LocalRenderBox(registry, current, localMin, localMax)) {
					glm::vec3 worldMin, worldMax;
					TransformBox(localMin, localMax, transform->WorldMatrix, worldMin, worldMax);
					min3 = glm::min(min3, worldMin);
					max3 = glm::max(max3, worldMax);
					any3 = true;
				}
				continue;
			}
#endif
#if FUNKE_WITH_2D
			if (registry.all_of<SpriteRendererComponent>(current)) {
				// The renderer's quad from the last frame; a sprite it has not placed yet can't be judged.
				const auto* drawn = registry.try_get<DynamicRenderData>(current);
				if (!drawn || !drawn->Valid) return {};
				min2 = glm::min(min2, drawn->CachedAABB.Min);
				max2 = glm::max(max2, drawn->CachedAABB.Max);
				any2 = true;
			}
#endif
		}
		if (any3) return SphereAround(min3, max3);
		if (!any2) return {};
		AnimationBounds bounds;
		bounds.Kind = Space::World2D;
		const Vec2 grow = (max2 - min2) * 0.5f * (k_MotionScale - 1.0f) + Vec2(k_MotionPadding);
		bounds.Box = AABB(min2 - grow, max2 + grow);
		return bounds;
	}

	AnimationBounds AnimationBounds::FromLocalBox(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& world) {
		glm::vec3 worldMin, worldMax;
		TransformBox(localMin, localMax, world, worldMin, worldMax);
		return SphereAround(worldMin, worldMax);
	}

	bool AnimationBounds::IsVisible(const AnimationViewer& viewer) const {
		switch (Kind) {
		case Space::World3D:
			if (!viewer.Has3D) return true;
			for (const glm::vec4& plane : viewer.Planes) {
				if (glm::dot(glm::vec3(plane), Center) + plane.w < -Radius) return false;
			}
			return true;
		case Space::World2D:
			return !viewer.Has2D || AABB::Intersects(viewer.View2D, Box);
		case Space::None:
		default:
			return true;
		}
	}
}
