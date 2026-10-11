#pragma once
#include "Collections/Vec2.hpp"
#include "Scene/EntityHandle.hpp"

namespace Index {
	class Scene;

	struct Collision2D {
		EntityHandle entityA;
		EntityHandle entityB;
		Scene* sceneA = nullptr;
		Scene* sceneB = nullptr;
		Vec2 contactPoint{ 0.0f, 0.0f };
		// Points from A to B; zero when unknown.
		Vec2 normal{ 0.0f, 0.0f };
		// Sensor overlap: routed to OnTrigger* instead of OnCollision*.
		bool isTrigger = false;

		template<typename Callback>
		void ForEachParticipant(Callback&& callback) const {
			callback(sceneA, entityA, sceneB, entityB);
			if (sceneB != sceneA || entityB != entityA) {
				callback(sceneB, entityB, sceneA, entityA);
			}
		}
	};
}
