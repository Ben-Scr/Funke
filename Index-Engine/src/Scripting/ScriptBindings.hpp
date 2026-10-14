#pragma once
#include "Core/EngineModules.hpp"
#include "Core/Export.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <vector>

namespace Index {
	class INDEX_SCRIPTING_API ScriptBindings {
	public:
		static void PopulateNativeBindings(NativeBindings& bindings);
		static bool IsScriptInputEnabled();

		// Real bindings for active engine modules, signature-matched stubs for the rest.
		static void PopulateModuleBindings(NativeBindings& bindings);
		// Slots whose module populates them but the stub list misses (or the reverse); empty when in sync.
		static std::vector<size_t> FindUnstubbedModuleSlots(EngineModule module);
	};

}
