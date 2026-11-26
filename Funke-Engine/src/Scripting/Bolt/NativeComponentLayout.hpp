#pragma once

#include "Scene/ComponentRegistry.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/Bolt/NativeApi.hpp"

namespace Funke::BoltScriptHost {
	inline const ComponentInfo* NativeLayout(const std::string& name, uint64_t size) {
		const auto id = ScriptApi().Component_GetTypeId(name.c_str());
		const auto* info = id ? SceneManager::GetComponentRegistry().GetByTypeId(id) : nullptr;
		return info && (info->rawSize == size || (name == "Sprite Renderer" && info->bytePayloadSize == size)) ? info : nullptr;
	}

	inline const char* BuiltInLayoutName(std::string_view type) {
		if (type == "Funke.Native.NativeTransform2D") return "Transform 2D";
		if (type == "Funke.Native.NativeSpriteRenderer") return "Sprite Renderer";
		return nullptr;
	}
}
