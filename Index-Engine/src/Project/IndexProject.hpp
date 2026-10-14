#pragma once
#include "Collections/Vec2.hpp"
#include "Core/Export.hpp"
#include "Core/WindowSpecification.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Graphics/Text/FontHandle.hpp"
#include <cstdint>
#include <string>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace Index {
	enum class BuildPlatform : uint8_t;
}

namespace Index {
	namespace Json { class Value; }
	struct ProjectTemplate;

	struct INDEX_API IndexProject {
		using CreateProgressCallback = std::function<void(float progress, std::string_view stage)>;

		struct GlobalSystemRegistration {
			std::string ClassName;
			bool Active = true;
		};

		// Disabled entries stay listed so the editor remembers their order and the opt-out.
		struct BuildSceneEntry {
			std::string Name;
			bool Enabled = true;
		};

		std::string Name;
		std::string RootDirectory;
		std::string AssetsDirectory;
		std::string ScriptsDirectory;
		std::string ScenesDirectory;
		std::string IndexAssetsDirectory;
		std::string NativeScriptsDir;
		std::string NativeSourceDir;
		std::string PackagesDirectory;
		std::string CsprojPath;
		std::string SlnPath;
		std::string ProjectFilePath;
		std::string EngineVersion;
		static constexpr int k_CurrentProjectFormatVersion = 1;
		int ProjectFormatVersion = k_CurrentProjectFormatVersion;
		std::string PreservedProjectJson;

		// Persistence
		std::string StartupScene = "SampleScene";
		std::string LastOpenedScene = "SampleScene";
		std::string GameViewAspect = "16:9";
		bool GameViewVsync = true;
		// Editor View 3D toggle; unset (never toggled) falls back to PrefersThreeD().
		std::optional<bool> EditorView3D;

		// Build settings
		int BuildWidth = 1920;
		int BuildHeight = 1080;
		// 0 = no limit.
		int BuildMinWidth = 0;
		int BuildMinHeight = 0;
		int BuildMaxWidth = 0;
		int BuildMaxHeight = 0;
		bool BuildFullscreen = true;
		// Persisted as a string label in project.json; unknown labels fall back to Exclusive on load.
		FullscreenMode BuildFullscreenMode = FullscreenMode::Exclusive;
		// Android / iOS: allowed device orientations ("mobileOrientation" in project.json).
		ScreenOrientation MobileOrientation = ScreenOrientation::Auto;
		bool BuildResizable = true;
		bool BuildRunInBackground = true;

		std::string BuildAspect = "Free Aspect";

		// Empty = use project Name. Stored without extension; build step appends the platform extension.
		std::string ExecutableName;

		// Free-form display strings surfaced to scripting via Application.Version / Application.Company.
		std::string BuildVersion = "1.0.0";
		std::string BuildCompany;

		// How the reference resolution maps onto the screen. Fit keeps the whole reference visible on any
		// aspect ratio; Fill covers the screen and may push edges off it.
		enum class UIScaleMode : uint8_t {
			MatchWidthOrHeight = 0,
			Fit,
			Fill,
		};

		// UIScaleMatch (0..1, MatchWidthOrHeight only): 0.0 = width-only, 1.0 = height-only, 0.5 = geometric mean.
		int UIReferenceWidth = 1920;
		int UIReferenceHeight = 1080;
		UIScaleMode UIScaling = UIScaleMode::MatchWidthOrHeight;
		float UIScaleMatch = 0.5f;
		std::string AppIconPath;
		std::vector<BuildSceneEntry> BuildSceneList;
		std::vector<GlobalSystemRegistration> GlobalSystems;

		std::vector<std::string> GetEnabledBuildScenes() const {
			std::vector<std::string> scenes;
			for (const BuildSceneEntry& entry : BuildSceneList) {
				if (entry.Enabled) scenes.push_back(entry.Name);
			}
			return scenes;
		}

		// Empty == legacy "scan everything" mode.
		std::vector<std::string> Packages;

		// Engine feature modules the project uses ("Index.2D", "Index.3D"). The editor offers only these
		// modules' components and a game build ships a runtime compiled without the others.
		// Projects without the field predate modules and keep both.
		static constexpr std::string_view k_Module2D = "Index.2D";
		static constexpr std::string_view k_Module3D = "Index.3D";
		std::vector<std::string> Modules{ std::string(k_Module2D), std::string(k_Module3D) };
		bool HasModule(std::string_view module) const {
			for (const std::string& entry : Modules) {
				if (entry == module) return true;
			}
			return false;
		}
		// UI is core, so a 3D game with a HUD is 3D-only; with both modules the 2D view stays the default.
		bool PrefersThreeD() const { return HasModule(k_Module3D) && !HasModule(k_Module2D); }

		struct ProfilerSettings {
			bool EnableInRuntime = false; // Ctrl+F6 panel in shipped runtime; off by default
			bool TrackInBackground = false; // collect even with the panel closed
			int  SamplingHz = 60;
			int  TrackingSpan = 200;
			std::vector<std::pair<std::string, bool>> ModuleEnabled;
		} Profiler;

		bool ShowRuntimeStats = true;
		bool ShowRuntimeLogs = true;
		GraphicsSettings Graphics{};

		// FixedUpdate frequency; bounds mirror Time::SetFixedDeltaTime's clamp.
		// Applied at startup and each editor play entry; scripts may override it per run.
		static constexpr int k_MinPhysicsHz = 1;
		static constexpr int k_MaxPhysicsHz = 240;
		int PhysicsTickRate = 50;

		// Single source of truth for the Hz -> fixed-timestep-seconds conversion shared by
		// every apply site (startup, play-entry, the settings slider).
		float GetFixedDeltaTimeSeconds() const { return 1.0f / static_cast<float>(PhysicsTickRate); }

		// Reverse-DNS id ("com.<company>.<name>") for the OS: Wayland app_id, bundle / package id.
		std::string GetApplicationIdentifier() const;
		static std::string MakeApplicationIdentifier(std::string_view company, std::string_view name);

		bool AutoRecompileScripts = true;
		bool RecompileScriptsOnPlay = false;

		enum class ProjectAssetSerializationFormat : uint8_t {
			Json = 0,
			Binary = 1,
		};
		ProjectAssetSerializationFormat AssetSerializationFormat = ProjectAssetSerializationFormat::Binary;

		enum class EditorEntityNameSuffixStyle : uint8_t {
			SpaceNumber = 0,       // Entity 1
			ParenthesizedNumber,   // Entity (1)
			HyphenNumber,          // Entity-1
			UnderscoreNumber,      // Entity_1
		};

		bool EditorEnsureUniqueEntityNames = true;
		EditorEntityNameSuffixStyle EditorEntityNameSuffix = EditorEntityNameSuffixStyle::ParenthesizedNumber;
		EditorEntityNameSuffixStyle EditorAssetDuplicateSuffix = EditorEntityNameSuffixStyle::ParenthesizedNumber;

		// Transient migration bag for pre-2026-05 fields. Load() populates these; Save() never re-emits them.
		struct LegacyEditorPrefsMigration {
			bool ShowFileExtensions = false;
			bool ShowFileExtensionsPresent = false;
			bool AutoSaveScenes = false;
			bool AutoSaveScenesPresent = false;
			float AutoSaveIntervalSeconds = 120.0f;
			bool AutoSaveIntervalSecondsPresent = false;
			bool AutoRecompileScripts = true;
			bool AutoRecompileScriptsPresent = false;
			bool RecompileScriptsOnPlay = false;
			bool RecompileScriptsOnPlayPresent = false;
		} LegacyEditorPrefs;

		// Project-relative paths. UIInteractableCursorImagePath is swapped in by UIEventSystem over Interactable elements; empty = use OS default.
		std::string CursorImagePath;
		std::string UIInteractableCursorImagePath;
		// Click point in source-image pixels from the top-left.
		Vec2Int CursorHotspot{ 0, 0 };
		Vec2Int UIInteractableCursorHotspot{ 0, 0 };

		uint64_t DefaultFontAssetId = k_DefaultFontAssetId;

		// Font atlas baking knobs (applied to FontManager on project load).
		// FontBakeStep: 0 = adaptive ladder (fine small / coarse large); N>0 = bake
		// only at multiples of N px (fewer atlases = less memory).
		// FontAtlasBudgetMB: 0 = unlimited; otherwise LRU-evict atlases above this cap.
		int FontBakeStep = 0;
		int FontAtlasBudgetMB = 128;

		enum class BuildProfile : uint8_t {
			Development = 0,
			Release     = 1,
		};
		BuildProfile ActiveBuildProfile = BuildProfile::Development;

		enum class RenderBackend : uint8_t {
			Auto       = 0,
			Vulkan     = 1,
			Direct3D11 = 2,
			Direct3D12 = 3,
			OpenGL     = 4,
			Metal      = 5,
			OpenGLES   = 6,
		};
		RenderBackend ActiveRenderBackend = RenderBackend::Auto;

		std::string ActiveBuildProfileName;

		std::string GetBuildProfilesDirectory() const;

		struct SplashScreenSettings {
			bool Enabled = true;
			float DurationSeconds = 0.7f;
			float FadeInSeconds = 0.5f;
			float FadeOutSeconds = 0.0f;
			std::string ImagePath;
			// Project-relative path. Empty = solid Background{R,G,B} fill.
			std::string BackgroundImagePath;
			std::string CustomText;
			float BackgroundR = 0.05f;
			float BackgroundG = 0.05f;
			float BackgroundB = 0.07f;
			float FontColorR = 1.0f;
			float FontColorG = 1.0f;
			float FontColorB = 1.0f;
			float FontSize = 16.0f;
		} SplashScreen;

		// Baked into both C# <DefineConstants> and native CMakeLists at script compile time. Names only, no =value.
		std::vector<std::string> CustomDefines;

		// Compile-time only (requires engine rebuild). Allowed: 16/20/22/24/28; live-entity cap = (2^N)-1.
		int EntityBits = 20;

		std::string GetUserAssemblyOutputPath(std::string_view configuration = {}) const;

		std::string GetActiveBuildProfileDefine() const;
		static const char* BuildProfileToString(BuildProfile profile);
		static BuildProfile BuildProfileFromString(std::string_view value);

		static const char* RenderBackendToString(RenderBackend backend);
		static RenderBackend RenderBackendFromString(std::string_view value);
		static const char* ProjectAssetSerializationFormatToString(ProjectAssetSerializationFormat format);
		static ProjectAssetSerializationFormat ProjectAssetSerializationFormatFromString(std::string_view value);
		static const char* EditorEntityNameSuffixStyleToString(EditorEntityNameSuffixStyle style);
		static EditorEntityNameSuffixStyle EditorEntityNameSuffixStyleFromString(std::string_view value);

		static const char* FullscreenModeToString(FullscreenMode mode);
		static FullscreenMode FullscreenModeFromString(std::string_view value);
		static const char* ScreenOrientationToString(ScreenOrientation orientation);
		static ScreenOrientation ScreenOrientationFromString(std::string_view value);
		static const char* UIScaleModeToString(UIScaleMode mode);
		static UIScaleMode UIScaleModeFromString(std::string_view value);

		// Factor from reference-resolution UI units to pixels for a UI surface of the given size.
		float ComputeUIScale(float surfaceWidth, float surfaceHeight) const;

		std::string GetNativeDllPath(std::string_view configuration = {}) const;
		std::string GetSceneFilePath(const std::string& sceneName) const;
		void EnsureNativeScriptBootstrapFiles() const;
		void EnsureNativeScriptProjectFiles() const;
		bool HasNativeScriptSources() const;

		// Returns true on a confirmed atomic write. Callers clearing dirty state MUST check the return value.
		bool Save();

		// Writes Packages/IndexDefines.props so VS IntelliSense sees CustomDefines + build profile as active.
		// `outAddedImport` is set true if the .csproj was patched (older projects lack the Import line).
		bool WriteManagedDefinesProps(bool* outAddedImport = nullptr) const;

		static std::string GetActiveBuildConfiguration();
		static std::string GetActiveBuildDefineConstant();
		static std::string GetManagedPlatformDefine();
		// C# define for a build target (INDEX_ANDROID, INDEX_WEB, ...), independent of the editor host.
		static std::string GetManagedPlatformDefine(BuildPlatform platform);
		static std::string BuildManagedDefineConstants(std::string_view primarySymbol);

		// Scene JSON written as the startup scene of a new project without a template: an empty scene with a camera.
		static Json::Value BuildStarterScene(const std::string& sceneName);
		static IndexProject Create(const std::string& name, const std::string& parentDir,
			const CreateProgressCallback& progressCallback = {});
		static IndexProject Create(const std::string& name, const std::string& parentDir,
			const std::string& directoryName, const CreateProgressCallback& progressCallback = {});
		// Null template = the built-in 2D starter; otherwise the template's Assets/ and modules are used, plus its
		// project.json settings, BuildProfiles/, native sources and local packages when the template has them.
		static IndexProject Create(const std::string& name, const std::string& parentDir,
			const std::string& directoryName, const ProjectTemplate* projectTemplate,
			const CreateProgressCallback& progressCallback = {});
		// Deep-copies an existing project to a new name/location. Inherits every
		// setting, rewrites the name-bound files (project.json, .csproj/.sln
		// rename + .sln/.vscode contents), and skips build artifacts
		// (bin/obj/.vs/Builds/NativeScripts/build, *.user) and .git. Throws on failure.
		static IndexProject Duplicate(const std::string& sourceRootDir, const std::string& newName,
			const std::string& parentDir, const std::string& directoryName,
			const CreateProgressCallback& progressCallback = {});
		static bool TryLoad(const std::string& rootDir, IndexProject& outProject,
			std::string* outError = nullptr, bool performMigrations = true);
		static IndexProject Load(const std::string& rootDir);
		static bool Validate(const std::string& rootDir);
		static bool IsValidProjectName(const std::string& name);
		static std::string GetDefaultProjectsDir();

		static std::string GetEngineRootDir();
		static std::string GetPremakePath();

		struct RegenerateResult {
			bool   Succeeded = false;
			int    ExitCode = -1;
			std::string Output;
		};
		static RegenerateResult RegenerateSolutionForProject(const std::string& projectRootDir);

		static std::string GetMSBuildPath();

		// IMPORTANT: builds the whole solution; locked DLLs (e.g. Index-Engine.dll already loaded) cause LNK1104. Use BuildSolutionTargets for package-only builds.
		struct BuildResult {
			bool   Succeeded = false;
			int    ExitCode = -1;
			std::string Output;
		};
		static BuildResult BuildSolution(const std::string& configuration = "Debug",
			const std::string& platform = "x64");

		static BuildResult BuildSolutionTargets(const std::vector<std::string>& targets,
			const std::string& configuration = "Debug",
			const std::string& platform = "x64");
		static RegenerateResult RegeneratePackageWorkspaceForProject(const std::string& projectRootDir);
		static BuildResult BuildProjectPackages(const std::string& projectRootDir,
			const std::string& configuration = "Debug",
			const std::string& platform = "x64");
		static std::string GetProjectPackageOutputDirectory(const std::string& projectRootDir,
			const std::string& packageTarget, const std::string& configuration = "Debug");
		static std::string GetProjectNativePackageLibraryPath(const std::string& projectRootDir,
			const std::string& packageName, const std::string& configuration = "Debug");

		static std::vector<std::string> EnumerateProjectLocalPackages(const std::string& projectRoot);

		struct AutomateResult {
			RegenerateResult Regenerate;
			BuildResult      Build;
			bool             RanBuild = false; // false if regen errored or no MSBuild
		};
		static AutomateResult AutomateForProject(const std::string& projectRootDir,
			const std::string& configuration = "Debug",
			const std::string& platform = "x64");
	};

} // namespace Index
