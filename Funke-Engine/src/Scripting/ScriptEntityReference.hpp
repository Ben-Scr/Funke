#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>

namespace Funke {
	class Scene;

	// Resolves the id C# scripts hold for an entity (Entity.ID) to its loaded scene and handle.
	FUNKE_API bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);
	// The id scripts see for an entity; 0 when it has none.
	FUNKE_API uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle);
}
