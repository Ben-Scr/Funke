#pragma once
#include "Scene/Entity.hpp"
#include "Scripting/ScriptDiscovery.hpp"

namespace Funke::EditorScriptAttachment {
	bool AttachScriptToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry);
	bool AttachManagedComponentToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry);
}
