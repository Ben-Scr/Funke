#pragma once
#include "Core/EngineModules.hpp"
#include "Core/Export.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <cstdint>
#include <vector>

namespace Funke {
	enum class BuildPlatform : uint8_t;
	class Entity;
	struct ComponentInfo;

	class FUNKE_SCRIPTING_API ScriptBindings {
	public:
		static void PopulateNativeBindings(NativeBindings& bindings);
		static void PopulateAssetBindings(NativeBindings& bindings);
		static void PumpAssetLoadCompletions();
		static bool IsScriptInputEnabled();
		static bool AddComponent(const Entity& entity, const ComponentInfo& info);
		static bool RemoveComponent(const Entity& entity, const ComponentInfo& info, bool notifyManaged = true);
		// C# Funke.Platform value: Windows 1, Linux 2, MacOS 3, Android 4, iOS 5, WebGL 6, VisionOS 7.
		static int ToManagedPlatform(BuildPlatform platform);

		// Real bindings for active engine modules, signature-matched stubs for the rest.
		static void PopulateModuleBindings(NativeBindings& bindings);
		// Slots whose module populates them but the stub list misses (or the reverse); empty when in sync.
		static std::vector<size_t> FindUnstubbedModuleSlots(EngineModule module);
	};

}
