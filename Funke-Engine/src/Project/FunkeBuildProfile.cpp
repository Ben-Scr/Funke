#include "pch.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Core/Log.hpp"

#include <algorithm>
#include <filesystem>

namespace Funke {

	const char* FunkeBuildProfile::PlatformToString(BuildPlatform platform) {
		switch (platform) {
			case BuildPlatform::Windows:  return "Windows";
			case BuildPlatform::Linux:    return "Linux";
			case BuildPlatform::MacOS:    return "MacOS";
			case BuildPlatform::Android:  return "Android";
			case BuildPlatform::IOS:      return "iOS";
			case BuildPlatform::VisionOS: return "visionOS";
			case BuildPlatform::Web:      return "Web";
		}
		return "Windows";
	}

	BuildPlatform FunkeBuildProfile::PlatformFromString(std::string_view value) {
		if (value == "Windows"  || value == "windows"  || value == "Win" || value == "win"
			|| value == "WindowsDesktop" || value == "Windows Desktop")                  return BuildPlatform::Windows;
		if (value == "Linux"    || value == "linux")                                     return BuildPlatform::Linux;
		if (value == "MacOS"    || value == "macOS"    || value == "macos"
			|| value == "OSX"   || value == "osx"      || value == "Mac")                return BuildPlatform::MacOS;
		if (value == "Android"  || value == "android")                                   return BuildPlatform::Android;
		if (value == "iOS"      || value == "IOS"      || value == "ios")                return BuildPlatform::IOS;
		if (value == "visionOS" || value == "VisionOS" || value == "visionos")           return BuildPlatform::VisionOS;
		if (value == "Web"      || value == "web"      || value == "HTML5" || value == "WebAssembly") return BuildPlatform::Web;
		return BuildPlatform::Windows;
	}

	const char* FunkeBuildProfile::PlatformDisplayName(BuildPlatform platform) {
		switch (platform) {
			case BuildPlatform::Windows:  return "Windows Desktop";
			case BuildPlatform::Linux:    return "Linux";
			case BuildPlatform::MacOS:    return "macOS";
			case BuildPlatform::Android:  return "Android";
			case BuildPlatform::IOS:      return "iOS";
			case BuildPlatform::VisionOS: return "visionOS";
			case BuildPlatform::Web:      return "Web";
		}
		return "Windows Desktop";
	}

	const char* FunkeBuildProfile::PlatformIconName(BuildPlatform platform) {
		switch (platform) {
			case BuildPlatform::Windows:  return "windows";
			case BuildPlatform::Linux:    return "linux";
			case BuildPlatform::MacOS:    return "macos";
			case BuildPlatform::Android:  return "android";
			case BuildPlatform::IOS:      return "ios";
			case BuildPlatform::VisionOS: return "visionos";
			case BuildPlatform::Web:      return "web";
		}
		return nullptr;
	}

	std::vector<BuildPlatform> FunkeBuildProfile::AllPlatforms() {
		return {
			BuildPlatform::Android,
			BuildPlatform::IOS,
			BuildPlatform::Linux,
			BuildPlatform::MacOS,
			BuildPlatform::VisionOS,
			BuildPlatform::Web,
			BuildPlatform::Windows,
		};
	}

	std::vector<FunkeProject::RenderBackend> FunkeBuildProfile::AllowedBackends(BuildPlatform platform) {
		switch (platform) {
			case BuildPlatform::Windows:
				return {
					FunkeProject::RenderBackend::Direct3D12,
					FunkeProject::RenderBackend::Direct3D11,
					FunkeProject::RenderBackend::Vulkan,
					FunkeProject::RenderBackend::OpenGL,
				};
			case BuildPlatform::Linux:
				return {
					FunkeProject::RenderBackend::Vulkan,
					FunkeProject::RenderBackend::OpenGL,
				};
			case BuildPlatform::MacOS:
			case BuildPlatform::IOS:
			case BuildPlatform::VisionOS:
				return {
					FunkeProject::RenderBackend::Metal,
				};
			case BuildPlatform::Android:
				return {
					FunkeProject::RenderBackend::Vulkan,
					FunkeProject::RenderBackend::OpenGLES,
				};
			case BuildPlatform::Web:
				return {
					FunkeProject::RenderBackend::OpenGLES,
				};
		}
		return {};
	}

	bool FunkeBuildProfile::IsBackendAllowed(BuildPlatform platform, FunkeProject::RenderBackend backend) {
		const auto allowed = AllowedBackends(platform);
		return std::find(allowed.begin(), allowed.end(), backend) != allowed.end();
	}

	void FunkeBuildProfile::ApplyTo(FunkeProject& project) const {
		if (Configuration) project.ActiveBuildProfile = *Configuration;
		for (const std::string& define : Defines) {
			if (define.empty()) continue;
			if (std::find(project.CustomDefines.begin(), project.CustomDefines.end(), define) == project.CustomDefines.end()) {
				project.CustomDefines.push_back(define);
			}
		}
		if (ShowRuntimeStats) project.ShowRuntimeStats = ShowRuntimeStats;
		if (ShowRuntimeLogs) project.ShowRuntimeLogs = ShowRuntimeLogs;
		if (DevConsole) project.DevConsole = DevConsole;
		if (IsolatedPrefs) project.PrefsProfile = GetPrefsProfileName();
	}

	std::string FunkeBuildProfile::GetPrefsProfileName() const {
		std::string name = FunkeProject::SanitizePrefsProfileName(Name);
		return name.empty() ? std::string("Test") : name;
	}

	bool FunkeBuildProfile::Save(const std::string& filePath) const {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("name", Name);
		root.AddMember("platform", std::string(PlatformToString(Platform)));
		root.AddMember("renderBackend", std::string(FunkeProject::RenderBackendToString(RenderBackend)));
		if (!PlatformOptions.empty()) {
			Json::Value options = Json::Value::MakeObject();
			for (const auto& [key, value] : PlatformOptions) options.AddMember(key, value);
			root.AddMember("platformOptions", std::move(options));
		}
		if (Configuration) {
			root.AddMember("configuration", std::string(FunkeProject::BuildProfileToString(*Configuration)));
		}
		if (!Defines.empty()) {
			Json::Value defines = Json::Value::MakeArray();
			for (const std::string& define : Defines) {
				if (!define.empty()) defines.Append(Json::Value(define));
			}
			root.AddMember("defines", std::move(defines));
		}
		if (ShowRuntimeStats) root.AddMember("showRuntimeStats", *ShowRuntimeStats);
		if (ShowRuntimeLogs) root.AddMember("showRuntimeLogs", *ShowRuntimeLogs);
		if (DevConsole) root.AddMember("devConsole", *DevConsole);
		if (IsolatedPrefs) root.AddMember("isolatedPrefs", true);
		root.AddMember("bundleDotNetRuntime", BundleDotNetRuntime);

		std::error_code ec;
		const std::filesystem::path target(filePath);
		if (target.has_parent_path()) {
			std::filesystem::create_directories(target.parent_path(), ec);
		}
		return File::WriteAllText(filePath, Json::Stringify(root, true));
	}

	FunkeBuildProfile FunkeBuildProfile::Load(const std::string& filePath) {
		FunkeBuildProfile profile;
		profile.Name = std::filesystem::path(filePath).stem().string();

		if (!File::Exists(filePath)) {
			return profile;
		}

		Json::Value root;
		std::string parseError;
		const std::string text = File::ReadAllText(filePath);
		if (!Json::TryParse(text, root, &parseError) || !root.IsObject()) {
			FNK_WARN_TAG("Build", "Failed to parse build profile '{}': {} — using defaults.",
				filePath, parseError.empty() ? std::string("not a JSON object") : parseError);
			return profile;
		}

		if (const Json::Value* v = root.FindMember("name")) {
			std::string name = v->AsStringOr();
			if (!name.empty()) profile.Name = std::move(name);
		}
		if (const Json::Value* v = root.FindMember("platform")) {
			profile.Platform = PlatformFromString(v->AsStringOr("Windows"));
		}
		if (const Json::Value* v = root.FindMember("renderBackend")) {
			profile.RenderBackend = FunkeProject::RenderBackendFromString(v->AsStringOr("Auto"));
		}
		if (const Json::Value* v = root.FindMember("platformOptions"); v && v->IsObject()) {
			for (const auto& [key, value] : v->GetObject()) {
				profile.PlatformOptions.emplace_back(key, value.IsString() ? value.AsStringOr() : Json::Stringify(value));
			}
		}
		if (const Json::Value* v = root.FindMember("configuration"); v && v->IsString()) {
			const std::string configuration = v->AsStringOr();
			if (configuration == "Debug" || configuration == "Development" || configuration == "Release") {
				profile.Configuration = FunkeProject::BuildProfileFromString(configuration);
			} else if (!configuration.empty()) {
				FNK_WARN_TAG("Build", "Build profile '{}' has unknown configuration '{}' — using the project's.",
					profile.Name, configuration);
			}
		}
		if (const Json::Value* v = root.FindMember("defines"); v && v->IsArray()) {
			for (const Json::Value& define : v->GetArray()) {
				std::string name = define.AsStringOr();
				if (!name.empty()) profile.Defines.push_back(std::move(name));
			}
		}
		auto readOptionalBool = [&root](const char* key, std::optional<bool>& out) {
			if (const Json::Value* v = root.FindMember(key); v && v->IsBool()) out = v->AsBoolOr(false);
		};
		readOptionalBool("showRuntimeStats", profile.ShowRuntimeStats);
		readOptionalBool("showRuntimeLogs", profile.ShowRuntimeLogs);
		readOptionalBool("devConsole", profile.DevConsole);
		if (const Json::Value* v = root.FindMember("isolatedPrefs"); v && v->IsBool()) {
			profile.IsolatedPrefs = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("bundleDotNetRuntime"); v && v->IsBool()) {
			profile.BundleDotNetRuntime = v->AsBoolOr(true);
		}

		if (!IsBackendAllowed(profile.Platform, profile.RenderBackend)) {
			const auto allowed = AllowedBackends(profile.Platform);
			if (!allowed.empty()) {
				FNK_WARN_TAG("Build",
					"Build profile '{}' has backend '{}' which isn't valid for platform '{}' — coercing to '{}'.",
					profile.Name,
					FunkeProject::RenderBackendToString(profile.RenderBackend),
					PlatformToString(profile.Platform),
					FunkeProject::RenderBackendToString(allowed.front()));
				profile.RenderBackend = allowed.front();
			}
		}
		return profile;
	}

	int FunkeBuildProfile::WriteDefaultProfiles(const std::string& directory) {
		if (directory.empty()) return 0;

		std::error_code ec;
		std::filesystem::create_directories(directory, ec);

		struct DefaultSpec {
			const char* Name;
			BuildPlatform Platform;
			FunkeProject::RenderBackend Backend;
		};
		const DefaultSpec defaults[] = {
			{ "Windows", BuildPlatform::Windows, FunkeProject::RenderBackend::Direct3D12 },
			{ "Linux",   BuildPlatform::Linux,   FunkeProject::RenderBackend::Vulkan     },
			{ "macOS",   BuildPlatform::MacOS,   FunkeProject::RenderBackend::Metal      },
		};

		int written = 0;
		for (const auto& spec : defaults) {
			const std::filesystem::path target =
				std::filesystem::path(directory) /
				(std::string(spec.Name) + std::string(FileExtension));

			// Never clobber an existing file — preserves user edits if this
			// helper is ever invoked over a project that already has profiles.
			if (std::filesystem::exists(target, ec)) continue;

			FunkeBuildProfile profile;
			profile.Name = spec.Name;
			profile.Platform = spec.Platform;
			profile.RenderBackend = spec.Backend;
			if (profile.Save(target.string())) {
				FNK_INFO_TAG("Build", "Seeded default build profile '{}' at '{}'.",
					profile.Name, target.string());
				++written;
			} else {
				FNK_WARN_TAG("Build", "Failed to write default build profile '{}' to '{}'.",
					profile.Name, target.string());
			}
		}
		return written;
	}

} // namespace Funke
