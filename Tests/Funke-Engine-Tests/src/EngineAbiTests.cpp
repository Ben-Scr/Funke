#include <doctest/doctest.h>

#include "Core/Engine.hpp"
#include "Core/EngineAbi.hpp"
#include "Core/Log.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Scripting/NativeScriptField.hpp"
#include "Utils/Process.hpp"

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <vector>

using namespace Funke;

TEST_CASE("Native ABI rejects incompatible replacements and recovers in the same host") {
	const char* fixtureDirectory = std::getenv("FUNKE_NATIVE_ABI_FIXTURES");
	if (!fixtureDirectory) { MESSAGE("Set FUNKE_NATIVE_ABI_FIXTURES to compiled AbiBoundary fixtures"); return; }
	const std::filesystem::path fixtures(fixtureDirectory);
#ifdef _WIN32
	const std::string suffix = ".dll";
#else
	const std::string suffix = ".so";
#endif
	const std::string stem = "abi-replacement-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto replacement = fixtures / (stem + suffix);
	const auto shadows = std::filesystem::temp_directory_path() / "FunkeNativeScriptHost";
	auto shadowCount = [&] {
		size_t count = 0;
		if (std::filesystem::exists(shadows)) for (const auto& entry : std::filesystem::directory_iterator(shadows))
			if (entry.path().stem().string().starts_with(stem + "-")) ++count;
		return count;
	};
	NativeScriptHost host;
	struct Cleanup {
		NativeScriptHost& Host; std::filesystem::path Path;
		~Cleanup() { Host.UnloadDLL(); std::error_code error; std::filesystem::remove(Path, error); }
	} cleanup{ host, replacement };
	std::vector<std::string> messages;
	const auto listener = Log::OnLog.Add([&](const Log::Entry& entry) { messages.push_back(entry.Message); });
	struct Unsubscribe { EventId Id; ~Unsubscribe() { Log::OnLog.Remove(Id); } } unsubscribe{ listener };
	auto install = [&](const char* name) {
		std::filesystem::copy_file(fixtures / (name + suffix), replacement, std::filesystem::copy_options::overwrite_existing);
	};
	install("valid");
	REQUIRE(host.LoadDLL(replacement.string()));
	const auto originalPath = host.GetDLLPath();
	REQUIRE(host.HasClass("original"));
	CHECK(shadowCount() == 1);
	for (const char* invalid : { "old", "future", "no-abi", "no-create", "no-destroy" }) {
		install(invalid);
		messages.clear();
		CHECK_FALSE(host.Reload());
		CHECK(host.IsLoaded());
		CHECK(host.GetDLLPath() == originalPath);
		CHECK(host.HasClass("original"));
		CHECK_FALSE(host.HasClass("recovered"));
		CHECK(shadowCount() == 1);
		REQUIRE_FALSE(messages.empty());
		std::string diagnostics;
		for (const auto& message : messages) diagnostics += message;
		CHECK(diagnostics.find("Rebuild") != std::string::npos);
		if (std::string_view(invalid) == "old" || std::string_view(invalid) == "future" || std::string_view(invalid) == "no-abi") {
			CHECK(diagnostics.find("expected " + std::to_string(k_NativeScriptAbiVersion)) != std::string::npos);
			CHECK(diagnostics.find("received ") != std::string::npos);
		}
	}
	install("recovered");
	REQUIRE(host.Reload());
	CHECK_FALSE(host.HasClass("original"));
	CHECK(host.HasClass("recovered"));
	CHECK(shadowCount() == 1);
	host.UnloadDLL();
	CHECK(shadowCount() == 0);
	CHECK_FALSE(host.IsLoaded());
}

TEST_CASE("The test executable matches the engine headers of the loaded DLL") {
	// Both come from scripts/generate_build_metadata.py in the same build.
	CHECK_FALSE(Engine::GetAbiKey().empty());
	CHECK(Engine::GetAbiKey() == std::string_view(FNK_ENGINE_ABI));
	CHECK(DescribeEngineAbiMismatch("Funke-Engine-Tests.exe").empty());
}

TEST_CASE("Loader exit codes before main() get a readable cause") {
	const auto status = [](uint32_t code) { return static_cast<int>(code); };
	CHECK(Process::DescribeLoaderFailure(status(0xC0000138u)).find("Funke-Engine.dll") != std::string::npos);
	CHECK(Process::DescribeLoaderFailure(status(0xC0000139u)).find("lacks a function") != std::string::npos);
	CHECK_FALSE(Process::DescribeLoaderFailure(status(0xC0000135u)).empty());
	CHECK(Process::DescribeLoaderFailure(0).empty());
	CHECK(Process::DescribeLoaderFailure(1).empty());
	CHECK(Process::DescribeLoaderFailure(-1).empty());
}
