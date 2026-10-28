#pragma once
#include "Core/Export.hpp"
#include "Scene/SceneSystem.hpp"
#include <cstddef>

namespace Funke {
	struct Transform2DPropagationStats {
		size_t TransformsVisited = 0;
		size_t PhysicsRoots = 0;
		bool FullTraversal = false;
	};

	// Physics body transforms are left alone (engine-written), but their children are still recursed into.
	class TransformHierarchySystem : public SceneSystem {
	public:
		void Awake(Scene& scene) override;
		void Update(Scene& scene) override;
		void OnPreRender(Scene& scene) override;

		// FUNKE_API: editor calls Propagate() directly between inspector input and viewport draw so edits appear same-frame.
		static FUNKE_API void Propagate(Scene& scene);
		static FUNKE_API Transform2DPropagationStats GetLastPropagationStats(const Scene& scene);
	};
}
