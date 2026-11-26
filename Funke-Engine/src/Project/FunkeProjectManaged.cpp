#include "pch.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/FunkeProjectInternal.hpp"
#include "Project/EngineSdk.hpp"
#include "Project/ManagedSdk.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include <algorithm>
#include <sstream>

namespace Funke {
	using namespace ProjectInternal;

	namespace {
		std::string EscapeXml(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size());

			for (char c : value) {
				switch (c) {
				case '&':  escaped += "&amp;";  break;
				case '<':  escaped += "&lt;";   break;
				case '>':  escaped += "&gt;";   break;
				case '"':  escaped += "&quot;"; break;
				case '\'': escaped += "&apos;"; break;
				default:   escaped += c;        break;
				}
			}

			return escaped;
		}

		std::vector<std::string> GetPreferredBuildConfigurations() {
			std::vector<std::string> configurations;
			configurations.reserve(3);

			auto appendUnique = [&configurations](std::string value) {
				if (value.empty()) {
					return;
				}

				if (std::find(configurations.begin(), configurations.end(), value) == configurations.end()) {
					configurations.push_back(std::move(value));
				}
			};

			appendUnique(FunkeProject::GetActiveBuildConfiguration());
			appendUnique("Release");
			appendUnique("Debug");
			appendUnique("Dist");
			return configurations;
		}

		std::vector<std::filesystem::path> BuildScriptCoreArtifactDirectories(const std::filesystem::path& engineRoot) {
			std::vector<std::filesystem::path> directories;
			if (engineRoot.empty()) {
				return directories;
			}

			auto appendUnique = [&directories](std::filesystem::path path) {
				if (path.empty()) {
					return;
				}

				std::error_code ec;
				const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
				const std::filesystem::path key = ec ? path.lexically_normal() : normalized;
				if (std::find(directories.begin(), directories.end(), key) == directories.end()) {
					directories.push_back(key);
				}
			};

			// Packaged SDK drops the DLL flat under EngineSDK/Funke-ScriptCore/ (no per-config bin tree).
			appendUnique(engineRoot / "Funke-ScriptCore");

			const std::string platformDirectory = GetNativeBuildPlatformDirectory();
			const auto preferredConfigurations = GetPreferredBuildConfigurations();
			for (const std::string& config : preferredConfigurations) {
				if (!platformDirectory.empty()) {
					appendUnique(engineRoot / "bin" / (config + "-" + platformDirectory) / "Funke-ScriptCore");
				}
			}

			const std::filesystem::path binDirectory = engineRoot / "bin";
			std::error_code ec;
			if (std::filesystem::exists(binDirectory, ec) && std::filesystem::is_directory(binDirectory, ec)) {
				for (const auto& entry : std::filesystem::directory_iterator(binDirectory, ec)) {
					if (ec || !entry.is_directory()) {
						continue;
					}

					appendUnique(entry.path() / "Funke-ScriptCore");
				}
			}

			return directories;
		}

		std::filesystem::path FindScriptCoreArtifact(const std::filesystem::path& engineRoot, const std::string& filename) {
			for (const auto& directory : BuildScriptCoreArtifactDirectories(engineRoot)) {
				const std::filesystem::path candidate = directory / filename;
				if (std::filesystem::exists(candidate)) {
					return candidate;
				}
			}

			return {};
		}
	}

	bool ProjectInternal::EnsureManagedSdkGlobalJson(const FunkeProject& project, std::string* outError) {
		const std::filesystem::path globalJsonPath = Path::FromUtf8(project.RootDirectory) / "global.json";
		const std::string globalJsonPathUtf8 = Path::ToUtf8(globalJsonPath);
		const bool existed = File::Exists(globalJsonPathUtf8);
		const std::string original = existed ? File::ReadAllText(globalJsonPathUtf8) : std::string{};

		Json::Value root = Json::Value::MakeObject();
		if (existed) {
			std::string parseError;
			if (!Json::TryParse(original, root, &parseError) || !root.IsObject()) {
				if (outError) *outError = "Could not migrate global.json: " + parseError;
				return false;
			}
		}

		Json::Value* sdk = root.FindMember("sdk");
		if (sdk && !sdk->IsObject()) {
			if (outError) *outError = "Could not migrate global.json: 'sdk' must be an object";
			return false;
		}
		if (!sdk) sdk = &root.AddMember("sdk", Json::Value::MakeObject());
		sdk->AddMember("version", ManagedSdk::k_Version);
		sdk->AddMember("rollForward", ManagedSdk::k_RollForward);
		sdk->AddMember("allowPrerelease", false);

		const std::string updated = Json::Stringify(root, true);
		if (existed && updated == original) return true;
		if (existed) {
			const std::filesystem::path backupPath = globalJsonPath.parent_path()
				/ "global.json.pre-index-net10.bak";
			const std::string backupPathUtf8 = Path::ToUtf8(backupPath);
			if (!File::Exists(backupPathUtf8) && !File::WriteAllText(backupPathUtf8, original)) {
				if (outError) *outError = "Could not back up global.json before migration";
				return false;
			}
		}
		if (!File::WriteAllText(globalJsonPathUtf8, updated)) {
			if (outError) *outError = "Could not write global.json";
			return false;
		}
		return true;
	}

	bool ProjectInternal::EnsureManagedSolutionHeader(const FunkeProject& project, std::string* outError) {
		if (!File::Exists(project.CsprojPath) || !File::Exists(project.SlnPath)) return true;
		const std::string csproj = File::ReadAllText(project.CsprojPath);
		if (csproj.find("<TargetFramework>net10.0</TargetFramework>") == std::string::npos) return true;

		std::string solution = File::ReadAllText(project.SlnPath);
		constexpr std::string_view requiredHeader = "# Visual Studio Version 18";
		const size_t headerStart = solution.find("# Visual Studio Version ");
		if (headerStart != std::string::npos) {
			const size_t headerEnd = solution.find_first_of("\r\n", headerStart);
			const size_t headerLength = (headerEnd == std::string::npos ? solution.size() : headerEnd) - headerStart;
			if (solution.compare(headerStart, headerLength, requiredHeader) == 0) return true;
			solution.replace(headerStart, headerLength, requiredHeader);
		}
		else {
			const size_t formatLine = solution.find("Microsoft Visual Studio Solution File, Format Version 12.00");
			if (formatLine == std::string::npos) {
				if (outError) *outError = "Could not migrate solution: unrecognized .sln header";
				return false;
			}
			const size_t lineEnd = solution.find('\n', formatLine);
			if (lineEnd == std::string::npos) solution += "\n" + std::string(requiredHeader) + "\n";
			else solution.insert(lineEnd + 1, std::string(requiredHeader) + "\n");
		}

		if (!File::WriteAllText(project.SlnPath, solution)) {
			if (outError) *outError = "Could not update the Visual Studio solution header";
			return false;
		}
		return true;
	}

	bool FunkeProject::WriteManagedDefinesProps(bool* outAddedImport) const {
		if (outAddedImport) *outAddedImport = false;
		if (CsprojPath.empty()) return false;

		// FUNKE_EDITOR (not FUNKE_BUILD): this props file drives VS IntelliSense for the editor's hot-reload
		// context; shipping builds override DefineConstants on the command line and ignore this file.
		std::string defines = "$(DefineConstants);FUNKE_EDITOR";
		const std::string profile = GetActiveBuildProfileDefine();
		if (!profile.empty()) {
			defines += ';';
			defines += profile;
		}
		// Same platform and profile defines as the editor's script compile (BuildManagedDefineConstants).
		const EditorScriptTarget target = GetEditorScriptTarget();
		const std::string platform = GetManagedPlatformDefine(target.Platform);
		if (!platform.empty()) {
			defines += ';';
			defines += platform;
		}
		std::vector<std::string> customDefines = CustomDefines;
		for (const std::string& profileDefine : target.ProfileDefines) {
			if (std::find(customDefines.begin(), customDefines.end(), profileDefine) == customDefines.end()) {
				customDefines.push_back(profileDefine);
			}
		}
		for (const std::string& custom : customDefines) {
			if (custom.empty()) continue;
			defines += ';';
			defines += custom;
		}

		// Emit alongside Packages/FunkePackages.props so the same .csproj
		// import pattern works. Don't fail the save if the Packages dir
		// doesn't exist yet (project might be mid-create) — try to create it.
		const std::filesystem::path csprojDir =
			Path::FromUtf8(CsprojPath).parent_path();
		const std::filesystem::path packagesDir = csprojDir / "Packages";
		std::error_code ec;
		std::filesystem::create_directories(packagesDir, ec);
		const std::filesystem::path propsPath = packagesDir / "FunkeDefines.props";

		// Brief, hand-edited header so users who open it understand it
		// regenerates on every save.
		std::string contents =
			"<!-- Auto-generated by the Funke editor on every project save. -->\n"
			"<!-- Edit Project Settings → Build Settings → Custom Defines instead. -->\n"
			"<Project>\n"
			"  <PropertyGroup>\n"
			"    <DefineConstants>" + defines + "</DefineConstants>\n"
			"  </PropertyGroup>\n"
			"</Project>\n";

		const std::string propsPathUtf8 = Path::ToUtf8(propsPath);
		const bool wrote = (File::Exists(propsPathUtf8) && File::ReadAllText(propsPathUtf8) == contents)
			|| File::WriteAllText(propsPathUtf8, contents);
		if (!wrote) {
			FNK_CORE_WARN_TAG("FunkeProject",
				"Failed to write {} — IntelliSense will not see custom defines.",
				propsPathUtf8);
			return false;
		}

		// One-shot migration: inject the FunkeDefines.props import if the .csproj doesn't already have it.
		if (!File::Exists(CsprojPath)) return true;
		std::string csproj = File::ReadAllText(CsprojPath);
		if (csproj.find("Packages/FunkeDefines.props") != std::string::npos
			|| csproj.find("Packages\\FunkeDefines.props") != std::string::npos)
		{
			return true;
		}

		// Insert right after the FunkePackages.props import so both
		// Funke-managed imports stay grouped. Fall back to inserting
		// before </Project> if the FunkePackages line isn't found.
		const std::string anchor = "Packages/FunkePackages.props";
		const size_t anchorPos = csproj.find(anchor);
		const std::string newImport =
			"\n  <Import Project=\"Packages/FunkeDefines.props\" "
			"Condition=\"Exists('Packages/FunkeDefines.props')\" />";

		bool patched = false;
		if (anchorPos != std::string::npos) {
			const size_t lineEnd = csproj.find('\n', anchorPos);
			if (lineEnd != std::string::npos) {
				csproj.insert(lineEnd, newImport);
				patched = true;
			}
		}
		if (!patched) {
			const size_t closeTag = csproj.rfind("</Project>");
			if (closeTag != std::string::npos) {
				csproj.insert(closeTag, newImport + "\n");
				patched = true;
			}
		}
		if (patched) {
			if (File::WriteAllText(CsprojPath, csproj)) {
				if (outAddedImport) *outAddedImport = true;
				FNK_CORE_INFO_TAG("FunkeProject",
					"Migrated '{}' to import FunkeDefines.props.", CsprojPath);
			}
		}
		return true;
	}

	bool ProjectInternal::ConfigurePackages(const FunkeProject& project, std::string* outError) {
		const auto installation = FunkeProject::GetEngineInstallation();
		if (!installation.IsValid() || !installation.ManagedProjects) {
			if (outError) *outError = installation.IsValid()
				? "This Funke SDK does not support managed projects. Install the C# development component."
				: installation.Error;
			return false;
		}
		const std::filesystem::path& engineRoot = installation.Root;
		const std::filesystem::path scriptCoreOutput = FindScriptCoreArtifact(engineRoot, "Funke-ScriptCore.dll");
		if (scriptCoreOutput.empty()) {
			if (outError) *outError = "Funke-ScriptCore.dll was not found in the engine installation";
			return false;
		}
		const std::filesystem::path scriptCorePdb = scriptCoreOutput.parent_path() / "Funke-ScriptCore.pdb";
		const std::vector<std::uint8_t> sourceDll = File::ReadAllBytes(Path::ToUtf8(scriptCoreOutput));
		if (sourceDll.empty()) {
			if (outError) *outError = "Funke-ScriptCore.dll could not be read";
			return false;
		}

		std::error_code error;
		const bool sourcePdbExists = std::filesystem::is_regular_file(scriptCorePdb, error) && !error;
		const std::vector<std::uint8_t> sourcePdb = sourcePdbExists
			? File::ReadAllBytes(Path::ToUtf8(scriptCorePdb)) : std::vector<std::uint8_t>{};
		if (sourcePdbExists && sourcePdb.empty()) {
			if (outError) *outError = "Funke-ScriptCore.pdb could not be read";
			return false;
		}

		const std::filesystem::path packageDir = Path::FromUtf8(project.PackagesDirectory) / "Funke-ScriptCore";
		std::filesystem::create_directories(packageDir, error);
		if (error) {
			if (outError) *outError = "Could not create the Funke-ScriptCore package directory: " + error.message();
			return false;
		}
		const std::filesystem::path destinationDll = packageDir / "Funke-ScriptCore.dll";
		const std::filesystem::path destinationPdb = packageDir / "Funke-ScriptCore.pdb";
		const bool destinationDllExists = std::filesystem::is_regular_file(destinationDll, error) && !error;
		error.clear();
		const bool destinationPdbExists = std::filesystem::is_regular_file(destinationPdb, error) && !error;
		const std::vector<std::uint8_t> oldDll = destinationDllExists
			? File::ReadAllBytes(Path::ToUtf8(destinationDll)) : std::vector<std::uint8_t>{};
		const std::vector<std::uint8_t> oldPdb = destinationPdbExists
			? File::ReadAllBytes(Path::ToUtf8(destinationPdb)) : std::vector<std::uint8_t>{};
		if ((destinationDllExists && oldDll.empty()) || (destinationPdbExists && oldPdb.empty())) {
			if (outError) *outError = "The existing Funke-ScriptCore package could not be read safely";
			return false;
		}

		const bool dllChanged = oldDll != sourceDll;
		if (dllChanged && !File::WriteAllBytes(Path::ToUtf8(destinationDll), sourceDll)) {
			if (outError) *outError = "Could not refresh Packages/Funke-ScriptCore/Funke-ScriptCore.dll";
			return false;
		}
		auto restoreDll = [&]() {
			if (!dllChanged) return;
			if (destinationDllExists) (void)File::WriteAllBytes(Path::ToUtf8(destinationDll), oldDll);
			else { std::error_code cleanup; std::filesystem::remove(destinationDll, cleanup); }
		};

		if (sourcePdbExists) {
			if (oldPdb != sourcePdb && !File::WriteAllBytes(Path::ToUtf8(destinationPdb), sourcePdb)) {
				restoreDll();
				if (outError) *outError = "Could not refresh Packages/Funke-ScriptCore/Funke-ScriptCore.pdb";
				return false;
			}
		}
		else if (destinationPdbExists) {
			std::filesystem::remove(destinationPdb, error);
			if (error) {
				restoreDll();
				if (outError) *outError = "Could not remove a stale Funke-ScriptCore.pdb: " + error.message();
				return false;
			}
		}

		// API docs for IntelliSense. Optional: a missing or unreadable file only costs tooltips.
		const std::filesystem::path scriptCoreXml = scriptCoreOutput.parent_path() / "Funke-ScriptCore.xml";
		if (std::filesystem::is_regular_file(scriptCoreXml, error) && !error) {
			const std::vector<std::uint8_t> sourceXml = File::ReadAllBytes(Path::ToUtf8(scriptCoreXml));
			const std::filesystem::path destinationXml = packageDir / "Funke-ScriptCore.xml";
			const bool upToDate = std::filesystem::is_regular_file(destinationXml, error)
				&& File::ReadAllBytes(Path::ToUtf8(destinationXml)) == sourceXml;
			if (!sourceXml.empty() && !upToDate && !File::WriteAllBytes(Path::ToUtf8(destinationXml), sourceXml)) {
				FNK_CORE_WARN_TAG("Project", "Could not copy Funke-ScriptCore.xml; script API tooltips will be missing.");
			}
		}
		return true;
	}
}
