#include "Build/DotNetRuntimeBundle.hpp"
#include "Core/PlatformDetection.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <sstream>

namespace Funke::DotNetRuntimeBundle {
	namespace {
		bool ParseVersion(std::string_view text, std::array<unsigned, 3>& parts) {
			for (size_t i = 0; i < parts.size(); ++i) {
				const size_t end = text.find('.');
				const auto part = text.substr(0, end);
				const auto parsed = std::from_chars(part.data(), part.data() + part.size(), parts[i]);
				if (part.empty() || parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size()) return false;
				if (i == parts.size() - 1) return end == std::string_view::npos;
				if (end == std::string_view::npos) return false;
				text.remove_prefix(end + 1);
			}
			return false;
		}

		const char* HostFxrFilename() {
#if defined(FNK_PLATFORM_WINDOWS)
			return "hostfxr.dll";
#elif defined(FNK_PLATFORM_MACOS)
			return "libhostfxr.dylib";
#else
			return "libhostfxr.so";
#endif
		}

		const char* CoreClrFilename() {
#if defined(FNK_PLATFORM_WINDOWS)
			return "coreclr.dll";
#elif defined(FNK_PLATFORM_MACOS)
			return "libcoreclr.dylib";
#else
			return "libcoreclr.so";
#endif
		}

		const char* HostPolicyFilename() {
#if defined(FNK_PLATFORM_WINDOWS)
			return "hostpolicy.dll";
#elif defined(FNK_PLATFORM_MACOS)
			return "libhostpolicy.dylib";
#else
			return "libhostpolicy.so";
#endif
		}

		bool MatchesArchitecture(const std::filesystem::path& path) {
			std::ifstream input(path, std::ios::binary);
			std::array<unsigned char, 64> header{};
			if (!input.read(reinterpret_cast<char*>(header.data()), header.size())) return false;
			const auto u16 = [](const unsigned char* p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); };
			const auto u32 = [&](const unsigned char* p) { return u16(p) | (u16(p + 2) << 16); };
#if defined(FNK_PLATFORM_WINDOWS)
			if (header[0] != 'M' || header[1] != 'Z') return false;
			input.seekg(u32(header.data() + 60));
			std::array<unsigned char, 6> pe{};
			if (!input.read(reinterpret_cast<char*>(pe.data()), pe.size())
				|| u32(pe.data()) != 0x4550) return false;
#if defined(_M_ARM64)
			return u16(pe.data() + 4) == 0xaa64;
#else
			return u16(pe.data() + 4) == 0x8664;
#endif
#elif defined(FNK_PLATFORM_MACOS)
			if (u32(header.data()) != 0xfeedfacf) return false;
#if defined(__aarch64__)
			return u32(header.data() + 4) == 0x0100000c;
#else
			return u32(header.data() + 4) == 0x01000007;
#endif
#else
			if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F'
				|| header[4] != 2 || header[5] != 1) return false;
#if defined(__aarch64__)
			return u16(header.data() + 18) == 183;
#else
			return u16(header.data() + 18) == 62;
#endif
#endif
		}
	}

	bool SelectRuntime(std::string_view installedRuntimes, std::string_view requiredVersion,
		Runtime& runtime, std::string& error) {
		runtime = {};
		error.clear();
		std::array<unsigned, 3> required{}, best{};
		if (!ParseVersion(requiredVersion, required)) {
			error = "Unsupported .NET runtime version: " + std::string(requiredVersion);
			return false;
		}
		std::istringstream lines{ std::string(installedRuntimes) };
		std::string line;
		while (std::getline(lines, line)) {
			std::istringstream fields(line);
			std::string name, version;
			fields >> name >> version;
			if (name != "Microsoft.NETCore.App") continue;
			std::array<unsigned, 3> candidate{};
			if (!ParseVersion(version, candidate) || candidate[0] != required[0]
				|| candidate[1] != required[1] || candidate[2] < required[2]
				|| (!runtime.Version.empty() && candidate <= best)) continue;
			const size_t begin = line.find('['), end = line.rfind(']');
			if (begin == std::string::npos || end == std::string::npos || end <= begin + 1) continue;
			const auto frameworkRoot = Path::FromUtf8(line.substr(begin + 1, end - begin - 1));
			if (!frameworkRoot.is_absolute() || frameworkRoot.filename() != "Microsoft.NETCore.App"
				|| frameworkRoot.parent_path().filename() != "shared") continue;
			best = candidate;
			runtime = { version, frameworkRoot.parent_path().parent_path(), frameworkRoot / version };
		}
		if (!runtime.Version.empty()) return true;
		error = "Install the .NET " + std::to_string(required[0]) + "." + std::to_string(required[1])
			+ " runtime for this editor's architecture, or disable Include .NET Runtime in the build profile.";
		return false;
	}

	bool Stage(const Runtime& runtime, const std::filesystem::path& destination, std::string& error) {
		error.clear();
		try {
			std::array<unsigned, 3> required{}, latestHost{};
			if (!ParseVersion(runtime.Version, required)) {
				error = "Invalid runtime version.";
				return false;
			}
			std::filesystem::path hostDirectory;
			const auto hostRoot = runtime.Root / "host" / "fxr";
			if (std::filesystem::is_directory(hostRoot)) {
				for (const auto& entry : std::filesystem::directory_iterator(hostRoot)) {
					std::array<unsigned, 3> version{};
					if (!entry.is_directory() || !ParseVersion(entry.path().filename().string(), version)
						|| version[0] != required[0] || version[1] != required[1]
						|| (!hostDirectory.empty() && version <= latestHost)) continue;
					hostDirectory = entry.path();
					latestHost = version;
				}
			}
			if (hostDirectory.empty()) {
				error = "The .NET installation has no compatible hostfxr in " + Path::ToUtf8(hostRoot);
				return false;
			}
			for (const auto& file : { runtime.FrameworkDirectory / CoreClrFilename(),
				runtime.FrameworkDirectory / HostPolicyFilename(), runtime.FrameworkDirectory / "Microsoft.NETCore.App.deps.json",
				runtime.FrameworkDirectory / "System.Private.CoreLib.dll", hostDirectory / HostFxrFilename(),
				runtime.Root / "LICENSE.txt", runtime.Root / "ThirdPartyNotices.txt" }) {
				if (!std::filesystem::is_regular_file(file)) {
					error = "Incomplete .NET runtime installation: " + Path::ToUtf8(file);
					return false;
				}
			}
			if (!MatchesArchitecture(runtime.FrameworkDirectory / CoreClrFilename())
				|| !MatchesArchitecture(hostDirectory / HostFxrFilename())) {
				error = "The installed .NET runtime does not match the editor's architecture.";
				return false;
			}
			std::filesystem::create_directories(destination / "shared" / "Microsoft.NETCore.App");
			std::filesystem::create_directories(destination / "host" / "fxr");
			std::filesystem::copy(runtime.FrameworkDirectory,
				destination / "shared" / "Microsoft.NETCore.App" / runtime.Version,
				std::filesystem::copy_options::recursive);
			std::filesystem::copy(hostDirectory, destination / "host" / "fxr" / hostDirectory.filename(),
				std::filesystem::copy_options::recursive);
			for (const char* file : { "LICENSE.txt", "ThirdPartyNotices.txt" }) {
				std::filesystem::copy_file(runtime.Root / file, destination / file);
			}
			if (!File::WriteAllText(Path::ToUtf8(destination.parent_path() / ".funke-dotnet-runtime"), runtime.Version)) {
				error = "Could not write the bundled .NET runtime marker.";
				return false;
			}
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool StageInstalled(const std::filesystem::path& runtimeConfig,
		const std::filesystem::path& destination, std::string& error) {
		Json::Value config;
		if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(runtimeConfig)), config, &error)) return false;
		const auto* options = config.FindMember("runtimeOptions");
		const auto* framework = options ? options->FindMember("framework") : nullptr;
		const auto* name = framework ? framework->FindMember("name") : nullptr;
		const auto* version = framework ? framework->FindMember("version") : nullptr;
		if (!name || name->AsStringOr() != "Microsoft.NETCore.App" || !version) {
			error = "ScriptCore runtimeconfig must declare Microsoft.NETCore.App.";
			return false;
		}
		const auto installed = Process::Run({ "dotnet", "--list-runtimes" }, {}, std::chrono::seconds(30));
		if (!installed.Succeeded()) {
			error = "Could not query installed .NET runtimes: " + installed.Output;
			return false;
		}
		Runtime runtime;
		return SelectRuntime(installed.Output, version->AsStringOr(), runtime, error)
			&& Stage(runtime, destination, error);
	}
}
