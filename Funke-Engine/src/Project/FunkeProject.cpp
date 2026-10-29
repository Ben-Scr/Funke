#include "pch.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/EngineSdk.hpp"
#include "Project/ManagedSdk.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/ProjectTemplate.hpp"
#include "Collections/AspectRatio.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializerStorage.hpp"
#include "Utils/Process.hpp"
#include "Core/Log.hpp"
#include "Core/Version.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace Funke {
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

		void ReportCreateProgress(const FunkeProject::CreateProgressCallback& callback, float progress, std::string_view stage) {
			if (callback) {
				callback(progress, stage);
			}
		}

		bool ToLocalTime(std::time_t value, std::tm& outTime) {
#if defined(_WIN32)
			return localtime_s(&outTime, &value) == 0;
#else
			return localtime_r(&value, &outTime) != nullptr;
#endif
		}

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

		std::string MakeRelativePathOrEmpty(const std::filesystem::path& from, const std::filesystem::path& to) {
			std::error_code ec;
			const std::filesystem::path relative = std::filesystem::relative(to, from, ec);
			if (ec) {
				return {};
			}

			return Path::ToGenericUtf8(relative);
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

		bool IsSafeBuildPathToken(std::string_view value, bool allowDot) {
			if (value.empty() || value == "." || value == "..") return false;
			if (allowDot && (value.front() == '.' || value.back() == '.'
				|| value.find("..") != std::string_view::npos)) return false;
			return std::all_of(value.begin(), value.end(), [allowDot](unsigned char c) {
				return std::isalnum(c) || c == '-' || c == '_' || (allowDot && c == '.');
			});
		}

		std::string GetNativeBuildArchitectureDirectory() {
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

		// dotnet arguments for an engine-generated .csproj; those always carry the x64 platform label.
		std::vector<std::string> ManagedPlatformArguments(const std::string& platform) {
			std::vector<std::string> arguments{ "-p:Platform=" + platform };
#if defined(_M_ARM64) || defined(__aarch64__)
			// An arm64 CoreCLR refuses IL marked x64.
			arguments.push_back("-p:PlatformTarget=AnyCPU");
#endif
			return arguments;
		}

		std::string ToLowerAscii(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
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

		std::string NormalizeNativeProjectText(std::string text) {
			text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
			return text;
		}

		std::string BuildNativeScriptExportsSource(bool includeAbiVersion = true) {
			std::string source = R"(#include <Scripting/NativeScript.hpp>

#if defined(_WIN32)
#define FUNKE_NATIVE_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__)
#define FUNKE_NATIVE_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define FUNKE_NATIVE_SCRIPT_EXPORT extern "C"
#endif

FUNKE_NATIVE_SCRIPT_EXPORT void FunkeInitialize(void* engineAPI) {
	Funke::g_EngineAPI = static_cast<Funke::NativeEngineAPI*>(engineAPI);
}

FUNKE_NATIVE_SCRIPT_EXPORT Funke::NativeScript* FunkeCreateScript(const char* className) {
	return Funke::NativeScriptRegistry::Create(className);
}

FUNKE_NATIVE_SCRIPT_EXPORT int FunkeHasScript(const char* className) {
	return Funke::NativeScriptRegistry::Has(className) ? 1 : 0;
}

FUNKE_NATIVE_SCRIPT_EXPORT void FunkeDestroyScript(Funke::NativeScript* script) {
	delete script;
}
)";
			if (includeAbiVersion) {
				source += R"(
FUNKE_NATIVE_SCRIPT_EXPORT uint32_t FunkeNativeScriptAbiVersion() {
	return Funke::k_NativeScriptAbiVersion;
}
)";
			}
			return source;
		}

		void WriteNativeScriptFileIfChanged(const std::filesystem::path& path, const std::string& content) {
			const std::string pathString = Path::ToUtf8(path);
			if (File::Exists(pathString) && File::ReadAllText(pathString) == content) return;
			if (!File::WriteAllText(pathString, content)) {
				FNK_ERROR_TAG("NativeScripts", "Could not write native script project file '{}'", pathString);
			}
		}

		void EnsureNativeScriptExportsFile(const FunkeProject& project) {
			if (project.NativeSourceDir.empty()) {
				return;
			}

			const std::filesystem::path sourceDirectory = Path::FromUtf8(project.NativeSourceDir);
			std::error_code ec;
			std::filesystem::create_directories(sourceDirectory, ec);

			const std::filesystem::path exportsPath = sourceDirectory / "NativeScriptExports.cpp";
			if (std::filesystem::exists(exportsPath, ec)) {
				const std::string existing = File::ReadAllText(Path::ToUtf8(exportsPath));
				if (NormalizeNativeProjectText(existing) != BuildNativeScriptExportsSource(false)) return;
			}

			WriteNativeScriptFileIfChanged(exportsPath, BuildNativeScriptExportsSource());
		}

		bool IsNativeScriptBootstrapFile(const std::filesystem::path& path) {
			return Path::ToUtf8(path.filename()) == "NativeScriptExports.cpp";
		}

		bool IsNativeScriptSourceExtension(const std::filesystem::path& path) {
			std::string extension = Path::ToUtf8(path.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});

			return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx";
		}

		bool HasNativeScriptSourceFiles(const std::filesystem::path& sourceDirectory) {
			std::error_code ec;
			if (sourceDirectory.empty() || !std::filesystem::exists(sourceDirectory, ec) || ec) {
				return false;
			}

			for (std::filesystem::recursive_directory_iterator it(sourceDirectory, std::filesystem::directory_options::skip_permission_denied, ec), end;
				 it != end;
				 it.increment(ec)) {
				if (ec) {
					ec.clear();
					continue;
				}

				if (!it->is_regular_file(ec) || ec) {
					ec.clear();
					continue;
				}

				const std::filesystem::path path = it->path();
				if (IsNativeScriptBootstrapFile(path)) {
					continue;
				}

				if (IsNativeScriptSourceExtension(path)) {
					return true;
				}
			}

			return false;
		}

		// Retained verbatim to recognize the old generated file without replacing user CMake customizations.
		std::string BuildLegacyNativeScriptCMakeLists(const FunkeProject& project) {
			const std::filesystem::path engineRoot = ResolveEngineRoot();
			const std::string nativeFunkeRootFallback = engineRoot.empty()
				? std::string()
				: MakeRelativePathOrEmpty(Path::FromUtf8(project.NativeScriptsDir), engineRoot);

			// Empty when no custom defines — the placeholder substitution drops the line entirely.
			std::string customDefinesBlock;
			if (!project.CustomDefines.empty()) {
				customDefinesBlock = "target_compile_definitions(${PROJECT_NAME} PRIVATE";
				for (const std::string& d : project.CustomDefines) {
					if (d.empty()) continue;
					customDefinesBlock += " ";
					customDefinesBlock += d;
				}
				customDefinesBlock += ")";
			}

			std::string funkeRootBootstrap = R"(set(FUNKE_DIR "$ENV{FUNKE_DIR}" CACHE PATH "Path to the Funke repository root")
)";
			if (!nativeFunkeRootFallback.empty()) {
				funkeRootBootstrap += "if(NOT FUNKE_DIR)\n";
				funkeRootBootstrap += "    get_filename_component(FUNKE_DIR \"${CMAKE_CURRENT_LIST_DIR}/" + nativeFunkeRootFallback + "\" ABSOLUTE)\n";
				funkeRootBootstrap += "endif()\n";
			}
			funkeRootBootstrap += R"(
if(NOT FUNKE_DIR OR NOT EXISTS "${FUNKE_DIR}/Funke-Engine/src")
    message(FATAL_ERROR "Funke engine sources not found. Set FUNKE_DIR to the Funke repository root before configuring native scripts.")
endif()

)";

			std::string output = R"(cmake_minimum_required(VERSION 3.20)
project()" + project.Name + R"(-NativeScripts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "Flags used by the C++ compiler for Dist builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "Flags used by the shared linker for Dist builds." FORCE)

function(fnk_normalize_native_arch out_var raw_arch)
    string(TOLOWER "${raw_arch}" raw_arch_lower)
    if(raw_arch_lower STREQUAL "amd64" OR raw_arch_lower STREQUAL "x86_64" OR raw_arch_lower STREQUAL "x64")
        set(${out_var} "x86_64" PARENT_SCOPE)
    elseif(raw_arch_lower STREQUAL "arm64" OR raw_arch_lower STREQUAL "aarch64")
        set(${out_var} "arm64" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unsupported native script architecture: ${raw_arch}")
    endif()
endfunction()

)" + funkeRootBootstrap + R"(
file(GLOB_RECURSE NATIVE_SOURCES CONFIGURE_DEPENDS "Source/*.cpp" "Source/*.h" "Source/*.hpp")
add_library(${PROJECT_NAME} SHARED ${NATIVE_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${FUNKE_DIR}/Funke-Engine/src"
    "${FUNKE_DIR}/External/spdlog/include"
    "${FUNKE_DIR}/External/glm"
    "${FUNKE_DIR}/External/entt/src"
    "${FUNKE_DIR}/External/stb"
    "${FUNKE_DIR}/External/magic_enum/include"
    "${FUNKE_DIR}/External/rapidxml"
    "${FUNKE_DIR}/External/SDL/include"
    "${FUNKE_DIR}/External/miniaudio"
    "${FUNKE_DIR}/External/box2d/include"
    "${FUNKE_DIR}/External/dotnet"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    FUNKE_ALL_MODULES=1
    $<$<CONFIG:Debug>:FNK_DEBUG;_DEBUG>
    $<$<CONFIG:Release>:FNK_RELEASE;NDEBUG>
    $<$<CONFIG:Dist>:FNK_DIST;NDEBUG>
)

# FUNKE_BUILD_DEVELOPMENT/RELEASE — mirrors the C# define so native scripts can use #ifdef.
if(NOT DEFINED FUNKE_BUILD_PROFILE)
    set(FUNKE_BUILD_PROFILE "DEVELOPMENT")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE FUNKE_BUILD_${FUNKE_BUILD_PROFILE})

# Project custom defines — same list as C# <DefineConstants>, baked at save time.
)" + std::string(R"(@FUNKE_PROJECT_CUSTOM_DEFINES_BLOCK@)") + R"(

if(WIN32)
    fnk_normalize_native_arch(FNK_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(FNK_NATIVE_PLATFORM "windows-${FNK_NATIVE_ARCH}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE FNK_PLATFORM_WINDOWS)
elseif(APPLE AND NOT IOS)
    fnk_normalize_native_arch(FNK_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(FNK_NATIVE_PLATFORM "macosx-${FNK_NATIVE_ARCH}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE FNK_PLATFORM_MACOS)
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
    fnk_normalize_native_arch(FNK_NATIVE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    set(FNK_NATIVE_PLATFORM "linux-${FNK_NATIVE_ARCH}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE FNK_PLATFORM_LINUX)
else()
    message(FATAL_ERROR "Unsupported platform for native scripts")
endif()

target_link_directories(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:${FUNKE_DIR}/bin/Debug-${FNK_NATIVE_PLATFORM}/Funke-Engine>"
    "$<$<CONFIG:Release>:${FUNKE_DIR}/bin/Release-${FNK_NATIVE_PLATFORM}/Funke-Engine>"
    "$<$<CONFIG:Dist>:${FUNKE_DIR}/bin/Dist-${FNK_NATIVE_PLATFORM}/Funke-Engine>"
)
target_link_libraries(${PROJECT_NAME} PRIVATE Funke-Engine)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

set(NATIVE_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
foreach(config Debug Release Dist)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${NATIVE_OUTPUT_ROOT}/${config}-${FNK_NATIVE_PLATFORM}/${PROJECT_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${NATIVE_OUTPUT_ROOT}/${config}-${FNK_NATIVE_PLATFORM}/${PROJECT_NAME}"
    )
endforeach()
)";

			// Substitute placeholder; strip the line entirely when no defines were configured.
			const std::string placeholder = "@FUNKE_PROJECT_CUSTOM_DEFINES_BLOCK@";
			const auto pos = output.find(placeholder);
			if (pos != std::string::npos) {
				if (customDefinesBlock.empty()) {
					std::size_t lineEnd = output.find('\n', pos);
					if (lineEnd == std::string::npos) lineEnd = output.size();
					else lineEnd += 1;
					output.erase(pos, lineEnd - pos);
				} else {
					output.replace(pos, placeholder.size(), customDefinesBlock);
				}
			}
			return output;
		}

		std::string BuildNativeScriptCMakeModule(const FunkeProject& project) {
			std::string output = BuildLegacyNativeScriptCMakeLists(project);
			auto replace = [&output](std::string_view from, std::string_view to) {
				const std::size_t offset = output.find(from);
				if (offset != std::string::npos) output.replace(offset, from.size(), to);
			};
			replace("cmake_minimum_required(VERSION 3.20)\nproject(" + project.Name + "-NativeScripts LANGUAGES CXX)\n",
				"# Generated by Funke. Put project-specific settings in CMakeLists.txt.\n");
			replace(R"(if(NOT FUNKE_DIR OR NOT EXISTS "${FUNKE_DIR}/Funke-Engine/src")
    message(FATAL_ERROR "Funke engine sources not found. Set FUNKE_DIR to the Funke repository root before configuring native scripts.")
endif())", R"(if(EXISTS "${FUNKE_DIR}/FunkeSDK.json")
    file(READ "${FUNKE_DIR}/FunkeSDK.json" FUNKE_SDK_MANIFEST)
    string(JSON FUNKE_NATIVE_DEVELOPMENT ERROR_VARIABLE FUNKE_SDK_ERROR GET "${FUNKE_SDK_MANIFEST}" capabilities nativeDevelopment)
    if(FUNKE_SDK_ERROR OR NOT FUNKE_NATIVE_DEVELOPMENT)
        message(FATAL_ERROR "This Funke installation cannot build C++ scripts. Install the matching Native SDK component.")
    endif()
endif()
if(NOT FUNKE_DIR OR NOT EXISTS "${FUNKE_DIR}/Funke-Engine/src/Core/Export.hpp")
    message(FATAL_ERROR "Funke native development headers are missing. Install the Native SDK or set FUNKE_DIR to a matching source checkout.")
endif())");
			replace("file(GLOB_RECURSE NATIVE_SOURCES CONFIGURE_DEPENDS \"Source/*.cpp\" \"Source/*.h\" \"Source/*.hpp\")", R"(file(GLOB_RECURSE NATIVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.c" "${CMAKE_CURRENT_LIST_DIR}/Source/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/Source/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.h" "${CMAKE_CURRENT_LIST_DIR}/Source/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.hpp" "${CMAKE_CURRENT_LIST_DIR}/Source/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.inl" "${CMAKE_CURRENT_LIST_DIR}/Source/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.c" "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.h" "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.hpp" "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.inl" "${CMAKE_CURRENT_LIST_DIR}/../Assets/*.ipp")
list(SORT NATIVE_SOURCES))");
			replace("    \"${FUNKE_DIR}/Funke-Engine/src\"", R"(    "${CMAKE_CURRENT_LIST_DIR}/Source"
    "${CMAKE_CURRENT_LIST_DIR}/../Assets"
    "${FUNKE_DIR}/Funke-Engine/src"
    "${FUNKE_DIR}/Funke-Engine/src/Generated"
    "${FUNKE_DIR}/External/dawn/include"
    "${FUNKE_DIR}/External/dawn/build/gen/include")");
			replace("target_compile_definitions(${PROJECT_NAME} PRIVATE\n    FUNKE_ALL_MODULES=1", R"(option(FUNKE_WITH_2D "Match the linked engine's 2D module" ON)
option(FUNKE_WITH_3D "Match the linked engine's 3D module" ON)
if(NOT DEFINED FUNKE_NATIVE_WITH_EDITOR)
    set(FUNKE_NATIVE_WITH_EDITOR "$<NOT:$<CONFIG:Dist>>")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    FNK_IMPORT_DLL
    FUNKE_ALL_MODULES=1
    FUNKE_WITH_2D=$<BOOL:${FUNKE_WITH_2D}>
    FUNKE_WITH_3D=$<BOOL:${FUNKE_WITH_3D}>
    FUNKE_WITH_EDITOR=${FUNKE_NATIVE_WITH_EDITOR})");
			replace("target_link_directories(${PROJECT_NAME} PRIVATE\n", R"(if(FUNKE_ENGINE_LIBRARY_DIR)
    target_link_directories(${PROJECT_NAME} BEFORE PRIVATE "${FUNKE_ENGINE_LIBRARY_DIR}")
endif()
target_link_directories(${PROJECT_NAME} PRIVATE
)");
			replace("    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)", R"(    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"))");
			replace("set(NATIVE_OUTPUT_ROOT \"${CMAKE_CURRENT_SOURCE_DIR}/../bin\")", R"(if(FUNKE_NATIVE_OUTPUT_ROOT)
    set(NATIVE_OUTPUT_ROOT "${FUNKE_NATIVE_OUTPUT_ROOT}")
else()
    set(NATIVE_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
endif())");
			return output;
		}

		std::string NativeScriptCMakeEntryPoint(const FunkeProject& project) {
			return "# Generated by Funke. Add project-specific settings after the include.\n"
				"cmake_minimum_required(VERSION 3.20)\nproject(" + project.GetNativeScriptTargetName() + " LANGUAGES C CXX)\n"
				"include(\"${CMAKE_CURRENT_LIST_DIR}/FunkeNativeScripts.cmake\")\n";
		}

		std::vector<std::string> RegisteredNativeScriptClasses(const std::string& source) {
			std::vector<std::string> names;
			for (std::size_t i = 0; i < source.size();) {
				if (source.compare(i, 2, "//") == 0) {
					const auto end = source.find('\n', i + 2);
					i = end == std::string::npos ? source.size() : end + 1;
					continue;
				}
				if (source.compare(i, 2, "/*") == 0) {
					const auto end = source.find("*/", i + 2);
					i = end == std::string::npos ? source.size() : end + 2;
					continue;
				}
				if (source[i] == '"' || source[i] == '\'') {
					const char quote = source[i++];
					while (i < source.size()) {
						if (source[i] == '\\') { i = std::min(i + 2, source.size()); continue; }
						if (source[i++] == quote) break;
					}
					continue;
				}
				if (!std::isalpha(static_cast<unsigned char>(source[i])) && source[i] != '_') { ++i; continue; }
				const auto start = i++;
				while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
				if (std::string_view(source).substr(start, i - start) != "REGISTER_SCRIPT") continue;
				while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i]))) ++i;
				if (i == source.size() || source[i++] != '(') continue;
				const auto end = source.find(')', i);
				if (end == std::string::npos) break;
				std::string name = source.substr(i, end - i);
				name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }), name.end());
				if (!name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
					return std::isalnum(c) || c == '_' || c == ':';
				})) names.push_back(std::move(name));
				i = end + 1;
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		void MigrateLegacyNativeScriptMirrors(const FunkeProject& project) {
			const auto assetsDirectory = Path::FromUtf8(project.AssetsDirectory);
			const auto sourceDirectory = Path::FromUtf8(project.NativeSourceDir);
			std::error_code ec;
			if (!std::filesystem::is_directory(assetsDirectory, ec) || !std::filesystem::is_directory(sourceDirectory, ec)) return;
			std::unordered_map<std::string, std::vector<std::filesystem::path>> assets;
			for (std::filesystem::recursive_directory_iterator it(assetsDirectory, std::filesystem::directory_options::skip_permission_denied, ec), end;
				it != end; it.increment(ec)) {
				if (ec) { ec.clear(); continue; }
				if (it->is_regular_file(ec)) assets[ToLowerAscii(Path::ToUtf8(it->path().filename()))].push_back(it->path());
			}
			for (std::filesystem::directory_iterator it(sourceDirectory, ec), end; it != end; it.increment(ec)) {
				if (ec) { ec.clear(); continue; }
				if (!it->is_regular_file(ec) || IsNativeScriptBootstrapFile(it->path())) continue;
				const std::string extension = ToLowerAscii(Path::ToUtf8(it->path().extension()));
				if (!IsNativeScriptSourceExtension(it->path()) && extension != ".h" && extension != ".hh"
					&& extension != ".hpp" && extension != ".hxx" && extension != ".inl" && extension != ".ipp") continue;
				if (std::filesystem::is_symlink(it->symlink_status(ec))) continue;
				const auto found = assets.find(ToLowerAscii(Path::ToUtf8(it->path().filename())));
				if (found == assets.end()) continue;
				if (found->second.size() != 1) {
					FNK_WARN_TAG("NativeScripts", "Kept ambiguous legacy source '{}'; multiple assets share its filename.", Path::ToUtf8(it->path()));
					continue;
				}
				const auto assetPath = found->second.front();
				if (std::filesystem::is_symlink(std::filesystem::symlink_status(assetPath, ec))) continue;
				const std::string mirror = File::ReadAllText(Path::ToUtf8(it->path()));
				const std::string asset = File::ReadAllText(Path::ToUtf8(assetPath));
				if (mirror.empty() || asset.empty()) continue;
				const auto classes = RegisteredNativeScriptClasses(mirror);
				if (mirror != asset && (classes.empty() || classes != RegisteredNativeScriptClasses(asset))) {
					FNK_WARN_TAG("NativeScripts", "Kept distinct legacy source '{}' and asset '{}'; merge them manually if they are copies.", Path::ToUtf8(it->path()), Path::ToUtf8(assetPath));
					continue;
				}
				const auto backupRoot = Path::FromUtf8(project.NativeScriptsDir) / "LegacySourceBackup";
				auto backupDirectory = backupRoot / it->path().filename();
				for (unsigned int suffix = 1; std::filesystem::exists(backupDirectory, ec); ++suffix) {
					backupDirectory = backupRoot / (Path::ToUtf8(it->path().filename()) + "." + std::to_string(suffix));
				}
				std::filesystem::create_directories(backupDirectory, ec);
				if (ec) continue;
				std::filesystem::copy_file(assetPath, backupDirectory / "Asset.original", std::filesystem::copy_options::none, ec);
				if (ec) continue;
				const auto mirrorTime = std::filesystem::last_write_time(it->path(), ec);
				if (ec) continue;
				const auto assetTime = std::filesystem::last_write_time(assetPath, ec);
				if (ec) continue;
				if (mirror != asset && mirrorTime > assetTime) {
					std::filesystem::copy_file(it->path(), assetPath, std::filesystem::copy_options::overwrite_existing, ec);
					if (ec) continue;
				}
				std::filesystem::rename(it->path(), backupDirectory / "Source.original", ec);
				if (ec) {
					FNK_ERROR_TAG("NativeScripts", "Could not archive legacy source '{}': {}", Path::ToUtf8(it->path()), ec.message());
					continue;
				}
				FNK_INFO_TAG("NativeScripts", "Native script now uses '{}'; both previous copies are preserved in '{}'.", Path::ToUtf8(assetPath), Path::ToUtf8(backupDirectory));
			}
		}

		void EnsureNativeScriptCMakeListsFile(const FunkeProject& project) {
			if (project.NativeScriptsDir.empty()) {
				return;
			}

			std::error_code ec;
			std::filesystem::create_directories(Path::FromUtf8(project.NativeScriptsDir), ec);

			const std::filesystem::path cmakeListsPath = Path::FromUtf8(project.NativeScriptsDir) / "CMakeLists.txt";
			if (std::filesystem::exists(cmakeListsPath, ec)) {
				const std::string existing = File::ReadAllText(Path::ToUtf8(cmakeListsPath));
				if (NormalizeNativeProjectText(existing) == NormalizeNativeProjectText(BuildLegacyNativeScriptCMakeLists(project))) {
					MigrateLegacyNativeScriptMirrors(project);
					WriteNativeScriptFileIfChanged(cmakeListsPath, NativeScriptCMakeEntryPoint(project));
				} else {
					std::string normalized = NormalizeNativeProjectText(existing);
					const std::string prefix = "# Generated by Funke. Add project-specific settings after the include.\n"
						"cmake_minimum_required(VERSION 3.20)\nproject(";
					const std::string suffix = " LANGUAGES C CXX)\ninclude(\"${CMAKE_CURRENT_LIST_DIR}/FunkeNativeScripts.cmake\")\n";
					const auto end = normalized.find(suffix, prefix.size());
					if (normalized.starts_with(prefix) && end != std::string::npos) {
						normalized.replace(prefix.size(), end - prefix.size(), project.GetNativeScriptTargetName());
						WriteNativeScriptFileIfChanged(cmakeListsPath, normalized);
					}
				}
			} else {
				WriteNativeScriptFileIfChanged(cmakeListsPath, NativeScriptCMakeEntryPoint(project));
			}
			WriteNativeScriptFileIfChanged(Path::FromUtf8(project.NativeScriptsDir) / "FunkeNativeScripts.cmake", BuildNativeScriptCMakeModule(project));
		}
	}

	static std::string GenerateGUID() {
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

		auto hex = [&](int bytes) {
			std::stringstream ss;
			ss << std::hex << std::setfill('0');
			for (int i = 0; i < bytes; i++)
				ss << std::setw(2) << (dist(gen) & 0xFF);
			return ss.str();
		};

		return hex(4) + "-" + hex(2) + "-" + hex(2) + "-" + hex(2) + "-" + hex(6);
	}

	static std::string NowISO8601() {
		auto t = std::time(nullptr);
		std::tm tm{};
		if (!ToLocalTime(t, tm)) {
			return {};
		}
		std::stringstream ss;
		ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
		return ss.str();
	}

	static void ResolvePaths(FunkeProject& p) {
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

	static bool ConfigurePackages(const FunkeProject& project, std::string* outError);

	static bool EnsureManagedSdkGlobalJson(const FunkeProject& project, std::string* outError) {
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

	static bool EnsureManagedSolutionHeader(const FunkeProject& project, std::string* outError) {
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

	static bool IsKnownProjectMember(std::string_view name) {
		static constexpr std::array<std::string_view, 64> knownMembers = {
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
			"fontAtlasBudgetMb", "customDefines", "entityBits", "modules"
		};
		return std::find(knownMembers.begin(), knownMembers.end(), name) != knownMembers.end();
	}

	static bool IsKnownNestedProjectMember(std::string_view parent, std::string_view name) {
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
		for (std::string_view name : { "buildScenes", "globalSystems", "packages", "customDefines" }) {
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
		for (std::string_view name : { "splashScreen", "profiler", "graphics" }) {
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

		for (std::string_view arrayName : { "packages", "customDefines" }) {
			if (const Json::Value* values = root.FindMember(arrayName)) {
				for (const Json::Value& value : values->GetArray()) {
					if (!value.IsString()) {
						outError = "'" + std::string(arrayName) + "' entries must be strings";
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

	static Json::Value BuildProjectJson(const FunkeProject& project) {
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
			systemValue.AddMember("className", registration.ClassName);
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
				if (!module.empty()) {
					modules.Append(module);
				}
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

	std::string FunkeProject::GetNativeScriptTargetName() const {
		std::string targetName;
		targetName.reserve(Name.size() + 14);
		for (const unsigned char c : Name) {
			const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '+';
			targetName.push_back(safe ? static_cast<char>(c) : '_');
		}
		if (targetName.empty()) targetName = "Game";
		return targetName + "-NativeScripts";
	}

	std::string FunkeProject::GetNativeDllPath(std::string_view configuration) const {
		const std::string targetName = GetNativeScriptTargetName();
		const std::string libraryFilename = GetSharedLibraryFilename(targetName);
		const std::string buildConfig = configuration.empty()
			? std::string(FNK_BUILD_CONFIG_NAME)
			: std::string(configuration);
		const std::string platformDirectory = GetNativeBuildPlatformDirectory();

		std::vector<std::filesystem::path> candidates;
		if (!platformDirectory.empty()) {
			candidates.emplace_back(Path::FromUtf8(RootDirectory) / "bin" / (buildConfig + "-" + platformDirectory) / targetName / libraryFilename);
		}

		candidates.emplace_back(Path::FromUtf8(NativeScriptsDir) / "build" / buildConfig / libraryFilename);
		candidates.emplace_back(Path::FromUtf8(NativeScriptsDir) / "build" / libraryFilename);

		for (const auto& candidate : candidates) {
			if (std::filesystem::exists(candidate)) {
				auto normalizedCandidate = candidate;
				return Path::ToUtf8(normalizedCandidate.make_preferred());
			}
		}

		auto fallbackCandidate = candidates.front();
		return Path::ToUtf8(fallbackCandidate.make_preferred());
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
		if (!std::filesystem::is_directory(assetsRoot, ec) || ec) {
			FNK_CORE_ERROR_TAG("FunkeProject", "Scene lookup for '{}' has no readable Assets directory", sceneName);
			return {};
		}
		for (std::filesystem::recursive_directory_iterator it(
				 assetsRoot,
				 std::filesystem::directory_options::none,
				 ec),
			 end;
			 it != end;
			 it.increment(ec)) {
			if (ec) {
				scanFailed = true;
				break;
			}

			std::error_code fileError;
			if (it->is_regular_file(fileError) && !fileError && sameSceneFileName(Path::ToUtf8(it->path().filename()))) {
				if (!resolvedPath.empty()) {
					FNK_CORE_ERROR_TAG("FunkeProject",
						"Scene name '{}' is ambiguous: '{}' and '{}' share the same basename",
						sceneName, Path::ToUtf8(resolvedPath), Path::ToUtf8(it->path()));
					return {};
				}
				resolvedPath = it->path();
			}
			if (fileError) scanFailed = true;
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

	void FunkeProject::EnsureNativeScriptBootstrapFiles() const {
		EnsureNativeScriptExportsFile(*this);
	}

	void FunkeProject::EnsureNativeScriptProjectFiles() const {
		EnsureNativeScriptCMakeListsFile(*this);
		EnsureNativeScriptExportsFile(*this);
	}

	bool FunkeProject::HasNativeScriptSources() const {
		return HasNativeScriptSourceFiles(Path::FromUtf8(NativeSourceDir))
			|| HasNativeScriptSourceFiles(Path::FromUtf8(AssetsDirectory));
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
		const bool wrote = File::WriteAllText(propsPathUtf8, contents);
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
		std::error_code assetsError;
		const std::filesystem::path assetsPath = Path::FromUtf8(rootDir) / "Assets";
		if (!std::filesystem::is_directory(assetsPath, assetsError) || assetsError) {
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

	void CreateDirectoryTree(const FunkeProject& project) {
		Directory::Create(project.RootDirectory);
		Directory::Create(project.AssetsDirectory);
		Directory::Create(project.ScriptsDirectory);
		Directory::Create(project.ScenesDirectory);
		Directory::Create(project.FunkeAssetsDirectory);
		Directory::Create(project.NativeScriptsDir);
		Directory::Create(project.NativeSourceDir);
		Directory::Create(project.PackagesDirectory);
		Directory::Create(project.GetBuildProfilesDirectory());
		Directory::Create(Path::Combine(project.RootDirectory, "bin"));
		Directory::Create(Path::Combine(project.RootDirectory, ".vscode"));
	}

	bool ConfigurePackages(const FunkeProject& project, std::string* outError) {
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

	Json::Value FunkeProject::BuildStarterScene(const std::string& sceneName) {
		// An empty scene with only a Camera2D: the deserializer no longer auto-injects a camera.
		Json::Value sceneRoot = Json::Value::MakeObject();
		sceneRoot.AddMember("version", 1);
		sceneRoot.AddMember("name", sceneName);
		sceneRoot.AddMember("systems", Json::Value::MakeArray());

		Json::Value transform2D = Json::Value::MakeObject();
		transform2D.AddMember("posX", 0.0);
		transform2D.AddMember("posY", 0.0);
		transform2D.AddMember("rotation", 0.0);
		transform2D.AddMember("scaleX", 1.0);
		transform2D.AddMember("scaleY", 1.0);

		Json::Value camera2D = Json::Value::MakeObject();
		camera2D.AddMember("orthoSize", 5.0);
		camera2D.AddMember("zoom", 1.0);

		Json::Value cameraEntity = Json::Value::MakeObject();
		cameraEntity.AddMember("name", std::string("Camera"));
		cameraEntity.AddMember("Transform2D", std::move(transform2D));
		cameraEntity.AddMember("Camera2D", std::move(camera2D));

		Json::Value entities = Json::Value::MakeArray();
		entities.Append(std::move(cameraEntity));
		sceneRoot.AddMember("entities", std::move(entities));
		return sceneRoot;
	}

	FunkeProject FunkeProject::Create(const std::string& name, const std::string& parentDir,
		const CreateProgressCallback& progressCallback) {
		return Create(name, parentDir, name, progressCallback);
	}

	FunkeProject FunkeProject::Create(const std::string& name, const std::string& parentDir,
		const std::string& directoryName, const CreateProgressCallback& progressCallback) {
		return Create(name, parentDir, directoryName, nullptr, progressCallback);
	}

	namespace {
		bool IsTemplateTextFile(const std::string& lowerExtension) {
			return lowerExtension == ".cs" || lowerExtension == ".json" || lowerExtension == ".md"
				|| lowerExtension == ".txt" || lowerExtension == ".meta" || lowerExtension == ".dataasset";
		}

		// Scenes are authored as JSON in the template and written in the project's serialization format.
		void CopyTemplateAssets(const ProjectTemplate& projectTemplate, const FunkeProject& project, SceneSerializationFormat sceneFormat) {
			const std::filesystem::path sourceRoot = Path::FromUtf8(projectTemplate.GetAssetsDirectory());
			const std::filesystem::path destinationRoot = Path::FromUtf8(project.AssetsDirectory);
			std::error_code ec;
			if (!std::filesystem::is_directory(sourceRoot, ec)) {
				throw std::runtime_error("Template '" + projectTemplate.Id + "' has no Assets folder");
			}

			for (auto it = std::filesystem::recursive_directory_iterator(sourceRoot, ec);
				!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
				const std::filesystem::path relative = std::filesystem::relative(it->path(), sourceRoot, ec);
				if (ec) break;
				const std::filesystem::path destination = destinationRoot / relative;
				if (it->is_directory(ec)) {
					std::filesystem::create_directories(destination, ec);
					continue;
				}
				std::filesystem::create_directories(destination.parent_path(), ec);

				const std::string sourcePath = Path::ToUtf8(it->path());
				const std::string destinationPath = Path::ToUtf8(destination);
				std::string extension = Path::ToUtf8(it->path().extension());
				std::transform(extension.begin(), extension.end(), extension.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

				if (extension == ".scene") {
					// Templates saved from binary-format projects carry binary scenes; those have no placeholders.
					std::string parseError;
					Json::Value sceneRoot;
					const bool parsed = SceneSerializerStorage::IsBinaryData(File::ReadAllBytes(sourcePath))
						? SceneSerializerStorage::ReadRootFromFile(sourcePath, sceneRoot, &parseError)
						: Json::TryParse(ProjectTemplates::ExpandPlaceholders(File::ReadAllText(sourcePath), project.Name),
							sceneRoot, &parseError);
					if (!parsed || !SceneSerializerStorage::WriteRootToFile(destinationPath, sceneRoot, sceneFormat)) {
						throw std::runtime_error("Could not write template scene '" + destinationPath + "': " + parseError);
					}
					continue;
				}

				const std::string text = IsTemplateTextFile(extension) ? File::ReadAllText(sourcePath) : std::string{};
				const std::string expanded = text.empty() ? text : ProjectTemplates::ExpandPlaceholders(text, project.Name);
				if (expanded != text) {
					if (!File::WriteAllText(destinationPath, expanded)) {
						throw std::runtime_error("Could not write template file: " + destinationPath);
					}
				}
				else {
					std::filesystem::copy_file(it->path(), destination, std::filesystem::copy_options::overwrite_existing, ec);
					if (ec) {
						throw std::runtime_error("Could not copy template file '" + sourcePath + "': " + ec.message());
					}
				}
			}
			if (ec) {
				throw std::runtime_error("Could not read template '" + projectTemplate.Id + "': " + ec.message());
			}
		}

		// Verbatim copy of an optional template subtree (build profiles, native sources, local packages).
		void CopyTemplateDirectory(const std::filesystem::path& source, const std::filesystem::path& destination) {
			std::error_code ec;
			if (!std::filesystem::is_directory(source, ec)) return;
			std::filesystem::create_directories(destination, ec);
			std::filesystem::copy(source, destination,
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				throw std::runtime_error("Could not copy template folder '" + Path::ToUtf8(source) + "': " + ec.message());
			}
		}

		// Settings saved with the template; name, location and per-user editor state belong to the new project.
		FunkeProject LoadTemplateProjectSettings(const ProjectTemplate& projectTemplate) {
			FunkeProject settings;
			std::string error;
			if (!FunkeProject::TryLoad(projectTemplate.RootDirectory, settings, &error, false)) {
				throw std::runtime_error("Template '" + projectTemplate.Id + "' has invalid project settings: " + error);
			}
			if (settings.ExecutableName == settings.Name) {
				settings.ExecutableName.clear();
			}
			settings.EditorView3D.reset();
			settings.ProjectFormatVersion = FunkeProject::k_CurrentProjectFormatVersion;
			return settings;
		}
	}

	FunkeProject FunkeProject::Create(const std::string& name, const std::string& parentDir,
		const std::string& directoryName, const ProjectTemplate* projectTemplate,
		const CreateProgressCallback& progressCallback) {
		const bool templateHasSettings = projectTemplate && !projectTemplate->GetProjectSettingsPath().empty();
		FunkeProject project = templateHasSettings ? LoadTemplateProjectSettings(*projectTemplate) : FunkeProject{};
		project.Name = name;
		project.RootDirectory = Path::Combine(parentDir, directoryName.empty() ? name : directoryName);
		project.EngineVersion = FNK_VERSION;
		if (!templateHasSettings) {
			// Text scenes/prefabs diff in git and stay editable by tools; builds still ship them as binary.
			project.AssetSerializationFormat = ProjectAssetSerializationFormat::Json;
		}
		if (projectTemplate) {
			if (!projectTemplate->Modules.empty()) {
				project.Modules = projectTemplate->Modules;
			}
			project.StartupScene = projectTemplate->StartupScene;
			project.LastOpenedScene = projectTemplate->StartupScene;
		}
		else {
			// The built-in starter scene is 2D.
			project.Modules = { std::string(k_Module2D) };
		}
		ResolvePaths(project);

		ReportCreateProgress(progressCallback, 0.08f, "Creating project folders...");
		CreateDirectoryTree(project);
		ReportCreateProgress(progressCallback, 0.18f, "Configuring engine packages...");
		std::string packageError;
		if (!ConfigurePackages(project, &packageError)) {
			throw std::runtime_error(packageError);
		}
		const std::filesystem::path templateRoot = projectTemplate ? Path::FromUtf8(projectTemplate->RootDirectory) : std::filesystem::path{};
		std::error_code templateProfilesError;
		if (projectTemplate && std::filesystem::is_directory(templateRoot / "BuildProfiles", templateProfilesError)) {
			ReportCreateProgress(progressCallback, 0.20f, "Copying template build profiles...");
			CopyTemplateDirectory(templateRoot / "BuildProfiles", Path::FromUtf8(project.GetBuildProfilesDirectory()));
		}
		else {
			ReportCreateProgress(progressCallback, 0.20f, "Creating default build profiles...");
			FunkeBuildProfile::WriteDefaultProfiles(project.GetBuildProfilesDirectory());
		}

		std::string engineAssets = Path::ResolveFunkeAssets("");
		if (!engineAssets.empty() && Directory::Exists(engineAssets)) {
			ReportCreateProgress(progressCallback, 0.34f, "Copying Funke assets...");
			try {
				std::filesystem::copy(Path::FromUtf8(engineAssets), Path::FromUtf8(project.FunkeAssetsDirectory),
					std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
			}
			catch (const std::exception& ex) {
				FNK_CORE_WARN_TAG("FunkeProject", "Failed to copy Funke assets from '{}' to '{}': {}",
					engineAssets, project.FunkeAssetsDirectory, ex.what());
			}
			catch (...) {
				FNK_CORE_WARN_TAG("FunkeProject", "Failed to copy Funke assets from '{}' to '{}': unknown error",
					engineAssets, project.FunkeAssetsDirectory);
			}
		}

		if (project.BuildSceneList.empty()) {
			project.BuildSceneList.push_back({ project.StartupScene, true });
		}

		// Write project.json
		{
			ReportCreateProgress(progressCallback, 0.48f, "Writing project configuration...");
			Json::Value root = BuildProjectJson(project);
			if (!File::WriteAllText(project.ProjectFilePath, Json::Stringify(root, true))) {
				throw std::runtime_error("Could not write project configuration: " + project.ProjectFilePath);
			}
		}

		const SceneSerializationFormat starterFormat =
			project.AssetSerializationFormat == FunkeProject::ProjectAssetSerializationFormat::Binary
				? SceneSerializationFormat::Binary
				: SceneSerializationFormat::Json;
		if (projectTemplate) {
			ReportCreateProgress(progressCallback, 0.56f, "Copying template '" + projectTemplate->DisplayName + "'...");
			CopyTemplateAssets(*projectTemplate, project, starterFormat);
			CopyTemplateDirectory(templateRoot / "NativeScripts" / "Source", Path::FromUtf8(project.NativeSourceDir));
			CopyTemplateDirectory(templateRoot / "Packages", Path::FromUtf8(project.PackagesDirectory));
			const std::string startupScenePath = project.GetSceneFilePath(project.StartupScene);
			if (startupScenePath.empty() || !File::Exists(startupScenePath)) {
				throw std::runtime_error("Template '" + projectTemplate->Id + "' does not contain its startup scene '"
					+ project.StartupScene + "'");
			}
		}
		else {
			ReportCreateProgress(progressCallback, 0.56f, "Generating starter scene...");
			const Json::Value sceneRoot = BuildStarterScene(project.StartupScene);
			const std::string starterScenePath = project.GetSceneFilePath(project.StartupScene);
			if (starterScenePath.empty()
				|| !SceneSerializerStorage::WriteRootToFile(starterScenePath, sceneRoot, starterFormat)) {
				throw std::runtime_error("Could not write starter scene: " + starterScenePath);
			}
		}

		// Preserve existing NuGet PackageReference entries if .csproj already exists
		std::string existingPackageRefs;
		if (File::Exists(project.CsprojPath)) {
			std::ifstream existing(project.CsprojPath);
			std::string content((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
			existing.close();

			// Extract all ItemGroup blocks that contain PackageReference
			size_t searchPos = 0;
			while (searchPos < content.size()) {
				size_t igStart = content.find("<ItemGroup>", searchPos);
				if (igStart == std::string::npos) break;
				size_t igEnd = content.find("</ItemGroup>", igStart);
				if (igEnd == std::string::npos) break;
				igEnd += 12; // length of "</ItemGroup>"

				std::string block = content.substr(igStart, igEnd - igStart);
				const bool referencesScriptCore = block.find("Funke-ScriptCore") != std::string::npos;
				if (block.find("PackageReference") != std::string::npos
					|| (block.find("<Reference ") != std::string::npos && !referencesScriptCore)
					|| (block.find("ProjectReference") != std::string::npos && !referencesScriptCore)) {
					existingPackageRefs += "  " + block + "\n";
				}
				searchPos = igEnd;
			}
		}

		ReportCreateProgress(progressCallback, 0.66f, "Generating C# project files...");
		std::string csproj = R"CS(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Library</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <Configurations>Debug;Release;Dist</Configurations>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <CopyLocalLockFileAssemblies>true</CopyLocalLockFileAssemblies>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <EnableDefaultNoneItems>false</EnableDefaultNoneItems>
    <Nullable>enable</Nullable>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Debug'">
    <OutputPath>bin\Debug\</OutputPath>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Release'">
    <OutputPath>bin\Release\</OutputPath>
    <Optimize>true</Optimize>
  </PropertyGroup>

  <PropertyGroup Condition="'$(Configuration)' == 'Dist'">
    <OutputPath>bin\Dist\</OutputPath>
    <Optimize>true</Optimize>
  </PropertyGroup>

  <ItemGroup>
    <Compile Include="Assets\**\*.cs" />
  </ItemGroup>

  <ItemGroup>
    <Reference Include="Funke-ScriptCore">
      <HintPath>Packages\Funke-ScriptCore\Funke-ScriptCore.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>

  <!-- Auto-generated by Funke Package Manager every time a package is installed/removed. -->
  <Import Project="Packages/FunkePackages.props" Condition="Exists('Packages/FunkePackages.props')" />
  <!-- Auto-generated on every project save with CustomDefines + active build profile. -->
  <Import Project="Packages/FunkeDefines.props" Condition="Exists('Packages/FunkeDefines.props')" />
)CS" + existingPackageRefs + R"CS(
</Project>
)CS";
		if (!File::WriteAllText(project.CsprojPath, csproj)) {
			throw std::runtime_error("Could not write C# project: " + project.CsprojPath);
		}
		std::string toolingError;
		if (!EnsureManagedSdkGlobalJson(project, &toolingError)) {
			throw std::runtime_error(toolingError);
		}
		if (!project.WriteManagedDefinesProps()) {
			throw std::runtime_error("Could not write managed IntelliSense definitions");
		}

		// Funke-ScriptCore is linked via DLL reference in the .csproj — no project entry needed.
		ReportCreateProgress(progressCallback, 0.72f, "Generating solution and starter scripts...");
		std::string projGuid = GenerateGUID();

		std::string sln = R"(
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 18
Project("{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}") = ")" + name + R"(", ")" + name + R"(.csproj", "{)" + projGuid + R"(}"
EndProject
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|Any CPU = Debug|Any CPU
		Release|Any CPU = Release|Any CPU
		Dist|Any CPU = Dist|Any CPU
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		{)" + projGuid + R"(}.Debug|Any CPU.ActiveCfg = Debug|AnyCPU
		{)" + projGuid + R"(}.Debug|Any CPU.Build.0 = Debug|AnyCPU
		{)" + projGuid + R"(}.Release|Any CPU.ActiveCfg = Release|AnyCPU
		{)" + projGuid + R"(}.Release|Any CPU.Build.0 = Release|AnyCPU
		{)" + projGuid + R"(}.Dist|Any CPU.ActiveCfg = Dist|AnyCPU
		{)" + projGuid + R"(}.Dist|Any CPU.Build.0 = Dist|AnyCPU
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
EndGlobal
)";
		if (!File::WriteAllText(project.SlnPath, sln)) {
			throw std::runtime_error("Could not write Visual Studio solution: " + project.SlnPath);
		}

		// Templates ship their own scripts; without one the project gets an empty GameScript.
		if (!projectTemplate) {
			std::string starterScript = R"(using Funke;

public class GameScript : EntityScript
{
    public override void OnStart()
    {
        Log.Info("Hello from )" + name + R"(!");
    }

    public override void OnUpdate()
    {
    }
}
)";
			(void)File::WriteAllText(Path::Combine(project.ScriptsDirectory, "GameScript.cs"), starterScript);
		}

		// Generate .vscode/settings.json
		std::string vsCodeSettings = R"({
    "omnisharp.projectFilesExcludePattern": [],
    "dotnet.defaultSolution": ")" + name + R"(.sln"
})";
		(void)File::WriteAllText(Path::Combine(project.RootDirectory, ".vscode", "settings.json"), vsCodeSettings);

		project.EnsureNativeScriptProjectFiles();

		// Generate .gitignore
		(void)File::WriteAllText(Path::Combine(project.RootDirectory, ".gitignore"),
			"bin/\nobj/\n.vs/\n*.user\nNativeScripts/build/\nPackages/\n");

		ReportCreateProgress(progressCallback, 0.82f, "Project files ready.");
		FNK_INFO_TAG("FunkeProject", "Created project: {} at {}", name, project.RootDirectory);
		return project;
	}

	FunkeProject FunkeProject::Duplicate(const std::string& sourceRootDir, const std::string& newName,
		const std::string& parentDir, const std::string& directoryName,
		const CreateProgressCallback& progressCallback) {

		if (!Validate(sourceRootDir)) {
			throw std::runtime_error("Source is not a valid Funke project: " + sourceRootDir);
		}

		// Load the source so the duplicate inherits every setting (packages, build
		// profiles, scene list, ...). Only the name and on-disk location change.
		FunkeProject source = Load(sourceRootDir);
		const std::string oldName = source.Name;

		const std::string targetDirName = directoryName.empty() ? newName : directoryName;
		const std::string targetRoot = Path::Combine(parentDir, targetDirName);
		if (std::filesystem::exists(Path::FromUtf8(targetRoot))) {
			throw std::runtime_error("Target directory already exists: " + targetRoot);
		}

		ReportCreateProgress(progressCallback, 0.05f, "Copying project files...");

		// Recursive copy, skipping build artifacts and VCS metadata so the duplicate
		// starts clean (these are regenerated on first open/build).
		auto isExcluded = [](const std::filesystem::path& rel) -> bool {
			if (rel.empty()) return false;
			const std::string top = rel.begin()->string();
			if (top == "bin" || top == "obj" || top == ".vs" || top == ".git" || top == "Builds") {
				return true;
			}
			auto it = rel.begin();
			if (it != rel.end() && it->string() == "NativeScripts") {
				++it;
				if (it != rel.end() && it->string() == "build") return true;
			}
			return rel.extension() == ".user";
		};

		const std::filesystem::path src = Path::FromUtf8(sourceRootDir);
		const std::filesystem::path dst = Path::FromUtf8(targetRoot);
		std::error_code ec;
		std::filesystem::create_directories(dst, ec);
		if (ec) {
			throw std::runtime_error("Failed to create target directory '" + targetRoot + "': " + ec.message());
		}

		for (std::filesystem::recursive_directory_iterator it(src,
				std::filesystem::directory_options::skip_permission_denied, ec);
			!ec && it != std::filesystem::recursive_directory_iterator();
			it.increment(ec)) {
			const std::filesystem::path rel = std::filesystem::relative(it->path(), src, ec);
			if (ec) { ec.clear(); continue; }
			if (isExcluded(rel)) {
				if (it->is_directory(ec)) it.disable_recursion_pending();
				ec.clear();
				continue;
			}
			const std::filesystem::path outPath = dst / rel;
			if (it->is_directory(ec)) {
				std::filesystem::create_directories(outPath, ec);
				ec.clear();
			}
			else {
				std::filesystem::create_directories(outPath.parent_path(), ec);
				ec.clear();
				std::filesystem::copy_file(it->path(), outPath,
					std::filesystem::copy_options::overwrite_existing, ec);
				if (ec) {
					FNK_CORE_WARN_TAG("FunkeProject",
						"Duplicate: failed to copy '{}': {}", Path::ToUtf8(it->path()), ec.message());
					ec.clear();
				}
			}
		}

		ReportCreateProgress(progressCallback, 0.6f, "Rewriting project metadata...");

		// New project view: inherit settings, retarget name + paths.
		FunkeProject target = source;
		target.Name = newName;
		target.EngineVersion = FNK_VERSION;
		target.RootDirectory = targetRoot;
		ResolvePaths(target);

		auto replaceAll = [](std::string s, const std::string& from, const std::string& to) {
			if (from.empty()) return s;
			std::size_t pos = 0;
			while ((pos = s.find(from, pos)) != std::string::npos) {
				s.replace(pos, from.size(), to);
				pos += to.size();
			}
			return s;
		};

		// Rename .csproj / .sln to the new name (their basenames are the project
		// name; the .csproj body uses SDK defaults keyed off the file name).
		if (oldName != newName) {
			const std::filesystem::path oldCsproj = dst / (oldName + ".csproj");
			if (std::filesystem::exists(oldCsproj)) {
				std::filesystem::rename(oldCsproj, Path::FromUtf8(target.CsprojPath), ec);
				if (ec) ec.clear();
			}
			const std::filesystem::path oldSln = dst / (oldName + ".sln");
			if (std::filesystem::exists(oldSln)) {
				std::filesystem::rename(oldSln, Path::FromUtf8(target.SlnPath), ec);
				if (ec) ec.clear();
			}
		}

		// The .sln body embeds the display name and the "<name>.csproj" reference.
		if (File::Exists(target.SlnPath)) {
			std::string sln = File::ReadAllText(target.SlnPath);
			sln = replaceAll(sln, "\"" + oldName + ".csproj\"", "\"" + newName + ".csproj\"");
			sln = replaceAll(sln, "= \"" + oldName + "\"", "= \"" + newName + "\"");
			(void)File::WriteAllText(target.SlnPath, sln);
		}

		// .vscode/settings.json points dotnet.defaultSolution at "<name>.sln".
		const std::string vscodeSettings = Path::Combine(target.RootDirectory, ".vscode", "settings.json");
		if (File::Exists(vscodeSettings)) {
			std::string s = File::ReadAllText(vscodeSettings);
			s = replaceAll(s, oldName + ".sln", newName + ".sln");
			(void)File::WriteAllText(vscodeSettings, s);
		}

		// Persist project.json with the new name (all other settings inherited).
		{
			Json::Value root = BuildProjectJson(target);
			if (!File::WriteAllText(target.ProjectFilePath, Json::Stringify(root, true))) {
				throw std::runtime_error("Could not write duplicated project configuration: " + target.ProjectFilePath);
			}
		}

		// Native script project files (CMakeLists) embed the target name; regenerate.
		target.EnsureNativeScriptProjectFiles();

		ReportCreateProgress(progressCallback, 1.0f, "Duplicate ready.");
		FNK_INFO_TAG("FunkeProject", "Duplicated project '{}' -> '{}' at {}", oldName, newName, target.RootDirectory);
		return target;
	}

	std::string FunkeProject::GetEngineRootDir() {
		const std::filesystem::path root = ResolveEngineRoot();
		return root.empty() ? std::string{} : Path::ToGenericUtf8(root);
	}

	EngineSdk::Installation FunkeProject::GetEngineInstallation() {
		return EngineSdk::Resolve(Path::FromUtf8(Path::ExecutableDir()), EngineSdk::HostPlatform(), EngineSdk::HostArchitecture(), FNK_VERSION);
	}

#if !defined(_WIN32)
	namespace {
		std::filesystem::path FindExecutableOnPath(std::string_view name) {
			const char* searchPath = std::getenv("PATH");
			std::string_view remaining = searchPath ? searchPath : "";
			while (!remaining.empty()) {
				const size_t separator = remaining.find(':');
				const std::string_view directory = remaining.substr(0, separator);
				if (!directory.empty()) {
					const std::filesystem::path candidate = std::filesystem::path(std::string(directory)) / std::string(name);
					std::error_code ec;
					const auto permissions = std::filesystem::status(candidate, ec).permissions();
					if (!ec && std::filesystem::is_regular_file(candidate, ec)
						&& (permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
						return candidate;
					}
				}
				if (separator == std::string_view::npos) break;
				remaining.remove_prefix(separator + 1);
			}
			return {};
		}
	}
#endif

	std::string FunkeProject::GetPremakePath() {
		const std::filesystem::path engineRoot = ResolveEngineRoot();
		if (engineRoot.empty()) {
			return {};
		}

#if defined(_WIN32)
		const char* const exeName = "premake5.exe";
#else
		const char* const exeName = "premake5";
#endif

		// Dev layout vendors premake under vendor/bin/. Packaged SDK ships it
		// flat at the SDK root because vendor/ is not redistributed.
		const std::vector<std::filesystem::path> candidates = {
			engineRoot / "vendor" / "bin" / exeName,
			engineRoot / exeName,
		};

		for (const auto& candidate : candidates) {
			std::error_code ec;
			if (std::filesystem::exists(candidate, ec) && !ec) {
				return Path::ToGenericUtf8(candidate);
			}
		}
#if !defined(_WIN32)
		// The repo only vendors premake5.exe; Linux and macOS clones may rely on Homebrew's or the distro's.
		if (const std::filesystem::path onPath = FindExecutableOnPath(exeName); !onPath.empty()) {
			return Path::ToGenericUtf8(onPath);
		}
#endif
		return {};
	}

	std::string FunkeProject::GetMakeProgram() {
#if defined(__APPLE__)
		// Managed Macs can block the Command Line Tools' make ("Permission denied"); Homebrew's GNU make runs the same Makefiles.
		if (const std::filesystem::path gmake = FindExecutableOnPath("gmake"); !gmake.empty()) {
			return Path::ToUtf8(gmake);
		}
#endif
		return "make";
	}

	FunkeProject::RegenerateResult FunkeProject::RegenerateSolutionForProject(const std::string& projectRootDir) {
		return RegeneratePackageWorkspaceForProject(projectRootDir);
	}

#if defined(_WIN32)
	namespace {
		std::string FindMSBuildWithVswhere(const char* versionRange, bool requireCppTools) {
			const std::filesystem::path vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
			std::error_code vswhereEc;
			if (!std::filesystem::exists(vswhere, vswhereEc) || vswhereEc) return {};

			std::vector<std::string> command{
				Path::ToUtf8(vswhere),
				"-latest",
				"-products", "*",
				"-version", versionRange,
				"-requires", "Microsoft.Component.MSBuild"
			};
			if (requireCppTools) command.push_back("Microsoft.VisualStudio.Component.VC.Tools.x86.x64");
			command.insert(command.end(), { "-find", "MSBuild\\**\\Bin\\MSBuild.exe", "-nologo" });

			Process::Result vswhereResult = Process::Run(command, {}, std::chrono::seconds(30));
			if (!vswhereResult.Succeeded()) return {};

			const std::string& out = vswhereResult.Output;
			const auto newline = out.find_first_of("\r\n");
			std::string firstLine = (newline == std::string::npos) ? out : out.substr(0, newline);
			if (firstLine.size() >= 3 && static_cast<unsigned char>(firstLine[0]) == 0xEF
				&& static_cast<unsigned char>(firstLine[1]) == 0xBB
				&& static_cast<unsigned char>(firstLine[2]) == 0xBF)
				firstLine.erase(0, 3);
			while (!firstLine.empty() && (firstLine.front() == ' ' || firstLine.front() == '\t')) {
				firstLine.erase(firstLine.begin());
			}
			while (!firstLine.empty() && (firstLine.back() == ' ' || firstLine.back() == '\t')) {
				firstLine.pop_back();
			}
			std::error_code existEc;
			if (!firstLine.empty() && std::filesystem::exists(Path::FromUtf8(firstLine), existEc) && !existEc) {
				return firstLine;
			}
			return {};
		}

		std::string FindMSBuildUnder(std::initializer_list<const char*> visualStudioRoots) {
			for (const char* root : visualStudioRoots) {
				for (const char* edition : { "Community", "Professional", "Enterprise", "BuildTools" }) {
					const std::filesystem::path candidate = std::filesystem::path(root) / edition
						/ "MSBuild" / "Current" / "Bin" / "MSBuild.exe";
					std::error_code error;
					if (std::filesystem::is_regular_file(candidate, error) && !error)
						return Path::ToGenericUtf8(candidate);
				}
			}
			return {};
		}
	}
#endif

	std::string FunkeProject::GetMSBuildPath() {
#if defined(_WIN32)
		if (std::string msbuild = FindMSBuildWithVswhere("[18.0,)", false); !msbuild.empty()) return msbuild;
		return FindMSBuildUnder({
			"C:\\Program Files\\Microsoft Visual Studio\\18",
			"C:\\Program Files\\Microsoft Visual Studio\\2026",
			"C:\\Program Files (x86)\\Microsoft Visual Studio\\18",
			"C:\\Program Files (x86)\\Microsoft Visual Studio\\2026"
		});
#else
		return {};
#endif
	}

	std::string FunkeProject::GetNativeMSBuildPath() {
#if defined(_WIN32)
		if (const char* overridePath = std::getenv("FUNKE_MSBUILD"); overridePath && *overridePath) {
			std::error_code error;
			if (std::filesystem::is_regular_file(Path::FromUtf8(overridePath), error) && !error) return overridePath;
			FNK_WARN_TAG("FunkeProject", "FUNKE_MSBUILD '{}' does not exist; searching for MSBuild instead.", overridePath);
		}
		// VS 18 keeps its own copy of the v143 headers; compiling against PCHs built from VS 2022's copy
		// includes each header twice (C2375 on intrinsics). Build.ps1 and the vs2022 workspaces use VS 2022.
		if (std::string msbuild = FindMSBuildWithVswhere("[17.0,18.0)", true); !msbuild.empty()) return msbuild;
		if (std::string msbuild = FindMSBuildUnder({
				"C:\\Program Files\\Microsoft Visual Studio\\2022",
				"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022"
			}); !msbuild.empty())
			return msbuild;
#endif
		return GetMSBuildPath();
	}

	FunkeProject::BuildResult FunkeProject::BuildSolution(const std::string& configuration,
		const std::string& platform) {
		BuildResult result;
		const auto installation = GetEngineInstallation();
		if (!installation.IsValid() || !installation.SourceBuild) {
			result.Output = installation.IsValid() ? "This binary Funke installation cannot rebuild the engine. Use a source checkout." : installation.Error;
			return result;
		}

		const std::string engineRoot = GetEngineRootDir();
		if (engineRoot.empty()) {
			result.Output = "Could not resolve engine root directory; skipping build.";
			FNK_WARN_TAG("FunkeProject", "{}", result.Output);
			return result;
		}

		const std::filesystem::path solutionPath = Path::FromUtf8(engineRoot) / "Funke.sln";
		std::error_code ec;
		if (!std::filesystem::exists(solutionPath, ec) || ec) {
			result.Output = "Funke.sln not found at " + Path::ToGenericUtf8(solutionPath);
			FNK_WARN_TAG("FunkeProject", "{}", result.Output);
			return result;
		}

		const std::string msbuild = GetMSBuildPath();
		if (msbuild.empty()) {
			result.Output = "Visual Studio 2026 MSBuild was not found.";
			return result;
		}

		FNK_INFO_TAG("FunkeProject", "Building Funke.sln (Configuration={}, Platform={}) via {}...",
			configuration, platform, msbuild);

		Process::Result processResult = Process::Run({
			msbuild,
			Path::ToGenericUtf8(solutionPath),
			"-p:Configuration=" + configuration,
			"-p:Platform=" + platform,
			"-m",
			"-verbosity:minimal",
			"-nologo"
		}, Path::FromUtf8(engineRoot), std::chrono::minutes(30));

		result.ExitCode = processResult.ExitCode;
		result.Succeeded = processResult.Succeeded();
		result.Output = std::move(processResult.Output);

		if (!result.Succeeded) {
			FNK_ERROR_TAG("FunkeProject", "MSBuild failed (exit code {}): {}", result.ExitCode, result.Output);
		}
		else {
			FNK_INFO_TAG("FunkeProject", "MSBuild succeeded.");
		}
		return result;
	}

	FunkeProject::BuildResult FunkeProject::BuildSolutionTargets(const std::vector<std::string>& targets,
		const std::string& configuration, const std::string& platform) {
		BuildResult result;

		// Empty target list = success no-op. A project with no local packages skips MSBuild entirely.
		if (targets.empty()) {
			result.Succeeded = true;
			result.ExitCode = 0;
			result.Output = "No targets to build.";
			return result;
		}
		const auto installation = GetEngineInstallation();
		if (!installation.IsValid() || !installation.SourceBuild) {
			result.Output = installation.IsValid() ? "This binary Funke installation cannot build engine targets. Use a source checkout." : installation.Error;
			return result;
		}

		const std::string engineRoot = GetEngineRootDir();
		if (engineRoot.empty()) {
			result.Output = "Could not resolve engine root directory; skipping build.";
			FNK_WARN_TAG("FunkeProject", "{}", result.Output);
			return result;
		}

		const std::filesystem::path solutionPath = Path::FromUtf8(engineRoot) / "Funke.sln";
		std::error_code ec;
		if (!std::filesystem::exists(solutionPath, ec) || ec) {
			result.Output = "Funke.sln not found at " + Path::ToGenericUtf8(solutionPath);
			FNK_WARN_TAG("FunkeProject", "{}", result.Output);
			return result;
		}

		// MSBuild solution targets: dots in project name must be '_'; ":Build" suffix required or MSBuild
		// treats the name as a bare target on every project (MSB4057 on all of them).
		auto escapeTarget = [](const std::string& t) {
			std::string out = t;
			std::replace(out.begin(), out.end(), '.', '_');
			return out + ":Build";
		};
		std::string targetsArg = "-t:";
		for (size_t i = 0; i < targets.size(); ++i) {
			if (i > 0) targetsArg += ";";
			targetsArg += escapeTarget(targets[i]);
		}

		const std::string msbuild = GetMSBuildPath();
		if (msbuild.empty()) {
			result.Output = "Visual Studio 2026 MSBuild was not found.";
			return result;
		}

		FNK_INFO_TAG("FunkeProject", "Building {} target(s) in Funke.sln (Configuration={}, Platform={}): {}",
			targets.size(), configuration, platform, targetsArg);

		Process::Result processResult = Process::Run({
			msbuild,
			Path::ToGenericUtf8(solutionPath),
			"-p:Configuration=" + configuration,
			"-p:Platform=" + platform,
			targetsArg,
			"-m:1",
			"-nr:false",
			"-p:CL_MPCount=1",
			"-p:UseMultiToolTask=false",
			"-verbosity:minimal",
			"-nologo"
		}, Path::FromUtf8(engineRoot), std::chrono::minutes(30));

		result.ExitCode = processResult.ExitCode;
		result.Succeeded = processResult.Succeeded();
		result.Output = std::move(processResult.Output);

		if (!result.Succeeded) {
			FNK_ERROR_TAG("FunkeProject", "MSBuild failed (exit code {}): {}", result.ExitCode, result.Output);
		}
		else {
			FNK_INFO_TAG("FunkeProject", "MSBuild target build succeeded.");
		}
		return result;
	}

	FunkeProject::RegenerateResult FunkeProject::RegeneratePackageWorkspaceForProject(
		const std::string& projectRootDir) {
		RegenerateResult result;
		if (projectRootDir.empty()) {
			result.Output = "Project root is empty; package workspace cannot be generated.";
			return result;
		}

		const auto installation = GetEngineInstallation();
		if (!installation.IsValid()) {
			result.Output = installation.Error;
			return result;
		}
		const std::string engineRoot = Path::ToGenericUtf8(installation.Root);

		const std::filesystem::path generator =
			Path::FromUtf8(engineRoot) / "premake" / "project-packages.lua";
		std::error_code ec;
		if (!std::filesystem::is_regular_file(generator, ec) || ec) {
			result.Output = "Project package generator not found at " + Path::ToGenericUtf8(generator);
			return result;
		}

		const std::string premakePath = GetPremakePath();
		if (premakePath.empty()) {
			result.Output = "premake5 is required to generate project-local package builds.";
			return result;
		}

		const std::filesystem::path projectFile =
			Path::FromUtf8(projectRootDir) / "project.json";
		if (!std::filesystem::is_regular_file(projectFile, ec) || ec) {
			result.Output = "Project package generation requires " + Path::ToGenericUtf8(projectFile);
			return result;
		}

		const std::string projectArgument =
			"--funke-project=" + Path::ToGenericUtf8(std::filesystem::absolute(Path::FromUtf8(projectRootDir)));
#if defined(_WIN32)
		const std::vector<std::vector<std::string>> actions = { { "vs2022" } };
#else
		// vs2022 still emits the .csproj files dotnet builds C# packages from; gmake builds the native layers.
	#if defined(__APPLE__)
		const std::string hostSystem = "--os=macosx";
	#else
		const std::string hostSystem = "--os=linux";
	#endif
		const std::vector<std::vector<std::string>> actions = { { hostSystem, "vs2022" }, { "gmake" } };
#endif
		for (const std::vector<std::string>& action : actions) {
			std::vector<std::string> command{ premakePath, "--file=" + Path::ToGenericUtf8(generator) };
			command.insert(command.end(), action.begin(), action.end());
			command.push_back(projectArgument);
			Process::Result processResult = Process::Run(command, Path::FromUtf8(engineRoot), std::chrono::minutes(2));

			result.ExitCode = processResult.ExitCode;
			result.Succeeded = processResult.Succeeded();
			if (!result.Output.empty()) result.Output += "\n";
			result.Output += processResult.Output;
			if (!result.Succeeded) {
				FNK_ERROR_TAG("FunkeProject", "Project package generation failed (exit code {}): {}",
					result.ExitCode, result.Output);
				return result;
			}
		}
		return result;
	}

	FunkeProject::BuildResult FunkeProject::BuildProjectPackages(const std::string& projectRootDir,
		const std::string& configuration, const std::string& platform, const CreateProgressCallback& progressCallback) {
		BuildResult result;
		if (!IsSafeBuildPathToken(configuration, false) || !IsSafeBuildPathToken(platform, false)) {
			result.Output = "Invalid package build configuration or platform.";
			return result;
		}

		const std::filesystem::path workspaceRoot =
			Path::FromUtf8(projectRootDir) / ".funke" / "build" / "packages";
		const std::filesystem::path generatedRoot = workspaceRoot / "generated";
		std::error_code ec;
		if (!std::filesystem::is_directory(generatedRoot, ec) || ec) {
			result.Output = "Generated project package directory not found at " + Path::ToGenericUtf8(generatedRoot);
			return result;
		}

		std::vector<std::filesystem::path> nativeProjects;
		std::vector<std::filesystem::path> managedProjects;
		for (std::filesystem::recursive_directory_iterator it(
				generatedRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
			it != end; it.increment(ec)) {
			if (ec) {
				result.Output = "Could not enumerate generated package projects: " + ec.message();
				return result;
			}
			if (!it->is_regular_file(ec) || ec) {
				ec.clear();
				continue;
			}
			if (it->path().extension() == ".vcxproj") {
				// Premake leaves its empty-workspace utility behind after the first package is added.
				if (it->path() != generatedRoot / "FunkeProjectPackages.Empty" / "FunkeProjectPackages.Empty.vcxproj")
					nativeProjects.push_back(it->path());
			}
			else if (it->path().extension() == ".csproj") managedProjects.push_back(it->path());
		}
		std::sort(nativeProjects.begin(), nativeProjects.end());
		std::sort(managedProjects.begin(), managedProjects.end());

		const auto appendOutput = [&](const std::filesystem::path& projectPath,
			const Process::Result& processResult) {
			if (!result.Output.empty()) result.Output += "\n";
			result.Output += "[" + Path::ToUtf8(projectPath.filename()) + "]\n" + processResult.Output;
		};
		// Generated projects are named Pkg.<Package>.Native / Pkg.<Package>.
		const size_t totalSteps = std::max<size_t>(1, nativeProjects.size() + managedProjects.size());
		size_t stepsDone = 0;
		const auto reportStep = [&](const std::filesystem::path& projectPath, std::string_view layer) {
			std::string name = Path::ToUtf8(projectPath.stem());
			if (name.rfind("Pkg.", 0) == 0) name.erase(0, 4);
			if (const size_t native = name.rfind(".Native"); native != std::string::npos && native + 7 == name.size()) name.erase(native);
			ReportCreateProgress(progressCallback, static_cast<float>(stepsDone) / static_cast<float>(totalSteps),
				"Building " + name + " (" + std::string(layer) + ")...");
		};

		if (!nativeProjects.empty()) {
			const auto installation = GetEngineInstallation();
			if (!installation.IsValid() || !installation.NativeDevelopment) {
				result.Output = installation.IsValid()
					? "This Funke installation cannot compile native packages. Install the matching Native SDK component."
					: installation.Error;
				return result;
			}
#if defined(_WIN32)
			const std::string msbuild = GetMSBuildPath();
			if (msbuild.empty()) {
				result.Output = "MSBuild was not found.";
				return result;
			}
			for (const auto& projectPath : nativeProjects) {
				reportStep(projectPath, "C++");
				const Process::Result processResult = Process::Run({
					msbuild, Path::ToGenericUtf8(projectPath),
					"-p:Configuration=" + configuration,
					"-p:Platform=" + platform,
					"-p:PreferredToolArchitecture=x64",
					"-m:1", "-nr:false",
					"-p:CL_MPCount=1", "-p:UseMultiToolTask=false",
					"-verbosity:minimal", "-nologo"
				}, workspaceRoot, std::chrono::minutes(10));
				appendOutput(projectPath, processResult);
				if (!processResult.Succeeded()) {
					result.ExitCode = processResult.ExitCode;
					FNK_ERROR_TAG("FunkeProject", "Native project package build failed: {}", result.Output);
					return result;
				}
				++stepsDone;
			}
#else
			// The gmake workspace from RegeneratePackageWorkspaceForProject; targets share the .vcxproj names.
			std::vector<std::string> command{ GetMakeProgram(), "-C", Path::ToUtf8(workspaceRoot),
				"config=" + ToLowerAscii(configuration),
				"-j" + std::to_string(std::max(1u, std::thread::hardware_concurrency())) };
			for (const auto& projectPath : nativeProjects) command.push_back(Path::ToUtf8(projectPath.stem()));
			ReportCreateProgress(progressCallback, 0.0f, "Building native package layers...");
			const Process::Result processResult = Process::Run(command, workspaceRoot, std::chrono::minutes(20));
			appendOutput(workspaceRoot / "Makefile", processResult);
			if (!processResult.Succeeded()) {
				result.ExitCode = processResult.ExitCode;
				FNK_ERROR_TAG("FunkeProject", "Native project package build failed: {}", result.Output);
				return result;
			}
			stepsDone += nativeProjects.size();
#endif
		}

		for (const auto& projectPath : managedProjects) {
			reportStep(projectPath, "C#");
			std::vector<std::string> command{ "dotnet", "build", Path::ToGenericUtf8(projectPath), "-c", configuration };
			for (std::string& argument : ManagedPlatformArguments(platform)) command.push_back(std::move(argument));
			command.insert(command.end(), { "--nologo", "-v", "minimal", "-nodeReuse:false", "-p:UseSharedCompilation=false" });
			const Process::Result processResult = Process::Run(command, workspaceRoot, std::chrono::minutes(10));
			appendOutput(projectPath, processResult);
			if (!processResult.Succeeded()) {
				result.ExitCode = processResult.ExitCode;
				FNK_ERROR_TAG("FunkeProject", "Managed project package build failed: {}", result.Output);
				return result;
			}
			++stepsDone;
		}

		ReportCreateProgress(progressCallback, 1.0f, "Package build complete");
		result.ExitCode = 0;
		result.Succeeded = true;
		return result;
	}

	std::string FunkeProject::GetProjectPackageOutputDirectory(const std::string& projectRootDir,
		const std::string& packageTarget, const std::string& configuration) {
		const std::string platformDirectory = GetNativeBuildPlatformDirectory();
		if (projectRootDir.empty() || platformDirectory.empty()
			|| !IsSafeBuildPathToken(packageTarget, true)
			|| !IsSafeBuildPathToken(configuration, false)) {
			return {};
		}

		return Path::ToGenericUtf8(Path::FromUtf8(projectRootDir)
			/ ".funke" / "build" / "packages" / "bin"
			/ (configuration + "-" + platformDirectory)
			/ packageTarget);
	}

	std::string FunkeProject::GetProjectNativePackageLibraryPath(const std::string& projectRootDir,
		const std::string& packageName, const std::string& configuration) {
		if (!IsSafeBuildPathToken(packageName, true)) return {};
		const std::string targetName = "Pkg." + packageName + ".Native";
		const std::string outputDirectory =
			GetProjectPackageOutputDirectory(projectRootDir, targetName, configuration);
		if (outputDirectory.empty()) return {};
		return Path::ToGenericUtf8(Path::FromUtf8(outputDirectory)
			/ GetSharedLibraryFilename(targetName));
	}

	std::vector<std::string> FunkeProject::EnumerateProjectLocalPackages(const std::string& projectRoot) {
		// Returns directory names under <projectRoot>/Packages containing an funke-package.lua.
		std::vector<std::string> names;
		if (projectRoot.empty()) return names;

		const std::filesystem::path packagesDir = Path::FromUtf8(projectRoot) / "Packages";
		std::error_code ec;
		if (!std::filesystem::exists(packagesDir, ec) || ec) return names;
		if (!std::filesystem::is_directory(packagesDir, ec) || ec) return names;

		for (const auto& entry : std::filesystem::directory_iterator(packagesDir, ec)) {
			if (ec) break;
			if (!entry.is_directory(ec) || ec) continue;
			const std::filesystem::path manifest = entry.path() / "funke-package.lua";
			if (std::filesystem::exists(manifest, ec) && !ec) {
				names.push_back(Path::ToUtf8(entry.path().filename()));
			}
		}

		return names;
	}

	FunkeProject::AutomateResult FunkeProject::AutomateForProject(const std::string& projectRootDir,
		const std::string& configuration, const std::string& platform, const CreateProgressCallback& progressCallback) {
		AutomateResult result;
		ReportCreateProgress(progressCallback, 0.05f, "Generating package workspace...");
		result.Regenerate = RegeneratePackageWorkspaceForProject(projectRootDir);

		if (!result.Regenerate.Succeeded) return result;

		// Workspace generation ~15%, the package builds the rest.
		CreateProgressCallback buildProgress;
		if (progressCallback) {
			buildProgress = [&progressCallback](float progress, std::string_view stage) {
				progressCallback(0.15f + 0.85f * progress, stage);
			};
		}
		result.Build = BuildProjectPackages(projectRootDir, configuration, platform, buildProgress);
		result.RanBuild = true;
		return result;
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

} // namespace Funke
