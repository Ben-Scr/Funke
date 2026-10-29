#include <doctest/doctest.h>

#include "Core/PlatformDetection.hpp"
#include "DevTools/EditorBridge.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/OwnedTask.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace Funke;

namespace {
	struct TempDir {
		TempDir() {
			Root = std::filesystem::temp_directory_path()
				/ ("FunkeBridge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Root);
		}
		~TempDir() {
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}
		std::filesystem::path Root;
	};

	EditorBridge::Instance MakeInstance(uint32_t pid, const std::filesystem::path& project, int64_t started) {
		EditorBridge::Instance instance;
		instance.Pid = pid;
		instance.ProjectRoot = Path::ToUtf8(project);
		instance.ProjectName = Path::ToUtf8(project.filename());
		instance.Endpoint = "endpoint-" + std::to_string(pid);
		instance.Token = "token";
		instance.StartedAtUnixMs = started;
		return instance;
	}
}

TEST_CASE("EditorBridge requests round-trip and reject malformed lines") {
	EditorBridge::Request request;
	request.Id = "7";
	request.Token = "abc";
	request.Command = "screenshot";
	request.Args.AddMember("path", "C:/shots/a.png");
	request.Args.AddMember("width", 1080);
	request.TimeoutMs = 5000;

	EditorBridge::Request parsed;
	std::string error;
	REQUIRE(EditorBridge::ParseRequest(EditorBridge::FormatRequest(request), parsed, error));
	CHECK(parsed.Id == "7");
	CHECK(parsed.Token == "abc");
	CHECK(parsed.Command == "screenshot");
	CHECK(parsed.TimeoutMs == 5000);
	REQUIRE(parsed.Args.FindMember("width"));
	CHECK(parsed.Args.FindMember("width")->AsIntOr(0) == 1080);

	// Command names are case-insensitive; missing args become an empty object.
	REQUIRE(EditorBridge::ParseRequest(R"({"command":"STATUS"})", parsed, error));
	CHECK(parsed.Command == "status");
	CHECK(parsed.Args.IsObject());

	CHECK_FALSE(EditorBridge::ParseRequest("not json", parsed, error));
	CHECK_FALSE(EditorBridge::ParseRequest(R"({"id":"1"})", parsed, error));
	CHECK(error.find("command") != std::string::npos);
	CHECK_FALSE(EditorBridge::ParseRequest(R"({"command":"play","args":[1,2]})", parsed, error));
}

TEST_CASE("EditorBridge responses carry results, errors and the busy flag") {
	Json::Value result = Json::Value::MakeObject();
	result.AddMember("state", "playing");
	EditorBridge::Response response;
	std::string error;
	REQUIRE(EditorBridge::ParseResponse(EditorBridge::FormatResult("3", result), response, error));
	CHECK(response.Ok);
	CHECK(response.Id == "3");
	REQUIRE(response.Result.FindMember("state"));
	CHECK(response.Result.FindMember("state")->AsStringOr() == "playing");

	REQUIRE(EditorBridge::ParseResponse(EditorBridge::FormatError("4", "busy: building", true), response, error));
	CHECK_FALSE(response.Ok);
	CHECK(response.Busy);
	CHECK(response.Error == "busy: building");

	REQUIRE(EditorBridge::ParseResponse(R"({"ok":false})", response, error));
	CHECK_FALSE(response.Error.empty());
	CHECK_FALSE(EditorBridge::ParseResponse("[1]", response, error));
}

TEST_CASE("EditorBridge discovery files list live editors and drop stale ones") {
	TempDir temp;
	const uint32_t self = EditorBridge::GetCurrentProcessId();
	EditorBridge::Instance live = MakeInstance(self, temp.Root / "Game", 100);
	live.Version = "test";
	REQUIRE(File::WriteAllText(Path::ToUtf8(temp.Root / "live.json"), EditorBridge::SerializeInstance(live)));

	EditorBridge::Instance parsed;
	REQUIRE(EditorBridge::ParseInstance(EditorBridge::SerializeInstance(live), parsed));
	CHECK(parsed.Pid == self);
	CHECK(parsed.Endpoint == live.Endpoint);
	CHECK(parsed.Version == "test");
	CHECK(parsed.Protocol == EditorBridge::k_ProtocolVersion);

	// A pid that can't be running (process ids are multiples of 4 on Windows, so this odd one never exists).
	const EditorBridge::Instance dead = MakeInstance(0x7FFFFFF1u, temp.Root / "Old", 50);
	const std::filesystem::path deadFile = temp.Root / "dead.json";
	REQUIRE(File::WriteAllText(Path::ToUtf8(deadFile), EditorBridge::SerializeInstance(dead)));
	REQUIRE(File::WriteAllText(Path::ToUtf8(temp.Root / "broken.json"), "{ nope"));

	const std::vector<EditorBridge::Instance> instances = EditorBridge::ListInstances(temp.Root);
	REQUIRE(instances.size() == 1);
	CHECK(instances[0].Pid == self);
	CHECK_FALSE(instances[0].FilePath.empty());
	CHECK_FALSE(std::filesystem::exists(deadFile));
}

TEST_CASE("EditorBridge picks the editor by pid, by project folder, or the only one") {
	TempDir temp;
	std::filesystem::create_directories(temp.Root / "Alpha" / "Assets" / "Scripts");
	std::filesystem::create_directories(temp.Root / "Beta");
	std::filesystem::create_directories(temp.Root / "Gamma");
	const std::vector<EditorBridge::Instance> both = {
		MakeInstance(10, temp.Root / "Alpha", 2),
		MakeInstance(20, temp.Root / "Beta", 1),
	};
	std::string error;

	const EditorBridge::Instance* chosen = EditorBridge::SelectInstance(both, 20, {}, false, error);
	REQUIRE(chosen);
	CHECK(chosen->Pid == 20);
	CHECK_FALSE(EditorBridge::SelectInstance(both, 99, {}, false, error));

	// A folder inside the project counts as the project.
	chosen = EditorBridge::SelectInstance(both, 0, temp.Root / "Alpha" / "Assets" / "Scripts", false, error);
	REQUIRE(chosen);
	CHECK(chosen->Pid == 10);

	// Two editors and no project match: ambiguous.
	CHECK_FALSE(EditorBridge::SelectInstance(both, 0, temp.Root / "Gamma", false, error));
	CHECK(error.find("--pid") != std::string::npos);

	const std::vector<EditorBridge::Instance> one = { MakeInstance(30, temp.Root / "Beta", 1) };
	chosen = EditorBridge::SelectInstance(one, 0, temp.Root / "Gamma", false, error);
	REQUIRE(chosen);
	CHECK(chosen->Pid == 30);
	// An explicit --project never falls back to some other project's editor.
	CHECK_FALSE(EditorBridge::SelectInstance(one, 0, temp.Root / "Gamma", true, error));

	CHECK_FALSE(EditorBridge::SelectInstance({}, 0, {}, false, error));
	CHECK(error.find("no Funke editor") != std::string::npos);
}

TEST_CASE("EditorBridge helpers: sizes, tokens and surface mapping") {
	int width = 0, height = 0;
	REQUIRE(EditorBridge::ParseSize("1080x2400", width, height));
	CHECK(width == 1080);
	CHECK(height == 2400);
	REQUIRE(EditorBridge::ParseSize("1920X1080", width, height));
	CHECK(width == 1920);
	CHECK_FALSE(EditorBridge::ParseSize("1080", width, height));
	CHECK_FALSE(EditorBridge::ParseSize("0x100", width, height));
	CHECK_FALSE(EditorBridge::ParseSize("100x", width, height));
	CHECK_FALSE(EditorBridge::ParseSize("99999x10", width, height));

	const std::string token = EditorBridge::GenerateToken();
	CHECK(token.size() == 32);
	CHECK(token != EditorBridge::GenerateToken());
	CHECK(EditorBridge::TokensMatch(token, token));
	CHECK_FALSE(EditorBridge::TokensMatch(token, token.substr(1)));
	CHECK_FALSE(EditorBridge::TokensMatch("", ""));

	// A Game View image at (100, 50) of the window, shown at its own pixel size.
	Vec2 window = EditorBridge::SurfaceToWindow({ 0.5f, 0.25f }, { 100.0f, 50.0f }, { 800.0f, 600.0f }, { 1.0f, 1.0f });
	CHECK(window.x == doctest::Approx(500.0f));
	CHECK(window.y == doctest::Approx(200.0f));
	// A 1080x2400 surface shown at half size: region offsets are in surface pixels.
	window = EditorBridge::SurfaceToWindow({ 0.5f, 0.5f }, { 200.0f, 100.0f }, { 1080.0f, 2400.0f }, { 2.0f, 2.0f });
	CHECK(window.x == doctest::Approx((540.0f + 200.0f) / 2.0f));
	CHECK(window.y == doctest::Approx((1200.0f + 100.0f) / 2.0f));
}

#if FNK_PLATFORM_DESKTOP
TEST_CASE("EditorBridge server and client exchange one line per connection") {
	const std::string token = EditorBridge::GenerateToken();
	const std::string endpoint = EditorBridge::MakeEndpointName(EditorBridge::GetCurrentProcessId(), EditorBridge::GenerateToken());
	EditorBridge::Server server;
	std::string error;
	REQUIRE_MESSAGE(server.Start(endpoint, token, error), error);
	CHECK(server.IsRunning());

	// A second server can't take over the name.
	EditorBridge::Server squatter;
	CHECK_FALSE(squatter.Start(endpoint, token, error));
	CHECK_FALSE(squatter.IsRunning());
	squatter.Stop();

	// Stands in for the editor's main thread: answers every request with its own command name.
	std::atomic<int> abandoned{ 0 };
	OwnedTask mainThread([&](OwnedTask::StopToken stop) {
		while (!stop.stop_requested()) {
			abandoned += static_cast<int>(server.TakeAbandoned().size());
			for (EditorBridge::Server::Incoming& incoming : server.TakeRequests()) {
				Json::Value result = Json::Value::MakeObject();
				result.AddMember("echo", incoming.Message.Command);
				if (incoming.Message.Command == "never") continue;
				server.Respond(incoming.Ticket, EditorBridge::FormatResult(incoming.Message.Id, result));
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	});

	auto send = [&](const std::string& command, const std::string& withToken, int timeoutMs, EditorBridge::Response& out) {
		EditorBridge::Request request;
		request.Id = "1";
		request.Token = withToken;
		request.Command = command;
		request.TimeoutMs = timeoutMs;
		std::string reply, sendError;
		if (!EditorBridge::Exchange(endpoint, EditorBridge::FormatRequest(request), timeoutMs + 2000, reply, sendError)) return false;
		return EditorBridge::ParseResponse(reply, out, sendError);
	};

	EditorBridge::Response response;
	REQUIRE(send("status", token, 5000, response));
	CHECK(response.Ok);
	REQUIRE(response.Result.FindMember("echo"));
	CHECK(response.Result.FindMember("echo")->AsStringOr() == "status");

	// Connections are sequential; the server keeps serving after each one.
	REQUIRE(send("play", token, 5000, response));
	CHECK(response.Result.FindMember("echo")->AsStringOr() == "play");

	// Wrong token: refused by the server thread, the main thread never sees it.
	REQUIRE(send("play", "wrong", 5000, response));
	CHECK_FALSE(response.Ok);
	CHECK(response.Error.find("unauthorized") != std::string::npos);

	// No answer in time: the server answers "timeout" itself, flagged busy.
	REQUIRE(send("never", token, 300, response));
	CHECK_FALSE(response.Ok);
	CHECK(response.Busy);

	// A client that gives up early (killed, its own timeout) frees the single pipe instance right away.
	EditorBridge::Request slow;
	slow.Token = token;
	slow.Command = "never";
	slow.TimeoutMs = 60000;
	std::string slowReply;
	CHECK_FALSE(EditorBridge::Exchange(endpoint, EditorBridge::FormatRequest(slow), 300, slowReply, error));
	const auto afterGiveUp = std::chrono::steady_clock::now();
	REQUIRE(send("status", token, 5000, response));
	CHECK(response.Ok);
	CHECK(std::chrono::steady_clock::now() - afterGiveUp < std::chrono::seconds(3));

	mainThread.StopAndJoin();
	// Both unanswered requests were taken by the main thread, so it hears that nobody waits for them.
	CHECK(abandoned.load() + static_cast<int>(server.TakeAbandoned().size()) == 2);
	server.Stop();
	CHECK_FALSE(server.IsRunning());

	std::string reply;
	CHECK_FALSE(EditorBridge::Exchange(endpoint, "{}", 500, reply, error));
}
#endif

#if FNK_PLATFORM_DESKTOP && !defined(FNK_PLATFORM_WINDOWS)
TEST_CASE("EditorBridge preserves an occupied filesystem path after a failed start") {
	const std::string endpoint = EditorBridge::MakeEndpointName(EditorBridge::GetCurrentProcessId(), EditorBridge::GenerateToken());
	struct RemoveFile {
		std::filesystem::path Path;
		~RemoveFile() {
			std::error_code ec;
			std::filesystem::remove(Path, ec);
		}
	} cleanup{Path::FromUtf8(endpoint)};
	REQUIRE(File::WriteAllText(endpoint, "already owned"));
	EditorBridge::Server server;
	std::string error;
	CHECK_FALSE(server.Start(endpoint, EditorBridge::GenerateToken(), error));
	CHECK_FALSE(server.IsRunning());
	server.Stop();
	CHECK(std::filesystem::is_regular_file(cleanup.Path));
	CHECK(File::ReadAllText(endpoint) == "already owned");

	REQUIRE(std::filesystem::remove(cleanup.Path));
	REQUIRE_MESSAGE(server.Start(endpoint, EditorBridge::GenerateToken(), error), error);
	server.Stop();
	CHECK_FALSE(std::filesystem::exists(cleanup.Path));
}
#endif
