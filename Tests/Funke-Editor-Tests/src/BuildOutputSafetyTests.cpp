#include <doctest/doctest.h>

#include "Project/BuildOutputSafety.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace Funke::BuildOutputSafety;

namespace {
	struct ScopedBuildOutputDirectory {
		std::filesystem::path Root;

		ScopedBuildOutputDirectory() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path()
				/ ("FunkeBuildOutputSafety_" + std::to_string(suffix));
			std::filesystem::create_directories(Root / "Assets");
		}

		~ScopedBuildOutputDirectory() {
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}
	};
}

TEST_CASE("Build output accepts the default project Builds directory") {
	ScopedBuildOutputDirectory directory;
	const auto result = ValidateOutputPath(
		directory.Root,
		directory.Root / "Assets",
		directory.Root / "Builds" / "Windows");

	CHECK(result.IsValid());
	CHECK(result.Error == OutputPathError::None);
	CHECK(result.CanonicalOutput.is_absolute());
}

TEST_CASE("Build output rejects the project root after normalization") {
	ScopedBuildOutputDirectory directory;

	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets", directory.Root).Error
		== OutputPathError::ProjectRoot);
	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets",
		directory.Root / "Builds" / "..").Error == OutputPathError::ProjectRoot);
}

TEST_CASE("Build output rejects project ancestors and source directories") {
	ScopedBuildOutputDirectory directory;

	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets",
		directory.Root.parent_path()).Error == OutputPathError::ContainsProject);
	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets",
		directory.Root / "Source").Error == OutputPathError::ProjectDirectory);
	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets",
		directory.Root / "NativeScripts" / "Build").Error == OutputPathError::ProjectDirectory);
}

TEST_CASE("Build output rejects a filesystem root") {
	ScopedBuildOutputDirectory directory;
	const auto filesystemRoot = directory.Root.root_path();

	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets", filesystemRoot).Error
		== OutputPathError::FilesystemRoot);
}

TEST_CASE("Build output only replaces empty or Funke-owned directories") {
	ScopedBuildOutputDirectory directory;
	const auto output = directory.Root / "Builds" / "Windows";
	std::filesystem::create_directories(output);
	std::ofstream(output / "unrelated.txt") << "keep";

	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets", output).Error
		== OutputPathError::UnownedExistingDirectory);
	std::ofstream(output / std::string(OwnershipMarkerFilename)) << "Funke build output";
	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets", output).IsValid());
}

TEST_CASE("Build output recognizes pre-marker Funke builds") {
	ScopedBuildOutputDirectory directory;
	const auto output = directory.Root / "Builds" / "Windows";
	std::filesystem::create_directories(output / "Assets");
	std::ofstream(output / "project.json") << "{}";
	std::ofstream(output / "Funke-Engine.dll") << "binary";

	CHECK(LooksLikeLegacyFunkeBuildOutput(output));
	CHECK(ValidateOutputPath(directory.Root, directory.Root / "Assets", output).IsValid());
}

TEST_CASE("Build path segments reject traversal and platform separators") {
	CHECK(IsSafePathSegment("My Game"));
	CHECK(IsSafePathSegment("Pkg.Example"));
	CHECK_FALSE(IsSafePathSegment(".."));
	CHECK_FALSE(IsSafePathSegment("../escape"));
	CHECK_FALSE(IsSafePathSegment("..\\escape"));
	CHECK_FALSE(IsSafePathSegment("C:escape"));
#ifdef FNK_PLATFORM_WINDOWS
	CHECK_FALSE(IsSafePathSegment("CON"));
	CHECK_FALSE(IsSafePathSegment("nul.txt"));
	CHECK_FALSE(IsSafePathSegment("COM1"));
	CHECK_FALSE(IsSafePathSegment("Lpt9.log"));
#endif
}

TEST_CASE("Build relative paths reject destination escapes") {
	CHECK(IsSafeRelativePath(std::filesystem::path("Textures") / "player.png"));
	CHECK_FALSE(IsSafeRelativePath({}));
	CHECK_FALSE(IsSafeRelativePath("../outside.txt"));
	CHECK_FALSE(IsSafeRelativePath(std::filesystem::path("nested") / ".." / "outside.txt"));
	CHECK_FALSE(IsSafeRelativePath(std::filesystem::temp_directory_path() / "outside.txt"));
}

TEST_CASE("Build artifacts resolve only from the first complete candidate") {
	ScopedBuildOutputDirectory directory;
	const auto incomplete = directory.Root / "incomplete";
	const auto complete = directory.Root / "complete";
	std::filesystem::create_directories(incomplete);
	std::filesystem::create_directories(complete);
	std::ofstream(incomplete / "runtime.exe") << "runtime";
	std::ofstream(complete / "runtime.exe") << "runtime";
	std::ofstream(complete / "engine.dll") << "engine";

	CHECK_FALSE(ContainsRequiredFiles(incomplete, { "runtime.exe", "engine.dll" }));
	CHECK(ContainsRequiredFiles(complete, { "runtime.exe", "engine.dll" }));
	CHECK(FindFirstCompleteArtifactDirectory({ incomplete, complete },
		{ "runtime.exe", "engine.dll" }) == complete);
}

TEST_CASE("Native package artifact lookup requires compatibility metadata") {
	ScopedBuildOutputDirectory directory;
	const auto stalePrebuilt = directory.Root / "prebuilt";
	const auto rebuilt = directory.Root / "rebuilt";
	std::filesystem::create_directories(stalePrebuilt);
	std::filesystem::create_directories(rebuilt);
	std::ofstream(stalePrebuilt / "Pkg.Game.Tools.Native.dll") << "stale";
	std::ofstream(rebuilt / "Pkg.Game.Tools.Native.dll") << "rebuilt";
	std::ofstream(rebuilt / "Pkg.Game.Tools.Native.dll.funkeabi") << "schema=1";

	CHECK(FindFirstCompleteArtifactDirectory({ stalePrebuilt, rebuilt },
		{ "Pkg.Game.Tools.Native.dll", "Pkg.Game.Tools.Native.dll.funkeabi" }) == rebuilt);
}

TEST_CASE("Build artifact lookup fails closed for unsafe relative requirements") {
	ScopedBuildOutputDirectory directory;
	std::ofstream(directory.Root.parent_path() / "outside.dll") << "outside";

	CHECK_FALSE(ContainsRequiredFiles(directory.Root, { "../outside.dll" }));
	CHECK(FindFirstCompleteArtifactDirectory({ directory.Root },
		{ "../outside.dll" }).empty());
}

TEST_CASE("Release game exports do not require the profiler runtime library") {
	CHECK(IsProfilerRuntimeLibraryRequired(true, RuntimePayloadKind::Workspace));
	CHECK(IsProfilerRuntimeLibraryRequired(true, RuntimePayloadKind::BundledDevelopment));
	CHECK_FALSE(IsProfilerRuntimeLibraryRequired(true, RuntimePayloadKind::BundledDist));
	CHECK_FALSE(IsProfilerRuntimeLibraryRequired(false, RuntimePayloadKind::Workspace));
	CHECK_FALSE(IsProfilerRuntimeLibraryRequired(false, RuntimePayloadKind::BundledDevelopment));
	CHECK_FALSE(IsProfilerRuntimeLibraryRequired(false, RuntimePayloadKind::BundledDist));
}

TEST_CASE("Build input fingerprints detect source-tree changes") {
	ScopedBuildOutputDirectory directory;
	std::ofstream(directory.Root / "Assets" / "player.cs") << "first";
	const DirectoryFingerprint before = CaptureDirectoryFingerprint(directory.Root / "Assets");
	REQUIRE(before.Succeeded());

	std::ofstream(directory.Root / "Assets" / "enemy.cs") << "second";
	const DirectoryFingerprint after = CaptureDirectoryFingerprint(directory.Root / "Assets");
	REQUIRE(after.Succeeded());
	CHECK_FALSE(before == after);
	CHECK(after.Entries.size() == before.Entries.size() + 1);
}

TEST_CASE("Build output rejects the Assets tree after normalization") {
	ScopedBuildOutputDirectory directory;
	const auto assets = directory.Root / "Assets";

	CHECK(ValidateOutputPath(directory.Root, assets, assets).Error
		== OutputPathError::AssetsDirectory);
	CHECK(ValidateOutputPath(directory.Root, assets, assets / "Build").Error
		== OutputPathError::AssetsDirectory);
	CHECK(ValidateOutputPath(directory.Root, assets,
		directory.Root / "Builds" / ".." / "Assets" / "Nested").Error
		== OutputPathError::AssetsDirectory);
	CHECK(ValidateOutputPath(directory.Root, assets, directory.Root / "AssetsBackup").Error
		== OutputPathError::ProjectDirectory);
}

#ifdef FNK_PLATFORM_WINDOWS
TEST_CASE("Build output comparison is case-insensitive on Windows") {
	ScopedBuildOutputDirectory directory;
	const auto assets = directory.Root / "Assets";
	const auto lowerCaseAssets = directory.Root / "assets" / "Build";

	CHECK(ValidateOutputPath(directory.Root, assets, lowerCaseAssets).Error
		== OutputPathError::AssetsDirectory);
}
#endif
