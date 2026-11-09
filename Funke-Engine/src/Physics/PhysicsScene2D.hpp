#pragma once
#include "Scene/SceneExtensions.hpp"
#include "Scene/EntityHandle.hpp"
#include <entt/entity/fwd.hpp>

namespace Funke {
	class PhysicsScene2D {
	public:
		explicit PhysicsScene2D(Scene& scene) : m_Scene(scene) {}
		static SceneExtension Extension();

	private:
		void Connect();
		void Disconnect();
		void Initialize();
		void SetEnabled(EntityHandle entity, bool enabled);
		void OnRigidBody2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnRigidBody2DComponentDestroy(entt::registry& registry, EntityHandle entity);
		void OnBoxCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnBoxCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity);
		void OnCircleCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnCircleCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity);
		void OnPolygonCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity);
		void OnPolygonCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity);
		template <typename TJoint>
		void OnJoint2DComponentDestroy(entt::registry& registry, EntityHandle entity);
		Scene& m_Scene;
	};
}
