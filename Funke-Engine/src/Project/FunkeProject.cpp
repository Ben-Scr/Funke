#include "pch.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/FunkeProjectInternal.hpp"
#include "Project/EngineSdk.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <mutex>

namespace Funke {
	using namespace ProjectInternal;

	namespace ProjectInternal {
		bool IsValidSceneName(std::string_view value) {
			if (value.empty()) return false;
			if (value.front() == '.') return false;
			if (value.find("..") != std::string_view::npos) return false;
			for (char c : value) {
				if (c == '/' || c == '\\') return false;
				if (c == ':' || c == '\0') return false;
				if (c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
			}
			return true;
		}

		void ReportCreateProgress(const FunkeProject::CreateProgressCallback& callback, float progress, std::string_view stage) {
			if (callback) {
				callback(progress, stage);
			}
		}

		std::filesystem::path ResolveEngineRoot() {
			const auto installation = FunkeProject::GetEngineInstallation();
			return installation.IsValid() ? installation.Root : std::filesystem::path{};
		}

		std::string GetSharedLibraryFilename(const std::string& targetName) {
#if defined(_WIN32)
			return targetName + ".dll";
#elif defined(__APPLE__)
			return "lib" + targetName + ".dylib";
#else
			return "lib" + targetName + ".so";
#endif
		}

		static std::string GetNativeBuildArchitectureDirectory() {
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
			return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
			return "arm64";
#else
			return {};
#endif
		}

		std::string GetNativeBuildPlatformDirectory() {
			const std::string architectureDirectory = GetNativeBuildArchitectureDirectory();
			if (architectureDirectory.empty()) {
				return {};
			}

			// Matches premake's %{cfg.system}-%{cfg.architecture} output folders.
#if defined(FNK_PLATFORM_WINDOWS)
			return "windows-" + architectureDirectory;
#elif defined(FNK_PLATFORM_MACOS)
			return "macosx-" + architectureDirectory;
#elif defined(FNK_PLATFORM_LINUX)
			return "linux-" + architectureDirectory;
#else
			return {};
#endif
		}

		std::string ToLowerAscii(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		void ResolvePaths(FunkeProject& p) {
			p.AssetsDirectory = Path::Combine(p.RootDirectory, "Assets");
			p.ScriptsDirectory = Path::Combine(p.AssetsDirectory, "Scripts");
			p.ScenesDirectory = Path::Combine(p.AssetsDirectory, "Scenes");
			p.FunkeAssetsDirectory = Path::Combine(p.RootDirectory, "FunkeAssets");
			p.NativeScriptsDir = Path::Combine(p.RootDirectory, "NativeScripts");
			p.NativeSourceDir = Path::Combine(p.NativeScriptsDir, "Source");
			p.PackagesDirectory = Path::Combine(p.RootDirectory, "Packages");
			p.CsprojPath = Path::Combine(p.RootDirectory, p.Name + ".csproj");
			p.SlnPath = Path::Combine(p.RootDirectory, p.Name + ".sln");
			p.ProjectFilePath = Path::Combine(p.RootDirectory, "project.json");
		}

	}

	std::string FunkeProject::GetUserAssemblyOutputPath(std::string_view configuration) const {
		const std::string resolvedConfiguration = configuration.empty()
			? GetActiveBuildConfiguration()
			: std::string(configuration);
		return Path::Combine(RootDirectory, "bin", resolvedConfiguration, Name + ".dll");
	}

	std::string FunkeProject::GetActiveBuildConfiguration() {
		return FNK_BUILD_CONFIG_NAME;
	}

	std::string FunkeProject::GetActiveBuildDefineConstant() {
#if defined(FNK_DEBUG)
		return "FNK_DEBUG";
#elif defined(FNK_RELEASE)
		return "FNK_RELEASE";
#elif defined(FNK_DIST)
		return "FNK_DIST";
#else
		return {};
#endif
	}

	std::string FunkeProject::GetManagedPlatformDefine(BuildPlatform platform) {
		switch (platform) {
			case BuildPlatform::Windows:  return "FUNKE_WINDOWS";
			case BuildPlatform::Linux:    return "FUNKE_LINUX";
			case BuildPlatform::MacOS:    return "FUNKE_MACOS";
			case BuildPlatform::Android:  return "FUNKE_ANDROID";
			case BuildPlatform::IOS:      return "FUNKE_IOS";
			case BuildPlatform::VisionOS: return "FUNKE_VISIONOS";
			case BuildPlatform::Web:      return "FUNKE_WEB";
		}
		return {};
	}

	std::string FunkeProject::GetManagedPlatformDefine() {
#if defined(FNK_PLATFORM_WINDOWS)
		return "FUNKE_WINDOWS";
#elif defined(FNK_PLATFORM_MACOS)
		return "FUNKE_MACOS";
#elif defined(FNK_PLATFORM_LINUX)
		return "FUNKE_LINUX";
#elif defined(FNK_PLATFORM_ANDROID)
		return "FUNKE_ANDROID";
#elif defined(FNK_PLATFORM_IOS)
		return "FUNKE_IOS";
#elif defined(FNK_PLATFORM_WEB)
		return "FUNKE_WEB";
#else
		return {};
#endif
	}

	std::string FunkeProject::BuildManagedDefineConstants(std::string_view primarySymbol, bool editorScriptTarget) {
		std::string defineConstants;
		if (!primarySymbol.empty()) {
			defineConstants.assign(primarySymbol);
		}

		const std::string buildDefine = GetActiveBuildDefineConstant();
		if (!buildDefine.empty()) {
			if (!defineConstants.empty()) {
				defineConstants += "%3B";
			}
			defineConstants += buildDefine;
		}

		FunkeProject* project = ProjectManager::GetCurrentProject();
		const std::string profileDefine = (primarySymbol == "FUNKE_EDITOR")
			? std::string("FUNKE_BUILD_DEVELOPMENT")
			: (project ? project->GetActiveBuildProfileDefine() : std::string("FUNKE_BUILD_DEVELOPMENT"));
		if (!profileDefine.empty()) {
			if (!defineConstants.empty()) defineConstants += "%3B";
			defineConstants += profileDefine;
		}
		// The editor compiles for the active build profile's platform, so platform-specific code can be played here.
		const bool simulate = editorScriptTarget && project && primarySymbol == "FUNKE_EDITOR";
		const EditorScriptTarget target = simulate ? project->GetEditorScriptTarget() : EditorScriptTarget{};
		const std::string platformDefine = simulate ? GetManagedPlatformDefine(target.Platform) : GetManagedPlatformDefine();
		if (!platformDefine.empty()) {
			if (!defineConstants.empty()) defineConstants += "%3B";
			defineConstants += platformDefine;
		}
		if (project) {
			std::vector<std::string> customDefines = project->CustomDefines;
			for (const std::string& define : target.ProfileDefines) {
				if (std::find(customDefines.begin(), customDefines.end(), define) == customDefines.end()) {
					customDefines.push_back(define);
				}
			}
			for (const std::string& custom : customDefines) {
				if (custom.empty()) continue;
				if (!defineConstants.empty()) defineConstants += "%3B";
				defineConstants += custom;
			}
		}

		return defineConstants;
	}

	std::string FunkeProject::GetActiveBuildProfileDefine() const {
		switch (ActiveBuildProfile) {
			case BuildProfile::Release:     return "FUNKE_BUILD_RELEASE";
			case BuildProfile::Development: return "FUNKE_BUILD_DEVELOPMENT";
			case BuildProfile::Debug:       return "FUNKE_BUILD_DEBUG";
		}
		return "FUNKE_BUILD_DEVELOPMENT";
	}

	const char* FunkeProject::BuildProfileToString(BuildProfile profile) {
		switch (profile) {
			case BuildProfile::Release:     return "Release";
			case BuildProfile::Development: return "Development";
			case BuildProfile::Debug:       return "Debug";
		}
		return "Development";
	}

	FunkeProject::BuildProfile FunkeProject::BuildProfileFromString(std::string_view value) {
		if (value == "Release") return BuildProfile::Release;
		if (value == "Debug") return BuildProfile::Debug;
		return BuildProfile::Development;
	}

	std::string FunkeProject::GetBuildConfigurationFor(BuildProfile profile) {
		switch (profile) {
			case BuildProfile::Release: return "Dist";
			case BuildProfile::Debug:   return "Debug";
			case BuildProfile::Development: break;
		}
		return GetActiveBuildConfiguration();
	}

	std::string FunkeProject::SanitizePrefsProfileName(std::string_view name) {
		std::string result;
		result.reserve(name.size());
		for (const char c : name) {
			const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				|| c == '-' || c == '_' || c == '.';
			result.push_back(safe ? c : '_');
		}
		while (!result.empty() && (result.front() == '.' || result.back() == '.')) {
			if (result.front() == '.') result.erase(result.begin());
			else result.pop_back();
		}
		return result;
	}

	const char* FunkeProject::RenderBackendToString(RenderBackend backend) {
		switch (backend) {
			case RenderBackend::Auto:       return "Auto";
			case RenderBackend::Vulkan:     return "Vulkan";
			case RenderBackend::Direct3D11: return "Direct3D11";
			case RenderBackend::Direct3D12: return "Direct3D12";
			case RenderBackend::OpenGL:     return "OpenGL";
			case RenderBackend::Metal:      return "Metal";
			case RenderBackend::OpenGLES:   return "OpenGLES";
		}
		return "Auto";
	}

	FunkeProject::RenderBackend FunkeProject::RenderBackendFromString(std::string_view value) {
		// Accept the canonical PascalCase token plus a few common
		// spellings so a hand-edited project file doesn't bite users
		// who type "vulkan" or "d3d11".
		if (value == "Vulkan"     || value == "vulkan"  || value == "VK")    return RenderBackend::Vulkan;
		if (value == "Direct3D11" || value == "DX11"    || value == "d3d11") return RenderBackend::Direct3D11;
		if (value == "Direct3D12" || value == "DX12"    || value == "d3d12") return RenderBackend::Direct3D12;
		if (value == "OpenGL"     || value == "opengl"  || value == "GL")    return RenderBackend::OpenGL;
		if (value == "Metal"      || value == "metal")                        return RenderBackend::Metal;
		if (value == "OpenGLES"   || value == "opengles" || value == "GLES")  return RenderBackend::OpenGLES;
		return RenderBackend::Auto;
	}

	const char* FunkeProject::FullscreenModeToString(FullscreenMode mode) {
		switch (mode) {
			case FullscreenMode::Exclusive:          return "Exclusive";
			case FullscreenMode::BorderlessWindowed: return "BorderlessWindowed";
			case FullscreenMode::Maximized:          return "Maximized";
		}
		return "Exclusive";
	}

	namespace {
		constexpr std::pair<ScreenOrientation, const char*> k_ScreenOrientationNames[] = {
			{ ScreenOrientation::Auto,                "Auto" },
			{ ScreenOrientation::Landscape,           "Landscape" },
			{ ScreenOrientation::LandscapeLeft,       "LandscapeLeft" },
			{ ScreenOrientation::LandscapeRight,      "LandscapeRight" },
			{ ScreenOrientation::Portrait,            "Portrait" },
			{ ScreenOrientation::PortraitUpsideDown,  "PortraitUpsideDown" },
			{ ScreenOrientation::PortraitAny,         "PortraitAny" },
			{ ScreenOrientation::AnyExceptUpsideDown, "AnyExceptUpsideDown" },
			{ ScreenOrientation::Any,                 "Any" },
		};
	}

	const char* FunkeProject::ScreenOrientationToString(ScreenOrientation orientation) {
		for (const auto& [value, name] : k_ScreenOrientationNames) {
			if (value == orientation) return name;
		}
		return "Auto";
	}

	ScreenOrientation FunkeProject::ScreenOrientationFromString(std::string_view value) {
		for (const auto& [orientation, name] : k_ScreenOrientationNames) {
			if (value == name) return orientation;
		}
		return ScreenOrientation::Auto;
	}

	namespace {
		constexpr std::pair<FunkeProject::UIScaleMode, const char*> k_UIScaleModeNames[] = {
			{ FunkeProject::UIScaleMode::MatchWidthOrHeight, "MatchWidthOrHeight" },
			{ FunkeProject::UIScaleMode::Fit,                "Fit" },
			{ FunkeProject::UIScaleMode::Fill,               "Fill" },
		};
	}

	const char* FunkeProject::UIScaleModeToString(UIScaleMode mode) {
		for (const auto& [value, name] : k_UIScaleModeNames) {
			if (value == mode) return name;
		}
		return "MatchWidthOrHeight";
	}

	FunkeProject::UIScaleMode FunkeProject::UIScaleModeFromString(std::string_view value) {
		for (const auto& [mode, name] : k_UIScaleModeNames) {
			if (value == name) return mode;
		}
		return UIScaleMode::MatchWidthOrHeight;
	}

	float FunkeProject::ComputeUIScale(float surfaceWidth, float surfaceHeight) const {
		const float xRatio = surfaceWidth / static_cast<float>(std::max(1, UIReferenceWidth));
		const float yRatio = surfaceHeight / static_cast<float>(std::max(1, UIReferenceHeight));
		if (!(xRatio > 0.0f) || !(yRatio > 0.0f)) return 1.0f;

		float scale = 1.0f;
		switch (UIScaling) {
			case UIScaleMode::Fit:  scale = std::min(xRatio, yRatio); break;
			case UIScaleMode::Fill: scale = std::max(xRatio, yRatio); break;
			case UIScaleMode::MatchWidthOrHeight: {
				// Blend in log space so 0.5 is the geometric mean: doubling width and halving height cancel out.
				const float match = std::clamp(UIScaleMatch, 0.0f, 1.0f);
				scale = std::exp((1.0f - match) * std::log(xRatio) + match * std::log(yRatio));
				break;
			}
		}
		return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
	}

	FullscreenMode FunkeProject::FullscreenModeFromString(std::string_view value) {
		if (value == "BorderlessWindowed" || value == "Borderless" || value == "borderless")
			return FullscreenMode::BorderlessWindowed;
		if (value == "Maximized" || value == "maximized")
			return FullscreenMode::Maximized;
		// Default + "Exclusive" / "exclusive" / unknown.
		return FullscreenMode::Exclusive;
	}

	const char* FunkeProject::ProjectAssetSerializationFormatToString(ProjectAssetSerializationFormat format) {
		switch (format) {
			case ProjectAssetSerializationFormat::Binary: return "Binary";
			case ProjectAssetSerializationFormat::Json:   return "Json";
		}
		return "Binary";
	}

	FunkeProject::ProjectAssetSerializationFormat FunkeProject::ProjectAssetSerializationFormatFromString(std::string_view value) {
		if (value == "Binary" || value == "binary" || value == "bin") {
			return ProjectAssetSerializationFormat::Binary;
		}
		if (value == "Json" || value == "json") {
			return ProjectAssetSerializationFormat::Json;
		}
		return ProjectAssetSerializationFormat::Binary;
	}

	const char* FunkeProject::EditorEntityNameSuffixStyleToString(EditorEntityNameSuffixStyle style) {
		switch (style) {
			case EditorEntityNameSuffixStyle::SpaceNumber:         return "SpaceNumber";
			case EditorEntityNameSuffixStyle::ParenthesizedNumber: return "ParenthesizedNumber";
			case EditorEntityNameSuffixStyle::HyphenNumber:        return "HyphenNumber";
			case EditorEntityNameSuffixStyle::UnderscoreNumber:    return "UnderscoreNumber";
		}
		return "ParenthesizedNumber";
	}

	FunkeProject::EditorEntityNameSuffixStyle FunkeProject::EditorEntityNameSuffixStyleFromString(std::string_view value) {
		if (value == "SpaceNumber" || value == "Entity 1" || value == "space") {
			return EditorEntityNameSuffixStyle::SpaceNumber;
		}
		if (value == "HyphenNumber" || value == "Entity-1" || value == "hyphen") {
			return EditorEntityNameSuffixStyle::HyphenNumber;
		}
		if (value == "UnderscoreNumber" || value == "Entity_1" || value == "underscore") {
			return EditorEntityNameSuffixStyle::UnderscoreNumber;
		}
		return EditorEntityNameSuffixStyle::ParenthesizedNumber;
	}

	std::string FunkeProject::GetBuildProfilesDirectory() const {
		if (RootDirectory.empty()) {
			return {};
		}
		return Path::Combine(RootDirectory, "BuildProfiles");
	}

	FunkeProject::EditorScriptTarget FunkeProject::GetEditorScriptTarget() const {
		const std::optional<BuildPlatform> host = BuildPlatformSupport::HostDesktopPlatform();
		EditorScriptTarget hostTarget;
		hostTarget.Platform = host.value_or(BuildPlatform::Windows);
		if (ActiveBuildProfileName.empty() || RootDirectory.empty()) return hostTarget;

		// The toolbar badge and script bindings ask every frame: re-stat the file twice a second, re-parse on change.
		struct Cache {
			std::mutex Mutex;
			std::string ProfilePath;
			std::filesystem::file_time_type WriteTime{};
			std::chrono::steady_clock::time_point CheckedAt{};
			EditorScriptTarget Target;
		};
		static Cache s_Cache;

		const std::string path = Path::Combine(GetBuildProfilesDirectory(),
			ActiveBuildProfileName + std::string(FunkeBuildProfile::FileExtension));
		const auto now = std::chrono::steady_clock::now();
		std::lock_guard lock(s_Cache.Mutex);
		const bool samePath = s_Cache.ProfilePath == path && s_Cache.CheckedAt != std::chrono::steady_clock::time_point{};
		if (samePath && now - s_Cache.CheckedAt < std::chrono::milliseconds(500)) return s_Cache.Target;

		std::error_code ec;
		const std::filesystem::path filePath = Path::FromUtf8(path);
		const bool exists = std::filesystem::is_regular_file(filePath, ec);
		const std::filesystem::file_time_type writeTime = exists ? std::filesystem::last_write_time(filePath, ec)
			: std::filesystem::file_time_type{};
		s_Cache.CheckedAt = now;
		if (samePath && s_Cache.WriteTime == writeTime) return s_Cache.Target;

		s_Cache.ProfilePath = path;
		s_Cache.WriteTime = writeTime;
		s_Cache.Target = hostTarget;
		if (exists) {
			const FunkeBuildProfile profile = FunkeBuildProfile::Load(path);
			s_Cache.Target.Platform = profile.Platform;
			s_Cache.Target.Simulated = !host || *host != profile.Platform;
			s_Cache.Target.ProfileName = ActiveBuildProfileName;
			s_Cache.Target.ProfileDefines = profile.Defines;
		}
		return s_Cache.Target;
	}

	std::string FunkeProject::GetSceneFilePath(const std::string& sceneName) const {
		// Defense in depth: runtime callers may pass untrusted input; reject traversal chars before path-splicing.
		if (!IsValidSceneName(sceneName)) {
			FNK_CORE_WARN_TAG("FunkeProject",
				"Refusing to resolve scene path for unsafe name '{}'", sceneName);
			return {};
		}
		const std::filesystem::path assetsRoot = Path::FromUtf8(AssetsDirectory);
		const std::string sceneFileName = sceneName + ".scene";
		auto sameSceneFileName = [&sceneFileName](std::string candidate) {
			std::string expected = sceneFileName;
			std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return candidate == expected;
		};
		std::filesystem::path resolvedPath;
		bool scanFailed = false;
		std::error_code ec;
		if (!File::IsDirectory(assetsRoot)) {
			FNK_CORE_ERROR_TAG("FunkeProject", "Scene lookup for '{}' has no readable Assets directory", sceneName);
			return {};
		}
		for (const auto& path : File::ListFiles(assetsRoot, true, ec)) {
			if (sameSceneFileName(Path::ToUtf8(path.filename()))) {
				if (!resolvedPath.empty()) {
					FNK_CORE_ERROR_TAG("FunkeProject",
						"Scene name '{}' is ambiguous: '{}' and '{}' share the same basename",
						sceneName, Path::ToUtf8(resolvedPath), Path::ToUtf8(path));
					return {};
				}
				resolvedPath = path;
			}
		}
		if (ec) scanFailed = true;
		if (scanFailed) {
			FNK_CORE_ERROR_TAG("FunkeProject", "Scene lookup for '{}' could not scan the full Assets tree", sceneName);
			return {};
		}
		if (!resolvedPath.empty()) {
			return Path::ToUtf8(resolvedPath.make_preferred());
		}

		return Path::Combine(ScenesDirectory, sceneFileName);
	}

	std::string FunkeProject::GetDefaultProjectsDir() {
		return Path::Combine(Path::GetSpecialFolderPath(SpecialFolder::User), "Funke", "Projects");
	}

	bool FunkeProject::IsValidProjectName(const std::string& name) {
		if (name.empty() || name.size() > 64) return false;
		if (name.front() == '.' || name.back() == '.' || name.back() == ' ') return false;
		for (char c : name) {
			if (static_cast<unsigned char>(c) < 32) return false;
			if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
				return false;
		}

		std::string deviceName = name.substr(0, name.find('.'));
		std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(), [](unsigned char c) {
			return static_cast<char>(std::toupper(c));
		});
		static constexpr std::array<std::string_view, 4> reservedNames = { "CON", "PRN", "AUX", "NUL" };
		if (std::find(reservedNames.begin(), reservedNames.end(), deviceName) != reservedNames.end()) return false;
		if (deviceName.size() == 4
			&& (deviceName.starts_with("COM") || deviceName.starts_with("LPT"))
			&& deviceName[3] >= '1' && deviceName[3] <= '9') return false;
		return true;
	}

	bool FunkeProject::Validate(const std::string& rootDir) {
		std::error_code error;
		const bool projectFileExists = std::filesystem::exists(
			Path::FromUtf8(Path::Combine(rootDir, "project.json")), error);
		if (error || !projectFileExists) return false;
		const bool assetsExist = std::filesystem::exists(
			Path::FromUtf8(Path::Combine(rootDir, "Assets")), error);
		return !error && assetsExist;
	}

	std::string FunkeProject::MakeApplicationIdentifier(std::string_view company, std::string_view name) {
		// Strictest common subset (Android package names): lowercase alnum/underscore, no leading digit.
		const auto segment = [](std::string_view text, std::string_view fallback) {
			std::string out;
			for (char c : text) {
				const unsigned char u = static_cast<unsigned char>(c);
				if (std::isalnum(u) && u < 0x80) out.push_back(static_cast<char>(std::tolower(u)));
				else if (c == '_') out.push_back('_');
			}
			if (out.empty()) return std::string(fallback);
			if (std::isdigit(static_cast<unsigned char>(out.front()))) out.insert(0, "app");
			return out;
		};
		return "com." + segment(company, "funke") + "." + segment(name, "game");
	}

	std::string FunkeProject::GetApplicationIdentifier() const {
		return MakeApplicationIdentifier(BuildCompany, Name);
	}
}
