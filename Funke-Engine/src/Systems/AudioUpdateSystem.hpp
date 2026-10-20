#pragma once
#include "Audio/AudioSpatial.hpp"
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/SceneSystem.hpp"

#include <entt/entity/fwd.hpp>

namespace Funke {
	class AudioSourceComponent;

	// Plays AudioSources on awake, and keeps spatial audio in place: the listener on the main camera, spatial sources on
	// their entities.
	class FUNKE_API AudioUpdateSystem : public SceneSystem {
	public:
		void Start(Scene& scene) override;
		void Update(Scene& scene) override;

		// The main Camera3D of the loaded scenes (the one the game renders through), else the main Camera2D at z 0.
		// False without a camera; outListener keeps its velocity.
		static bool FindListener(Scene& scene, AudioListenerState& outListener);
		// Copies the entity's world position (Transform3D, or Transform2D at z 0) into the source; dt > 0 also
		// derives its velocity for the doppler effect.
		static void SyncPosition(const entt::registry& registry, EntityHandle entity, AudioSourceComponent& source, float dt);

	private:
		// Pulled out so re-spawn-after-Start could reuse it later. Today only
		// Start() calls this; documenting the contract makes future expansion
		// (e.g. polling for newly-added AudioSourceComponents) cheap.
		void StartPlayOnAwake(Scene& scene);
	};
}
