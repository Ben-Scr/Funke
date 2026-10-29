#pragma once
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Funke {

	enum class SceneSerializationFormat : std::uint8_t {
		Json = 0,
		Binary = 1
	};

	class Scene;

	class Entity;

	namespace Json {
		class Value;
	}

	class FUNKE_API SceneSerializer {
	public:
		static Json::Value SerializeScene(Scene& scene);
		static bool DeserializeScene(Scene& scene, const Json::Value& root, std::string_view source = {});

		static bool SaveToFile(Scene& scene, const std::string& path);
		static bool SaveToFile(Scene& scene, const std::string& path, bool logSuccess);
		static bool SaveToFile(Scene& scene, const std::string& path, SceneSerializationFormat format);
		static bool SaveToFile(Scene& scene, const std::string& path, SceneSerializationFormat format, bool logSuccess);
		static bool LoadFromFile(Scene& scene, const std::string& path);
		// Worker-safe file read and parse; apply the result with DeserializeScene on the main thread.
		static bool PrepareFile(const std::string& path, Json::Value& outRoot, std::string* outError = nullptr);

		// Prefab support: save/load single entities
		static Json::Value SerializeEntityFull(Scene& scene, EntityHandle entity);
		static Json::Value SerializeEntityForClipboard(Scene& scene, EntityHandle entity);
		static Json::Value SerializePrefabSource(Scene& scene, EntityHandle entity, uint64_t prefabGuid = 0);
		static EntityHandle DeserializeEntityFromValue(
			Scene& scene,
			const Json::Value& entityValue,
			EntityOrigin origin = EntityOrigin::Scene);
		// True for a .prefab/clipboard root holding an entity tree: "Entities" array, "Entity" or legacy "prefab".
		static bool HasPrefabEntityPayload(const Json::Value& root);
		// Rebuilds SerializeEntityFull output (root first, children linked by parentUuid) with the
		// serialized persistent ids kept, e.g. for editor undo. Returns the root or null on failure.
		static EntityHandle RestoreEntityTree(Scene& scene, const std::vector<Json::Value>& entityValues);
		static Json::Value SerializeComponent(Scene& scene, EntityHandle entity, std::string_view componentName);
		static bool DeserializeComponent(Scene& scene, EntityHandle entity, std::string_view componentName, const Json::Value& componentValue);
		static bool ResetComponent(Scene& scene, EntityHandle entity, std::string_view componentName);
		static bool SaveEntityToFile(Scene& scene, EntityHandle entity, const std::string& path);
		static bool SaveEntityToFile(Scene& scene, EntityHandle entity, const std::string& path, SceneSerializationFormat format);
		// Saves entity subtree as a .prefab, converts the live subtree in-place into an instance (all entities get Origin::Prefab + PrefabInstanceComponent matching the written uuid). Returns GUID or 0 on failure.
		static uint64_t SaveEntityAsPrefabInstance(Scene& scene, EntityHandle entity, const std::string& path);
		static EntityHandle LoadEntityFromFile(Scene& scene, const std::string& path);
		static bool ConvertFileFormat(const std::string& path, SceneSerializationFormat format);
		static bool IsBinarySerializedFile(const std::string& path);
		static EntityHandle InstantiatePrefab(Scene& scene, uint64_t prefabGuid);
		static bool ApplyPrefabInstanceOverrides(Scene& scene, EntityHandle entity);
		static EntityHandle RevertPrefabInstanceOverride(Scene& scene, EntityHandle entity, const std::string& overridePath);

		// Accepts the prior prefab root or Entity block as the override baseline.
		static EntityHandle RefreshPrefabInstance(Scene& scene, EntityHandle existing,
			const Json::Value& previousSourceEntityValue);

		// A prefab whose file changed outside SaveEntityToFile after the engine parsed it.
		// PreviousRoot is the parse loaded instances were built from: the RefreshPrefabInstance baseline.
		struct ExternalPrefabChange {
			std::string Path;
			std::shared_ptr<const Json::Value> PreviousRoot;
		};
		// Drains the changes seen so far, including ones a re-read already picked up.
		static std::vector<ExternalPrefabChange> TakeExternallyChangedPrefabs();
		// For writers other than SaveEntityToFile, so their own write is not reported as external.
		static void ForgetCachedPrefabRoot(const std::string& path);
		// Main-thread maintenance; force bypasses the periodic collection interval.
		static std::size_t PurgeUnusedCachedPrefabRoots(bool force = false);
		static void ClearCachedPrefabRoots();

		// outOverrides is filled with dot-path keys e.g. "Transform2D.posX". Returns false for non-instances and orphans (source GUID not in AssetRegistry).
		static bool ComputeInstanceOverrides(Scene& scene, EntityHandle entity, Json::Value& outOverrides);

		// NOTE: PrefabInstanceComponent is on every entity in the subtree (not just the root), so the root is determined by walking HierarchyComponent.Parent upward.
		static EntityHandle GetPrefabInstanceRoot(Scene& scene, EntityHandle entity);

		// True for the root of a prefab instance, including an instance parented under another instance of the same prefab.
		static bool IsPrefabInstanceRoot(Scene& scene, EntityHandle entity);

		// Walks the entire subtree so a child-only edit still enables Apply/Revert All on the root. `entity` may be any member of the instance.
		static bool HasPrefabInstanceOverrides(Scene& scene, EntityHandle entity);

	private:
		friend class PrefabTemplateCache;
		friend class SceneManager;
		class NewSceneLoadScope {
		public:
			explicit NewSceneLoadScope(Scene& scene);
			~NewSceneLoadScope();
			bool HasRollbackFailure() const;
			NewSceneLoadScope(const NewSceneLoadScope&) = delete;
			NewSceneLoadScope& operator=(const NewSceneLoadScope&) = delete;
		private:
			Scene* m_Target;
			Scene* m_Previous;
			Scene* m_PreviousRollbackFailure;
		};
		template <typename Load>
		static bool DeserializeNewScene(Scene& scene, Load&& load, std::string_view source);
		static void TrackPrefabCacheScene(Scene& scene);

		// Shared by the whole-DOM load (DeserializeScene) and the streaming binary load: the
		// parent-link / child-order fixups deferred until every entity exists.
		struct SceneLoadState {
			std::vector<std::pair<EntityHandle, uint64_t>> pendingParents;
			std::unordered_map<uint32_t, int> childIndexByEntity;
		};

		static bool ProcessSceneHeader(
			Scene& scene,
			const Json::Value& header,
			std::string_view source,
			bool& restartLifecycle);
		// Pass-1 body: create one entity and record its deferred parent/child-order links.
		static bool ProcessLoadedEntity(Scene& scene, const Json::Value& entityValue, SceneLoadState& state);
		// Passes 2-3 + entity-ref fixups + finish content replacement.
		static void FinalizeSceneLoad(Scene& scene, const SceneLoadState& state, bool restartLifecycle);
		static bool DeserializeSceneInPlace(
			Scene& scene, const Json::Value& root, std::string_view source, bool logSuccess);
		static bool DeserializeSceneStreamingBinary(Scene& scene, const std::vector<std::uint8_t>& bytes, std::string_view source);
		static bool DeserializeSceneStreamingBinaryInPlace(Scene& scene, const std::vector<std::uint8_t>& bytes, std::string_view source);
		static bool CommitStagedScene(Scene& scene, Scene& stagedScene, std::string_view source);

		static EntityHandle DeserializeEntity(Scene& scene, const Json::Value& entityValue);
		static EntityHandle DeserializeFullEntity(
			Scene& scene,
			const Json::Value& entityValue,
			EntityOrigin origin,
			uint64_t prefabGuid = 0);
		static EntityHandle DeserializeEntityTree(
			Scene& scene,
			const std::vector<Json::Value>& entityValues,
			EntityOrigin origin,
			uint64_t prefabGuid = 0,
			bool preserveSerializedIdentity = true,
			bool* outComplete = nullptr);
		static EntityHandle DeserializePrefabInstance(Scene& scene, const Json::Value& entityValue);
	};

} // namespace Funke
