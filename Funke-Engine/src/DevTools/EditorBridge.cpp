#include "pch.hpp"
#include "DevTools/EditorBridge.hpp"

#include "Core/Platform.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <random>

#if !defined(FNK_PLATFORM_WINDOWS)
#include <unistd.h>
#endif

namespace Funke::EditorBridge {

	namespace {
		std::string ToLower(std::string_view value) {
			std::string out(value);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// Case-insensitive on Windows, where C:\Game and c:\game are the same folder.
		std::string ComparablePath(const std::filesystem::path& path) {
			std::error_code ec;
			std::filesystem::path absolute = std::filesystem::weakly_canonical(path, ec);
			if (ec) absolute = std::filesystem::absolute(path, ec);
			std::string text = Path::ToUtf8(absolute.lexically_normal());
			std::replace(text.begin(), text.end(), '\\', '/');
			while (text.size() > 1 && text.back() == '/') text.pop_back();
#if defined(FNK_PLATFORM_WINDOWS)
			text = ToLower(text);
#endif
			return text;
		}

		bool IsInside(const std::string& child, const std::string& parent) {
			if (parent.empty() || child.size() < parent.size() || child.compare(0, parent.size(), parent) != 0) return false;
			return child.size() == parent.size() || child[parent.size()] == '/';
		}
	}

	bool ParseRequest(std::string_view line, Request& out, std::string& outError) {
		out = Request{};
		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(line, root, &parseError) || !root.IsObject()) {
			outError = "request is not a JSON object" + (parseError.empty() ? std::string() : ": " + parseError);
			return false;
		}
		if (const Json::Value* id = root.FindMember("id")) {
			out.Id = id->IsString() ? id->AsStringOr() : Json::Stringify(*id);
		}
		const Json::Value* command = root.FindMember("command");
		if (!command || !command->IsString() || command->AsStringOr().empty()) {
			outError = "request has no 'command'";
			return false;
		}
		out.Command = ToLower(command->AsStringOr());
		if (const Json::Value* token = root.FindMember("token")) out.Token = token->AsStringOr();
		if (const Json::Value* args = root.FindMember("args")) {
			if (!args->IsObject() && !args->IsNull()) {
				outError = "'args' must be an object";
				return false;
			}
			if (args->IsObject()) out.Args = *args;
		}
		if (const Json::Value* timeout = root.FindMember("timeoutMs")) {
			out.TimeoutMs = std::max(0, timeout->AsIntOr(0));
		}
		return true;
	}

	std::string FormatRequest(const Request& request) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("id", request.Id);
		root.AddMember("token", request.Token);
		root.AddMember("command", request.Command);
		root.AddMember("args", request.Args.IsObject() ? request.Args : Json::Value::MakeObject());
		if (request.TimeoutMs > 0) root.AddMember("timeoutMs", request.TimeoutMs);
		return Json::Stringify(root, false);
	}

	bool ParseResponse(std::string_view line, Response& out, std::string& outError) {
		out = Response{};
		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(line, root, &parseError) || !root.IsObject()) {
			outError = "the editor sent an unreadable answer" + (parseError.empty() ? std::string() : ": " + parseError);
			return false;
		}
		if (const Json::Value* id = root.FindMember("id")) out.Id = id->AsStringOr();
		if (const Json::Value* ok = root.FindMember("ok")) out.Ok = ok->AsBoolOr(false);
		if (const Json::Value* busy = root.FindMember("busy")) out.Busy = busy->AsBoolOr(false);
		if (const Json::Value* error = root.FindMember("error")) out.Error = error->AsStringOr();
		if (const Json::Value* result = root.FindMember("result")) out.Result = *result;
		if (!out.Ok && out.Error.empty()) out.Error = "the editor reported a failure without a message";
		return true;
	}

	std::string FormatResult(std::string_view id, const Json::Value& result) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("id", std::string(id));
		root.AddMember("ok", true);
		root.AddMember("result", result);
		return Json::Stringify(root, false);
	}

	std::string FormatError(std::string_view id, std::string_view message, bool busy) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("id", std::string(id));
		root.AddMember("ok", false);
		root.AddMember("error", std::string(message));
		if (busy) root.AddMember("busy", true);
		return Json::Stringify(root, false);
	}

	std::filesystem::path GetDiscoveryDirectory() {
		return Platform::GetUserDataRoot() / "Funke" / "EditorBridge";
	}

	std::string SerializeInstance(const Instance& instance) {
		Json::Value root = Json::Value::MakeObject();
		root.AddMember("protocol", instance.Protocol);
		root.AddMember("pid", static_cast<int64_t>(instance.Pid));
		root.AddMember("project", instance.ProjectRoot);
		root.AddMember("projectName", instance.ProjectName);
		root.AddMember("endpoint", instance.Endpoint);
		root.AddMember("token", instance.Token);
		root.AddMember("version", instance.Version);
		root.AddMember("startedAtUnixMs", instance.StartedAtUnixMs);
		return Json::Stringify(root, true);
	}

	bool ParseInstance(std::string_view json, Instance& out) {
		out = Instance{};
		Json::Value root;
		if (!Json::TryParse(json, root) || !root.IsObject()) return false;
		if (const Json::Value* v = root.FindMember("protocol")) out.Protocol = v->AsIntOr(0);
		if (const Json::Value* v = root.FindMember("pid")) out.Pid = static_cast<uint32_t>(v->AsInt64Or(0));
		if (const Json::Value* v = root.FindMember("project")) out.ProjectRoot = v->AsStringOr();
		if (const Json::Value* v = root.FindMember("projectName")) out.ProjectName = v->AsStringOr();
		if (const Json::Value* v = root.FindMember("endpoint")) out.Endpoint = v->AsStringOr();
		if (const Json::Value* v = root.FindMember("token")) out.Token = v->AsStringOr();
		if (const Json::Value* v = root.FindMember("version")) out.Version = v->AsStringOr();
		if (const Json::Value* v = root.FindMember("startedAtUnixMs")) out.StartedAtUnixMs = v->AsInt64Or(0);
		return out.Pid != 0 && !out.Endpoint.empty();
	}

	std::vector<Instance> ListInstances(const std::filesystem::path& directory) {
		std::vector<Instance> instances;
		const std::filesystem::path root = directory.empty() ? GetDiscoveryDirectory() : directory;
		std::error_code ec;
		if (!std::filesystem::is_directory(root, ec)) return instances;
		for (std::filesystem::directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
			const std::filesystem::path& path = it->path();
			if (path.extension() != ".json") continue;
			const std::string pathText = Path::ToUtf8(path);
			Instance instance;
			if (!ParseInstance(File::ReadAllText(pathText), instance)) continue;
			if (!Process::IsProcessRunning(instance.Pid)) {
				// Left behind by an editor that crashed or was killed.
				std::error_code removeError;
				std::filesystem::remove(path, removeError);
				continue;
			}
			instance.FilePath = pathText;
			instances.push_back(std::move(instance));
		}
		std::sort(instances.begin(), instances.end(), [](const Instance& a, const Instance& b) {
			return a.StartedAtUnixMs > b.StartedAtUnixMs;
		});
		return instances;
	}

	const Instance* SelectInstance(const std::vector<Instance>& instances, uint32_t pid,
		const std::filesystem::path& projectDir, bool strictProject, std::string& outError)
	{
		if (instances.empty()) {
			outError = "no Funke editor is running (start one with `funke open`)";
			return nullptr;
		}
		auto describe = [](const Instance& instance) {
			return "pid " + std::to_string(instance.Pid) + " (" + (instance.ProjectName.empty() ? instance.ProjectRoot : instance.ProjectName) + ")";
		};
		if (pid != 0) {
			for (const Instance& instance : instances) {
				if (instance.Pid == pid) return &instance;
			}
			outError = "no Funke editor with pid " + std::to_string(pid) + " is running";
			return nullptr;
		}

		if (!projectDir.empty()) {
			const std::string wanted = ComparablePath(projectDir);
			std::vector<const Instance*> matches;
			for (const Instance& instance : instances) {
				// A directory inside the project (Assets/Scripts) still means that project.
				if (!instance.ProjectRoot.empty() && IsInside(wanted, ComparablePath(Path::FromUtf8(instance.ProjectRoot)))) {
					matches.push_back(&instance);
				}
			}
			if (matches.size() == 1) return matches.front();
			if (matches.size() > 1) {
				outError = "several editors have this project open; pick one with --pid:";
				for (const Instance* match : matches) outError += "\n  " + describe(*match);
				return nullptr;
			}
			if (strictProject) {
				outError = "no running editor has " + Path::ToUtf8(projectDir) + " open; running:";
				for (const Instance& instance : instances) outError += "\n  " + describe(instance) + "  " + instance.ProjectRoot;
				return nullptr;
			}
		}
		if (instances.size() == 1) return &instances.front();

		outError = "several Funke editors are running; pick one with --project <dir> or --pid:";
		for (const Instance& instance : instances) outError += "\n  " + describe(instance) + "  " + instance.ProjectRoot;
		return nullptr;
	}

	uint32_t GetCurrentProcessId() {
#if defined(FNK_PLATFORM_WINDOWS)
		return static_cast<uint32_t>(::GetCurrentProcessId());
#else
		return static_cast<uint32_t>(::getpid());
#endif
	}

	std::string GenerateToken() {
		// std::random_device draws from the OS generator (RtlGenRandom, /dev/urandom).
		std::random_device device;
		static constexpr char k_Hex[] = "0123456789abcdef";
		std::string token;
		token.reserve(32);
		for (int i = 0; i < 4; ++i) {
			uint32_t bits = device();
			for (int nibble = 0; nibble < 8; ++nibble) {
				token.push_back(k_Hex[bits & 0xF]);
				bits >>= 4;
			}
		}
		return token;
	}

	bool TokensMatch(std::string_view expected, std::string_view actual) {
		if (expected.empty() || expected.size() != actual.size()) return false;
		unsigned char difference = 0;
		for (size_t i = 0; i < expected.size(); ++i) {
			difference |= static_cast<unsigned char>(expected[i] ^ actual[i]);
		}
		return difference == 0;
	}

	bool ParseSize(std::string_view text, int& outWidth, int& outHeight) {
		const size_t separator = text.find_first_of("xX*");
		if (separator == std::string_view::npos) return false;
		auto parse = [](std::string_view part, int& out) {
			int value = 0;
			const auto result = std::from_chars(part.data(), part.data() + part.size(), value);
			if (result.ec != std::errc{} || result.ptr != part.data() + part.size()) return false;
			out = value;
			return true;
		};
		int width = 0, height = 0;
		if (!parse(text.substr(0, separator), width) || !parse(text.substr(separator + 1), height)) return false;
		// 16384 is the largest texture WebGPU guarantees.
		if (width < 1 || height < 1 || width > 16384 || height > 16384) return false;
		outWidth = width;
		outHeight = height;
		return true;
	}

	Vec2 SurfaceToWindow(Vec2 normalized, Vec2 regionOffset, Vec2 regionSize, Vec2 windowToRegionScale) {
		const Vec2 local{ normalized.x * regionSize.x, normalized.y * regionSize.y };
		const float sx = windowToRegionScale.x > 0.0f ? windowToRegionScale.x : 1.0f;
		const float sy = windowToRegionScale.y > 0.0f ? windowToRegionScale.y : 1.0f;
		return { (local.x + regionOffset.x) / sx, (local.y + regionOffset.y) / sy };
	}

	std::string MakeEndpointName(uint32_t pid, std::string_view nonce) {
#if defined(FNK_PLATFORM_WINDOWS)
		return "\\\\.\\pipe\\funke-editor-" + std::to_string(pid) + "-" + std::string(nonce);
#else
		// sun_path holds about 100 bytes, so the socket lives in a short directory, not the discovery folder.
		const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
		const std::string directory = runtimeDir && *runtimeDir ? std::string(runtimeDir) : std::string("/tmp");
		return directory + "/funke-editor-" + std::to_string(pid) + "-" + std::string(nonce.substr(0, 12)) + ".sock";
#endif
	}

} // namespace Funke::EditorBridge
