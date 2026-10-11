#include "pch.hpp"
#include "Physics/IndexPhysicsWorld2D.hpp"

#include <BoxCollider.hpp>
#include <CircleCollider.hpp>

namespace Index {

	IndexPhysicsWorld2D::IndexPhysicsWorld2D() : m_World() {}

	IndexPhysicsWorld2D::IndexPhysicsWorld2D(const IndexPhys::WorldSettings& settings)
		: m_World(settings) {
	}

	IndexPhysicsWorld2D::~IndexPhysicsWorld2D() = default;

	void IndexPhysicsWorld2D::Step(float dt) {
		m_World.Step(dt);
		DispatchContacts();
	}

	void IndexPhysicsWorld2D::Destroy() {
		m_ContactCallbacks.clear();
		m_BodyToEntity.clear();
		m_BodyToScene.clear();
		m_AttachedColliderKind.clear();
		m_PreviousContacts.clear();

		for (auto& [key, body] : m_Bodies) {
			m_World.DetachCollider(*body);
			m_World.UnregisterBody(*body);
		}
		for (auto& [key, collider] : m_Colliders) {
			(void)key;
			m_World.UnregisterCollider(*collider);
		}

		m_Bodies.clear();
		m_Colliders.clear();
	}

	IndexPhys::Body* IndexPhysicsWorld2D::CreateBody(EntityHandle entity, IndexPhys::BodyType type, Scene* scene) {
		const BodyKey key{ scene, static_cast<uint32_t>(entity) };
		if (m_Bodies.count(key)) {
			IDX_CORE_WARN_TAG("IndexPhysics", "Entity already has a Index-Physics body");
			return m_Bodies[key].get();
		}

		auto body = std::make_unique<IndexPhys::Body>(type);
		IndexPhys::Body* ptr = body.get();
		m_World.RegisterBody(*ptr);
		m_BodyToEntity[ptr] = entity;
		m_BodyToScene[ptr] = scene;
		m_Bodies[key] = std::move(body);
		return ptr;
	}

	void IndexPhysicsWorld2D::DestroyBody(EntityHandle entity, Scene* scene) {
		const BodyKey key{ scene, static_cast<uint32_t>(entity) };

		auto it = m_Bodies.find(key);
		if (it != m_Bodies.end()) {
			IndexPhys::Body* ptr = it->second.get();
			// Collider components own their allocations, so only detach them here.
			m_World.DetachCollider(*ptr);
			m_World.UnregisterBody(*ptr);
			m_BodyToEntity.erase(ptr);
			m_BodyToScene.erase(ptr);
			m_Bodies.erase(it);
		}
		m_AttachedColliderKind.erase(key);
		m_ContactCallbacks.erase(key);

		// Drop any cached contact pair referencing this entity so the next
		// DispatchScriptContacts pass doesn't synthesise a phantom Exit for
		// an entity that no longer exists.
		for (auto pairIt = m_PreviousContacts.begin(); pairIt != m_PreviousContacts.end(); ) {
			if (pairIt->entityA == key || pairIt->entityB == key) {
				pairIt = m_PreviousContacts.erase(pairIt);
			} else {
				++pairIt;
			}
		}
	}

	IndexPhys::Body* IndexPhysicsWorld2D::GetBody(EntityHandle entity, Scene* scene) {
		const BodyKey key{ scene, static_cast<uint32_t>(entity) };
		auto it = m_Bodies.find(key);
		return it != m_Bodies.end() ? it->second.get() : nullptr;
	}

	bool IndexPhysicsWorld2D::ResolveColliderEntity(const IndexPhys::Collider* collider, Scene*& outScene, EntityHandle& outEntity) const {
		outScene = nullptr;
		outEntity = entt::null;
		if (!collider) return false;

		// The collider's body carries the entity/scene mapping; an unattached collider has none.
		IndexPhys::Body* body = const_cast<IndexPhys::Collider*>(collider)->GetBody();
		if (!body) return false;

		auto itEntity = m_BodyToEntity.find(body);
		if (itEntity == m_BodyToEntity.end()) return false;
		outEntity = itEntity->second;

		if (auto itScene = m_BodyToScene.find(body); itScene != m_BodyToScene.end()) {
			outScene = itScene->second;
		}
		return true;
	}

	IndexPhys::BoxCollider* IndexPhysicsWorld2D::CreateBoxCollider(EntityHandle entity, const Vec2& halfExtents, Scene* scene) {
		const BodyKey key{ scene, static_cast<uint32_t>(entity) };

		// Replace only the BOX collider — keeps a coexisting circle collider's pointer valid.
		DestroyCollider(entity, FastColliderKind::Box, scene);

		auto collider = std::make_unique<IndexPhys::BoxCollider>(IndexPhys::Vec2(halfExtents.x, halfExtents.y));
		IndexPhys::BoxCollider* ptr = collider.get();
		m_World.RegisterCollider(*ptr);

		// Only attach if nothing else is currently attached: deserialization/scripting can produce dual colliders; the second just registers without attaching.
		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()
			&& m_AttachedColliderKind.find(key) == m_AttachedColliderKind.end()) {
			m_World.AttachCollider(*bodyIt->second, *ptr);
			m_AttachedColliderKind[key] = FastColliderKind::Box;
		}

		m_Colliders[ColliderKey{ key, FastColliderKind::Box }] = std::move(collider);
		return ptr;
	}

	IndexPhys::CircleCollider* IndexPhysicsWorld2D::CreateCircleCollider(EntityHandle entity, float radius, Scene* scene) {
		const BodyKey key{ scene, static_cast<uint32_t>(entity) };

		DestroyCollider(entity, FastColliderKind::Circle, scene);

		auto collider = std::make_unique<IndexPhys::CircleCollider>(radius);
		IndexPhys::CircleCollider* ptr = collider.get();
		m_World.RegisterCollider(*ptr);

		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()
			&& m_AttachedColliderKind.find(key) == m_AttachedColliderKind.end()) {
			m_World.AttachCollider(*bodyIt->second, *ptr);
			m_AttachedColliderKind[key] = FastColliderKind::Circle;
		}

		m_Colliders[ColliderKey{ key, FastColliderKind::Circle }] = std::move(collider);
		return ptr;
	}

	void IndexPhysicsWorld2D::DestroyCollider(EntityHandle entity, FastColliderKind kind, Scene* scene) {
		const BodyKey key{ scene, static_cast<uint32_t>(entity) };
		auto it = m_Colliders.find(ColliderKey{ key, kind });
		if (it == m_Colliders.end()) return;

		IndexPhys::Collider* ptr = it->second.get();

		// Only detach if THIS kind is the one currently attached to the
		// body. Detaching when a different kind is attached would silently
		// strip the wrong collider from the body's collision state.
		auto bodyIt = m_Bodies.find(key);
		auto attachedIt = m_AttachedColliderKind.find(key);
		const bool wasAttached =
			bodyIt != m_Bodies.end()
			&& attachedIt != m_AttachedColliderKind.end()
			&& attachedIt->second == kind;
		if (wasAttached) {
			m_World.DetachCollider(*bodyIt->second);
			m_AttachedColliderKind.erase(attachedIt);
		}

		m_World.UnregisterCollider(*ptr);
		m_Colliders.erase(it);

		// If a different-kind collider still exists on this entity (the
		// dual-collider case made possible by deserialization), promote
		// it to the active attachment so the body keeps colliding.
		if (wasAttached && bodyIt != m_Bodies.end()) {
			const FastColliderKind otherKind =
				kind == FastColliderKind::Box ? FastColliderKind::Circle : FastColliderKind::Box;
			auto otherIt = m_Colliders.find(ColliderKey{ key, otherKind });
			if (otherIt != m_Colliders.end()) {
				m_World.AttachCollider(*bodyIt->second, *otherIt->second);
				m_AttachedColliderKind[key] = otherKind;
			}
		}
	}

	void IndexPhysicsWorld2D::PurgeBodiesForScene(Scene* scene) {
		if (!scene) return;

		std::vector<BodyKey> bodies;
		for (const auto& [key, body] : m_Bodies) {
			(void)body;
			if (key.scene == scene) bodies.push_back(key);
		}

		for (const BodyKey& key : bodies) {
			const EntityHandle entity = static_cast<EntityHandle>(static_cast<entt::entity>(key.entity));
			DestroyBody(entity, key.scene);
		}

		for (auto it = m_Colliders.begin(); it != m_Colliders.end();) {
			if (it->first.owner.scene == scene) {
				m_World.UnregisterCollider(*it->second);
				it = m_Colliders.erase(it);
			}
			else {
				++it;
			}
		}
		for (auto it = m_AttachedColliderKind.begin(); it != m_AttachedColliderKind.end();) {
			if (it->first.scene == scene) it = m_AttachedColliderKind.erase(it);
			else ++it;
		}
		for (auto it = m_ContactCallbacks.begin(); it != m_ContactCallbacks.end();) {
			if (it->first.scene == scene) it = m_ContactCallbacks.erase(it);
			else ++it;
		}
		for (auto it = m_PreviousContacts.begin(); it != m_PreviousContacts.end();) {
			if (it->entityA.scene == scene || it->entityB.scene == scene) {
				it = m_PreviousContacts.erase(it);
			}
			else {
				++it;
			}
		}
	}

	void IndexPhysicsWorld2D::DestroyAllCollidersOnEntity(EntityHandle entity, Scene* scene) {
		const BodyKey key{ scene, static_cast<uint32_t>(entity) };
		auto bodyIt = m_Bodies.find(key);
		if (bodyIt != m_Bodies.end()) {
			m_World.DetachCollider(*bodyIt->second);
		}
		m_AttachedColliderKind.erase(key);
		for (auto it = m_Colliders.begin(); it != m_Colliders.end();) {
			if (it->first.owner == key) {
				m_World.UnregisterCollider(*it->second);
				it = m_Colliders.erase(it);
			}
			else {
				++it;
			}
		}
	}

	void IndexPhysicsWorld2D::RegisterContactCallback(EntityHandle entity, Scene* scene, IndexContactCallback callback) {
		m_ContactCallbacks[BodyKey{ scene, static_cast<uint32_t>(entity) }] = std::move(callback);
	}

	void IndexPhysicsWorld2D::UnregisterContactCallback(EntityHandle entity, Scene* scene) {
		m_ContactCallbacks.erase(BodyKey{ scene, static_cast<uint32_t>(entity) });
	}

	void IndexPhysicsWorld2D::DispatchScriptContacts(
		const ScriptDispatchCallback& onEnter,
		const ScriptDispatchCallback& onStay,
		const ScriptDispatchCallback& onExit)
	{
		std::unordered_set<ContactPair, ContactPairHash> currentContacts;
		struct ResolvedContact {
			ContactPair Pair;
			Collision2D Collision;
		};
		std::vector<ResolvedContact> currentResolved;

		const auto& contacts = m_World.GetContacts();
		currentContacts.reserve(contacts.size());
		currentResolved.reserve(contacts.size());
		const auto bodyKeyLess = [](const BodyKey& lhs, const BodyKey& rhs) {
			if (lhs.scene != rhs.scene) return std::less<Scene*>{}(lhs.scene, rhs.scene);
			return lhs.entity < rhs.entity;
		};

		for (const auto& contact : contacts) {
			auto itA = m_BodyToEntity.find(contact.bodyA);
			auto itB = m_BodyToEntity.find(contact.bodyB);
			if (itA == m_BodyToEntity.end() || itB == m_BodyToEntity.end()) continue;

			Scene* sceneA = nullptr;
			Scene* sceneB = nullptr;
			if (auto sit = m_BodyToScene.find(contact.bodyA); sit != m_BodyToScene.end()) sceneA = sit->second;
			if (auto sit = m_BodyToScene.find(contact.bodyB); sit != m_BodyToScene.end()) sceneB = sit->second;

			BodyKey a{ sceneA, static_cast<uint32_t>(itA->second) };
			BodyKey b{ sceneB, static_cast<uint32_t>(itB->second) };
			if (bodyKeyLess(b, a)) {
				std::swap(a, b);
			}
			ContactPair pair{ a, b };
			if (!currentContacts.insert(pair).second) {
				continue;
			}

			// After canonical-pair swap the "A" side may not match IndexPhys's A; use body-position midpoint as contact point (same fallback as Box2D path).
			Vec2 midpoint{ 0.0f, 0.0f };
			if (contact.bodyA && contact.bodyB) {
				auto pa = contact.bodyA->GetPosition();
				auto pb = contact.bodyB->GetPosition();
				midpoint = { (pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f };
			}

			Collision2D collision{};
			collision.entityA = static_cast<EntityHandle>(static_cast<entt::entity>(pair.entityA.entity));
			collision.entityB = static_cast<EntityHandle>(static_cast<entt::entity>(pair.entityB.entity));
			collision.sceneA = pair.entityA.scene;
			collision.sceneB = pair.entityB.scene;
			collision.contactPoint = midpoint;

			currentResolved.push_back({ pair, collision });
		}

		// Enter + Stay
		for (const auto& resolved : currentResolved) {
			if (m_PreviousContacts.count(resolved.Pair)) {
				if (onStay) onStay(resolved.Collision);
			} else {
				if (onEnter) onEnter(resolved.Collision);
			}
		}

		// Exit — pairs that were active last frame but aren't anymore.
		if (onExit) {
			for (const ContactPair& pair : m_PreviousContacts) {
				if (currentContacts.count(pair)) continue;
				Collision2D collision{};
				collision.entityA = static_cast<EntityHandle>(static_cast<entt::entity>(pair.entityA.entity));
				collision.entityB = static_cast<EntityHandle>(static_cast<entt::entity>(pair.entityB.entity));
				collision.sceneA = pair.entityA.scene;
				collision.sceneB = pair.entityB.scene;
				onExit(collision);
			}
		}

		m_PreviousContacts = std::move(currentContacts);
	}

	void IndexPhysicsWorld2D::DispatchContacts() {
		const auto& contacts = m_World.GetContacts();
		for (const auto& contact : contacts) {
			EntityHandle entityA = entt::null;
			EntityHandle entityB = entt::null;

			auto itA = m_BodyToEntity.find(contact.bodyA);
			auto itB = m_BodyToEntity.find(contact.bodyB);
			if (itA != m_BodyToEntity.end()) entityA = itA->second;
			if (itB != m_BodyToEntity.end()) entityB = itB->second;

			IndexContact2D indexContact{
				entityA, entityB,
				{ contact.normal.x, contact.normal.y },
				contact.penetration
			};

			// Copy callbacks out before invoking: a callback may call UnregisterContactCallback, erasing the map entry while operator() is still on the stack.
			IndexContactCallback cbA;
			IndexContactCallback cbB;
			if (entityA != entt::null) {
				Scene* sceneA = nullptr;
				if (auto sceneIt = m_BodyToScene.find(contact.bodyA); sceneIt != m_BodyToScene.end()) {
					sceneA = sceneIt->second;
				}
				auto cbIt = m_ContactCallbacks.find(BodyKey{ sceneA, static_cast<uint32_t>(entityA) });
				if (cbIt != m_ContactCallbacks.end()) cbA = cbIt->second;
			}
			if (entityB != entt::null) {
				Scene* sceneB = nullptr;
				if (auto sceneIt = m_BodyToScene.find(contact.bodyB); sceneIt != m_BodyToScene.end()) {
					sceneB = sceneIt->second;
				}
				auto cbIt = m_ContactCallbacks.find(BodyKey{ sceneB, static_cast<uint32_t>(entityB) });
				if (cbIt != m_ContactCallbacks.end()) cbB = cbIt->second;
			}

			if (cbA) cbA(indexContact);
			if (cbB) cbB(indexContact);
		}
	}

}
