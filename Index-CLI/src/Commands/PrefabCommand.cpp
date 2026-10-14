#include "pch.hpp"
#include "Commands.hpp"
#include "Commands/CommandHelpers.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Project/IndexProject.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <filesystem>
#include <vector>

namespace Index::Cli {

	namespace {
		EntityHandle InstanceRoot(Scene& scene, EntityHandle entity, std::string& outError) {
			if (scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
				outError = DescribeEntity(scene, entity) + " is not a prefab instance";
				return entt::null;
			}
			const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, entity);
			return root != entt::null ? root : entity;
		}

		std::string PrefabPathOf(Scene& scene, EntityHandle root) {
			return AssetRegistry::ResolvePath(static_cast<uint64_t>(scene.GetPrefabGUID(root)));
		}

		int Create(const IndexProject& project, LoadedScene& loaded, EntityHandle entity, const std::string& output, bool force) {
			std::filesystem::path prefabPath = ResolveProjectPath(project, output);
			if (prefabPath.extension() != ".prefab") prefabPath += ".prefab";
			const std::filesystem::path assets = Path::FromUtf8(project.AssetsDirectory);
			const std::filesystem::path relative = prefabPath.lexically_relative(assets);
			if (relative.empty() || *relative.begin() == "..") return Fail("the prefab must live under Assets/");
			std::error_code fsError;
			if (std::filesystem::exists(prefabPath, fsError) && !force) {
				return Fail(Path::ToUtf8(prefabPath) + " already exists (--force overwrites it)");
			}
			if (loaded.Scene->GetEntityOrigin(entity) == EntityOrigin::Prefab) {
				return Fail(DescribeEntity(*loaded.Scene, entity) + " is already a prefab instance; unpack it first");
			}

			const uint64_t guid = SceneSerializer::SaveEntityAsPrefabInstance(*loaded.Scene, entity, Path::ToUtf8(prefabPath));
			if (guid == 0) return Fail("could not write " + Path::ToUtf8(prefabPath));
			std::string error;
			if (!SaveScene(loaded, error)) return Fail(error);
			Out("Created " + Path::ToGenericUtf8(relative) + " (GUID " + std::to_string(guid) + "); "
				+ DescribeEntity(*loaded.Scene, entity) + " is now an instance of it");
			return k_ExitOk;
		}

		int Apply(LoadedScene& loaded, EntityHandle entity) {
			Scene& scene = *loaded.Scene;
			std::string error;
			const EntityHandle root = InstanceRoot(scene, entity, error);
			if (root == entt::null) return Fail(error);
			const uint64_t guid = static_cast<uint64_t>(scene.GetPrefabGUID(root));
			const std::string prefabPath = PrefabPathOf(scene, root);
			if (prefabPath.empty()) return Fail("the prefab of " + DescribeEntity(scene, root) + " (GUID " + std::to_string(guid) + ") is missing");

			// Same route as the editor's Apply: rebuild the source in a scratch scene so instance-only state stays behind.
			const Json::Value source = SceneSerializer::SerializePrefabSource(scene, root, guid);
			auto staged = Scene::CreateDetachedScene("##PrefabApply");
			const EntityHandle stagedRoot = SceneSerializer::DeserializeEntityFromValue(*staged, source);
			if (stagedRoot == entt::null || !staged->IsValid(stagedRoot)) return Fail("could not stage the prefab source");
			const SceneSerializationFormat format = SceneSerializer::IsBinarySerializedFile(prefabPath)
				? SceneSerializationFormat::Binary : SceneSerializationFormat::Json;
			if (!SceneSerializer::SaveEntityToFile(*staged, stagedRoot, prefabPath, format)) return Fail("could not write " + prefabPath);
			PrefabTemplateCache::Get().Invalidate(guid);

			// Re-serializing the scene against the new source leaves this instance without overrides.
			if (!SaveScene(loaded, error)) return Fail(error);
			Out("Applied " + DescribeEntity(scene, root) + " to " + prefabPath);
			return k_ExitOk;
		}

		int Revert(LoadedScene& loaded, EntityHandle entity, const std::string& overridePath) {
			Scene& scene = *loaded.Scene;
			std::string error;
			const EntityHandle root = InstanceRoot(scene, entity, error);
			if (root == entt::null) return Fail(error);
			const std::string description = DescribeEntity(scene, root);
			const EntityHandle target = overridePath.empty() ? root : entity;
			if (SceneSerializer::RevertPrefabInstanceOverride(scene, target, overridePath) == entt::null) {
				return Fail(overridePath.empty() ? "could not revert " + description
					: "could not revert '" + overridePath + "' on " + description);
			}
			if (!SaveScene(loaded, error)) return Fail(error);
			Out(overridePath.empty() ? "Reverted all overrides of " + description : "Reverted " + overridePath + " on " + description);
			return k_ExitOk;
		}

		int Unpack(LoadedScene& loaded, EntityHandle entity) {
			Scene& scene = *loaded.Scene;
			std::string error;
			const EntityHandle root = InstanceRoot(scene, entity, error);
			if (root == entt::null) return Fail(error);
			const uint64_t guid = static_cast<uint64_t>(scene.GetPrefabGUID(root));

			// Mirrors the editor's Unpack: the whole instance, but nested instances of other prefabs stay instances.
			int unpacked = 0;
			std::vector<EntityHandle> stack{ root };
			while (!stack.empty()) {
				const EntityHandle current = stack.back();
				stack.pop_back();
				if (!scene.IsValid(current) || scene.GetEntityOrigin(current) != EntityOrigin::Prefab) continue;
				if (static_cast<uint64_t>(scene.GetPrefabGUID(current)) != guid) continue;
				if (const auto* hierarchy = scene.GetRegistry().try_get<HierarchyComponent>(current)) {
					stack.insert(stack.end(), hierarchy->Children.begin(), hierarchy->Children.end());
				}
				scene.SetEntityMetaData(current, EntityOrigin::Scene);
				++unpacked;
			}
			scene.MarkDirty();
			if (!SaveScene(loaded, error)) return Fail(error);
			Out("Unpacked " + std::to_string(unpacked) + " entit" + (unpacked == 1 ? "y" : "ies"));
			return k_ExitOk;
		}

		int List(LoadedScene& loaded) {
			Scene& scene = *loaded.Scene;
			Json::Value list = Json::Value::MakeArray();
			std::string text;
			for (const EntityHandle entity : scene.GetRegistry().view<PrefabInstanceComponent>()) {
				if (!SceneSerializer::IsPrefabInstanceRoot(scene, entity)) continue;
				Json::Value overrides = Json::Value::MakeObject();
				SceneSerializer::ComputeInstanceOverrides(scene, entity, overrides);
				const std::string prefabPath = PrefabPathOf(scene, entity);
				if (Options().Json) {
					Json::Value value = Json::Value::MakeObject();
					value.AddMember("entity", DescribeEntity(scene, entity));
					value.AddMember("prefab", prefabPath);
					value.AddMember("overrides", std::move(overrides));
					list.Append(std::move(value));
					continue;
				}
				text += DescribeEntity(scene, entity) + "\n    prefab: " + (prefabPath.empty() ? "(missing)" : prefabPath) + "\n";
				if (overrides.IsObject()) {
					for (const auto& [path, value] : overrides.GetObject()) {
						text += "    " + path + " = " + Json::Stringify(value, false) + "\n";
					}
				}
			}
			Out(Options().Json ? Json::Stringify(list, true) : (text.empty() ? std::string("No prefab instances.") : text));
			return k_ExitOk;
		}
	}

	int RunPrefab(Args& args) {
		const std::optional<std::string> action = args.Positional();
		const std::optional<std::string> sceneArgument = args.Positional();
		const std::optional<std::string> entityArgument = args.Positional();
		const std::optional<std::string> outputArgument = args.Positional();
		std::string error;
		IndexProject* project = OpenProject(args, error);
		const std::string overridePath = args.Option("path").value_or("");
		const bool force = args.Flag("force");
		if (!args.Finish(error)) return Fail(error, k_ExitUsage);
		if (!project) return Fail(error);
		if (!action) return Fail("missing action: create, apply, revert, unpack or list", k_ExitUsage);
		if (!sceneArgument) return Fail("missing scene", k_ExitUsage);
		const bool needsEntity = *action != "list";
		if (needsEntity && !entityArgument) return Fail("missing entity (uuid, name or Parent/Child path)", k_ExitUsage);
		if (*action == "create" && !outputArgument) return Fail("missing output .prefab path", k_ExitUsage);

		AssetRegistry::Sync();
		LoadedScene loaded;
		if (!LoadScene(ResolveScenePath(*project, *sceneArgument), loaded, error)) return Fail(error);
		if (*action == "list") return List(loaded);

		const EntityHandle entity = FindEntity(*loaded.Scene, *entityArgument, error);
		if (entity == entt::null) return Fail(error);
		if (*action == "create") return Create(*project, loaded, entity, *outputArgument, force);
		if (*action == "apply") return Apply(loaded, entity);
		if (*action == "revert") return Revert(loaded, entity, overridePath);
		if (*action == "unpack") return Unpack(loaded, entity);
		return Fail("unknown action '" + *action + "' (create, apply, revert, unpack, list)", k_ExitUsage);
	}

} // namespace Index::Cli
