#include <doctest/doctest.h>

#include "Project/IndexProject.hpp"
#include "Project/ProjectTemplate.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializerStorage.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>

using namespace Index;

namespace {
	std::vector<ProjectTemplate> DiscoverRepositoryTemplates() {
		const std::string engineRoot = IndexProject::GetEngineRootDir();
		REQUIRE_FALSE(engineRoot.empty());
		return ProjectTemplates::DiscoverIn({ Path::Combine(engineRoot, "templates") });
	}

	struct ScopedEnvironmentVariable {
		std::string Name;
		ScopedEnvironmentVariable(std::string name, const std::string& value) : Name(std::move(name)) { Set(value.c_str()); }
		~ScopedEnvironmentVariable() { Set(""); }
		void Set(const char* value) const {
#ifdef _WIN32
			_putenv_s(Name.c_str(), value);
#else
			if (*value) setenv(Name.c_str(), value, 1); else unsetenv(Name.c_str());
#endif
		}
	};

	std::filesystem::path MakeTempDirectory(const char* prefix) {
		const std::filesystem::path directory = std::filesystem::temp_directory_path()
			/ (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		return directory;
	}
}

TEST_CASE("Repository ships the empty 2D and 3D project templates") {
	const std::vector<ProjectTemplate> templates = DiscoverRepositoryTemplates();
	auto find = [&](std::string_view id) -> const ProjectTemplate* {
		for (const ProjectTemplate& projectTemplate : templates) {
			if (projectTemplate.Id == id) return &projectTemplate;
		}
		return nullptr;
	};

	CHECK(templates.size() == 2);
	for (const char* id : { "Index.Empty2D", "Index.Empty3D" }) {
		CAPTURE(id);
		REQUIRE(find(id));
	}
	CHECK(find("Index.Empty2D")->Modules == std::vector<std::string>{ "Index.2D" });
	CHECK(find("Index.Empty3D")->Modules == std::vector<std::string>{ "Index.3D" });
	CHECK(find("Index.Empty3D")->Category == "3D");
	CHECK(find(ProjectTemplates::k_DefaultTemplateId));
}

TEST_CASE("Template placeholders expand to the project name and a C# identifier") {
	CHECK(ProjectTemplates::ExpandPlaceholders("Hello {{ProjectName}} / {{ProjectIdentifier}}", "My Game 2")
		== "Hello My Game 2 / My_Game_2");
	CHECK(ProjectTemplates::MakeIdentifier("3D Racer") == "_3D_Racer");
}

// INDEX_TEST_KEEP_TEMPLATE_DIR=<dir> keeps one project per template there (<dir>/<template id>) for manual or Run.ps1 checks.
TEST_CASE("Every template creates a project whose startup scene only uses registered components") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);
	const char* keepDir = std::getenv("INDEX_TEST_KEEP_TEMPLATE_DIR");
	const bool keep = keepDir && *keepDir;

	for (const ProjectTemplate& projectTemplate : DiscoverRepositoryTemplates()) {
		CAPTURE(projectTemplate.Id);
		const std::filesystem::path parent = keep
			? std::filesystem::path(keepDir) / projectTemplate.Id
			: MakeTempDirectory("index-template-");
		if (keep) {
			std::error_code ec;
			std::filesystem::remove_all(parent, ec);
			std::filesystem::create_directories(parent);
		}
		const IndexProject created = IndexProject::Create("TemplateGame", Path::ToUtf8(parent), "TemplateGame", &projectTemplate);

		const IndexProject loaded = IndexProject::Load(created.RootDirectory);
		CHECK(loaded.Modules == projectTemplate.Modules);
		CHECK(loaded.StartupScene == projectTemplate.StartupScene);
		CHECK(loaded.PrefersThreeD() == (projectTemplate.Category == "3D"));

		Json::Value sceneRoot;
		std::string error;
		REQUIRE(SceneSerializerStorage::ReadRootFromFile(loaded.GetSceneFilePath(loaded.StartupScene), sceneRoot, &error));
		const Json::Value* entities = sceneRoot.FindMember("entities");
		REQUIRE(entities);
		REQUIRE(entities->IsArray());
		CHECK_FALSE(entities->GetArray().empty());
		for (const Json::Value& entity : entities->GetArray()) {
			REQUIRE(entity.IsObject());
			for (const auto& [key, value] : entity.GetObject()) {
				(void)value;
				// Component blocks are PascalCase; entity metadata (name, uuid, ...) is camelCase.
				if (key.empty() || !std::isupper(static_cast<unsigned char>(key.front()))) continue;
				CAPTURE(key);
				CHECK(registry.FindBySerializedName(key));
			}
		}

		const std::string gameScript = File::ReadAllText(Path::Combine(loaded.ScriptsDirectory, "GameScript.cs"));
		CHECK(gameScript.find("TemplateGame") != std::string::npos);
		CHECK(gameScript.find("{{") == std::string::npos);

		if (!keep) {
			std::error_code ec;
			std::filesystem::remove_all(parent, ec);
		}
	}
}

TEST_CASE("A project saved as a template recreates its settings and asset layout") {
	const std::filesystem::path workDirectory = MakeTempDirectory("index-user-template-");
	const std::string templatesDirectory = Path::ToUtf8(workDirectory / "Templates");

	IndexProject source = IndexProject::Create("Source Game", Path::ToUtf8(workDirectory / "Projects"));
	source.BuildWidth = 1280;
	source.BuildHeight = 720;
	source.BuildCompany = "Acme";
	source.ExecutableName = "Source Game";
	source.CustomDefines = { "USE_ACME" };
	source.Modules = { std::string(IndexProject::k_Module3D) };
	source.AssetSerializationFormat = IndexProject::ProjectAssetSerializationFormat::Binary;
	source.LastOpenedScene = "Level2";
	REQUIRE(source.Save());

	// Binary scene: templates saved from binary-format projects must still convert on create.
	REQUIRE(SceneSerializerStorage::WriteRootToFile(source.GetSceneFilePath(source.StartupScene),
		IndexProject::BuildStarterScene(source.StartupScene), SceneSerializationFormat::Binary));
	std::filesystem::create_directories(Path::FromUtf8(source.AssetsDirectory) / "Art" / "Sprites");
	REQUIRE(File::WriteAllText(Path::Combine(source.AssetsDirectory, "Art", "Sprites", "readme.txt"), "art"));
	REQUIRE(File::WriteAllText(Path::Combine(source.GetBuildProfilesDirectory(), "Custom.json"), "{}"));

	ProjectTemplate created;
	std::string error;
	REQUIRE_MESSAGE(ProjectTemplates::CreateFromProject(source.RootDirectory,
		{ "My Starter", "Starter with settings", "" }, created, &error, templatesDirectory), error);
	CHECK(created.Id == "User.My_Starter");
	CHECK(created.DisplayName == "My Starter");
	CHECK(created.Category == "3D");
	CHECK(created.Modules == source.Modules);
	CHECK(created.SortOrder == ProjectTemplates::k_UserTemplateSortOrder);
	CHECK_FALSE(created.GetProjectSettingsPath().empty());

	ProjectTemplate second;
	REQUIRE(ProjectTemplates::CreateFromProject(source.RootDirectory, { "My Starter", "", "2D" }, second, &error, templatesDirectory));
	CHECK(second.Id == "User.My_Starter_2");
	CHECK(second.RootDirectory != created.RootDirectory);
	CHECK(second.Category == "2D");
	CHECK(ProjectTemplates::DiscoverIn({ templatesDirectory }).size() == 2);

	const IndexProject project = IndexProject::Create("New Game", Path::ToUtf8(workDirectory / "Projects"), "New Game", &created);
	const IndexProject loaded = IndexProject::Load(project.RootDirectory);
	CHECK(loaded.Name == "New Game");
	CHECK(loaded.BuildWidth == 1280);
	CHECK(loaded.BuildHeight == 720);
	CHECK(loaded.BuildCompany == "Acme");
	CHECK(loaded.ExecutableName.empty());
	CHECK(loaded.CustomDefines == std::vector<std::string>{ "USE_ACME" });
	CHECK(loaded.PrefersThreeD());
	CHECK(loaded.AssetSerializationFormat == IndexProject::ProjectAssetSerializationFormat::Binary);
	CHECK(loaded.LastOpenedScene == loaded.StartupScene);
	CHECK(File::ReadAllText(Path::Combine(loaded.AssetsDirectory, "Art", "Sprites", "readme.txt")) == "art");
	CHECK(File::Exists(Path::Combine(loaded.GetBuildProfilesDirectory(), "Custom.json")));
	CHECK(File::Exists(loaded.CsprojPath));

	Json::Value sceneRoot;
	REQUIRE(SceneSerializerStorage::ReadRootFromFile(loaded.GetSceneFilePath(loaded.StartupScene), sceneRoot, &error));
	CHECK(sceneRoot.FindMember("entities"));

	// Only templates inside the user template folder may be deleted.
	CHECK_FALSE(ProjectTemplates::DeleteUserTemplate(created, &error));
	CHECK(std::filesystem::exists(Path::FromUtf8(created.RootDirectory)));

	std::error_code ec;
	std::filesystem::remove_all(workDirectory, ec);
}

TEST_CASE("User templates can be edited, refreshed from their source project and deleted") {
	const std::filesystem::path workDirectory = MakeTempDirectory("index-user-template-edit-");
	const std::string userTemplates = Path::ToUtf8(workDirectory / "UserTemplates");
	const ScopedEnvironmentVariable userTemplatesOverride("INDEX_USER_TEMPLATES_DIR", userTemplates);
	REQUIRE(ProjectTemplates::GetUserTemplatesDirectory() == userTemplates);

	IndexProject source = IndexProject::Create("Edit Source", Path::ToUtf8(workDirectory / "Projects"));
	ProjectTemplate created;
	std::string error;
	REQUIRE_MESSAGE(ProjectTemplates::CreateFromProject(source.RootDirectory, { "Editable", "", "" }, created, &error), error);
	CHECK(created.IsUserTemplate);
	CHECK(created.SourceProjectPath == source.RootDirectory);

	ProjectTemplate edited;
	REQUIRE_MESSAGE(ProjectTemplates::UpdateUserTemplate(created, { "Renamed", "Now with levels", "Mobile" },
		created.SourceProjectPath, edited, &error), error);
	CHECK(edited.Id == created.Id);
	CHECK(edited.DisplayName == "Renamed");
	CHECK(edited.Description == "Now with levels");
	CHECK(edited.Category == "Mobile");
	CHECK(edited.IsUserTemplate);
	ProjectTemplate rejected;
	CHECK_FALSE(ProjectTemplates::UpdateUserTemplate(created, { "", "", "" }, {}, rejected, &error));
	CHECK_FALSE(ProjectTemplates::UpdateUserTemplate(created, { "Renamed", "", "" },
		Path::ToUtf8(workDirectory / "NoProject"), rejected, &error));

	std::filesystem::create_directories(Path::FromUtf8(source.AssetsDirectory) / "Levels");
	REQUIRE(File::WriteAllText(Path::Combine(source.AssetsDirectory, "Levels", "Level1.txt"), "level"));
	source.BuildWidth = 800;
	REQUIRE(source.Save());

	ProjectTemplate refreshed;
	REQUIRE_MESSAGE(ProjectTemplates::RefreshFromSourceProject(edited, refreshed, &error), error);
	CHECK(refreshed.Id == created.Id);
	CHECK(refreshed.DisplayName == "Renamed");
	CHECK(File::ReadAllText(Path::Combine(refreshed.GetAssetsDirectory(), "Levels", "Level1.txt")) == "level");
	IndexProject settings;
	REQUIRE(IndexProject::TryLoad(refreshed.RootDirectory, settings, &error, false));
	CHECK(settings.BuildWidth == 800);
	CHECK_FALSE(std::filesystem::exists(Path::FromUtf8(refreshed.RootDirectory + ".updating")));

	const std::vector<ProjectTemplate> discovered = ProjectTemplates::DiscoverIn({ userTemplates });
	REQUIRE(discovered.size() == 1);
	CHECK(discovered.front().IsUserTemplate);

	REQUIRE_MESSAGE(ProjectTemplates::DeleteUserTemplate(refreshed, &error), error);
	CHECK_FALSE(std::filesystem::exists(Path::FromUtf8(refreshed.RootDirectory)));

	std::error_code ec;
	std::filesystem::remove_all(workDirectory, ec);
}

TEST_CASE("Projects without a modules field load as 2D") {
	const std::filesystem::path parent = MakeTempDirectory("index-legacy-modules-");
	IndexProject project = IndexProject::Create("LegacyGame", Path::ToUtf8(parent));

	Json::Value root = Json::Parse(File::ReadAllText(project.ProjectFilePath));
	REQUIRE(root.IsObject());
	std::erase_if(root.GetObject(), [](const auto& member) { return member.first == "modules"; });
	REQUIRE(File::WriteAllText(project.ProjectFilePath, Json::Stringify(root, true)));

	const IndexProject loaded = IndexProject::Load(project.RootDirectory);
	CHECK(loaded.HasModule(IndexProject::k_Module2D));
	CHECK_FALSE(loaded.PrefersThreeD());

	std::error_code ec;
	std::filesystem::remove_all(parent, ec);
}
