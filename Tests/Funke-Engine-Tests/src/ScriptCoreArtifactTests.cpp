#include <doctest/doctest.h>

#include "Scripting/ScriptCoreArtifact.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace Funke::ScriptCoreArtifact;

namespace {
	struct ScopedArtifactDirectory {
		std::filesystem::path Root;

		ScopedArtifactDirectory() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path()
				/ ("FunkeScriptCoreArtifact_" + std::to_string(suffix));
			std::filesystem::create_directories(Root);
		}

		~ScopedArtifactDirectory() {
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}
	};

	void WriteArtifact(const std::filesystem::path& directory, const char* filename) {
		std::filesystem::create_directories(directory);
		std::ofstream(directory / filename) << filename;
	}

	void WriteCompleteArtifactSet(const std::filesystem::path& directory) {
		for (const char* filename : RequiredFilenames) {
			WriteArtifact(directory, filename);
		}
	}

	std::filesystem::path Normalized(const std::filesystem::path& path) {
		return std::filesystem::weakly_canonical(path);
	}
}

TEST_CASE("Complete executable-local ScriptCore artifacts win") {
	ScopedArtifactDirectory artifacts;
	const auto executableLocal = artifacts.Root / "Runtime";
	const auto sibling = artifacts.Root / "Funke-ScriptCore";
	WriteCompleteArtifactSet(executableLocal);
	WriteCompleteArtifactSet(sibling);
	const std::vector<std::filesystem::path> candidates = { executableLocal, sibling };

	CHECK(FindFirstCompleteDirectory(candidates) == Normalized(executableLocal));
	CHECK(FindFirstCompleteDll(candidates)
		== Normalized(executableLocal / DllFilename));
}

TEST_CASE("Incomplete local ScriptCore artifacts fall back to a complete sibling") {
	ScopedArtifactDirectory artifacts;
	const auto executableLocal = artifacts.Root / "Runtime";
	const auto sibling = artifacts.Root / "Funke-ScriptCore";
	WriteArtifact(executableLocal, DllFilename);
	WriteCompleteArtifactSet(sibling);
	const std::vector<std::filesystem::path> candidates = { executableLocal, sibling };

	CHECK(FindFirstCompleteDirectory(candidates) == Normalized(sibling));
	CHECK(FindFirstCompleteDll(candidates)
		== Normalized(sibling / DllFilename));
}

TEST_CASE("Incomplete ScriptCore artifact candidates fail closed") {
	ScopedArtifactDirectory artifacts;
	const auto executableLocal = artifacts.Root / "Runtime";
	const auto sibling = artifacts.Root / "Funke-ScriptCore";
	WriteArtifact(executableLocal, DllFilename);
	WriteArtifact(executableLocal, "Funke-ScriptCore.runtimeconfig.json");
	WriteArtifact(sibling, DllFilename);
	WriteArtifact(sibling, "Funke-ScriptCore.deps.json");
	const std::vector<std::filesystem::path> candidates = { executableLocal, sibling };

	CHECK(FindFirstCompleteDirectory(candidates).empty());
	CHECK(FindFirstCompleteDll(candidates).empty());
}
