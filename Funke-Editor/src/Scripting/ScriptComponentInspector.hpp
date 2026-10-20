#pragma once
#include "Scene/Entity.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Funke {
	class Scene;

	void DrawScriptComponentInspector(std::span<const Entity> entities);

	// The same sections one by one, so the Inspector can place each C# component and script among the
	// built-in components (ComponentOrder). Begin returns false when the first entity has no scripts;
	// End applies the removals asked for while drawing.
	struct ScriptSectionPass {
		std::span<const Entity> Entities;
		std::span<const Entity> Propagation;
		bool MultiEdit = true;
		std::size_t RemoveScriptIndex = SIZE_MAX;
		std::size_t RemoveManagedIndex = SIZE_MAX;
	};
	bool BeginScriptSections(std::span<const Entity> entities, ScriptSectionPass& pass);
	void DrawManagedComponentSection(ScriptSectionPass& pass, std::size_t index);
	void DrawScriptSection(ScriptSectionPass& pass, std::size_t index);
	void EndScriptSections(ScriptSectionPass& pass);

	// Edits are pushed to the managed instance (if alive) AND to Scene::SetSceneSystemFieldValue so they survive scene reload.
	void DrawSceneSystemFields(Scene& scene, const std::string& className);

	// Renders the inspector for a standalone DataAsset (keyed by its asset GUID).
	// Edits write through DataAssetManager (live instance + .dataasset file).
	void DrawDataAssetFields(uint64_t assetGuid);

	// Shutdown() resets picker state on Application::Reload; without it, the stale entry list from the previous project survives into the new session.
	namespace ScriptComponentInspector {
		void Shutdown();
	}
}
