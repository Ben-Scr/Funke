#pragma once
#include "Project/FunkeProject.hpp"
#include "Scene/EntityHandle.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace Funke {
	class Scene;
}

namespace Funke::Cli {

	void NoteProgress(float progress, std::string_view stage);

	bool ParseAssetFormat(std::string_view value, FunkeProject::ProjectAssetSerializationFormat& outFormat);

	// The same DefineConstants the editor/launcher pass: primary symbol, configuration, profile, platform, custom.
	std::string ManagedDefines(const FunkeProject& project, std::string_view primarySymbol, std::string_view configuration);
	bool CompileScripts(const FunkeProject& project, std::string_view primarySymbol, const std::string& configuration,
		std::string& outOutput);

	// A scene name (as in project.json) or a path to a .scene file.
	std::filesystem::path ResolveScenePath(const FunkeProject& project, const std::string& value);

	struct LoadedScene {
		std::unique_ptr<Scene> Scene;
		std::filesystem::path Path;
		SceneSerializationFormat Format = SceneSerializationFormat::Json;
	};
	bool LoadScene(const std::filesystem::path& path, LoadedScene& outScene, std::string& outError);
	// Keeps the format the file already had.
	bool SaveScene(LoadedScene& scene, std::string& outError);

	// A persistent uuid, an entity name, or a Parent/Child name path. Fails on ambiguous names.
	EntityHandle FindEntity(Scene& scene, const std::string& reference, std::string& outError);
	std::string DescribeEntity(Scene& scene, EntityHandle entity);

} // namespace Funke::Cli
