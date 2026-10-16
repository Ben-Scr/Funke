#include "pch.hpp"

#include <Components/Physics/Rigidbody2DComponent.hpp>
#include <Components/Physics/BoxCollider2DComponent.hpp>
#include <Components/Physics/CircleCollider2DComponent.hpp>
#include <Components/Physics/PolygonCollider2DComponent.hpp>
#include <Components/General/Transform2DComponent.hpp>
#include <Components/General/HierarchyComponent.hpp>
#include <Components/Tags.hpp>

#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Physics/Collision2D.hpp"

#include "Core/Application.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptSystem.hpp"


namespace Funke {
	bool PhysicsSystem2D::s_IsEnabled = true;
	std::optional<Box2DWorld> PhysicsSystem2D::s_MainWorld;

	namespace {
		std::vector<PhysicsExtension2D>& Extensions() {
			static std::vector<PhysicsExtension2D> s_Extensions;
			return s_Extensions;
		}

		bool OwnedByExtension(const entt::registry& registry, EntityHandle entity) {
			for (const PhysicsExtension2D& extension : Extensions()) {
				if (extension.BodyComponent == 0) continue;
				const auto* storage = registry.storage(extension.BodyComponent);
				if (storage && storage->contains(entity)) return true;
			}
			return false;
		}
	}

	void PhysicsSystem2D::RegisterExtension(PhysicsExtension2D extension) {
		UnregisterExtension(extension.Name);
		Extensions().push_back(std::move(extension));
	}

	void PhysicsSystem2D::UnregisterExtension(std::string_view name) {
		auto& extensions = Extensions();
		const auto it = std::find_if(extensions.begin(), extensions.end(),
			[&](const PhysicsExtension2D& e) { return e.Name == name; });
		if (it == extensions.end()) return;
		if (it->Shutdown) it->Shutdown();
		extensions.erase(it);
	}

	const std::vector<PhysicsExtension2D>& PhysicsSystem2D::GetExtensions() {
		return Extensions();
	}

	bool PhysicsSystem2D::IsTransformOwnedByPhysics(const entt::registry& registry, EntityHandle entity) {
		return registry.all_of<Rigidbody2DComponent>(entity) || OwnedByExtension(registry, entity);
	}

	void PhysicsSystem2D::ComposeWorldFromLocal(entt::registry& registry, EntityHandle entity, Transform2DComponent& tf) {
		const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(entity);
		const EntityHandle parent = hierarchy ? hierarchy->Parent : entt::null;
		const Transform2DComponent* parentTf = (parent != entt::null)
			? registry.try_get<Transform2DComponent>(parent) : nullptr;
		if (!parentTf) {
			tf.Position = tf.LocalPosition;
			tf.Rotation = tf.LocalRotation;
			tf.Scale = tf.LocalScale;
			return;
		}
		tf.Position = parentTf->TransformPoint(tf.LocalPosition);
		tf.Rotation = parentTf->Rotation + tf.LocalRotation;
		tf.Scale = { parentTf->Scale.x * tf.LocalScale.x,
					 parentTf->Scale.y * tf.LocalScale.y };
	}

	void PhysicsSystem2D::DecomposeWorldToLocal(entt::registry& registry, EntityHandle entity, Transform2DComponent& tf) {
		const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(entity);
		const EntityHandle parent = hierarchy ? hierarchy->Parent : entt::null;
		const Transform2DComponent* parentTf = (parent != entt::null)
			? registry.try_get<Transform2DComponent>(parent) : nullptr;
		if (!parentTf) {
			tf.LocalPosition = tf.Position;
			tf.LocalRotation = tf.Rotation;
			tf.LocalScale = tf.Scale;
			return;
		}
		const Vec2 translated{ tf.Position.x - parentTf->Position.x,
							   tf.Position.y - parentTf->Position.y };
		const Vec2 unrotated = Rotate(translated, -parentTf->Rotation);
		tf.LocalPosition = {
			std::abs(parentTf->Scale.x) > 0.00001f ? unrotated.x / parentTf->Scale.x : unrotated.x,
			std::abs(parentTf->Scale.y) > 0.00001f ? unrotated.y / parentTf->Scale.y : unrotated.y };
		tf.LocalRotation = tf.Rotation - parentTf->Rotation;
		tf.LocalScale = {
			std::abs(parentTf->Scale.x) > 0.00001f ? tf.Scale.x / parentTf->Scale.x : tf.Scale.x,
			std::abs(parentTf->Scale.y) > 0.00001f ? tf.Scale.y / parentTf->Scale.y : tf.Scale.y };
	}

	void PhysicsSystem2D::Initialize() {
		s_MainWorld.emplace();
		FNK_CORE_INFO_TAG("PhysicsSystem", "Box2D initialized");
	}

	void PhysicsSystem2D::SyncTransformsToPhysics() {
		// Collider shape rebuild FIRST: body loops clear the dirty flag, so shape sync must read it before they do.
		SceneManager::ForeachLoadedScene([](Scene& scene) {
			auto& registry = scene.GetRegistry();
			const bool anyExtension = !Extensions().empty();

			for (auto [ent, tf] : registry.view<Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (!tf.IsDirty()) continue;
				if (!registry.all_of<Rigidbody2DComponent>(ent) && !(anyExtension && OwnedByExtension(registry, ent))) continue;
				ComposeWorldFromLocal(registry, ent, tf);
			}

			for (auto [ent, box, tf] : registry.view<BoxCollider2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (tf.IsDirty() && box.IsValid()) {
					box.SyncWithTransform(scene);
				}
			}

			for (auto [ent, circle, tf] : registry.view<CircleCollider2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (tf.IsDirty() && circle.IsValid()) {
					circle.SyncWithTransform(scene);
				}
			}

			for (auto [ent, poly, tf] : registry.view<PolygonCollider2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (tf.IsDirty() && poly.IsValid()) {
					poly.SyncWithTransform(scene);
				}
			}

			// Extensions read the dirty flags before the Box2D body loop clears them.
			for (const PhysicsExtension2D& extension : Extensions()) {
				if (extension.SyncToPhysics) extension.SyncToPhysics(scene);
			}

			for (auto [ent, rb, tf] : registry.view<Rigidbody2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (tf.IsDirty() && rb.IsValid()) {
					rb.SetTransform(tf);
					tf.ClearDirty();
				}
			}

			// Clear dirty for collider-only entities (no body sync touched them).
			for (auto [ent, box, tf] : registry.view<BoxCollider2DComponent, Transform2DComponent>(entt::exclude<DisabledTag, Rigidbody2DComponent>).each()) {
				if (tf.IsDirty()) {
					tf.ClearDirty();
				}
			}
			for (auto [ent, circle, tf] : registry.view<CircleCollider2DComponent, Transform2DComponent>(entt::exclude<DisabledTag, Rigidbody2DComponent>).each()) {
				if (tf.IsDirty()) {
					tf.ClearDirty();
				}
			}
			for (auto [ent, poly, tf] : registry.view<PolygonCollider2DComponent, Transform2DComponent>(entt::exclude<DisabledTag, Rigidbody2DComponent>).each()) {
				if (tf.IsDirty()) {
					tf.ClearDirty();
				}
			}
		});
	}

	void PhysicsSystem2D::Update() {
		if (!s_IsEnabled) return;
		// Editor mode needs this; in play mode FixedUpdate already covers the sync so skip to avoid double-walking dirty flags.
		if (Application::GetIsPlaying() && !Application::IsPaused()) {
			return;
		}
		SyncTransformsToPhysics();
	}

	/* static */ void PhysicsSystem2D::WakeAllBodies() {
		SceneManager::ForeachLoadedScene([](Scene& scene) {
			auto& registry = scene.GetRegistry();
			for (auto [ent, rb] : registry.view<Rigidbody2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (rb.IsValid()) {
					b2Body_SetAwake(rb.GetBodyHandle(), true);
				}
			}
		});
	}

	void PhysicsSystem2D::FixedUpdate(float dt) {
		FUNKE_PROFILE_SCOPE("Physics");
		if (!s_IsEnabled) return;
		if (!s_MainWorld) return;

		SyncTransformsToPhysics();

		s_MainWorld->Step(dt);
		for (const PhysicsExtension2D& extension : Extensions()) {
			if (extension.Step) extension.Step(dt);
		}

		SceneManager::ForeachLoadedScene([](Scene& scene) {
			auto& registry = scene.GetRegistry();

			for (auto [ent, rb, tf] : registry.view<Rigidbody2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (!rb.IsValid()) continue;
				tf.Position = rb.GetPosition();
				tf.Rotation = rb.GetRotation();
				DecomposeWorldToLocal(registry, ent, tf);
				tf.ClearDirty();
			}

			for (const PhysicsExtension2D& extension : Extensions()) {
				if (extension.SyncFromPhysics) extension.SyncFromPhysics(scene);
			}
		});

		// Queued events carry raw Scene*; a callback calling LoadScene (Single) releases those scenes
		// mid-dispatch. Keep them alive until dispatch ends and skip events for scenes unloaded meanwhile.
		std::vector<std::shared_ptr<Scene>> sceneKeepAlive;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			if (auto scene = weakScene.lock()) sceneKeepAlive.push_back(std::move(scene));
		}
		const auto scenesStillLoaded = [](const Collision2D& collision) {
			return (!collision.sceneA || collision.sceneA->IsLoaded())
				&& (!collision.sceneB || collision.sceneB->IsLoaded());
		};
		const PhysicsExtension2D::CollisionHandler onEnter = [&](const Collision2D& collision) {
			if (scenesStillLoaded(collision)) ScriptSystem::DispatchCollisionEnter2D(collision);
		};
		const PhysicsExtension2D::CollisionHandler onStay = [&](const Collision2D& collision) {
			if (scenesStillLoaded(collision)) ScriptSystem::DispatchCollisionStay2D(collision);
		};
		const PhysicsExtension2D::CollisionHandler onExit = [&](const Collision2D& collision) {
			if (scenesStillLoaded(collision)) ScriptSystem::DispatchCollisionExit2D(collision);
		};

		s_MainWorld->GetDispatcher().Process(
			s_MainWorld->GetWorldID(),
			[world = &*s_MainWorld](b2ShapeId shapeId) { return world->ResolveShape(shapeId); },
			onEnter, onExit, onStay);

		// Copy: a collision callback may install or remove a package.
		const std::vector<PhysicsExtension2D> extensions = Extensions();
		for (const PhysicsExtension2D& extension : extensions) {
			if (extension.DispatchCollisions) extension.DispatchCollisions(onEnter, onStay, onExit);
		}
	}

	void PhysicsSystem2D::Shutdown() {
		for (const PhysicsExtension2D& extension : Extensions()) {
			if (extension.Shutdown) extension.Shutdown();
		}
		s_MainWorld.reset();
	}
}
