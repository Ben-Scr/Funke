#include "pch.hpp"
#include "Commands/CommandHelpers.hpp"
#include "Cli.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Funke::Cli {

	void NoteProgress(float progress, std::string_view stage) {
		if (stage.empty()) return;
		char percent[8];
		std::snprintf(percent, sizeof(percent), "[%3d%%]", static_cast<int>(std::lround(progress * 100.0f)));
		Note(std::string(percent) + " " + std::string(stage));
	}

	bool ParseAssetFormat(std::string_view value, FunkeProject::ProjectAssetSerializationFormat& outFormat) {
		const std::string lower = ToLower(value);
		if (lower == "json") outFormat = FunkeProject::ProjectAssetSerializationFormat::Json;
		else if (lower == "binary" || lower == "bin") outFormat = FunkeProject::ProjectAssetSerializationFormat::Binary;
		else return false;
		return true;
	}

	std::string ManagedDefines(const FunkeProject& project, std::string_view primarySymbol, std::string_view configuration) {
		std::string defines;
		const auto append = [&](std::string_view value) {
			if (value.empty()) return;
			if (!defines.empty()) defines += "%3B";
			defines.append(value);
		};
		append(primarySymbol);
		if (configuration == "Debug") append("FNK_DEBUG");
		else if (configuration == "Release") append("FNK_RELEASE");
		else if (configuration == "Dist") append("FNK_DIST");
		append(project.GetActiveBuildProfileDefine());
		append(FunkeProject::GetManagedPlatformDefine());
		for (const std::string& custom : project.CustomDefines) append(custom);
		return defines;
	}

	bool CompileScripts(const FunkeProject& project, std::string_view primarySymbol, const std::string& configuration,
		std::string& outOutput)
	{
		const Process::Result result = Process::Run({
			"dotnet", "build", project.CsprojPath,
			"-c", configuration,
			"--nologo", "-v", "q", "-nodeReuse:false",
			"-p:UseSharedCompilation=false",
			"-p:DefineConstants=" + ManagedDefines(project, primarySymbol, configuration)
		}, Path::FromUtf8(project.RootDirectory), std::chrono::minutes(5));
		outOutput = result.Output;
		return result.Succeeded();
	}

	std::filesystem::path ResolveScenePath(const FunkeProject& project, const std::string& value) {
		const std::filesystem::path asPath = Path::FromUtf8(value);
		if (asPath.has_extension() || asPath.has_parent_path()) return ResolveProjectPath(project, value);
		return Path::FromUtf8(project.GetSceneFilePath(value));
	}

	bool LoadScene(const std::filesystem::path& path, LoadedScene& outScene, std::string& outError) {
		std::error_code error;
		if (!std::filesystem::is_regular_file(path, error)) {
			outError = "scene not found: " + Path::ToUtf8(path);
			return false;
		}
		const std::string utf8Path = Path::ToUtf8(path);
		outScene.Path = path;
		outScene.Format = SceneSerializer::IsBinarySerializedFile(utf8Path)
			? SceneSerializationFormat::Binary : SceneSerializationFormat::Json;
		outScene.Scene = Scene::CreateDetachedScene(Path::ToUtf8(path.stem()));
		if (!SceneSerializer::LoadFromFile(*outScene.Scene, utf8Path)) {
			outError = "the engine rejected " + utf8Path + " (run with --verbose for details)";
			return false;
		}
		return true;
	}

	bool SaveScene(LoadedScene& scene, std::string& outError) {
		if (!SceneSerializer::SaveToFile(*scene.Scene, Path::ToUtf8(scene.Path), scene.Format, false)) {
			outError = "failed to write " + Path::ToUtf8(scene.Path);
			return false;
		}
		return true;
	}

	namespace {
		std::string NameOf(Scene& scene, EntityHandle entity) {
			const auto* name = scene.GetRegistry().try_get<NameComponent>(entity);
			return name ? name->Name : std::string();
		}

		EntityHandle ParentOf(Scene& scene, EntityHandle entity) {
			const auto* hierarchy = scene.GetRegistry().try_get<HierarchyComponent>(entity);
			return hierarchy ? hierarchy->Parent : EntityHandle(entt::null);
		}
	}

	EntityHandle FindEntity(Scene& scene, const std::string& reference, std::string& outError) {
		entt::registry& registry = scene.GetRegistry();

		const bool numeric = !reference.empty()
			&& reference.find_first_not_of("0123456789") == std::string::npos;
		if (numeric) {
			const uint64_t uuid = std::stoull(reference);
			for (const EntityHandle entity : registry.view<UUIDComponent>()) {
				if (static_cast<uint64_t>(registry.get<UUIDComponent>(entity).Id) == uuid) return entity;
			}
		}

		std::vector<std::string> segments;
		for (std::size_t start = 0; start <= reference.size();) {
			const std::size_t slash = reference.find('/', start);
			const std::size_t end = slash == std::string::npos ? reference.size() : slash;
			segments.push_back(reference.substr(start, end - start));
			if (slash == std::string::npos) break;
			start = slash + 1;
		}

		std::vector<EntityHandle> matches;
		for (const EntityHandle entity : registry.view<NameComponent>()) {
			// Walk up the hierarchy and compare the path from the leaf backwards.
			EntityHandle current = entity;
			bool matched = true;
			for (std::size_t i = segments.size(); i-- > 0;) {
				if (current == entt::null || !scene.IsValid(current) || NameOf(scene, current) != segments[i]) {
					matched = false;
					break;
				}
				current = ParentOf(scene, current);
			}
			if (matched) matches.push_back(entity);
		}

		if (matches.size() == 1) return matches.front();
		if (matches.empty()) {
			outError = "no entity '" + reference + "' in the scene";
		}
		else {
			outError = "'" + reference + "' matches " + std::to_string(matches.size()) + " entities; use a Parent/Child path or a uuid:";
			for (const EntityHandle entity : matches) outError += "\n  " + DescribeEntity(scene, entity);
		}
		return entt::null;
	}

	std::string DescribeEntity(Scene& scene, EntityHandle entity) {
		std::string path = NameOf(scene, entity);
		for (EntityHandle parent = ParentOf(scene, entity); parent != entt::null && scene.IsValid(parent);
			parent = ParentOf(scene, parent)) {
			path = NameOf(scene, parent) + "/" + path;
		}
		const auto* uuid = scene.GetRegistry().try_get<UUIDComponent>(entity);
		return path + (uuid ? "  (uuid " + std::to_string(static_cast<uint64_t>(uuid->Id)) + ")" : "");
	}

} // namespace Funke::Cli
