#include "pch.hpp"
#include "Project/FunkeProject.hpp"
#if FUNKE_WITH_SCRIPTING
#include "Scripting/BoltScriptHost.hpp"
#endif
#include "Project/FunkeProjectInternal.hpp"
#include "Collections/AspectRatio.hpp"
#include "Core/EngineModules.hpp"
#include "Core/Version.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#if FUNKE_WITH_SCRIPTING
#include "Scripting/ScriptEngine.hpp"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Funke {
	using namespace ProjectInternal;

	namespace {
		const char* UpscaleFilterToString(UpscaleFilterMode value) {
			return value == UpscaleFilterMode::Point ? "Point" : "Linear";
		}

		UpscaleFilterMode UpscaleFilterFromString(std::string_view value) {
			return value == "Point" ? UpscaleFilterMode::Point : UpscaleFilterMode::Linear;
		}

		const char* PostProcessingQualityToString(PostProcessingQualityLevel value) {
			switch (value) {
				case PostProcessingQualityLevel::Low: return "Low";
				case PostProcessingQualityLevel::Medium: return "Medium";
				case PostProcessingQualityLevel::High: return "High";
			}
			return "High";
		}

		PostProcessingQualityLevel PostProcessingQualityFromString(std::string_view value) {
			if (value == "Low") return PostProcessingQualityLevel::Low;
			if (value == "Medium") return PostProcessingQualityLevel::Medium;
			return PostProcessingQualityLevel::High;
		}

		const char* ParticleQualityToString(ParticleQualityLevel value) {
			switch (value) {
				case ParticleQualityLevel::Off: return "Off";
				case ParticleQualityLevel::Low: return "Low";
				case ParticleQualityLevel::Medium: return "Medium";
				case ParticleQualityLevel::High: return "High";
			}
			return "High";
		}

		ParticleQualityLevel ParticleQualityFromString(std::string_view value) {
			if (value == "Off") return ParticleQualityLevel::Off;
			if (value == "Low") return ParticleQualityLevel::Low;
			if (value == "Medium") return ParticleQualityLevel::Medium;
			return ParticleQualityLevel::High;
		}

		const char* ShadowQualityToString(ShadowQualityLevel value) {
			switch (value) {
				case ShadowQualityLevel::Off: return "Off";
				case ShadowQualityLevel::Low: return "Low";
				case ShadowQualityLevel::Medium: return "Medium";
				case ShadowQualityLevel::High: return "High";
			}
			return "Medium";
		}

		ShadowQualityLevel ShadowQualityFromString(std::string_view value) {
			if (value == "Off") return ShadowQualityLevel::Off;
			if (value == "Low") return ShadowQualityLevel::Low;
			if (value == "High") return ShadowQualityLevel::High;
			return ShadowQualityLevel::Medium;
		}

		const char* ToneMappingToString(ToneMappingMode value) {
			switch (value) {
				case ToneMappingMode::None: return "None";
				case ToneMappingMode::Reinhard: return "Reinhard";
				case ToneMappingMode::ACES: return "ACES";
			}
			return "ACES";
		}

		ToneMappingMode ToneMappingFromString(std::string_view value) {
			if (value == "None") return ToneMappingMode::None;
			if (value == "Reinhard") return ToneMappingMode::Reinhard;
			return ToneMappingMode::ACES;
		}

		const char* TextureQualityToString(TextureQualityLevel value) {
			switch (value) {
				case TextureQualityLevel::Full: return "Full";
				case TextureQualityLevel::Half: return "Half";
				case TextureQualityLevel::Quarter: return "Quarter";
			}
			return "Full";
		}

		TextureQualityLevel TextureQualityFromString(std::string_view value) {
			if (value == "Half") return TextureQualityLevel::Half;
			if (value == "Quarter") return TextureQualityLevel::Quarter;
			return TextureQualityLevel::Full;
		}

		const char* AnisotropicFilteringToString(AnisotropicFilteringLevel value) {
			switch (value) {
				case AnisotropicFilteringLevel::Off: return "Off";
				case AnisotropicFilteringLevel::X2: return "X2";
				case AnisotropicFilteringLevel::X4: return "X4";
				case AnisotropicFilteringLevel::X8: return "X8";
				case AnisotropicFilteringLevel::X16: return "X16";
			}
			return "X16";
		}

		AnisotropicFilteringLevel AnisotropicFilteringFromString(std::string_view value) {
			if (value == "Off") return AnisotropicFilteringLevel::Off;
			if (value == "X2") return AnisotropicFilteringLevel::X2;
			if (value == "X4") return AnisotropicFilteringLevel::X4;
			if (value == "X8") return AnisotropicFilteringLevel::X8;
			return AnisotropicFilteringLevel::X16;
		}

		const char* AntiAliasingToString(AntiAliasingMode value) {
			switch (value) {
				case AntiAliasingMode::Off: return "Off";
				case AntiAliasingMode::FXAA: return "FXAA";
				case AntiAliasingMode::MSAA4x: return "MSAA4x";
			}
			return "Off";
		}

		AntiAliasingMode AntiAliasingFromString(std::string_view value) {
			if (value == "FXAA") return AntiAliasingMode::FXAA;
			if (value == "MSAA4x") return AntiAliasingMode::MSAA4x;
			return AntiAliasingMode::Off;
		}

		const char* AntiAliasingQualityToString(AntiAliasingQualityLevel value) {
			switch (value) {
				case AntiAliasingQualityLevel::Low: return "Low";
				case AntiAliasingQualityLevel::Medium: return "Medium";
				case AntiAliasingQualityLevel::High: return "High";
			}
			return "High";
		}

		AntiAliasingQualityLevel AntiAliasingQualityFromString(std::string_view value) {
			if (value == "Low") return AntiAliasingQualityLevel::Low;
			if (value == "Medium") return AntiAliasingQualityLevel::Medium;
			return AntiAliasingQualityLevel::High;
		}

		// Reject scene/icon strings that try to escape the project tree or carry
		// shell/OS-control characters. Defense-in-depth: GetSceneFilePath also
		// guards, but rejecting at load means a tampered project.json
		// can't poison BuildSceneList / StartupScene / LastOpenedScene either.
		// Asset-path fields (splash, cursor) may be project-relative SUBpaths, so
		// they can't use IsValidSceneName (which forbids '/'). Lighter guard: reject
		// path traversal + absolute paths so a tampered project.json can't point
		// the engine at files outside the project tree. Empty = "none", allowed.
		bool IsSafeAssetPath(std::string_view value) {
			if (value.empty()) return true;
			if (value.find("..") != std::string_view::npos) return false;
			if (value.front() == '/' || value.front() == '\\') return false;
			if (value.size() >= 2 && value[1] == ':') return false;
			return true;
		}

		uint64_t ParseUInt64String(std::string_view value, uint64_t fallback = 0) {
			if (value.empty()) return fallback;
			try {
				size_t parsed = 0;
				uint64_t result = std::stoull(std::string(value), &parsed, 10);
				return parsed == value.size() ? result : fallback;
			}
			catch (...) {
				return fallback;
			}
		}
	}

	static bool IsKnownProjectMember(std::string_view name) {
		static constexpr std::array<std::string_view, 65> knownMembers = {
			"projectFormatVersion", "name", "engineVersion", "startupScene", "lastOpenedScene",
			"gameViewAspect", "gameViewVsync", "editorView3D", "buildWidth", "buildHeight", "buildMinWidth",
			"buildMinHeight", "buildMaxWidth", "buildMaxHeight", "buildFullscreen",
			"buildFullscreenMode", "buildResizable", "buildRunInBackground", "buildVsync", "buildAspect",
			"mobileOrientation", "uiReferenceWidth", "uiReferenceHeight", "uiScaleMode", "uiScaleMatch", "appIcon", "executableName",
			"productName", "buildVersion", "buildCompany", "splashScreen", "buildScenes", "globalSystems",
			"packages", "profiler", "showRuntimeStats", "showRuntimeLogs", "devConsole", "prefsProfile", "graphics",
			"enablePostProcessing", "physicsTickRate", "autoSaveScenes", "autoSaveIntervalSeconds",
			"autoRecompileScripts", "recompileScriptsOnPlay", "assetSerializationFormat",
			"showFileExtensions", "editorEnsureUniqueEntityNames", "editorEntityNameSuffix",
			"editorAssetDuplicateSuffix", "cursorImagePath", "uiInteractableCursorImagePath",
			"cursorHotspot", "uiInteractableCursorHotspot",
			"buildProfile", "renderBackend", "activeBuildProfile", "defaultFontAsset", "fontBakeStep",
			"fontAtlasBudgetMb", "customDefines", "entityBits", "modules", "threading"
		};
		return std::find(knownMembers.begin(), knownMembers.end(), name) != knownMembers.end();
	}

	static bool IsKnownNestedProjectMember(std::string_view parent, std::string_view name) {
		if (parent == "threading") {
			static constexpr std::array<std::string_view, 7> members = {
				"workerCount", "physics3DWorkerCount", "parallelParticles", "parallelSkeletalPoses",
				"parallelRendering2D", "parallelRendering3D", "parallelPhysics2D"
			};
			return std::find(members.begin(), members.end(), name) != members.end();
		}
		if (parent == "splashScreen") {
			static constexpr std::array<std::string_view, 14> members = {
				"enabled", "durationSeconds", "fadeInSeconds", "fadeOutSeconds", "imagePath",
				"backgroundImagePath", "customText", "backgroundR", "backgroundG", "backgroundB",
				"fontColorR", "fontColorG", "fontColorB", "fontSize"
			};
			return std::find(members.begin(), members.end(), name) != members.end();
		}
		if (parent == "profiler") {
			static constexpr std::array<std::string_view, 5> members = {
				"enableInRuntime", "trackInBackground", "samplingHz", "trackingSpan", "modules"
			};
			return std::find(members.begin(), members.end(), name) != members.end();
		}
		if (parent == "graphics") {
			static constexpr std::array<std::string_view, 17> members = {
				"renderScale", "upscaleFilter", "postProcessingEnabled", "postProcessingQuality",
				"particleQuality", "shadowQuality", "shadowDistance", "maxLights", "exposure", "toneMapping",
				"animationDistanceRate", "animationHalfRateDistance", "animationQuarterRateDistance",
				"textureQuality", "anisotropicFiltering", "antiAliasing", "antiAliasingQuality"
			};
			return std::find(members.begin(), members.end(), name) != members.end();
		}
		return true;
	}

	static void PreserveUnknownProjectMembers(const FunkeProject& project, Json::Value& root) {
		if (project.PreservedProjectJson.empty()) return;

		Json::Value preserved;
		if (!Json::TryParse(project.PreservedProjectJson, preserved) || !preserved.IsObject()) return;
		for (const auto& [name, value] : preserved.GetObject()) {
			if (!IsKnownProjectMember(name) && !root.FindMember(name)) {
				root.AddMember(name, value);
				continue;
			}
			if (!value.IsObject()) continue;

			Json::Value* target = root.FindMember(name);
			Json::Value missingObject = Json::Value::MakeObject();
			Json::Value* destination = target && target->IsObject() ? target : &missingObject;
			for (const auto& [childName, childValue] : value.GetObject()) {
				if (!IsKnownNestedProjectMember(name, childName) && !destination->FindMember(childName)) {
					destination->AddMember(childName, childValue);
				}
			}
			if (!target && !missingObject.GetObject().empty()) {
				root.AddMember(name, std::move(missingObject));
			}
		}
	}

	static bool ReadProjectFormatVersion(const Json::Value& root, int& outVersion, std::string& outError) {
		const Json::Value* version = root.FindMember("projectFormatVersion");
		if (!version) {
			outVersion = 0;
			return true;
		}
		if (!version->IsNumber()) {
			outError = "'projectFormatVersion' must be an integer";
			return false;
		}
		const double numericVersion = version->AsDoubleOr(-1.0);
		const int integerVersion = version->AsIntOr(-1);
		if (numericVersion < 0.0 || numericVersion != static_cast<double>(integerVersion)) {
			outError = "'projectFormatVersion' must be a non-negative integer";
			return false;
		}
		outVersion = integerVersion;
		return true;
	}

	static bool ValidateProjectJsonStructure(const Json::Value& root, int formatVersion, std::string& outError) {
		if (formatVersion >= 2 && !root.FindMember("modules")) {
			outError = "Project format 2 requires an explicit 'modules' array (empty is allowed)";
			return false;
		}
		auto require = [&](std::string_view name, auto predicate, std::string_view expected) {
			const Json::Value* value = root.FindMember(name);
			if (!value || predicate(*value)) return true;
			outError = "'" + std::string(name) + "' must be " + std::string(expected);
			return false;
		};
		auto requireNested = [&](const Json::Value& parent, std::string_view parentName,
			std::string_view name, auto predicate, std::string_view expected) {
			const Json::Value* value = parent.FindMember(name);
			if (!value || predicate(*value)) return true;
			outError = "'" + std::string(parentName) + "." + std::string(name)
				+ "' must be " + std::string(expected);
			return false;
		};

		for (std::string_view name : {
			"name", "engineVersion", "startupScene", "lastOpenedScene", "gameViewAspect",
			"buildFullscreenMode", "mobileOrientation", "uiScaleMode", "buildAspect", "appIcon", "executableName", "productName", "buildVersion",
			"buildCompany", "assetSerializationFormat", "editorEntityNameSuffix",
			"editorAssetDuplicateSuffix", "cursorImagePath", "uiInteractableCursorImagePath",
			"buildProfile", "renderBackend", "activeBuildProfile", "prefsProfile" }) {
			if (!require(name, [](const Json::Value& value) { return value.IsString(); }, "a string")) return false;
		}
		for (std::string_view name : {
			"gameViewVsync", "editorView3D", "buildFullscreen", "buildResizable", "buildRunInBackground", "buildVsync",
			"showRuntimeStats", "showRuntimeLogs", "devConsole", "enablePostProcessing", "autoSaveScenes",
			"autoRecompileScripts", "recompileScriptsOnPlay", "showFileExtensions",
			"editorEnsureUniqueEntityNames" }) {
			if (!require(name, [](const Json::Value& value) { return value.IsBool(); }, "a boolean")) return false;
		}
		for (std::string_view name : {
			"buildWidth", "buildHeight", "buildMinWidth", "buildMinHeight", "buildMaxWidth",
			"buildMaxHeight", "uiReferenceWidth", "uiReferenceHeight", "uiScaleMatch",
			"physicsTickRate", "autoSaveIntervalSeconds", "fontBakeStep", "fontAtlasBudgetMb",
			"entityBits" }) {
			if (!require(name, [](const Json::Value& value) { return value.IsNumber(); }, "a number")) return false;
		}
		for (std::string_view name : { "buildScenes", "globalSystems", "packages", "customDefines", "modules" }) {
			if (!require(name, [](const Json::Value& value) { return value.IsArray(); }, "an array")) return false;
		}
		for (std::string_view name : { "cursorHotspot", "uiInteractableCursorHotspot" }) {
			if (!require(name, [](const Json::Value& value) {
				if (!value.IsArray() || value.GetArray().size() != 2) return false;
				for (const Json::Value& component : value.GetArray()) {
					if (!component.IsNumber()) return false;
				}
				return true;
			}, "an [x, y] number array")) return false;
		}
		for (std::string_view name : { "splashScreen", "profiler", "graphics", "threading" }) {
			if (!require(name, [](const Json::Value& value) { return value.IsObject(); }, "an object")) return false;
		}
		if (const Json::Value* defaultFont = root.FindMember("defaultFontAsset");
			defaultFont && !defaultFont->IsString() && !defaultFont->IsNumber()) {
			outError = "'defaultFontAsset' must be a string or number";
			return false;
		}
		const Json::Value* name = root.FindMember("name");
		if (formatVersion > 0 && (!name || name->AsStringOr().empty())) {
			outError = "versioned projects require a non-empty 'name'";
			return false;
		}
		const Json::Value* engineVersion = root.FindMember("engineVersion");
		if (formatVersion > 0 && (!engineVersion || engineVersion->AsStringOr().empty())) {
			outError = "versioned projects require a non-empty 'engineVersion'";
			return false;
		}

		if (const Json::Value* threading = root.FindMember("threading")) {
			for (std::string_view member : { "workerCount", "physics3DWorkerCount" }) {
				if (!requireNested(*threading, "threading", member, [](const Json::Value& value) {
					const double count = value.AsDoubleOr(0.0);
					return value.IsNumber() && std::isfinite(count) && std::floor(count) == count;
				}, "a finite integer")) return false;
			}
			for (std::string_view member : { "parallelParticles", "parallelSkeletalPoses",
				"parallelRendering2D", "parallelRendering3D", "parallelPhysics2D" }) {
				if (!requireNested(*threading, "threading", member,
					[](const Json::Value& value) { return value.IsBool(); }, "a boolean")) return false;
			}
		}
		if (const Json::Value* splash = root.FindMember("splashScreen")) {
			if (!requireNested(*splash, "splashScreen", "enabled",
				[](const Json::Value& value) { return value.IsBool(); }, "a boolean")) return false;
			for (std::string_view member : {
				"durationSeconds", "fadeInSeconds", "fadeOutSeconds", "backgroundR", "backgroundG",
				"backgroundB", "fontColorR", "fontColorG", "fontColorB", "fontSize" }) {
				if (!requireNested(*splash, "splashScreen", member,
					[](const Json::Value& value) { return value.IsNumber(); }, "a number")) return false;
			}
			for (std::string_view member : { "imagePath", "backgroundImagePath", "customText" }) {
				if (!requireNested(*splash, "splashScreen", member,
					[](const Json::Value& value) { return value.IsString(); }, "a string")) return false;
			}
		}

		if (const Json::Value* profiler = root.FindMember("profiler")) {
			for (std::string_view member : { "enableInRuntime", "trackInBackground" }) {
				if (!requireNested(*profiler, "profiler", member,
					[](const Json::Value& value) { return value.IsBool(); }, "a boolean")) return false;
			}
			for (std::string_view member : { "samplingHz", "trackingSpan" }) {
				if (!requireNested(*profiler, "profiler", member,
					[](const Json::Value& value) { return value.IsNumber(); }, "a number")) return false;
			}
			if (const Json::Value* modules = profiler->FindMember("modules")) {
				if (!modules->IsObject()) {
					outError = "'profiler.modules' must be an object";
					return false;
				}
				for (const auto& [moduleName, enabled] : modules->GetObject()) {
					if (!enabled.IsBool()) {
						outError = "'profiler.modules." + moduleName + "' must be a boolean";
						return false;
					}
				}
			}
		}

		if (const Json::Value* graphics = root.FindMember("graphics")) {
			if (!requireNested(*graphics, "graphics", "renderScale",
				[](const Json::Value& value) { return value.IsNumber(); }, "a number")) return false;
			if (!requireNested(*graphics, "graphics", "postProcessingEnabled",
				[](const Json::Value& value) { return value.IsBool(); }, "a boolean")) return false;
			for (std::string_view member : { "upscaleFilter", "postProcessingQuality", "particleQuality", "shadowQuality", "toneMapping",
				"textureQuality", "anisotropicFiltering", "antiAliasing", "antiAliasingQuality" }) {
				if (!requireNested(*graphics, "graphics", member,
					[](const Json::Value& value) { return value.IsString(); }, "a string")) return false;
			}
			for (std::string_view member : { "shadowDistance", "maxLights", "exposure", "animationHalfRateDistance", "animationQuarterRateDistance" }) {
				if (!requireNested(*graphics, "graphics", member,
					[](const Json::Value& value) { return value.IsNumber(); }, "a number")) return false;
			}
			if (!requireNested(*graphics, "graphics", "animationDistanceRate",
				[](const Json::Value& value) { return value.IsBool(); }, "a boolean")) return false;
		}

		if (const Json::Value* scenes = root.FindMember("buildScenes")) {
			for (const Json::Value& scene : scenes->GetArray()) {
				if (scene.IsString()) continue;
				if (!scene.IsObject()) {
					outError = "'buildScenes' entries must be strings or objects";
					return false;
				}
				const Json::Value* sceneName = scene.FindMember("name");
				const Json::Value* enabled = scene.FindMember("enabled");
				if (!sceneName || !sceneName->IsString() || (enabled && !enabled->IsBool())) {
					outError = "'buildScenes' objects require a string name and optional boolean enabled";
					return false;
				}
			}
		}

		for (std::string_view arrayName : { "packages", "customDefines", "modules" }) {
			if (const Json::Value* values = root.FindMember(arrayName)) {
				for (const Json::Value& value : values->GetArray()) {
					if (!value.IsString()) {
						outError = "'" + std::string(arrayName) + "' entries must be strings";
						return false;
					}
					if (arrayName == "modules" && !EngineModules::FromId(value.AsStringOr())) {
						outError = EngineModules::GetStatusMessage(value.AsStringOr(), EngineModules::Status::Unknown);
						return false;
					}
				}
			}
		}

		if (const Json::Value* systems = root.FindMember("globalSystems")) {
			for (const Json::Value& system : systems->GetArray()) {
				if (system.IsString()) continue;
				if (!system.IsObject()) {
					outError = "'globalSystems' entries must be strings or objects";
					return false;
				}
				const Json::Value* className = system.FindMember("className");
				const Json::Value* active = system.FindMember("active");
				if (!className || !className->IsString() || (active && !active->IsBool())) {
					outError = "'globalSystems' objects require a string className and optional boolean active";
					return false;
				}
			}
		}
		return true;
	}

	static std::filesystem::path GetMigrationBackupPath(
		const std::filesystem::path& projectFile,
		int sourceVersion) {
		const std::string suffix = ".pre-v" + std::to_string(sourceVersion);
		std::filesystem::path candidate = projectFile;
		candidate += suffix + ".bak";
		for (int index = 1; index < 10000; ++index) {
			std::error_code ec;
			const bool exists = std::filesystem::exists(candidate, ec);
			if (ec) return {};
			if (!exists) return candidate;
			candidate = projectFile;
			candidate += suffix + "." + std::to_string(index) + ".bak";
		}
		return {};
	}

	Json::Value ProjectInternal::BuildProjectJson(const FunkeProject& project) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("projectFormatVersion", FunkeProject::k_CurrentProjectFormatVersion);
		root.AddMember("name", project.Name);
		root.AddMember("engineVersion", project.EngineVersion);
		root.AddMember("startupScene", project.StartupScene);
		root.AddMember("lastOpenedScene", project.LastOpenedScene);
		root.AddMember("gameViewAspect", project.GameViewAspect);
		root.AddMember("gameViewVsync", project.GameViewVsync);
		if (project.EditorView3D) root.AddMember("editorView3D", *project.EditorView3D);
		root.AddMember("buildWidth", project.BuildWidth);
		root.AddMember("buildHeight", project.BuildHeight);
		root.AddMember("buildMinWidth", project.BuildMinWidth);
		root.AddMember("buildMinHeight", project.BuildMinHeight);
		root.AddMember("buildMaxWidth", project.BuildMaxWidth);
		root.AddMember("buildMaxHeight", project.BuildMaxHeight);
		root.AddMember("buildFullscreen", project.BuildFullscreen);
		root.AddMember("buildFullscreenMode",
			std::string(FunkeProject::FullscreenModeToString(project.BuildFullscreenMode)));
		root.AddMember("mobileOrientation",
			std::string(FunkeProject::ScreenOrientationToString(project.MobileOrientation)));
		root.AddMember("buildResizable", project.BuildResizable);
		root.AddMember("buildRunInBackground", project.BuildRunInBackground);
		root.AddMember("buildVsync", project.BuildVsync);
		root.AddMember("buildAspect", project.BuildAspect);
		root.AddMember("uiReferenceWidth", project.UIReferenceWidth);
		root.AddMember("uiReferenceHeight", project.UIReferenceHeight);
		root.AddMember("uiScaleMode", std::string(FunkeProject::UIScaleModeToString(project.UIScaling)));
		root.AddMember("uiScaleMatch", project.UIScaleMatch);
		if (!project.AppIconPath.empty()) {
			root.AddMember("appIcon", project.AppIconPath);
		}
		if (!project.ExecutableName.empty()) {
			root.AddMember("executableName", project.ExecutableName);
		}
		if (!project.ProductName.empty()) {
			root.AddMember("productName", project.ProductName);
		}
		root.AddMember("buildVersion", project.BuildVersion);
		root.AddMember("buildCompany", project.BuildCompany);

		{
			Json::Value splash = Json::Value::MakeObject();
			splash.AddMember("enabled", project.SplashScreen.Enabled);
			splash.AddMember("durationSeconds", static_cast<double>(project.SplashScreen.DurationSeconds));
			splash.AddMember("fadeInSeconds", static_cast<double>(project.SplashScreen.FadeInSeconds));
			splash.AddMember("fadeOutSeconds", static_cast<double>(project.SplashScreen.FadeOutSeconds));
			if (!project.SplashScreen.ImagePath.empty()) {
				splash.AddMember("imagePath", project.SplashScreen.ImagePath);
			}
			if (!project.SplashScreen.BackgroundImagePath.empty()) {
				splash.AddMember("backgroundImagePath", project.SplashScreen.BackgroundImagePath);
			}
			if (!project.SplashScreen.CustomText.empty()) {
				splash.AddMember("customText", project.SplashScreen.CustomText);
			}
			splash.AddMember("backgroundR", static_cast<double>(project.SplashScreen.BackgroundR));
			splash.AddMember("backgroundG", static_cast<double>(project.SplashScreen.BackgroundG));
			splash.AddMember("backgroundB", static_cast<double>(project.SplashScreen.BackgroundB));
			splash.AddMember("fontColorR", static_cast<double>(project.SplashScreen.FontColorR));
			splash.AddMember("fontColorG", static_cast<double>(project.SplashScreen.FontColorG));
			splash.AddMember("fontColorB", static_cast<double>(project.SplashScreen.FontColorB));
			splash.AddMember("fontSize", static_cast<double>(project.SplashScreen.FontSize));
			root.AddMember("splashScreen", std::move(splash));
		}

		Json::Value buildScenes = Json::Value::MakeArray();
		for (const FunkeProject::BuildSceneEntry& entry : project.BuildSceneList) {
			if (entry.Name.empty()) continue;
			// Enabled entries stay plain strings so older readers keep working.
			if (entry.Enabled) {
				buildScenes.Append(entry.Name);
				continue;
			}
			Json::Value sceneValue = Json::Value::MakeObject();
			sceneValue.AddMember("name", entry.Name);
			sceneValue.AddMember("enabled", false);
			buildScenes.Append(std::move(sceneValue));
		}
		root.AddMember("buildScenes", std::move(buildScenes));

		Json::Value globalSystems = Json::Value::MakeArray();
		for (const auto& registration : project.GlobalSystems) {
			if (registration.ClassName.empty()) {
				continue;
			}

			Json::Value systemValue = Json::Value::MakeObject();
			std::string className = registration.ClassName;
#if FUNKE_WITH_SCRIPTING
			const std::string canonical = ScriptEngine::ResolveManagedTypeName(registration.ClassName);
			const bool collision = std::any_of(project.GlobalSystems.begin(), project.GlobalSystems.end(),
				[&](const auto& other) { return &other != &registration && other.ClassName == canonical; });
			if (!canonical.empty() && !collision) className = canonical;
#endif
			systemValue.AddMember("className", className);
			systemValue.AddMember("active", registration.Active);
			globalSystems.Append(std::move(systemValue));
		}
		root.AddMember("globalSystems", std::move(globalSystems));

		// An absent package list also disables optional packages.
		if (!project.Packages.empty()) {
			Json::Value packages = Json::Value::MakeArray();
			for (const std::string& name : project.Packages) {
				if (!name.empty()) {
					packages.Append(name);
				}
			}
			root.AddMember("packages", std::move(packages));
		}

		{
			Json::Value modules = Json::Value::MakeArray();
			for (const std::string& module : project.Modules) {
				modules.Append(module);
			}
			root.AddMember("modules", std::move(modules));
		}

		// Only written when non-default to keep virgin project files clean.
		const auto& prof = project.Profiler;
		const bool profilerNonDefault =
			prof.EnableInRuntime ||
			prof.TrackInBackground ||
			prof.SamplingHz != 60 ||
			prof.TrackingSpan != 200 ||
			!prof.ModuleEnabled.empty();
		if (profilerNonDefault) {
			Json::Value profilerJson = Json::Value::MakeObject();
			profilerJson.AddMember("enableInRuntime", prof.EnableInRuntime);
			profilerJson.AddMember("trackInBackground", prof.TrackInBackground);
			profilerJson.AddMember("samplingHz", prof.SamplingHz);
			profilerJson.AddMember("trackingSpan", prof.TrackingSpan);
			if (!prof.ModuleEnabled.empty()) {
				Json::Value modulesJson = Json::Value::MakeObject();
				for (const auto& [name, enabled] : prof.ModuleEnabled) {
					modulesJson.AddMember(name, enabled);
				}
				profilerJson.AddMember("modules", std::move(modulesJson));
			}
			root.AddMember("profiler", std::move(profilerJson));
		}

		if (project.ShowRuntimeStats) {
			root.AddMember("showRuntimeStats", *project.ShowRuntimeStats);
		}
		if (project.ShowRuntimeLogs) {
			root.AddMember("showRuntimeLogs", *project.ShowRuntimeLogs);
		}
		if (project.DevConsole) {
			root.AddMember("devConsole", *project.DevConsole);
		}
		if (!project.PrefsProfile.empty()) {
			root.AddMember("prefsProfile", project.PrefsProfile);
		}
		{
			ThreadingSettings settings = project.Threading;
			settings.Clamp();
			Json::Value threading = Json::Value::MakeObject();
			threading.AddMember("workerCount", settings.WorkerCount);
			threading.AddMember("physics3DWorkerCount", settings.Physics3DWorkerCount);
			threading.AddMember("parallelParticles", settings.ParallelParticles);
			threading.AddMember("parallelSkeletalPoses", settings.ParallelSkeletalPoses);
			threading.AddMember("parallelRendering2D", settings.ParallelRendering2D);
			threading.AddMember("parallelRendering3D", settings.ParallelRendering3D);
			threading.AddMember("parallelPhysics2D", settings.ParallelPhysics2D);
			root.AddMember("threading", std::move(threading));
		}
		if (project.Graphics != GraphicsSettings{}) {
			Json::Value graphics = Json::Value::MakeObject();
			graphics.AddMember("renderScale", project.Graphics.RenderScale);
			graphics.AddMember("upscaleFilter", std::string(UpscaleFilterToString(project.Graphics.UpscaleFilter)));
			graphics.AddMember("postProcessingEnabled", project.Graphics.PostProcessingEnabled);
			graphics.AddMember("postProcessingQuality", std::string(PostProcessingQualityToString(project.Graphics.PostProcessingQuality)));
			graphics.AddMember("particleQuality", std::string(ParticleQualityToString(project.Graphics.ParticleQuality)));
			graphics.AddMember("shadowQuality", std::string(ShadowQualityToString(project.Graphics.ShadowQuality)));
			graphics.AddMember("shadowDistance", project.Graphics.ShadowDistance);
			graphics.AddMember("maxLights", project.Graphics.MaxLights);
			graphics.AddMember("exposure", project.Graphics.Exposure);
			graphics.AddMember("toneMapping", std::string(ToneMappingToString(project.Graphics.ToneMapping)));
			graphics.AddMember("animationDistanceRate", project.Graphics.AnimationDistanceRateEnabled);
			graphics.AddMember("animationHalfRateDistance", project.Graphics.AnimationHalfRateDistance);
			graphics.AddMember("animationQuarterRateDistance", project.Graphics.AnimationQuarterRateDistance);
			graphics.AddMember("textureQuality", std::string(TextureQualityToString(project.Graphics.TextureQuality)));
			graphics.AddMember("anisotropicFiltering", std::string(AnisotropicFilteringToString(project.Graphics.AnisotropicFiltering)));
			graphics.AddMember("antiAliasing", std::string(AntiAliasingToString(project.Graphics.AntiAliasing)));
			graphics.AddMember("antiAliasingQuality", std::string(AntiAliasingQualityToString(project.Graphics.AntiAliasingQuality)));
			root.AddMember("graphics", std::move(graphics));
		}
		{
			Json::Value layers = Json::Value::MakeObject();
			Json::Value names = Json::Value::MakeArray();
			Json::Value matrix = Json::Value::MakeArray();
			for (uint32_t i = 0; i < PhysicsLayerSettings::Count; ++i) {
				names.Append(i == 0 ? std::string(PhysicsLayerSettings::DefaultNames[0]) : project.PhysicsLayers.Names[i]);
				matrix.Append(project.PhysicsLayers.CollisionMasks[i]);
			}
			layers.AddMember("names", std::move(names));
			layers.AddMember("collisionMasks", std::move(matrix));
			layers.AddMember("visibleCount", static_cast<int>(project.PhysicsLayers.VisibleCount));
			root.AddMember("physicsLayers", std::move(layers));
		}
		{
			Json::Value gravity2D = Json::Value::MakeArray();
			gravity2D.Append(project.PhysicsGravity2D.x);
			gravity2D.Append(project.PhysicsGravity2D.y);
			root.AddMember("physicsGravity2D", std::move(gravity2D));
			Json::Value gravity3D = Json::Value::MakeArray();
			gravity3D.Append(project.PhysicsGravity3D.x);
			gravity3D.Append(project.PhysicsGravity3D.y);
			gravity3D.Append(project.PhysicsGravity3D.z);
			root.AddMember("physicsGravity3D", std::move(gravity3D));
		}
		if (project.PhysicsTickRate != 50) {
			root.AddMember("physicsTickRate", project.PhysicsTickRate);
		}

		// autoSaveScenes/autoSaveIntervalSeconds/showFileExtensions moved to EditorPreferences (2026-05).
		root.AddMember("assetSerializationFormat",
			std::string(FunkeProject::ProjectAssetSerializationFormatToString(project.AssetSerializationFormat)));

		if (!project.EditorEnsureUniqueEntityNames) {
			root.AddMember("editorEnsureUniqueEntityNames", false);
		}
		if (project.EditorEntityNameSuffix != FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber) {
			root.AddMember("editorEntityNameSuffix",
				std::string(FunkeProject::EditorEntityNameSuffixStyleToString(project.EditorEntityNameSuffix)));
		}
		if (project.EditorAssetDuplicateSuffix != FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber) {
			root.AddMember("editorAssetDuplicateSuffix",
				std::string(FunkeProject::EditorEntityNameSuffixStyleToString(project.EditorAssetDuplicateSuffix)));
		}

		if (!project.CursorImagePath.empty()) {
			root.AddMember("cursorImagePath", project.CursorImagePath);
		}
		if (!project.UIInteractableCursorImagePath.empty()) {
			root.AddMember("uiInteractableCursorImagePath",
				project.UIInteractableCursorImagePath);
		}
		auto writeHotspot = [&root](const char* name, Vec2Int hotspot) {
			if (hotspot == Vec2Int(0, 0)) return;
			Json::Value value = Json::Value::MakeArray();
			value.Append(Json::Value(hotspot.x));
			value.Append(Json::Value(hotspot.y));
			root.AddMember(name, std::move(value));
		};
		writeHotspot("cursorHotspot", project.CursorHotspot);
		writeHotspot("uiInteractableCursorHotspot", project.UIInteractableCursorHotspot);

		if (project.ActiveBuildProfile != FunkeProject::BuildProfile::Development) {
			root.AddMember("buildProfile",
				std::string(FunkeProject::BuildProfileToString(project.ActiveBuildProfile)));
		}
		if (project.ActiveRenderBackend != FunkeProject::RenderBackend::Auto) {
			root.AddMember("renderBackend",
				std::string(FunkeProject::RenderBackendToString(project.ActiveRenderBackend)));
		}
		if (!project.ActiveBuildProfileName.empty()) {
			root.AddMember("activeBuildProfile", project.ActiveBuildProfileName);
		}
		if (project.DefaultFontAssetId != k_DefaultFontAssetId) {
			root.AddMember("defaultFontAsset",
				std::to_string(project.DefaultFontAssetId));
		}
		if (!project.FontFallbackAssets.empty()) {
            Json::Value fallbacks = Json::Value::MakeArray();
            for (const auto id : project.FontFallbackAssets) if (id) fallbacks.Append(std::to_string(id));
            root.AddMember("fontFallbackAssets", std::move(fallbacks));
        }
        if (project.FontBakeStep != 0) {
			root.AddMember("fontBakeStep", project.FontBakeStep);
		}
		if (project.FontAtlasBudgetMB != 128) {
			root.AddMember("fontAtlasBudgetMb", project.FontAtlasBudgetMB);
		}
		if (!project.CustomDefines.empty()) {
			Json::Value definesJson = Json::Value::MakeArray();
			for (const std::string& d : project.CustomDefines) {
				if (!d.empty()) definesJson.Append(Json::Value(d));
			}
			root.AddMember("customDefines", std::move(definesJson));
		}
		if (project.EntityBits != 20) {
			root.AddMember("entityBits", project.EntityBits);
		}

		PreserveUnknownProjectMembers(project, root);
		return root;
	}

	bool FunkeProject::Save() {
		if (ProjectFormatVersion != k_CurrentProjectFormatVersion || ProjectFilePath.empty()) {
			FNK_CORE_ERROR_TAG("FunkeProject", "Refusing to save unsupported project format version {}", ProjectFormatVersion);
			return false;
		}

		FunkeProject projectForWrite = *this;
		projectForWrite.EngineVersion = FNK_VERSION;
		if (File::Exists(ProjectFilePath)) {
			const std::string currentText = File::ReadAllText(ProjectFilePath);
			Json::Value currentRoot;
			std::string parseError;
			int diskVersion = 0;
			if (currentText.empty()
				|| !Json::TryParse(currentText, currentRoot, &parseError)
				|| !currentRoot.IsObject()
				|| !ReadProjectFormatVersion(currentRoot, diskVersion, parseError)) {
				FNK_CORE_ERROR_TAG("FunkeProject", "Refusing to overwrite unreadable project file '{}': {}",
					ProjectFilePath, parseError.empty() ? "empty or non-object JSON" : parseError);
				return false;
			}
			if (diskVersion > k_CurrentProjectFormatVersion) {
				FNK_CORE_ERROR_TAG("FunkeProject",
					"Refusing to overwrite project format {} with older supported format {}",
					diskVersion, k_CurrentProjectFormatVersion);
				return false;
			}
			if (!ValidateProjectJsonStructure(currentRoot, diskVersion, parseError)) {
				FNK_CORE_ERROR_TAG("FunkeProject", "Refusing to overwrite invalid project file '{}': {}",
					ProjectFilePath, parseError);
				return false;
			}
			projectForWrite.PreservedProjectJson = currentText;
		}

		Json::Value projectJson = BuildProjectJson(projectForWrite);
		std::string validationError;
		if (!ValidateProjectJsonStructure(projectJson, k_CurrentProjectFormatVersion, validationError)) {
			FNK_CORE_ERROR_TAG("FunkeProject", "Refusing to save invalid project data: {}", validationError);
			return false;
		}
		const std::string serializedProject = Json::Stringify(projectJson, true);
		if (!File::WriteAllText(ProjectFilePath, serializedProject)) {
			return false;
		}
		EngineVersion = FNK_VERSION;
		PreservedProjectJson = serializedProject;
		(void)WriteManagedDefinesProps();
		return true;
	}

	static bool TryLoadProjectImpl(
		const std::string& rootDir,
		FunkeProject& outProject,
		std::string* outError,
		bool performMigrations) {
		auto fail = [&](std::string message) {
			if (outError) *outError = std::move(message);
			return false;
		};

		FunkeProject project;
		project.RootDirectory = rootDir;

		std::string configPath = Path::Combine(rootDir, "project.json");
		if (!File::Exists(configPath)) {
			return fail("Project file not found: " + configPath);
		}
		const std::filesystem::path assetsPath = Path::FromUtf8(rootDir) / "Assets";
		if (!File::IsDirectory(assetsPath)) {
			return fail("Project Assets directory not found: " + Path::ToUtf8(assetsPath));
		}

		const std::string sourceJson = File::ReadAllText(configPath);
		if (sourceJson.empty()) {
			return fail("Project file is empty or unreadable: " + configPath);
		}

		Json::Value validatedRoot;
		std::string validationError;
		if (!Json::TryParse(sourceJson, validatedRoot, &validationError)) {
			return fail("Failed to parse '" + configPath + "': " + validationError);
		}
		if (!validatedRoot.IsObject()) {
			return fail("Project file root must be a JSON object: " + configPath);
		}

		int sourceFormatVersion = 0;
		if (!ReadProjectFormatVersion(validatedRoot, sourceFormatVersion, validationError)) {
			return fail("Invalid project format in '" + configPath + "': " + validationError);
		}
		if (sourceFormatVersion > FunkeProject::k_CurrentProjectFormatVersion) {
			return fail("Project format " + std::to_string(sourceFormatVersion)
				+ " is newer than this Editor supports (" + std::to_string(FunkeProject::k_CurrentProjectFormatVersion) + ")");
		}
		if (!ValidateProjectJsonStructure(validatedRoot, sourceFormatVersion, validationError)) {
			return fail("Invalid project data in '" + configPath + "': " + validationError);
		}
		project.ProjectFormatVersion = sourceFormatVersion;
		project.PreservedProjectJson = sourceJson;

		{
			const std::string& jsonText = sourceJson;
			Json::Value root;
			std::string parseError;
			if (!jsonText.empty() && Json::TryParse(jsonText, root, &parseError) && root.IsObject()) {
				if (const Json::Value* nameValue = root.FindMember("name")) {
					project.Name = nameValue->AsStringOr();
				}
				if (const Json::Value* versionValue = root.FindMember("engineVersion")) {
					project.EngineVersion = versionValue->AsStringOr();
				}
				if (const Json::Value* startupValue = root.FindMember("startupScene")) {
					const std::string startupScene = startupValue->AsStringOr();
					if (!startupScene.empty()) {
						if (IsValidSceneName(startupScene)) {
							project.StartupScene = startupScene;
						}
						else {
							FNK_CORE_WARN_TAG("FunkeProject",
								"Project '{}': ignoring invalid startupScene '{}'", configPath, startupScene);
						}
					}
				}
				if (const Json::Value* lastSceneValue = root.FindMember("lastOpenedScene")) {
					const std::string lastScene = lastSceneValue->AsStringOr();
					if (!lastScene.empty()) {
						if (IsValidSceneName(lastScene)) {
							project.LastOpenedScene = lastScene;
						}
						else {
							FNK_CORE_WARN_TAG("FunkeProject",
								"Project '{}': ignoring invalid lastOpenedScene '{}'", configPath, lastScene);
						}
					}
				}
				if (const Json::Value* gameViewAspectValue = root.FindMember("gameViewAspect")) {
					const std::string gameViewAspect = gameViewAspectValue->AsStringOr();
					if (!gameViewAspect.empty()) {
						project.GameViewAspect = gameViewAspect;
					}
				}
				if (const Json::Value* gameViewVsyncValue = root.FindMember("gameViewVsync")) {
					project.GameViewVsync = gameViewVsyncValue->AsBoolOr(true);
				}
				if (const Json::Value* editorView3DValue = root.FindMember("editorView3D")) {
					if (editorView3DValue->IsBool()) project.EditorView3D = editorView3DValue->AsBoolOr(false);
				}
				if (const Json::Value* buildWidthValue = root.FindMember("buildWidth")) {
					project.BuildWidth = buildWidthValue->AsIntOr(1920);
				}
				if (const Json::Value* buildHeightValue = root.FindMember("buildHeight")) {
					project.BuildHeight = buildHeightValue->AsIntOr(1080);
				}
				if (const Json::Value* fullscreenValue = root.FindMember("buildFullscreen")) {
					project.BuildFullscreen = fullscreenValue->AsBoolOr(true);
				}
				if (const Json::Value* v = root.FindMember("buildFullscreenMode")) {
					project.BuildFullscreenMode =
						FunkeProject::FullscreenModeFromString(v->AsStringOr("Exclusive"));
				}
				if (const Json::Value* v = root.FindMember("mobileOrientation")) {
					project.MobileOrientation = FunkeProject::ScreenOrientationFromString(v->AsStringOr("Auto"));
				}
				if (const Json::Value* resizableValue = root.FindMember("buildResizable")) {
					project.BuildResizable = resizableValue->AsBoolOr(true);
				}
				if (const Json::Value* v = root.FindMember("buildMinWidth")) {
					project.BuildMinWidth = std::max(0, v->AsIntOr(0));
				}
				if (const Json::Value* v = root.FindMember("buildMinHeight")) {
					project.BuildMinHeight = std::max(0, v->AsIntOr(0));
				}
				if (const Json::Value* v = root.FindMember("buildMaxWidth")) {
					project.BuildMaxWidth = std::max(0, v->AsIntOr(0));
				}
				if (const Json::Value* v = root.FindMember("buildMaxHeight")) {
					project.BuildMaxHeight = std::max(0, v->AsIntOr(0));
				}
				if (const Json::Value* runInBackgroundValue = root.FindMember("buildRunInBackground")) {
					project.BuildRunInBackground = runInBackgroundValue->AsBoolOr(true);
				}
				if (const Json::Value* vsyncValue = root.FindMember("buildVsync")) {
					project.BuildVsync = vsyncValue->AsBoolOr(true);
				}
				if (const Json::Value* buildAspectValue = root.FindMember("buildAspect")) {
					std::string raw = buildAspectValue->AsStringOr("Free Aspect");
					bool matched = false;
					for (const auto& preset : k_AspectRatioPresets) {
						if (raw == preset.Label) { matched = true; break; }
					}
					if (!matched) {
						FNK_CORE_WARN_TAG("Project",
							"Unknown buildAspect '{}' — falling back to 'Free Aspect'", raw);
						raw = "Free Aspect";
					}
					project.BuildAspect = std::move(raw);
				}
				if (const Json::Value* uiRefWidthValue = root.FindMember("uiReferenceWidth")) {
					project.UIReferenceWidth = std::max(1, uiRefWidthValue->AsIntOr(project.UIReferenceWidth));
				}
				if (const Json::Value* uiRefHeightValue = root.FindMember("uiReferenceHeight")) {
					project.UIReferenceHeight = std::max(1, uiRefHeightValue->AsIntOr(project.UIReferenceHeight));
				}
				if (const Json::Value* v = root.FindMember("uiScaleMode")) {
					project.UIScaling = FunkeProject::UIScaleModeFromString(v->AsStringOr("MatchWidthOrHeight"));
				}
				if (const Json::Value* uiMatchValue = root.FindMember("uiScaleMatch")) {
					const float raw = static_cast<float>(uiMatchValue->AsDoubleOr(project.UIScaleMatch));
					project.UIScaleMatch = std::clamp(raw, 0.0f, 1.0f);
				}
				if (const Json::Value* iconValue = root.FindMember("appIcon")) {
					const std::string iconPath = iconValue->AsStringOr();
					if (iconPath.empty() || IsSafeAssetPath(iconPath)) {
						project.AppIconPath = iconPath;
					}
					else {
						FNK_CORE_WARN_TAG("FunkeProject",
							"Project '{}': ignoring invalid appIcon '{}'", configPath, iconPath);
					}
				}
				if (const Json::Value* exeNameValue = root.FindMember("executableName")) {
					{ const std::string exeName = exeNameValue->AsStringOr(); if (exeName.empty() || IsValidSceneName(exeName)) project.ExecutableName = exeName; else FNK_CORE_WARN_TAG("FunkeProject", "Project '{}': ignoring invalid executableName '{}' (no path separators or traversal allowed)", configPath, exeName); }
				}
				if (const Json::Value* v = root.FindMember("productName")) {
					project.ProductName = v->AsStringOr(project.ProductName);
				}
				if (const Json::Value* v = root.FindMember("buildVersion")) {
					project.BuildVersion = v->AsStringOr(project.BuildVersion);
				}
				if (const Json::Value* v = root.FindMember("buildCompany")) {
					project.BuildCompany = v->AsStringOr(project.BuildCompany);
				}
				if (const Json::Value* splashValue = root.FindMember("splashScreen")) {
					if (const Json::Value* v = splashValue->FindMember("enabled")) {
						project.SplashScreen.Enabled = v->AsBoolOr(true);
					}
					if (const Json::Value* v = splashValue->FindMember("durationSeconds")) {
						project.SplashScreen.DurationSeconds = static_cast<float>(v->AsDoubleOr(0.7));
					}
					if (const Json::Value* v = splashValue->FindMember("fadeInSeconds")) {
						project.SplashScreen.FadeInSeconds = static_cast<float>(v->AsDoubleOr(0.5));
					}
					if (const Json::Value* v = splashValue->FindMember("fadeOutSeconds")) {
						project.SplashScreen.FadeOutSeconds = static_cast<float>(v->AsDoubleOr(0.0));
					}
					if (const Json::Value* v = splashValue->FindMember("imagePath")) {
						{ const std::string p = v->AsStringOr(); if (IsSafeAssetPath(p)) project.SplashScreen.ImagePath = p; else FNK_CORE_WARN_TAG("FunkeProject", "Project '{}': ignoring unsafe splash imagePath '{}'", configPath, p); }
					}
					if (const Json::Value* v = splashValue->FindMember("backgroundImagePath")) {
						{ const std::string p = v->AsStringOr(); if (IsSafeAssetPath(p)) project.SplashScreen.BackgroundImagePath = p; else FNK_CORE_WARN_TAG("FunkeProject", "Project '{}': ignoring unsafe splash backgroundImagePath '{}'", configPath, p); }
					}
					if (const Json::Value* v = splashValue->FindMember("customText")) {
						project.SplashScreen.CustomText = v->AsStringOr();
					}
					if (const Json::Value* v = splashValue->FindMember("backgroundR")) {
						project.SplashScreen.BackgroundR = static_cast<float>(v->AsDoubleOr(0.05));
					}
					if (const Json::Value* v = splashValue->FindMember("backgroundG")) {
						project.SplashScreen.BackgroundG = static_cast<float>(v->AsDoubleOr(0.05));
					}
					if (const Json::Value* v = splashValue->FindMember("backgroundB")) {
						project.SplashScreen.BackgroundB = static_cast<float>(v->AsDoubleOr(0.07));
					}
					if (const Json::Value* v = splashValue->FindMember("fontColorR")) {
						project.SplashScreen.FontColorR = static_cast<float>(v->AsDoubleOr(1.0));
					}
					if (const Json::Value* v = splashValue->FindMember("fontColorG")) {
						project.SplashScreen.FontColorG = static_cast<float>(v->AsDoubleOr(1.0));
					}
					if (const Json::Value* v = splashValue->FindMember("fontColorB")) {
						project.SplashScreen.FontColorB = static_cast<float>(v->AsDoubleOr(1.0));
					}
					if (const Json::Value* v = splashValue->FindMember("fontSize")) {
						project.SplashScreen.FontSize = static_cast<float>(v->AsDoubleOr(16.0));
					}
				}
				if (const Json::Value* buildScenesValue = root.FindMember("buildScenes")) {
					project.BuildSceneList.clear();
					// Mirror the packages-block pattern: degrade gracefully on
					// malformed input instead of asserting inside GetArray().
					if (!buildScenesValue->IsArray()) {
						FNK_CORE_WARN_TAG("FunkeProject",
							"Project '{}': 'buildScenes' field is not a JSON array; ignoring.", configPath);
					}
					else for (const Json::Value& sceneValue : buildScenesValue->GetArray()) {
						std::string sceneName;
						bool enabled = true;
						if (sceneValue.IsObject()) {
							const Json::Value* nameValue = sceneValue.FindMember("name");
							sceneName = nameValue ? nameValue->AsStringOr() : "";
							const Json::Value* enabledValue = sceneValue.FindMember("enabled");
							enabled = enabledValue ? enabledValue->AsBoolOr(true) : true;
						}
						else {
							sceneName = sceneValue.AsStringOr();
						}
						if (sceneName.empty()) continue;
						if (!IsValidSceneName(sceneName)) {
							FNK_CORE_WARN_TAG("FunkeProject",
								"Project '{}': skipping invalid buildScenes entry '{}'", configPath, sceneName);
							continue;
						}
						const bool duplicate = std::any_of(project.BuildSceneList.begin(), project.BuildSceneList.end(),
							[&](const FunkeProject::BuildSceneEntry& entry) { return entry.Name == sceneName; });
						if (duplicate) continue;
						project.BuildSceneList.push_back({ sceneName, enabled });
					}
				}
				if (const Json::Value* globalSystemsValue = root.FindMember("globalSystems")) {
					project.GlobalSystems.clear();
					if (!globalSystemsValue->IsArray()) {
						FNK_CORE_WARN_TAG("FunkeProject",
							"Project '{}': 'globalSystems' field is not a JSON array; ignoring.", configPath);
					}
					else for (const Json::Value& systemValue : globalSystemsValue->GetArray()) {
						if (systemValue.IsString()) {
							const std::string className = systemValue.AsStringOr();
							if (!className.empty()) {
								project.GlobalSystems.push_back({ className, true });
							}
							continue;
						}

						if (!systemValue.IsObject()) {
							continue;
						}

						const Json::Value* classNameValue = systemValue.FindMember("className");
						const std::string className = classNameValue ? classNameValue->AsStringOr() : "";
						if (className.empty()) {
							continue;
						}

						const Json::Value* activeValue = systemValue.FindMember("active");
						project.GlobalSystems.push_back({ className, activeValue ? activeValue->AsBoolOr(true) : true });
					}
				}
				if (const Json::Value* packagesValue = root.FindMember("packages")) {
					project.Packages.clear();
					// Skip + warn on malformed input rather than asserting in Json::Value::GetArray.
					if (packagesValue->IsArray()) {
						for (const Json::Value& packageValue : packagesValue->GetArray()) {
							const std::string packageName = packageValue.AsStringOr();
							if (!packageName.empty()) {
								project.Packages.push_back(packageName);
							}
						}
					}
					else {
						FNK_CORE_WARN_TAG("FunkeProject",
							"Project '{}': 'packages' field is not a JSON array; ignoring.", configPath);
					}
				}
				if (const Json::Value* modulesValue = root.FindMember("modules")) {
					if (modulesValue->IsArray()) {
						project.Modules.clear();
						for (const Json::Value& moduleValue : modulesValue->GetArray()) {
							std::string module = moduleValue.AsStringOr();
							if (!module.empty() && !project.HasModule(module)) {
								project.Modules.push_back(std::move(module));
							}
						}
					}
					else {
						FNK_CORE_WARN_TAG("FunkeProject",
							"Project '{}': 'modules' field is not a JSON array; keeping all modules.", configPath);
					}
				}
				if (const Json::Value* threading = root.FindMember("threading")) {
					if (const Json::Value* v = threading->FindMember("workerCount"))
						project.Threading.WorkerCount = static_cast<int>(std::clamp(v->AsDoubleOr(-1.0), -1.0, double(ThreadingSettings::MaxWorkers)));
					if (const Json::Value* v = threading->FindMember("physics3DWorkerCount"))
						project.Threading.Physics3DWorkerCount = static_cast<int>(std::clamp(v->AsDoubleOr(-1.0), -1.0, double(ThreadingSettings::MaxWorkers)));
					if (const Json::Value* v = threading->FindMember("parallelParticles"))
						project.Threading.ParallelParticles = v->AsBoolOr(true);
					if (const Json::Value* v = threading->FindMember("parallelSkeletalPoses"))
						project.Threading.ParallelSkeletalPoses = v->AsBoolOr(true);
					if (const Json::Value* v = threading->FindMember("parallelRendering2D"))
						project.Threading.ParallelRendering2D = v->AsBoolOr(true);
					if (const Json::Value* v = threading->FindMember("parallelRendering3D"))
						project.Threading.ParallelRendering3D = v->AsBoolOr(true);
					if (const Json::Value* v = threading->FindMember("parallelPhysics2D"))
						project.Threading.ParallelPhysics2D = v->AsBoolOr(true);
				}
				if (const Json::Value* profilerValue = root.FindMember("profiler")) {
					if (const Json::Value* v = profilerValue->FindMember("enableInRuntime"))
						project.Profiler.EnableInRuntime = v->AsBoolOr(false);
					if (const Json::Value* v = profilerValue->FindMember("trackInBackground"))
						project.Profiler.TrackInBackground = v->AsBoolOr(false);
					if (const Json::Value* v = profilerValue->FindMember("samplingHz"))
						project.Profiler.SamplingHz = static_cast<int>(v->AsInt64Or(60));
					if (const Json::Value* v = profilerValue->FindMember("trackingSpan"))
						project.Profiler.TrackingSpan = static_cast<int>(v->AsInt64Or(200));
					if (const Json::Value* modulesValue = profilerValue->FindMember("modules")) {
						project.Profiler.ModuleEnabled.clear();
						if (!modulesValue->IsObject()) {
							FNK_CORE_WARN_TAG("FunkeProject",
								"Project '{}': 'profiler.modules' is not a JSON object; ignoring.", configPath);
						}
						else for (const auto& [name, value] : modulesValue->GetObject()) {
							project.Profiler.ModuleEnabled.emplace_back(
								std::string(name), value.AsBoolOr(true));
						}
					}
				}
				if (const Json::Value* v = root.FindMember("showRuntimeStats"); v && v->IsBool())
					project.ShowRuntimeStats = v->AsBoolOr(true);
				if (const Json::Value* v = root.FindMember("showRuntimeLogs"); v && v->IsBool())
					project.ShowRuntimeLogs = v->AsBoolOr(true);
				if (const Json::Value* v = root.FindMember("devConsole"); v && v->IsBool())
					project.DevConsole = v->AsBoolOr(true);
				if (const Json::Value* v = root.FindMember("prefsProfile"); v && v->IsString())
					project.PrefsProfile = FunkeProject::SanitizePrefsProfileName(v->AsStringOr());
				bool hasNestedPostProcessingEnabled = false;
				if (const Json::Value* graphics = root.FindMember("graphics"); graphics && graphics->IsObject()) {
					if (const Json::Value* v = graphics->FindMember("renderScale"))
						project.Graphics.RenderScale = std::clamp(
							static_cast<float>(v->AsDoubleOr(1.0)),
							GraphicsSettings::MinRenderScale,
							GraphicsSettings::MaxRenderScale);
					if (const Json::Value* v = graphics->FindMember("upscaleFilter"))
						project.Graphics.UpscaleFilter = UpscaleFilterFromString(v->AsStringOr("Linear"));
					if (const Json::Value* v = graphics->FindMember("postProcessingEnabled")) {
						project.Graphics.PostProcessingEnabled = v->AsBoolOr(true);
						hasNestedPostProcessingEnabled = true;
					}
					if (const Json::Value* v = graphics->FindMember("postProcessingQuality"))
						project.Graphics.PostProcessingQuality = PostProcessingQualityFromString(v->AsStringOr("High"));
					if (const Json::Value* v = graphics->FindMember("particleQuality"))
						project.Graphics.ParticleQuality = ParticleQualityFromString(v->AsStringOr("High"));
					if (const Json::Value* v = graphics->FindMember("shadowQuality"))
						project.Graphics.ShadowQuality = ShadowQualityFromString(v->AsStringOr("Medium"));
					if (const Json::Value* v = graphics->FindMember("shadowDistance"))
						project.Graphics.ShadowDistance = static_cast<float>(v->AsDoubleOr(project.Graphics.ShadowDistance));
					if (const Json::Value* v = graphics->FindMember("maxLights"))
						project.Graphics.MaxLights = v->AsIntOr(project.Graphics.MaxLights);
					if (const Json::Value* v = graphics->FindMember("exposure"))
						project.Graphics.Exposure = static_cast<float>(v->AsDoubleOr(project.Graphics.Exposure));
					if (const Json::Value* v = graphics->FindMember("toneMapping"))
						project.Graphics.ToneMapping = ToneMappingFromString(v->AsStringOr("ACES"));
					if (const Json::Value* v = graphics->FindMember("animationDistanceRate"))
						project.Graphics.AnimationDistanceRateEnabled = v->AsBoolOr(false);
					if (const Json::Value* v = graphics->FindMember("animationHalfRateDistance"))
						project.Graphics.AnimationHalfRateDistance = static_cast<float>(v->AsDoubleOr(project.Graphics.AnimationHalfRateDistance));
					if (const Json::Value* v = graphics->FindMember("animationQuarterRateDistance"))
						project.Graphics.AnimationQuarterRateDistance = static_cast<float>(v->AsDoubleOr(project.Graphics.AnimationQuarterRateDistance));
					if (const Json::Value* v = graphics->FindMember("textureQuality"))
						project.Graphics.TextureQuality = TextureQualityFromString(v->AsStringOr("Full"));
					if (const Json::Value* v = graphics->FindMember("anisotropicFiltering"))
						project.Graphics.AnisotropicFiltering = AnisotropicFilteringFromString(v->AsStringOr("X16"));
					if (const Json::Value* v = graphics->FindMember("antiAliasing"))
						project.Graphics.AntiAliasing = AntiAliasingFromString(v->AsStringOr("Off"));
					if (const Json::Value* v = graphics->FindMember("antiAliasingQuality"))
						project.Graphics.AntiAliasingQuality = AntiAliasingQualityFromString(v->AsStringOr("High"));
					project.Graphics = ClampGraphicsSettings(project.Graphics);
				}
				if (!hasNestedPostProcessingEnabled) {
					if (const Json::Value* v = root.FindMember("enablePostProcessing"))
						project.Graphics.PostProcessingEnabled = v->AsBoolOr(true);
				}
				if (const Json::Value* layers = root.FindMember("physicsLayers"); layers && layers->IsObject()) {
					auto& settings = project.PhysicsLayers;
					if (const auto* count = layers->FindMember("visibleCount"))
						settings.VisibleCount = static_cast<uint32_t>(std::clamp(
							count->AsIntOr(PhysicsLayerSettings::DefaultVisibleCount), 1, static_cast<int>(PhysicsLayerSettings::Count)));
					if (const auto* names = layers->FindMember("names"); names && names->IsArray()) {
						// Clear first so valid swapped names also round-trip. Repair invalid/duplicate names below.
						settings.Names.fill("");
						settings.Names[0] = PhysicsLayerSettings::DefaultNames[0];
						for (uint32_t i = 1; i < std::min<size_t>(names->GetArray().size(), PhysicsLayerSettings::Count); ++i)
							settings.SetName(i, names->GetArray()[i].AsStringOr());
						for (uint32_t i = 1; i < PhysicsLayerSettings::Count; ++i) {
							if (!settings.Names[i].empty()) continue;
							std::string fallback = "Layer " + std::to_string(i);
							while (settings.FindIndex(fallback) >= 0) fallback += "_";
							settings.Names[i] = std::move(fallback);
						}
					}
					if (const auto* matrix = layers->FindMember("collisionMasks"); matrix && matrix->IsArray()) {
						for (uint32_t i = 0; i < std::min<size_t>(matrix->GetArray().size(), PhysicsLayerSettings::Count); ++i)
							settings.CollisionMasks[i] = matrix->GetArray()[i].AsUInt64Or(UINT64_MAX);
						// An asymmetric hand edit disables the pair in both directions.
						for (uint32_t a = 0; a < PhysicsLayerSettings::Count; ++a)
							for (uint32_t b = a; b < PhysicsLayerSettings::Count; ++b)
								settings.SetCollision(a, b, settings.CanCollide(a, b) && settings.CanCollide(b, a));
					}
				}
				const auto loadGravity = [&root](const char* key, auto& gravity) {
					if (const auto* value = root.FindMember(key); value && value->IsArray()) {
						const auto& values = value->GetArray();
						if (values.size() != static_cast<size_t>(gravity.length())) return;
						for (int i = 0; i < gravity.length(); ++i) {
							const float component = static_cast<float>(values[i].AsDoubleOr(gravity[i]));
							if (std::isfinite(component)) gravity[i] = component;
						}
					}
				};
				loadGravity("physicsGravity2D", project.PhysicsGravity2D);
				loadGravity("physicsGravity3D", project.PhysicsGravity3D);
				if (const Json::Value* v = root.FindMember("physicsTickRate"))
					project.PhysicsTickRate = std::clamp(v->AsIntOr(50),
						FunkeProject::k_MinPhysicsHz, FunkeProject::k_MaxPhysicsHz);
				// Parse legacy keys into LegacyEditorPrefs so they can migrate to EditorPreferences on next Save.
				if (const Json::Value* v = root.FindMember("autoSaveScenes")) {
					project.LegacyEditorPrefs.AutoSaveScenes = v->AsBoolOr(false);
					project.LegacyEditorPrefs.AutoSaveScenesPresent = true;
				}
				if (const Json::Value* v = root.FindMember("autoSaveIntervalSeconds")) {
					project.LegacyEditorPrefs.AutoSaveIntervalSeconds =
						static_cast<float>(v->AsDoubleOr(120.0));
					project.LegacyEditorPrefs.AutoSaveIntervalSecondsPresent = true;
				}
				if (const Json::Value* v = root.FindMember("autoRecompileScripts")) {
					project.LegacyEditorPrefs.AutoRecompileScripts = v->AsBoolOr(true);
					project.LegacyEditorPrefs.AutoRecompileScriptsPresent = true;
				}
				if (const Json::Value* v = root.FindMember("recompileScriptsOnPlay")) {
					project.LegacyEditorPrefs.RecompileScriptsOnPlay = v->AsBoolOr(false);
					project.LegacyEditorPrefs.RecompileScriptsOnPlayPresent = true;
				}
				if (const Json::Value* v = root.FindMember("assetSerializationFormat"))
					project.AssetSerializationFormat = FunkeProject::ProjectAssetSerializationFormatFromString(
						v->AsStringOr("Binary"));
				if (const Json::Value* v = root.FindMember("showFileExtensions")) {
					project.LegacyEditorPrefs.ShowFileExtensions = v->AsBoolOr(false);
					project.LegacyEditorPrefs.ShowFileExtensionsPresent = true;
				}
				if (const Json::Value* v = root.FindMember("editorEnsureUniqueEntityNames"))
					project.EditorEnsureUniqueEntityNames = v->AsBoolOr(true);
				if (const Json::Value* v = root.FindMember("editorEntityNameSuffix"))
					project.EditorEntityNameSuffix = FunkeProject::EditorEntityNameSuffixStyleFromString(
						v->AsStringOr("ParenthesizedNumber"));
				if (const Json::Value* v = root.FindMember("editorAssetDuplicateSuffix"))
					project.EditorAssetDuplicateSuffix = FunkeProject::EditorEntityNameSuffixStyleFromString(
						v->AsStringOr("ParenthesizedNumber"));
				if (const Json::Value* v = root.FindMember("cursorImagePath"))
					{ const std::string p = v->AsStringOr(); if (IsSafeAssetPath(p)) project.CursorImagePath = p; else FNK_CORE_WARN_TAG("FunkeProject", "Project '{}': ignoring unsafe cursorImagePath '{}'", configPath, p); }
				if (const Json::Value* v = root.FindMember("uiInteractableCursorImagePath"))
					{ const std::string p = v->AsStringOr(); if (IsSafeAssetPath(p)) project.UIInteractableCursorImagePath = p; else FNK_CORE_WARN_TAG("FunkeProject", "Project '{}': ignoring unsafe uiInteractableCursorImagePath '{}'", configPath, p); }
				auto readHotspot = [&root](const char* name, Vec2Int& outHotspot) {
					const Json::Value* v = root.FindMember(name);
					if (!v || !v->IsArray() || v->GetArray().size() != 2) return;
					outHotspot = { std::max(0, v->GetArray()[0].AsIntOr(0)), std::max(0, v->GetArray()[1].AsIntOr(0)) };
				};
				readHotspot("cursorHotspot", project.CursorHotspot);
				readHotspot("uiInteractableCursorHotspot", project.UIInteractableCursorHotspot);
				if (const Json::Value* v = root.FindMember("buildProfile"))
					project.ActiveBuildProfile = FunkeProject::BuildProfileFromString(v->AsStringOr("Development"));
				if (const Json::Value* v = root.FindMember("renderBackend"))
					project.ActiveRenderBackend = FunkeProject::RenderBackendFromString(v->AsStringOr("Auto"));
				if (const Json::Value* v = root.FindMember("activeBuildProfile"))
					project.ActiveBuildProfileName = v->AsStringOr();
				if (const Json::Value* v = root.FindMember("defaultFontAsset")) {
					project.DefaultFontAssetId = v->IsString()
						? ParseUInt64String(v->AsStringOr(), k_DefaultFontAssetId)
						: v->AsUInt64Or(k_DefaultFontAssetId);
					if (project.DefaultFontAssetId == 0) {
						project.DefaultFontAssetId = k_DefaultFontAssetId;
					}
				}
				if (const auto* values = root.FindMember("fontFallbackAssets"); values && values->IsArray()) {
                    project.FontFallbackAssets.clear();
                    for (const auto& value : values->GetArray()) {
                        const auto id = value.IsString() ? ParseUInt64String(value.AsStringOr(), 0) : value.AsUInt64Or(0);
                        if (id && std::find(project.FontFallbackAssets.begin(), project.FontFallbackAssets.end(), id) == project.FontFallbackAssets.end())
                            project.FontFallbackAssets.push_back(id);
                        if (project.FontFallbackAssets.size() == 16) break;
                    }
                }
                if (const Json::Value* v = root.FindMember("fontBakeStep")) {
					project.FontBakeStep = std::clamp(v->AsIntOr(0), 0, 256);
				}
				if (const Json::Value* v = root.FindMember("fontAtlasBudgetMb")) {
					project.FontAtlasBudgetMB = std::max(0, v->AsIntOr(128));
				}
				if (const Json::Value* v = root.FindMember("customDefines"); v && v->IsArray()) {
					project.CustomDefines.clear();
					for (const Json::Value& item : v->GetArray()) {
						if (item.IsString()) {
							std::string s = item.AsStringOr();
							if (!s.empty()) project.CustomDefines.push_back(std::move(s));
						}
					}
				}
				if (const Json::Value* v = root.FindMember("entityBits")) {
					const int bits = v->AsIntOr(20);
					if (bits == 16 || bits == 20 || bits == 22 || bits == 24 || bits == 28) {
						project.EntityBits = bits;
					}
					else {
						FNK_CORE_WARN_TAG("FunkeProject",
							"project.json: entityBits={} is not one of {{16, 20, 22, 24, 28}}; falling back to 20.",
							bits);
						project.EntityBits = 20;
					}
				}
			}
			else if (!jsonText.empty()) {
				FNK_CORE_WARN_TAG("FunkeProject", "Failed to parse '{}': {}", configPath, parseError);
			}
		}

		if (project.Name.empty())
			project.Name = Path::ToUtf8(Path::FromUtf8(rootDir).filename());
		if (!FunkeProject::IsValidProjectName(project.Name)) {
			return fail("Invalid project name in '" + configPath + "': " + project.Name);
		}
		if (project.EngineVersion.empty())
			project.EngineVersion = FNK_VERSION;

		ResolvePaths(project);

		if (sourceFormatVersion < 2) {
			project.Modules = EngineModules::ExpandLegacySelection(project.Modules);
		}

		if (performMigrations && sourceFormatVersion < FunkeProject::k_CurrentProjectFormatVersion) {
			const std::filesystem::path backupPath = GetMigrationBackupPath(Path::FromUtf8(configPath), sourceFormatVersion);
			const std::string backupPathUtf8 = Path::ToUtf8(backupPath);
			if (backupPath.empty() || !File::WriteAllText(backupPathUtf8, sourceJson)) {
				return fail("Could not create project migration backup: " + backupPathUtf8);
			}
			project.ProjectFormatVersion = FunkeProject::k_CurrentProjectFormatVersion;
			if (!project.Save()) {
				return fail("Could not migrate project file; original backup is at: " + backupPathUtf8);
			}
			FNK_CORE_INFO_TAG("FunkeProject", "Migrated project format {} to {} (backup: '{}')",
				sourceFormatVersion, FunkeProject::k_CurrentProjectFormatVersion, backupPathUtf8);
		}

		// One-shot migration: widen Assets\Scripts\**\*.cs glob to Assets\**\*.cs so scripts outside Scripts/ compile.
		if (performMigrations && File::Exists(project.CsprojPath)) {
			std::string csproj = File::ReadAllText(project.CsprojPath);
			constexpr std::string_view oldGlob = "<Compile Include=\"Assets\\Scripts\\**\\*.cs\" />";
			constexpr std::string_view newGlob = "<Compile Include=\"Assets\\**\\*.cs\" />";
			const size_t pos = csproj.find(oldGlob);
			if (pos != std::string::npos) {
				csproj.replace(pos, oldGlob.size(), newGlob);
				if (File::WriteAllText(project.CsprojPath, csproj)) {
					FNK_CORE_INFO_TAG("FunkeProject", "Migrated csproj compile glob to Assets\\**\\*.cs in '{}'", project.CsprojPath);
				}
			}
		}

		if (performMigrations) {
#if FUNKE_WITH_SCRIPTING
			BoltScriptHost::EnsureEditorProject(Path::FromUtf8(project.RootDirectory));
#endif
			std::string toolingError;
			if (!EnsureManagedSdkGlobalJson(project, &toolingError)) return fail(toolingError);
			if (!EnsureManagedSolutionHeader(project, &toolingError)) return fail(toolingError);
			if (!project.WriteManagedDefinesProps()) {
				return fail("Could not write managed IntelliSense definitions");
			}
			if (File::Exists(project.CsprojPath)
				&& File::ReadAllText(project.CsprojPath).find("Funke-ScriptCore") != std::string::npos
				&& !ConfigurePackages(project, &toolingError))
			{
				return fail(toolingError);
			}
		}

		outProject = std::move(project);
		if (outError) outError->clear();
		return true;
	}

	bool FunkeProject::TryLoad(
		const std::string& rootDir,
		FunkeProject& outProject,
		std::string* outError,
		bool performMigrations) {
		try {
			return TryLoadProjectImpl(rootDir, outProject, outError, performMigrations);
		}
		catch (const std::exception& exception) {
			if (outError) *outError = "Project load failed: " + std::string(exception.what());
			return false;
		}
		catch (...) {
			if (outError) *outError = "Project load failed with an unknown error";
			return false;
		}
	}

	FunkeProject FunkeProject::Load(const std::string& rootDir) {
		FunkeProject project;
		std::string error;
		if (!TryLoad(rootDir, project, &error)) {
			throw std::runtime_error(error);
		}
		return project;
	}
}
