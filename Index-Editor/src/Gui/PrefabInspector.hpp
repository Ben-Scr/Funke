#pragma once
#include "Scene/EntityHandle.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace Index {

	class Scene;
	namespace Json { class Value; }

	bool SavePrefabSourceTransactionally(Scene& sourceScene, EntityHandle sourceRoot,
		const std::string& prefabPath, std::string_view logTag);

	// Same propagation for a .prefab changed outside the editor: previousSource (the parse the loaded
	// instances were built from) is the override baseline. Edit mode only; scenes roll back on failure.
	bool RefreshPrefabInstancesFromDisk(const std::string& prefabPath, const Json::Value& previousSource,
		std::string_view logTag, size_t* outRefreshedCount = nullptr);

	// Inspector for a .prefab asset backed by a detached Scene. Component-destroy hooks that touch global subsystems are gated by Scene::IsDetached().
	class PrefabInspector {
	public:
		PrefabInspector();
		~PrefabInspector();

		PrefabInspector(const PrefabInspector&) = delete;
		PrefabInspector& operator=(const PrefabInspector&) = delete;

		// Load `prefabPath` into a fresh detached scene. Replaces any prefab
		// currently open without prompting — the caller is responsible for
		// driving the save/discard prompt via `HasUnsavedChanges()` first.
		bool Open(const std::string& prefabPath);

		// Tear down the detached scene. Caller should check `HasUnsavedChanges`.
		void Close();

		bool IsOpen() const { return m_PrefabScene != nullptr; }
		const std::string& GetCurrentPath() const { return m_PrefabPath; }
		bool HasUnsavedChanges() const;

		// Returns true when dirty content is ready for the caller to save.
		bool Render();

		bool Save();

	private:
		std::unique_ptr<Scene> m_PrefabScene;
		EntityHandle m_RootEntity = entt::null;
		std::string m_PrefabPath;
		char m_AddComponentSearchBuffer[128]{};
	};

}
