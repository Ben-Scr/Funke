#include <doctest/doctest.h>

#include "Core/Engine.hpp"
#include "Core/EngineAbi.hpp"
#include "Utils/Process.hpp"

#include <cstdint>

using namespace Index;

TEST_CASE("The test executable matches the engine headers of the loaded DLL") {
	// Both come from scripts/generate_build_metadata.py in the same build.
	CHECK_FALSE(Engine::GetAbiKey().empty());
	CHECK(Engine::GetAbiKey() == std::string_view(IDX_ENGINE_ABI));
	CHECK(DescribeEngineAbiMismatch("Index-Engine-Tests.exe").empty());
}

TEST_CASE("Loader exit codes before main() get a readable cause") {
	const auto status = [](uint32_t code) { return static_cast<int>(code); };
	CHECK(Process::DescribeLoaderFailure(status(0xC0000138u)).find("Index-Engine.dll") != std::string::npos);
	CHECK(Process::DescribeLoaderFailure(status(0xC0000139u)).find("lacks a function") != std::string::npos);
	CHECK_FALSE(Process::DescribeLoaderFailure(status(0xC0000135u)).empty());
	CHECK(Process::DescribeLoaderFailure(0).empty());
	CHECK(Process::DescribeLoaderFailure(1).empty());
	CHECK(Process::DescribeLoaderFailure(-1).empty());
}
