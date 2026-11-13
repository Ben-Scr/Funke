#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Funke {

	// Per-view overrides. Defaults preserve authored rendering; never mutate scene assets or graphics quality.
	struct RenderViewOptions {
		bool Lighting = true;
		bool Shadows = true;
		bool Fog = true;
		bool Skybox = true;
		bool CustomShaders = true;
		bool Particles = true;
		bool SceneEffects = true;
		std::vector<uint64_t> DisabledShaders;

		bool AllowsShader(uint64_t shader) const {
			return CustomShaders && std::find(DisabledShaders.begin(), DisabledShaders.end(), shader) == DisabledShaders.end();
		}
		bool operator==(const RenderViewOptions&) const = default;
	};

}
