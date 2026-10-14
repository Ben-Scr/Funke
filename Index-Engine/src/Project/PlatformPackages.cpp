#include "pch.hpp"
#include "Project/PlatformPackages.hpp"

#include "Core/Platform.hpp"
#include "Project/IndexProject.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>

namespace Index::PlatformPackages {

	namespace {
		std::mutex g_Mutex;
		std::vector<PlatformPackageInfo> g_Cache;
		bool g_Scanned = false;

		std::string ReadStringField(const std::string& content, const char* field) {
			const std::regex pattern(std::string(R"((^|[\s,{])\b)") + field + R"(\s*=\s*["']([^"'\r\n]*)["'])");
			std::smatch match;
			return std::regex_search(content, match, pattern) ? match[2].str() : std::string();
		}

		void AppendPathList(std::vector<std::filesystem::path>& roots, const char* value) {
			if (!value || !*value) return;
#if defined(IDX_PLATFORM_WINDOWS)
			constexpr char k_Separator = ';';
#else
			constexpr char k_Separator = ':';
#endif
			std::stringstream stream(value);
			std::string item;
			while (std::getline(stream, item, k_Separator)) {
				if (!item.empty()) roots.push_back(Path::FromUtf8(item));
			}
		}
	}

	std::filesystem::path GetUserInstallRoot() {
		const std::filesystem::path base = Platform::GetUserDataRoot();
		return base.empty() ? std::filesystem::path{} : base / "Index" / "Platforms";
	}

	std::vector<std::filesystem::path> GetSearchRoots() {
		std::vector<std::filesystem::path> roots;
		AppendPathList(roots, std::getenv("INDEX_PLATFORM_PACKAGES"));
		if (std::filesystem::path user = GetUserInstallRoot(); !user.empty()) roots.push_back(user);
		if (const std::string engine = IndexProject::GetEngineRootDir(); !engine.empty()) {
			roots.push_back(Path::FromUtf8(engine) / "platforms");
		}
		return roots;
	}

	std::optional<PlatformPackageInfo> ReadManifest(const std::filesystem::path& packageDirectory) {
		std::ifstream stream(packageDirectory / "index-package.lua", std::ios::binary);
		if (!stream) return std::nullopt;
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		const std::string content = buffer.str();

		PlatformPackageInfo info;
		info.Name = ReadStringField(content, "name");
		info.Version = ReadStringField(content, "version");
		info.Kind = ReadStringField(content, "kind");
		info.Description = ReadStringField(content, "description");
		info.Hosts = ReadStringField(content, "hosts");
		info.Directory = packageDirectory;
		if (info.Name.empty() || info.Version.empty()) return std::nullopt;
		if (info.Kind != "platform" && info.Kind != "build-component") return std::nullopt;

		const std::string protocol = ReadStringField(content, "protocol");
		info.Protocol = protocol.empty() ? 0 : std::atoi(protocol.c_str());
		if (const std::string builder = ReadStringField(content, "builder"); !builder.empty()) {
			info.Builder = packageDirectory / Path::FromUtf8(builder);
		}
		if (info.Kind == "platform") {
			const std::string platform = ReadStringField(content, "platform");
			if (platform.empty() || info.Builder.empty()) return std::nullopt;
			info.Platform = IndexBuildProfile::PlatformFromString(platform);
		}
		return info;
	}

	const std::vector<PlatformPackageInfo>& Discover(bool refresh) {
		std::scoped_lock lock(g_Mutex);
		if (g_Scanned && !refresh) return g_Cache;
		g_Cache.clear();
		for (const std::filesystem::path& root : GetSearchRoots()) {
			std::error_code ec;
			if (!std::filesystem::is_directory(root, ec)) continue;
			for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
				if (!entry.is_directory(ec)) continue;
				std::optional<PlatformPackageInfo> info = ReadManifest(entry.path());
				if (!info) continue;
				const bool duplicate = std::any_of(g_Cache.begin(), g_Cache.end(),
					[&](const PlatformPackageInfo& existing) { return existing.Name == info->Name; });
				if (duplicate) continue;  // earlier roots override later ones
				g_Cache.push_back(std::move(*info));
			}
		}
		g_Scanned = true;
		return g_Cache;
	}

	std::optional<PlatformPackageInfo> FindForPlatform(BuildPlatform platform) {
		for (const PlatformPackageInfo& info : Discover()) {
			if (info.Kind == "platform" && info.Platform == platform && info.Protocol == k_SupportedProtocol) return info;
		}
		return std::nullopt;
	}

	const char* CurrentHostName() {
#if defined(IDX_PLATFORM_WINDOWS)
		return "windows";
#elif defined(IDX_PLATFORM_MACOS)
		return "macos";
#else
		return "linux";
#endif
	}

	std::optional<PlatformPackageInfo> FindByName(const std::string& name) {
		for (const PlatformPackageInfo& info : Discover()) {
			if (info.Name == name) return info;
		}
		return std::nullopt;
	}

}
