#pragma once
#include "Core/Export.hpp"
#include "Scripting/ScriptGlue.hpp"

namespace Index {
	class INDEX_SCRIPTING_API ScriptBindings {
	public:
		static void PopulateNativeBindings(NativeBindings& bindings);
		static bool IsScriptInputEnabled();
	};

}
