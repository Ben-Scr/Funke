#pragma once
#include "Graphics/Material.hpp"
#include "Scene/Scene.hpp"
#if FUNKE_WITH_3D
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#endif

namespace Funke {

	inline void CollectIndirectSceneTextureReferences(const Scene& scene, const std::function<void(uint64_t)>& emit) {
		if (!emit) return;
		MaterialLibrary::CollectTextureReferences(scene, emit);
#if FUNKE_WITH_3D
		for (auto [entity, particles] : scene.GetRegistry().view<const ParticleSystem3DComponent>().each()) {
			if (particles.Texture != 0) emit(particles.Texture);
		}
#endif
	}

}
