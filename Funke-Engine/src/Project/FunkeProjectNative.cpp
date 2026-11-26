#include "pch.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/FunkeProjectInternal.hpp"
#include "Project/NativeScriptSnippets.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace Funke {
	using namespace ProjectInternal;

	namespace {
		std::string MakeRelativePathOrEmpty(const std::filesystem::path& from, const std::filesystem::path& to) {
			std::error_code ec;
			const std::filesystem::path relative = std::filesystem::relative(to, from, ec);
			if (ec) {
				return {};
			}

			return Path::ToGenericUtf8(relative);
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
option(FUNKE_WITH_PHYSICS2D "Match the linked engine's Physics2D module" ${FUNKE_WITH_2D})
option(FUNKE_WITH_PHYSICS3D "Match the linked engine's Physics3D module" ${FUNKE_WITH_3D})
if(NOT DEFINED FUNKE_NATIVE_WITH_EDITOR)
    set(FUNKE_NATIVE_WITH_EDITOR "$<NOT:$<CONFIG:Dist>>")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    FNK_IMPORT_DLL
    FUNKE_ALL_MODULES=1
    FUNKE_WITH_2D=$<BOOL:${FUNKE_WITH_2D}>
    FUNKE_WITH_3D=$<BOOL:${FUNKE_WITH_3D}>
    FUNKE_WITH_PHYSICS2D=$<BOOL:${FUNKE_WITH_PHYSICS2D}>
    FUNKE_WITH_PHYSICS3D=$<BOOL:${FUNKE_WITH_PHYSICS3D}>
    FUNKE_WITH_EDITOR=${FUNKE_NATIVE_WITH_EDITOR})");
			replace("target_link_directories(${PROJECT_NAME} PRIVATE\n", R"(if(FUNKE_ENGINE_LIBRARY_DIR)
    target_link_directories(${PROJECT_NAME} BEFORE PRIVATE "${FUNKE_ENGINE_LIBRARY_DIR}")
endif()
target_link_directories(${PROJECT_NAME} PRIVATE
)");
			replace("    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)", R"(    include(ProcessorCount)
    ProcessorCount(FUNKE_NATIVE_PROCESSORS)
    if(FUNKE_NATIVE_PROCESSORS LESS 1)
        set(FUNKE_NATIVE_PROCESSORS 1)
    elseif(FUNKE_NATIVE_PROCESSORS GREATER 12)
        set(FUNKE_NATIVE_PROCESSORS 12)
    endif()
    set(FUNKE_NATIVE_COMPILE_JOBS "${FUNKE_NATIVE_PROCESSORS}" CACHE STRING "Maximum concurrent native script compiler processes")
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /MP${FUNKE_NATIVE_COMPILE_JOBS})
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"))");
			output += R"(
# Opt in only when scripts do not configure SDK headers with per-source macros.
option(FUNKE_NATIVE_USE_PCH "Precompile the common native script SDK header" OFF)
if(FUNKE_NATIVE_USE_PCH)
    target_precompile_headers(${PROJECT_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${FUNKE_DIR}/Funke-Engine/src/Funke/Scripting.hpp>")
endif()
)";
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

	void FunkeProject::EnsureNativeScriptBootstrapFiles() const {
		EnsureNativeScriptExportsFile(*this);
	}

	void FunkeProject::EnsureNativeScriptProjectFiles() const {
		EnsureNativeScriptCMakeListsFile(*this);
		EnsureNativeScriptExportsFile(*this);
		if (RootDirectory.empty()) return;
		const auto snippets = Path::FromUtf8(RootDirectory) / ".vscode" / "funke-cpp.code-snippets";
		std::error_code error;
		if (std::filesystem::exists(snippets, error) || error) return;
		std::filesystem::create_directories(snippets.parent_path(), error);
		if (error) {
			FNK_CORE_WARN_TAG("NativeScripts", "Could not create snippets directory: {}", error.message());
			return;
		}
		WriteNativeScriptFileIfChanged(snippets, k_NativeScriptSnippets);
	}

	bool FunkeProject::HasNativeScriptSources() const {
		return HasNativeScriptSourceFiles(Path::FromUtf8(NativeSourceDir))
			|| HasNativeScriptSourceFiles(Path::FromUtf8(AssetsDirectory));
	}
}
