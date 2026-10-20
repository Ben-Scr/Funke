#pragma once
#include "Collections/Vec2.hpp"
#include "Core/Export.hpp"
#include "Serialization/Json.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Funke::EditorBridge {

	// Editor Bridge: `funke editor <command>` drives a running editor over a local, user-only channel
	// (a named pipe on Windows, a Unix socket elsewhere). One JSON line per request, one per answer.
	constexpr int k_ProtocolVersion = 1;

	struct Request {
		std::string Id;
		std::string Token;
		std::string Command;
		Json::Value Args = Json::Value::MakeObject();
		// How long the client waits for the answer; the editor drops answers that come later.
		int TimeoutMs = 0;
	};

	struct Response {
		std::string Id;
		bool Ok = false;
		// The editor could not take the command right now (loading, building, a dialog is open).
		bool Busy = false;
		std::string Error;
		Json::Value Result;
	};

	FUNKE_API bool ParseRequest(std::string_view line, Request& out, std::string& outError);
	FUNKE_API std::string FormatRequest(const Request& request);
	FUNKE_API bool ParseResponse(std::string_view line, Response& out, std::string& outError);
	FUNKE_API std::string FormatResult(std::string_view id, const Json::Value& result);
	FUNKE_API std::string FormatError(std::string_view id, std::string_view message, bool busy = false);

	// One file per running editor in GetDiscoveryDirectory(), named <pid>.json, removed on exit.
	struct Instance {
		uint32_t Pid = 0;
		int Protocol = k_ProtocolVersion;
		std::string ProjectRoot;
		std::string ProjectName;
		std::string Endpoint;
		std::string Token;
		std::string Version;
		int64_t StartedAtUnixMs = 0;
		std::string FilePath;
	};

	FUNKE_API std::filesystem::path GetDiscoveryDirectory();
	FUNKE_API std::string SerializeInstance(const Instance& instance);
	FUNKE_API bool ParseInstance(std::string_view json, Instance& out);
	// Discovery files in `directory` (default: GetDiscoveryDirectory()); files of exited processes are deleted.
	FUNKE_API std::vector<Instance> ListInstances(const std::filesystem::path& directory = {});
	// pid != 0 picks that editor; otherwise the one whose project contains projectDir, otherwise (unless
	// strictProject) the only one running.
	FUNKE_API const Instance* SelectInstance(const std::vector<Instance>& instances, uint32_t pid,
		const std::filesystem::path& projectDir, bool strictProject, std::string& outError);

	FUNKE_API uint32_t GetCurrentProcessId();
	FUNKE_API std::string GenerateToken();
	// Compares without an early exit, so the token can't be guessed byte by byte from timings.
	FUNKE_API bool TokensMatch(std::string_view expected, std::string_view actual);
	// "1080x2400" (also "1080X2400", "1080*2400").
	FUNKE_API bool ParseSize(std::string_view text, int& outWidth, int& outHeight);
	// Normalized game-surface position (0..1, top-left origin) to the window coordinates Input stores,
	// for a UI region mapping window coordinates w to w * scale - offset.
	FUNKE_API Vec2 SurfaceToWindow(Vec2 normalized, Vec2 regionOffset, Vec2 regionSize, Vec2 windowToRegionScale);

	// Pipe name (Windows) or socket path for this process; the nonce keeps the name unguessable.
	FUNKE_API std::string MakeEndpointName(uint32_t pid, std::string_view nonce);

	// Accepts one client at a time on a background thread. Each connection carries one request line;
	// the thread rejects malformed and unauthorized ones itself, the main thread takes the rest and
	// answers whenever it is done. Without an answer before the request's timeout the thread answers
	// "timeout" and a request the main thread hasn't taken yet is dropped, so it never runs late.
	class FUNKE_API Server {
	public:
		struct Incoming {
			uint64_t Ticket = 0;
			Request Message;
		};

		Server();
		~Server();
		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		bool Start(const std::string& endpoint, const std::string& token, std::string& outError);
		void Stop();
		bool IsRunning() const { return m_Thread.joinable(); }
		const std::string& GetEndpoint() const { return m_Endpoint; }

		std::vector<Incoming> TakeRequests();
		bool HasRequests() const;
		// Tickets the main thread took whose client gave up or left; their work can stop.
		std::vector<uint64_t> TakeAbandoned();
		// Thread-safe; an answer for a client that already gave up is dropped.
		void Respond(uint64_t ticket, std::string line);

		struct State;

	private:
		std::string m_Endpoint;
		std::shared_ptr<State> m_State;
		std::thread m_Thread;
	};

	// Client side: sends one line and waits for the one-line answer.
	FUNKE_API bool Exchange(const std::string& endpoint, const std::string& line, int timeoutMs,
		std::string& outReply, std::string& outError);

} // namespace Funke::EditorBridge
