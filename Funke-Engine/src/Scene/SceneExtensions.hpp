#pragma once
#include "Core/Export.hpp"

#include <string>
#include <string_view>

namespace Funke {
	class Scene;

	// Per-scene hooks for packages whose components need runtime state (physics bodies,
	// caches) next to the registry. Called for every scene, detached ones included, so
	// each hook checks Scene::IsDetached / IsLifecycleSuspended itself.
	struct SceneExtension {
		std::string Name;
		// The scene was constructed: connect registry signals here.
		void (*OnSceneCreated)(Scene& scene) = nullptr;
		// Runtime component state is (re)built for components that already exist,
		// e.g. after a staged load resumes or a play-mode copy is made.
		void (*OnRuntimeStateInitialized)(Scene& scene) = nullptr;
		// The scene is being unloaded or destroyed: drop everything keyed by it and
		// disconnect registry signals. May be called more than once per scene.
		void (*OnSceneDestroyed)(Scene& scene) = nullptr;
	};

	namespace SceneExtensions {
		// Replaces an extension with the same Name and applies OnSceneCreated and
		// OnRuntimeStateInitialized to the scenes that are already loaded.
		FUNKE_API void Register(SceneExtension extension);
		// Runs OnSceneDestroyed for the loaded scenes, then forgets the extension.
		FUNKE_API void Unregister(std::string_view name);

		void NotifySceneCreated(Scene& scene);
		void NotifyRuntimeStateInitialized(Scene& scene);
		void NotifySceneDestroyed(Scene& scene);
	}
}
