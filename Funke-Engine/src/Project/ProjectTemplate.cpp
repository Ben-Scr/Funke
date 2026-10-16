#include "pch.hpp"
#include "Project/ProjectTemplate.hpp"

#include "Core/Version.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SpecialFolder.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace Funke {

	namespace {
		bool IsBuildArtifactDirectory(const std::filesystem::path& name) {
			const std::string text = Path::ToUtf8(name);
			return text == "bin" || text == "obj" || text == "build" || text == ".vs" || text == ".git";
		}

		// Recursive copy that skips build output; a missing source is not an error.
		bool CopyTree(const std::filesystem::path& source, const std::filesystem::path& destination,
			bool skipBuildArtifacts, std::string& outError) {
			std::error_code ec;
			if (!std::filesystem::is_directory(source, ec)) return true;
			std::filesystem::create_directories(destination, ec);
			if (ec) {
				outError = "Could not create '" + Path::ToUtf8(destination) + "': " + ec.message();
				return false;
			}
			for (auto it = std::filesystem::recursive_directory_iterator(source, ec);
				!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
				const std::filesystem::path relative = std::filesystem::relative(it->path(), source, ec);
				if (ec) break;
				const bool isDirectory = it->is_directory(ec);
				if (skipBuildArtifacts && isDirectory && IsBuildArtifactDirectory(it->path().filename())) {
					it.disable_recursion_pending();
					continue;
				}
				const std::filesystem::path target = destination / relative;
				if (isDirectory) {
					std::filesystem::create_directories(target, ec);
				}
				else {
					std::filesystem::create_directories(target.parent_path(), ec);
					if (!ec) std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::overwrite_existing, ec);
				}
				if (ec) {
					outError = "Could not copy '" + Path::ToUtf8(it->path()) + "': " + ec.message();
					return false;
				}
			}
			if (ec) {
				outError = "Could not read '" + Path::ToUtf8(source) + "': " + ec.message();
				return false;
			}
			return true;
		}

		std::string MakeTemplateFolderName(std::string_view displayName) {
			std::string folder;
			for (char c : displayName) {
				const bool invalid = static_cast<unsigned char>(c) < 32
					|| c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*';
				folder.push_back(invalid ? '_' : c);
			}
			while (!folder.empty() && (folder.back() == ' ' || folder.back() == '.')) folder.pop_back();
			while (!folder.empty() && (folder.front() == ' ' || folder.front() == '.')) folder.erase(folder.begin());
			if (folder.size() > 64) folder.resize(64);
			return FunkeProject::IsValidProjectName(folder) ? folder : std::string("Template");
		}

		std::filesystem::path CanonicalOrSelf(const std::filesystem::path& path) {
			std::error_code ec;
			std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
			return ec ? path.lexically_normal() : canonical;
		}

		bool IsUserTemplateRoot(const std::string& rootDirectory) {
			const std::string userDirectory = ProjectTemplates::GetUserTemplatesDirectory();
			if (userDirectory.empty() || rootDirectory.empty()) return false;
			const std::filesystem::path root = CanonicalOrSelf(Path::FromUtf8(rootDirectory));
			return root.parent_path() == CanonicalOrSelf(Path::FromUtf8(userDirectory))
				&& File::Exists(Path::ToUtf8(root / std::string(ProjectTemplates::k_ManifestFileName)));
		}

		// Everything of a template except template.json: assets, settings, profiles, native sources, local packages, preview.
		bool SnapshotProject(const FunkeProject& project, const std::filesystem::path& root, std::string& outError,
			const ProjectTemplates::ProgressCallback& progressCallback) {
			auto report = [&progressCallback](float progress, std::string_view stage) {
				if (progressCallback) progressCallback(progress, stage);
			};
			const std::filesystem::path projectRoot = Path::FromUtf8(project.RootDirectory);

			report(0.05f, "Copying assets...");
			if (!CopyTree(Path::FromUtf8(project.AssetsDirectory), root / "Assets", false, outError)) return false;

			report(0.75f, "Copying build profiles and native scripts...");
			if (!CopyTree(Path::FromUtf8(project.GetBuildProfilesDirectory()), root / "BuildProfiles", false, outError)
				|| !CopyTree(Path::FromUtf8(project.NativeSourceDir), root / "NativeScripts" / "Source", true, outError)) {
				return false;
			}
			for (const std::string& package : FunkeProject::EnumerateProjectLocalPackages(project.RootDirectory)) {
				const std::filesystem::path packagePath = Path::FromUtf8(package);
				if (!CopyTree(projectRoot / "Packages" / packagePath, root / "Packages" / packagePath, true, outError)) return false;
			}

			report(0.9f, "Writing template settings...");
			Json::Value settings;
			std::string parseError;
			if (!Json::TryParse(File::ReadAllText(project.ProjectFilePath), settings, &parseError) || !settings.IsObject()) {
				outError = "Could not read '" + project.ProjectFilePath + "': " + parseError;
				return false;
			}
			// Per-user editor state of the source project; new projects open their startup scene.
			settings.AddMember("lastOpenedScene", project.StartupScene);
			std::erase_if(settings.GetObject(), [](const auto& member) { return member.first == "editorView3D"; });
			if (!File::WriteAllText(Path::ToUtf8(root / std::string(ProjectTemplates::k_ProjectSettingsFileName)),
				Json::Stringify(settings, true))) {
				outError = "Could not write the template project settings";
				return false;
			}

			std::error_code ec;
			const std::filesystem::path thumbnail = projectRoot / ".funke" / "thumbnail.png";
			if (std::filesystem::is_regular_file(thumbnail, ec)) {
				std::filesystem::copy_file(thumbnail, root / "preview.png", std::filesystem::copy_options::overwrite_existing, ec);
			}
			return true;
		}

		bool ReadManifest(const std::string& rootDirectory, Json::Value& outManifest, std::string& outError) {
			const std::string path = Path::Combine(rootDirectory, std::string(ProjectTemplates::k_ManifestFileName));
			std::string parseError;
			if (!Json::TryParse(File::ReadAllText(path), outManifest, &parseError) || !outManifest.IsObject()) {
				outError = "Invalid " + path + ": " + parseError;
				return false;
			}
			return true;
		}

		bool WriteManifest(const std::string& rootDirectory, const Json::Value& manifest, std::string& outError) {
			const std::string path = Path::Combine(rootDirectory, std::string(ProjectTemplates::k_ManifestFileName));
			if (!File::WriteAllText(path, Json::Stringify(manifest, true))) {
				outError = "Could not write " + path;
				return false;
			}
			return true;
		}
	}

	std::string ProjectTemplate::GetAssetsDirectory() const {
		return Path::Combine(RootDirectory, "Assets");
	}

	std::string ProjectTemplate::GetProjectSettingsPath() const {
		const std::string path = Path::Combine(RootDirectory, std::string(ProjectTemplates::k_ProjectSettingsFileName));
		return File::Exists(path) ? path : std::string{};
	}

	std::string ProjectTemplates::GetUserTemplatesDirectory() {
		// Tests point this at a scratch folder so they never touch the real one.
		if (const char* overrideDirectory = std::getenv("FUNKE_USER_TEMPLATES_DIR"); overrideDirectory && *overrideDirectory) {
			return overrideDirectory;
		}
		try {
			const std::string base = Path::GetSpecialFolderPath(SpecialFolder::LocalAppData);
			return base.empty() ? std::string{} : Path::Combine(base, "Funke", "Templates");
		}
		catch (...) {
			return {};
		}
	}

	std::vector<std::string> ProjectTemplates::GetSearchDirectories() {
		std::vector<std::string> directories;
		auto addUnique = [&directories](const std::filesystem::path& candidate) {
			std::error_code ec;
			const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, ec);
			if (ec || !std::filesystem::is_directory(canonical, ec)) {
				return;
			}
			const std::string utf8 = Path::ToUtf8(canonical);
			if (std::find(directories.begin(), directories.end(), utf8) == directories.end()) {
				directories.push_back(utf8);
			}
		};

		addUnique(Path::FromUtf8(Path::ExecutableDir()) / "templates");
		const std::string engineRoot = FunkeProject::GetEngineRootDir();
		if (!engineRoot.empty()) {
			addUnique(Path::FromUtf8(engineRoot) / "templates");
		}
		if (const std::string userDirectory = GetUserTemplatesDirectory(); !userDirectory.empty()) {
			addUnique(Path::FromUtf8(userDirectory));
		}
		return directories;
	}

	bool ProjectTemplates::TryLoad(const std::string& templateDirectory, ProjectTemplate& outTemplate, std::string* outError) {
		auto fail = [outError](std::string message) {
			if (outError) *outError = std::move(message);
			return false;
		};

		const std::string manifestPath = Path::Combine(templateDirectory, std::string(k_ManifestFileName));
		if (!File::Exists(manifestPath)) {
			return fail("Missing " + std::string(k_ManifestFileName) + " in " + templateDirectory);
		}

		std::string parseError;
		Json::Value root;
		if (!Json::TryParse(File::ReadAllText(manifestPath), root, &parseError) || !root.IsObject()) {
			return fail("Invalid " + manifestPath + ": " + parseError);
		}

		ProjectTemplate result;
		result.RootDirectory = templateDirectory;
		if (const Json::Value* v = root.FindMember("id")) result.Id = v->AsStringOr("");
		if (const Json::Value* v = root.FindMember("displayName")) result.DisplayName = v->AsStringOr("");
		if (const Json::Value* v = root.FindMember("description")) result.Description = v->AsStringOr("");
		if (const Json::Value* v = root.FindMember("category")) result.Category = v->AsStringOr("");
		if (const Json::Value* v = root.FindMember("startupScene")) result.StartupScene = v->AsStringOr(result.StartupScene);
		if (const Json::Value* v = root.FindMember("sortOrder")) result.SortOrder = v->AsIntOr(0);
		if (const Json::Value* v = root.FindMember("sourceProjectPath")) result.SourceProjectPath = v->AsStringOr("");
		if (const Json::Value* v = root.FindMember("modules"); v && v->IsArray()) {
			for (const Json::Value& module : v->GetArray()) {
				std::string id = module.AsStringOr("");
				if (!id.empty()) result.Modules.push_back(std::move(id));
			}
		}

		if (result.Id.empty()) {
			return fail(manifestPath + " has no \"id\"");
		}
		if (result.DisplayName.empty()) {
			result.DisplayName = result.Id;
		}
		if (!FunkeProject::IsValidProjectName(result.StartupScene)) {
			return fail(manifestPath + " has an invalid \"startupScene\"");
		}

		const std::string preview = Path::Combine(templateDirectory, "preview.png");
		if (File::Exists(preview)) {
			result.PreviewImagePath = preview;
		}

		outTemplate = std::move(result);
		return true;
	}

	std::vector<ProjectTemplate> ProjectTemplates::DiscoverIn(const std::vector<std::string>& searchDirectories) {
		std::vector<ProjectTemplate> templates;
		std::unordered_set<std::string> seenIds;
		const std::string userDirectory = GetUserTemplatesDirectory();
		const std::filesystem::path canonicalUserDirectory = userDirectory.empty()
			? std::filesystem::path{} : CanonicalOrSelf(Path::FromUtf8(userDirectory));
		for (const std::string& searchDirectory : searchDirectories) {
			const bool isUserDirectory = !canonicalUserDirectory.empty()
				&& CanonicalOrSelf(Path::FromUtf8(searchDirectory)) == canonicalUserDirectory;
			std::error_code ec;
			std::filesystem::directory_iterator it(Path::FromUtf8(searchDirectory), ec);
			if (ec) continue;

			std::vector<std::filesystem::path> folders;
			for (const auto& entry : it) {
				if (entry.is_directory(ec)) folders.push_back(entry.path());
			}
			std::sort(folders.begin(), folders.end());

			for (const auto& folder : folders) {
				ProjectTemplate loaded;
				std::string error;
				if (!TryLoad(Path::ToUtf8(folder), loaded, &error)) {
					FNK_CORE_WARN_TAG("ProjectTemplates", "Skipping template folder: {}", error);
					continue;
				}
				loaded.IsUserTemplate = isUserDirectory;
				if (seenIds.insert(loaded.Id).second) {
					templates.push_back(std::move(loaded));
				}
			}
		}

		std::stable_sort(templates.begin(), templates.end(), [](const ProjectTemplate& a, const ProjectTemplate& b) {
			if (a.SortOrder != b.SortOrder) return a.SortOrder < b.SortOrder;
			return a.DisplayName < b.DisplayName;
		});
		return templates;
	}

	std::vector<ProjectTemplate> ProjectTemplates::Discover() {
		return DiscoverIn(GetSearchDirectories());
	}

	bool ProjectTemplates::TryFind(std::string_view id, ProjectTemplate& outTemplate) {
		for (ProjectTemplate& candidate : Discover()) {
			if (candidate.Id == id) {
				outTemplate = std::move(candidate);
				return true;
			}
		}
		return false;
	}

	bool ProjectTemplates::CreateFromProject(const std::string& projectRootDirectory, const ProjectTemplateCreateInfo& info,
		ProjectTemplate& outTemplate, std::string* outError, const std::string& templatesDirectory,
		const ProgressCallback& progressCallback) {
		auto fail = [outError](std::string message) {
			if (outError) *outError = std::move(message);
			return false;
		};
		auto report = [&progressCallback](float progress, std::string_view stage) {
			if (progressCallback) progressCallback(progress, stage);
		};

		FunkeProject project;
		std::string loadError;
		if (!FunkeProject::TryLoad(projectRootDirectory, project, &loadError, false)) {
			return fail(loadError);
		}

		const std::string displayName = info.DisplayName.empty() ? project.Name : info.DisplayName;
		const std::string parentDirectory = templatesDirectory.empty() ? GetUserTemplatesDirectory() : templatesDirectory;
		if (parentDirectory.empty()) {
			return fail("Could not resolve the user template folder");
		}

		std::vector<std::string> searchDirectories = GetSearchDirectories();
		searchDirectories.push_back(parentDirectory);
		std::unordered_set<std::string> existingIds;
		for (const ProjectTemplate& existing : DiscoverIn(searchDirectories)) {
			existingIds.insert(existing.Id);
		}
		const std::string baseId = std::string(k_UserTemplateIdPrefix) + MakeIdentifier(displayName);
		std::string id = baseId;
		for (int suffix = 2; existingIds.contains(id); ++suffix) {
			id = baseId + "_" + std::to_string(suffix);
		}

		std::error_code ec;
		const std::filesystem::path parent = Path::FromUtf8(parentDirectory);
		const std::string baseFolder = MakeTemplateFolderName(displayName);
		std::filesystem::path root = parent / Path::FromUtf8(baseFolder);
		for (int suffix = 2; std::filesystem::exists(root, ec); ++suffix) {
			root = parent / Path::FromUtf8(baseFolder + " (" + std::to_string(suffix) + ")");
		}
		std::filesystem::create_directories(root, ec);
		if (ec) {
			return fail("Could not create template folder '" + Path::ToUtf8(root) + "': " + ec.message());
		}

		// Only ever removes the folder created above.
		auto failAndCleanUp = [&](std::string message) {
			std::error_code cleanup;
			std::filesystem::remove_all(root, cleanup);
			return fail(std::move(message));
		};

		std::string snapshotError;
		if (!SnapshotProject(project, root, snapshotError, progressCallback)) {
			return failAndCleanUp(snapshotError);
		}

		Json::Value manifest = Json::Value::MakeObject();
		manifest.AddMember("id", id);
		manifest.AddMember("displayName", displayName);
		manifest.AddMember("description", info.Description);
		manifest.AddMember("category", info.Category.empty() ? std::string(project.PrefersThreeD() ? "3D" : "2D") : info.Category);
		manifest.AddMember("sortOrder", k_UserTemplateSortOrder);
		Json::Value modules = Json::Value::MakeArray();
		for (const std::string& module : project.Modules) {
			modules.Append(module);
		}
		manifest.AddMember("modules", std::move(modules));
		manifest.AddMember("startupScene", project.StartupScene);
		manifest.AddMember("sourceProject", project.Name);
		manifest.AddMember("sourceProjectPath", project.RootDirectory);
		manifest.AddMember("engineVersion", std::string(FNK_VERSION));
		if (!File::WriteAllText(Path::ToUtf8(root / std::string(k_ManifestFileName)), Json::Stringify(manifest, true))) {
			return failAndCleanUp("Could not write " + std::string(k_ManifestFileName));
		}

		ProjectTemplate created;
		std::string loadTemplateError;
		if (!TryLoad(Path::ToUtf8(root), created, &loadTemplateError)) {
			return failAndCleanUp(loadTemplateError);
		}
		const std::string userDirectory = GetUserTemplatesDirectory();
		created.IsUserTemplate = !userDirectory.empty()
			&& CanonicalOrSelf(parent) == CanonicalOrSelf(Path::FromUtf8(userDirectory));

		report(1.0f, "Template ready.");
		FNK_CORE_INFO_TAG("ProjectTemplates", "Saved project '{}' as template '{}' at {}", project.Name, id, Path::ToUtf8(root));
		outTemplate = std::move(created);
		if (outError) outError->clear();
		return true;
	}

	bool ProjectTemplates::DeleteUserTemplate(const ProjectTemplate& projectTemplate, std::string* outError) {
		auto fail = [outError](std::string message) {
			if (outError) *outError = std::move(message);
			return false;
		};

		if (!IsUserTemplateRoot(projectTemplate.RootDirectory)) {
			return fail("'" + projectTemplate.RootDirectory + "' is not in the user template folder");
		}

		std::error_code ec;
		std::filesystem::remove_all(CanonicalOrSelf(Path::FromUtf8(projectTemplate.RootDirectory)), ec);
		if (ec) {
			return fail("Could not delete template '" + projectTemplate.DisplayName + "': " + ec.message());
		}
		FNK_CORE_INFO_TAG("ProjectTemplates", "Deleted user template '{}'", projectTemplate.Id);
		return true;
	}

	bool ProjectTemplates::UpdateUserTemplate(const ProjectTemplate& projectTemplate, const ProjectTemplateCreateInfo& info,
		const std::string& sourceProjectPath, ProjectTemplate& outTemplate, std::string* outError) {
		auto fail = [outError](std::string message) {
			if (outError) *outError = std::move(message);
			return false;
		};
		if (!IsUserTemplateRoot(projectTemplate.RootDirectory)) {
			return fail("Only templates in the user template folder can be edited");
		}
		if (info.DisplayName.empty()) {
			return fail("The template needs a name");
		}
		if (!sourceProjectPath.empty() && !FunkeProject::Validate(sourceProjectPath)) {
			return fail("'" + sourceProjectPath + "' is not a Funke project");
		}

		Json::Value manifest;
		std::string error;
		if (!ReadManifest(projectTemplate.RootDirectory, manifest, error)) return fail(error);
		manifest.AddMember("displayName", info.DisplayName);
		manifest.AddMember("description", info.Description);
		manifest.AddMember("category", info.Category);
		manifest.AddMember("sourceProjectPath", sourceProjectPath);
		if (!WriteManifest(projectTemplate.RootDirectory, manifest, error)) return fail(error);

		ProjectTemplate updated;
		if (!TryLoad(projectTemplate.RootDirectory, updated, &error)) return fail(error);
		updated.IsUserTemplate = true;
		outTemplate = std::move(updated);
		if (outError) outError->clear();
		return true;
	}

	bool ProjectTemplates::RefreshFromSourceProject(const ProjectTemplate& projectTemplate, ProjectTemplate& outTemplate,
		std::string* outError, const ProgressCallback& progressCallback) {
		auto fail = [outError](std::string message) {
			if (outError) *outError = std::move(message);
			return false;
		};
		if (!IsUserTemplateRoot(projectTemplate.RootDirectory)) {
			return fail("Only templates in the user template folder can be updated");
		}
		if (projectTemplate.SourceProjectPath.empty()) {
			return fail("Template '" + projectTemplate.DisplayName + "' has no source project");
		}

		FunkeProject project;
		std::string error;
		if (!FunkeProject::TryLoad(projectTemplate.SourceProjectPath, project, &error, false)) {
			return fail(error);
		}

		// Snapshot next to the template first, so a failed copy leaves the old content untouched.
		const std::filesystem::path root = CanonicalOrSelf(Path::FromUtf8(projectTemplate.RootDirectory));
		const std::filesystem::path staging = root.parent_path() / (Path::ToUtf8(root.filename()) + ".updating");
		std::error_code ec;
		std::filesystem::remove_all(staging, ec);
		std::filesystem::create_directories(staging, ec);
		if (ec) {
			return fail("Could not create '" + Path::ToUtf8(staging) + "': " + ec.message());
		}
		auto removeStaging = [&staging]() {
			std::error_code cleanup;
			std::filesystem::remove_all(staging, cleanup);
		};
		if (!SnapshotProject(project, staging, error, progressCallback)) {
			removeStaging();
			return fail(error);
		}

		if (progressCallback) progressCallback(0.95f, "Replacing template content...");
		auto listEntries = [&ec](const std::filesystem::path& directory) {
			std::vector<std::filesystem::path> entries;
			for (auto it = std::filesystem::directory_iterator(directory, ec);
				!ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
				entries.push_back(it->path());
			}
			return entries;
		};
		for (const std::filesystem::path& entry : listEntries(root)) {
			if (ec) break;
			if (entry.filename() == std::string(k_ManifestFileName)) continue;
			std::filesystem::remove_all(entry, ec);
		}
		if (!ec) {
			for (const std::filesystem::path& entry : listEntries(staging)) {
				if (ec) break;
				std::filesystem::rename(entry, root / entry.filename(), ec);
			}
		}
		removeStaging();
		if (ec) {
			return fail("Could not replace the content of template '" + projectTemplate.DisplayName + "': " + ec.message());
		}

		Json::Value manifest;
		if (!ReadManifest(projectTemplate.RootDirectory, manifest, error)) return fail(error);
		Json::Value modules = Json::Value::MakeArray();
		for (const std::string& module : project.Modules) {
			modules.Append(module);
		}
		manifest.AddMember("modules", std::move(modules));
		manifest.AddMember("startupScene", project.StartupScene);
		manifest.AddMember("sourceProject", project.Name);
		manifest.AddMember("engineVersion", std::string(FNK_VERSION));
		if (!WriteManifest(projectTemplate.RootDirectory, manifest, error)) return fail(error);

		ProjectTemplate updated;
		if (!TryLoad(projectTemplate.RootDirectory, updated, &error)) return fail(error);
		updated.IsUserTemplate = true;
		if (progressCallback) progressCallback(1.0f, "Template updated.");
		FNK_CORE_INFO_TAG("ProjectTemplates", "Updated template '{}' from {}", updated.Id, projectTemplate.SourceProjectPath);
		outTemplate = std::move(updated);
		if (outError) outError->clear();
		return true;
	}

	std::string ProjectTemplates::MakeIdentifier(std::string_view projectName) {
		std::string identifier;
		identifier.reserve(projectName.size() + 1);
		for (char c : projectName) {
			const unsigned char uc = static_cast<unsigned char>(c);
			identifier.push_back(std::isalnum(uc) || c == '_' ? c : '_');
		}
		if (identifier.empty() || std::isdigit(static_cast<unsigned char>(identifier.front()))) {
			identifier.insert(identifier.begin(), '_');
		}
		return identifier;
	}

	std::string ProjectTemplates::ExpandPlaceholders(std::string_view text, std::string_view projectName) {
		struct Replacement { std::string_view Token; std::string Value; };
		const Replacement replacements[] = {
			{ "{{ProjectName}}", std::string(projectName) },
			{ "{{ProjectIdentifier}}", MakeIdentifier(projectName) },
		};

		std::string result(text);
		for (const Replacement& replacement : replacements) {
			size_t position = 0;
			while ((position = result.find(replacement.Token, position)) != std::string::npos) {
				result.replace(position, replacement.Token.size(), replacement.Value);
				position += replacement.Value.size();
			}
		}
		return result;
	}

}
