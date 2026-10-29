#pragma once

#include "Core/PlatformDetection.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace Funke::EngineSdk {

	using RuntimePaths = std::map<std::string, std::filesystem::path>;

	struct Installation {
		std::filesystem::path Root;
		bool ManifestPresent = false;
		bool ManagedProjects = false;
		bool NativeDevelopment = false;
		bool SourceBuild = false;
		std::string EngineVersion;
		std::string Platform;
		std::string Architecture;
		RuntimePaths Runtimes;
		std::map<std::string, RuntimePaths> RuntimeVariants;
		std::string Error;

		bool IsValid() const { return !Root.empty() && Error.empty(); }
	};

	inline std::string_view HostPlatform() {
#if defined(FNK_PLATFORM_WINDOWS)
		return "windows";
#elif defined(FNK_PLATFORM_MACOS)
		return "macosx";
#elif defined(FNK_PLATFORM_LINUX)
		return "linux";
#else
		return "unsupported";
#endif
	}

	inline std::string_view HostArchitecture() {
#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
		return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
		return "x86_64";
#else
		return "unsupported";
#endif
	}

	inline bool IsSourceRoot(const std::filesystem::path& root) {
		std::error_code ec;
		for (const char* file : { "premake5.lua", "Funke-Engine/premake5.lua",
			"Funke-Engine/src/Core/Application.cpp", "Funke-ScriptCore/Funke-ScriptCore.csproj" }) {
			if (!std::filesystem::is_regular_file(root / file, ec) || ec) return false;
		}
		return std::filesystem::is_directory(root / "Funke-ScriptCore/Source", ec) && !ec;
	}

	inline bool ResolveDirectory(const std::filesystem::path& root, std::string_view relative,
		std::filesystem::path& resolved) {
		if (relative.empty() || relative.find_first_of(":\\") != std::string_view::npos
			|| relative.find('\0') != std::string_view::npos) return false;
		const auto path = Path::FromUtf8(std::string(relative));
		if (path.is_absolute() || path.has_root_path()) return false;
		for (const auto& part : path) {
			if (part == ".." || part == ".") return false;
		}
		std::error_code ec;
		resolved = std::filesystem::weakly_canonical(root / path, ec);
		if (ec) return false;
		auto child = resolved.begin();
		for (const auto& part : root) {
			if (child == resolved.end() || *child++ != part) return false;
		}
		return child != resolved.end() && std::filesystem::is_directory(resolved, ec) && !ec;
	}

	inline bool ReadRuntimePaths(const Json::Value& value, const std::filesystem::path& root,
		RuntimePaths& paths, std::string& error, bool requireProfiles = true) {
		if (!value.IsObject()) { error = "Runtime profiles must be an object."; return false; }
		for (const auto& [profile, path] : value.GetObject()) {
			if (profile != "Development" && profile != "Release" && profile != "Debug") {
				error = "Unknown runtime profile: " + profile;
				return false;
			}
			std::filesystem::path resolved;
			if (!path.IsString() || !ResolveDirectory(root, path.AsStringOr(), resolved)) {
				error = "Runtime " + profile + " must name an existing directory inside the SDK.";
				return false;
			}
			paths.emplace(profile, std::move(resolved));
		}
		if (requireProfiles && paths.empty()) {
			error = "The SDK must provide at least one runtime profile.";
			return false;
		}
		return true;
	}

	inline Installation ReadManifest(const std::filesystem::path& root,
		std::string_view expectedPlatform = HostPlatform(), std::string_view expectedArchitecture = HostArchitecture(),
		std::string_view expectedVersion = {}) {
		Installation result;
		result.ManifestPresent = true;
		std::error_code ec;
		result.Root = std::filesystem::weakly_canonical(root, ec);
		auto fail = [&](std::string reason) {
			result.Error = "Invalid Funke SDK at " + Path::ToUtf8(root) + ": " + reason;
			return result;
		};
		if (ec) return fail("Could not resolve its directory.");
		const auto manifest = result.Root / "FunkeSDK.json";
		const auto size = std::filesystem::file_size(manifest, ec);
		if (ec || size == 0 || size > 65536) return fail("FunkeSDK.json is missing, unreadable or too large.");
		std::ifstream stream(manifest, std::ios::binary);
		std::string text(static_cast<size_t>(size), '\0');
		if (!stream.read(text.data(), static_cast<std::streamsize>(text.size()))) return fail("Could not read FunkeSDK.json.");
		Json::Value document;
		std::string parseError;
		if (!Json::TryParse(text, document, &parseError) || !document.IsObject()) return fail("Malformed FunkeSDK.json: " + parseError);
		const auto* schema = document.FindMember("schemaVersion");
		if (!schema || !schema->IsNumber() || schema->AsDoubleOr(0) != 1) return fail("Unsupported schemaVersion; expected 1.");
		auto readString = [&](std::string_view key, std::string& target) {
			const auto* value = document.FindMember(key);
			if (!value || !value->IsString()) return false;
			target = value->AsStringOr();
			return !target.empty() && target.find_first_not_of(" \t\r\n") != std::string::npos;
		};
		if (!readString("engineVersion", result.EngineVersion)) return fail("engineVersion is required.");
		if (!expectedVersion.empty() && result.EngineVersion != expectedVersion) return fail("SDK engineVersion does not match this application.");
		if (!readString("platform", result.Platform) || result.Platform != expectedPlatform) return fail("SDK platform does not match this application.");
		if (!readString("architecture", result.Architecture) || result.Architecture != expectedArchitecture) return fail("SDK architecture does not match this application.");
		const auto* capabilities = document.FindMember("capabilities");
		if (!capabilities || !capabilities->IsObject()) return fail("capabilities must be an object.");
		auto readCapability = [&](std::string_view key, bool& target) {
			const auto* value = capabilities->FindMember(key);
			if (!value || !value->IsBool()) return false;
			target = value->AsBoolOr(false);
			return true;
		};
		if (!readCapability("managedProjects", result.ManagedProjects)
			|| !readCapability("nativeDevelopment", result.NativeDevelopment)
			|| !readCapability("sourceBuild", result.SourceBuild)) return fail("All SDK capabilities must be booleans.");
		if (result.SourceBuild && !IsSourceRoot(result.Root)) return fail("sourceBuild requires a complete engine source tree.");
		if (result.ManagedProjects) {
			for (const char* file : { "Funke-ScriptCore.dll", "Funke-ScriptCore.deps.json", "Funke-ScriptCore.runtimeconfig.json" }) {
				if (!std::filesystem::is_regular_file(result.Root / "Funke-ScriptCore" / file, ec) || ec)
					return fail(std::string("Missing compiled scripting artifact: ") + file);
			}
		}
		const auto* runtimes = document.FindMember("runtimes");
		std::string runtimeError;
		if (!runtimes || !ReadRuntimePaths(*runtimes, result.Root, result.Runtimes, runtimeError))
			return fail(runtimes ? runtimeError : "runtimes is required.");
		if (const auto* variants = document.FindMember("runtimeVariants")) {
			if (!variants->IsObject()) return fail("runtimeVariants must be an object.");
			for (const auto& [id, profiles] : variants->GetObject()) {
				if (id != "2d" && id != "3d" && id != "nomodules") return fail("Unknown runtime variant: " + id);
				if (!ReadRuntimePaths(profiles, result.Root, result.RuntimeVariants[id], runtimeError, false)) return fail(id + ": " + runtimeError);
			}
		}
		return result;
	}

	inline Installation Resolve(const std::filesystem::path& executableDirectory,
		std::string_view expectedPlatform = HostPlatform(), std::string_view expectedArchitecture = HostArchitecture(),
		std::string_view expectedVersion = {}) {
		const auto sdk = executableDirectory / "EngineSDK";
		std::error_code ec;
		const auto status = std::filesystem::symlink_status(sdk, ec);
		// A broken installation must not borrow compatible-looking files from a nearby checkout.
		if (std::filesystem::exists(status) || (ec && ec != std::errc::no_such_file_or_directory))
			return ReadManifest(sdk, expectedPlatform, expectedArchitecture, expectedVersion);
		for (const auto& candidate : { executableDirectory / "../../..", executableDirectory / ".." }) {
			const auto root = std::filesystem::weakly_canonical(candidate, ec);
			if (ec || !IsSourceRoot(root)) continue;
			Installation result;
			result.Root = root;
			result.ManagedProjects = result.NativeDevelopment = result.SourceBuild = true;
			result.Platform = expectedPlatform;
			result.Architecture = expectedArchitecture;
			return result;
		}
		Installation result;
		result.Error = "No Funke SDK manifest or engine source checkout was found beside this application.";
		return result;
	}
}
