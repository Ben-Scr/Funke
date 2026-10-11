#pragma once
#include "Collections/Vec2.hpp"
#include "Core/Export.hpp"
#include "Physics/Collision2D.hpp"
#include "Physics/IndexContact2D.hpp"
#include "Physics/IndexPhysicsInterop.hpp"
#include "Scene/EntityHandle.hpp"

#include <PhysicsWorld.hpp>
#include <WorldSettings.hpp>
#include <Contact.hpp>
#include <BoxCollider.hpp>
#include <CircleCollider.hpp>

#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Index {
	class Scene;
	using IndexContactCallback = std::function<void(const IndexContact2D&)>;

	/// Engine-level wrapper around the Index-Physics PhysicsWorld.
	class IndexPhysicsWorld2D {
	public:
		INDEX_PHYSICS_API IndexPhysicsWorld2D();
		INDEX_PHYSICS_API explicit IndexPhysicsWorld2D(const IndexPhys::WorldSettings& settings);
		INDEX_PHYSICS_API ~IndexPhysicsWorld2D();

		INDEX_PHYSICS_API void Step(float dt);
		INDEX_PHYSICS_API void Destroy();

		// Called by PhysicsSystem2D::FixedUpdate after Step so Fast* collision events match Box2D collider timing.
		using ScriptDispatchCallback = std::function<void(const Collision2D&)>;
		INDEX_PHYSICS_API void DispatchScriptContacts(
			const ScriptDispatchCallback& onEnter,
			const ScriptDispatchCallback& onStay,
			const ScriptDispatchCallback& onExit);

		IndexPhys::PhysicsWorld& GetWorld() { return m_World; }
		const IndexPhys::PhysicsWorld& GetWorld() const { return m_World; }

		void SetSettings(const IndexPhys::WorldSettings& settings) { m_World.SetSettings(settings); }
		const IndexPhys::WorldSettings& GetSettings() const { return m_World.GetSettings(); }

		// The scene is part of the key because EnTT handles are only unique within one registry.
		INDEX_PHYSICS_API IndexPhys::Body* CreateBody(EntityHandle entity, IndexPhys::BodyType type, Scene* scene);
		INDEX_PHYSICS_API void DestroyBody(EntityHandle entity, Scene* scene);
		INDEX_PHYSICS_API IndexPhys::Body* GetBody(EntityHandle entity, Scene* scene);

		// Colliders keyed by (entity, kind): programmatic dual-collider adds no longer dangle the existing pointer.
		enum class FastColliderKind : uint8_t {
			Box,
			Circle,
		};

		INDEX_PHYSICS_API IndexPhys::BoxCollider* CreateBoxCollider(EntityHandle entity, const Vec2& halfExtents, Scene* scene);
		INDEX_PHYSICS_API IndexPhys::CircleCollider* CreateCircleCollider(EntityHandle entity, float radius, Scene* scene);
		INDEX_PHYSICS_API void DestroyCollider(EntityHandle entity, FastColliderKind kind, Scene* scene);
		// DestroyBody intentionally does NOT call this: collider components own their lifetime and DestroyAllCollidersOnEntity would dangle their raw m_Collider pointers.
		INDEX_PHYSICS_API void DestroyAllCollidersOnEntity(EntityHandle entity, Scene* scene);

		// Called during scene teardown to remove every map entry keyed by its address.
		INDEX_PHYSICS_API void PurgeBodiesForScene(Scene* scene);

		// Contact callbacks per entity
		INDEX_PHYSICS_API void RegisterContactCallback(EntityHandle entity, Scene* scene, IndexContactCallback callback);
		INDEX_PHYSICS_API void UnregisterContactCallback(EntityHandle entity, Scene* scene);

		size_t GetBodyCount() const { return m_World.GetBodyCount(); }
		size_t GetColliderCount() const { return m_World.GetColliderCount(); }
		size_t GetContactCallbackCount() const { return m_ContactCallbacks.size(); }

		// Map a query-hit collider back to its owning (scene, entity) via its attached
		// body. Returns false when the collider has no registered body (e.g. an
		// unattached dual collider), leaving the out-params at scene=null / entity=null.
		INDEX_PHYSICS_API bool ResolveColliderEntity(const IndexPhys::Collider* collider, Scene*& outScene, EntityHandle& outEntity) const;

	private:
		void DispatchContacts();

		IndexPhys::PhysicsWorld m_World;

		struct BodyKey {
			Scene* scene;
			uint32_t entity;
			bool operator==(const BodyKey& other) const noexcept {
				return scene == other.scene && entity == other.entity;
			}
		};
		struct BodyKeyHash {
			size_t operator()(const BodyKey& key) const noexcept {
				const size_t sceneHash = std::hash<Scene*>{}(key.scene);
				const size_t entityHash = std::hash<uint32_t>{}(key.entity);
				return sceneHash ^ (entityHash + 0x9e3779b9u + (sceneHash << 6) + (sceneHash >> 2));
			}
		};
		struct ColliderKey {
			BodyKey owner;
			FastColliderKind kind;
			bool operator==(const ColliderKey& other) const noexcept {
				return owner == other.owner && kind == other.kind;
			}
		};
		struct ColliderKeyHash {
			size_t operator()(const ColliderKey& key) const noexcept {
				const size_t ownerHash = BodyKeyHash{}(key.owner);
				const size_t kindHash = std::hash<uint8_t>{}(static_cast<uint8_t>(key.kind));
				return ownerHash ^ (kindHash + 0x9e3779b9u + (ownerHash << 6) + (ownerHash >> 2));
			}
		};

		std::unordered_map<BodyKey, std::unique_ptr<IndexPhys::Body>, BodyKeyHash> m_Bodies;
		std::unordered_map<ColliderKey, std::unique_ptr<IndexPhys::Collider>, ColliderKeyHash> m_Colliders;

		// Tracks which kind is currently attached to the body (only one can be live); without this DestroyCollider would detach the wrong collider.
		std::unordered_map<BodyKey, FastColliderKind, BodyKeyHash> m_AttachedColliderKind;

		std::unordered_map<IndexPhys::Body*, EntityHandle> m_BodyToEntity;
		std::unordered_map<IndexPhys::Body*, Scene*> m_BodyToScene;

		// Contact callbacks per entity
		std::unordered_map<BodyKey, IndexContactCallback, BodyKeyHash> m_ContactCallbacks;

		// Last-frame contact pairs for Enter/Stay/Exit derivation.
		struct ContactPair {
			BodyKey entityA{};
			BodyKey entityB{};
			bool operator==(const ContactPair& other) const noexcept {
				return entityA == other.entityA && entityB == other.entityB;
			}
		};
		struct ContactPairHash {
			size_t operator()(const ContactPair& key) const noexcept {
				const size_t aHash = BodyKeyHash{}(key.entityA);
				const size_t bHash = BodyKeyHash{}(key.entityB);
				return aHash ^ (bHash + 0x9e3779b9u + (aHash << 6) + (aHash >> 2));
			}
		};
		std::unordered_set<ContactPair, ContactPairHash> m_PreviousContacts;
	};

}
