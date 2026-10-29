#include <doctest/doctest.h>

#ifdef FNK_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "Project/FunkeProject.hpp"
#include "Project/PackageBuildCache.hpp"

#include <chrono>
#include <filesystem>
#include <string_view>

namespace {
	struct PackageCacheFixture {
		std::filesystem::path Root = std::filesystem::temp_directory_path() / ("funke-package-cache-"
			+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		Funke::FunkeProject Project;
		PackageCacheFixture() {
			Project.RootDirectory = Funke::Path::ToUtf8(Root);
			Project.PackagesDirectory = Funke::Path::ToUtf8(Root / "Packages");
			std::filesystem::create_directories(Root);
		}
		~PackageCacheFixture() { std::error_code error; std::filesystem::remove_all(Root, error); }
		void Write(const std::filesystem::path& path, const std::string& text) {
			std::filesystem::create_directories(path.parent_path());
			REQUIRE(Funke::File::WriteAllText(Funke::Path::ToUtf8(path), text));
		}
	};
}

TEST_CASE("Project package preparation skips tools when there are no packages") {
	PackageCacheFixture fixture;
	const auto result = Funke::FunkeProject::PrepareProjectPackages(fixture.Project, "Release");
	CHECK(result.Succeeded);
	CHECK_FALSE(std::filesystem::exists(fixture.Root / ".funke"));
	CHECK_FALSE(Funke::FunkeProject::PrepareProjectPackages(fixture.Project, "../Release").Succeeded);
}

TEST_CASE("Project package input inventory detects additions deletions and same-stamp edits") {
	using namespace Funke::PackageBuildCache;
	PackageCacheFixture fixture;
	const auto source = fixture.Root / "Source/Example.cs";
	fixture.Write(source, "class Example { const int Value = 1; }");
	const auto stamp = std::filesystem::last_write_time(source);
	Inventory before;
	before.Tree(fixture.Root / "Source", true);
	REQUIRE_FALSE(before.Digest().empty());
	SUBCASE("same size and timestamp still detect changed source") {
		fixture.Write(source, "class Example { const int Value = 2; }");
		std::filesystem::last_write_time(source, stamp);
	}
	SUBCASE("new source") { fixture.Write(fixture.Root / "Source/New.cs", "class New {}"); }
	SUBCASE("removed source") { std::filesystem::remove(source); }
	Inventory after;
	after.Tree(fixture.Root / "Source", true);
	CHECK(before.Digest() != after.Digest());
}

TEST_CASE("Project package cache rejects missing changed and malformed artifacts") {
	using namespace Funke;
	using namespace Funke::PackageBuildCache;
	PackageCacheFixture fixture;
	Snapshot snapshot;
	snapshot.Inputs = "source-fingerprint";
	snapshot.ManagedTargets = { "Pkg.Bench.Managed" };
	const auto workspace = fixture.Root / ".funke/build/packages";
	const auto generated = workspace / "generated/Pkg.Bench.Managed/Pkg.Bench.Managed.csproj";
	const auto output = Path::FromUtf8(FunkeProject::GetProjectPackageOutputDirectory(fixture.Project.RootDirectory, "Pkg.Bench.Managed", "Release"));
	const auto library = output / "Pkg.Bench.Managed.dll";
	fixture.Write(generated, "<Project />");
	fixture.Write(library, "assembly");
	fixture.Write(output / "Pkg.Bench.Managed.deps.json", "{}");
	const auto cache = CachePath(fixture.Project, "Release", "x64");
	Store(cache, fixture.Project, snapshot, "Release");
	REQUIRE(Matches(cache, fixture.Project, snapshot, "Release"));
	SUBCASE("missing assembly") { std::filesystem::remove(library); }
	SUBCASE("changed assembly") { fixture.Write(library, "different assembly"); }
	SUBCASE("missing dependency manifest") { std::filesystem::remove(output / "Pkg.Bench.Managed.deps.json"); }
	SUBCASE("missing generated project") { std::filesystem::remove(generated); }
	SUBCASE("edited generated project") { fixture.Write(generated, "<Project><PropertyGroup /></Project>"); }
	SUBCASE("corrupt stamp") { fixture.Write(cache, "{partial"); }
	SUBCASE("different inputs") { snapshot.Inputs = "changed-source"; }
	CHECK_FALSE(Matches(cache, fixture.Project, snapshot, "Release"));
}

TEST_CASE("Project package cache refuses untracked external build inputs") {
	using Funke::PackageManifest;
	CHECK(PackageManifest::HasLocalBuildInputs("return { layers = { csharp = { sources = { 'Source/**.cs' } } } }"));
	CHECK(PackageManifest::HasLocalBuildInputs("return { layers = { native = { sources = { 'src/**.cpp' }, includes = { 'include' } } } }"));
	CHECK_FALSE(PackageManifest::HasLocalBuildInputs("return { layers = { csharp = { sources = { '../shared/**.cs' } } } }"));
	CHECK_FALSE(PackageManifest::HasLocalBuildInputs("return { layers = { native = { sources = { 'src/**.cpp' }, includes = { 'C:/shared' } } } }"));
	CHECK_FALSE(PackageManifest::HasLocalBuildInputs("return { layers = { native = { sources = { 'src/**.cpp' }, includes = { '%{workspace.location}' } } } }"));
	CHECK_FALSE(PackageManifest::HasLocalBuildInputs("return { layers = { csharp = { sources = { 'Source/**.cs' }, nuget = { 'Example:1.0.0' } } } }"));
}

TEST_CASE("Project package output paths are deterministic and traversal-safe") {
	const std::string root = (std::filesystem::path("FixtureProject") / "Root").string();
	const std::filesystem::path output =
		Funke::FunkeProject::GetProjectPackageOutputDirectory(
			root, "Pkg.Game.Tools", "Dist");

	REQUIRE_FALSE(output.empty());
	CHECK(output.filename() == "Pkg.Game.Tools");
#if defined(FNK_PLATFORM_WINDOWS)
	CHECK(output.parent_path().filename() == "Dist-windows-x86_64");
#elif defined(FNK_PLATFORM_LINUX) && (defined(__x86_64__) || defined(_M_X64))
	CHECK(output.parent_path().filename() == "Dist-linux-x86_64");
#elif defined(FNK_PLATFORM_MACOS) && defined(__aarch64__)
	CHECK(output.parent_path().filename() == "Dist-macosx-arm64");
#endif

	for (const std::string_view invalidTarget : {
		"", ".", "..", ".Pkg.Game", "Pkg.Game.", "Pkg..Game",
		"../Pkg.Game", "Pkg/Game", "Pkg\\Game", "Pkg Game" }) {
		CHECK(Funke::FunkeProject::GetProjectPackageOutputDirectory(
			root, std::string(invalidTarget), "Release").empty());
	}
	CHECK(Funke::FunkeProject::GetProjectPackageOutputDirectory(
		root, "Pkg.Game.Tools", "../Release").empty());
}

TEST_CASE("Project native package paths use platform library names") {
	const std::filesystem::path library =
		Funke::FunkeProject::GetProjectNativePackageLibraryPath(
			"FixtureProject", "Game.Tools", "Release");

#if defined(FNK_PLATFORM_WINDOWS)
	CHECK(library.filename() == "Pkg.Game.Tools.Native.dll");
#elif defined(__APPLE__)
	CHECK(library.filename() == "libPkg.Game.Tools.Native.dylib");
#else
	CHECK(library.filename() == "libPkg.Game.Tools.Native.so");
#endif

	CHECK(Funke::FunkeProject::GetProjectNativePackageLibraryPath(
		"FixtureProject", "../Game.Tools", "Release").empty());
}

TEST_CASE("Project package cache accepts stock repository build settings") {
	using namespace Funke::PackageBuildCache;
	std::filesystem::path repository;
	for (auto candidate : { std::filesystem::current_path(), Funke::Path::FromUtf8(Funke::Path::ExecutableDir()) }) {
		while (!candidate.empty()) {
			if (std::filesystem::exists(candidate / "premake5.lua") && std::filesystem::exists(candidate / "Directory.Build.props")) {
				repository = candidate;
				break;
			}
			const auto parent = candidate.parent_path();
			if (parent == candidate) break;
			candidate = parent;
		}
		if (!repository.empty()) break;
	}
	REQUIRE_FALSE(repository.empty());
	Inventory files;
	AddBuildSettings(files, repository);
	CHECK(files.Valid);
	CHECK_FALSE(files.Digest().empty());
}

TEST_CASE("Project package cache rejects custom MSBuild inputs") {
	using namespace Funke::PackageBuildCache;
	PackageCacheFixture fixture;
	std::string settings;
	SUBCASE("custom import") { settings = R"(<Import Project="custom.props" />)"; }
	SUBCASE("custom source") { settings = R"(<ItemGroup><Compile Include="../Shared.cs" /></ItemGroup>)"; }
	SUBCASE("custom reference") { settings = R"(<ItemGroup><Reference Include="Shared"><HintPath>../Shared.dll</HintPath></Reference></ItemGroup>)"; }
	SUBCASE("custom analyzer") { settings = R"(<ItemGroup><Analyzer Include="../Analyzer.dll" /></ItemGroup>)"; }
	SUBCASE("custom task") { settings = R"(<UsingTask TaskName="Prepare" AssemblyFile="../Task.dll" />)"; }
	SUBCASE("custom target") { settings = R"(<Target Name="Prepare" BeforeTargets="Build"><Exec Command="prepare" /></Target>)"; }
	SUBCASE("vcpkg dependencies") { settings = R"(<PropertyGroup><VcpkgEnabled>true</VcpkgEnabled></PropertyGroup>)"; }
	fixture.Write(fixture.Root / "Directory.Build.props", "<Project>" + settings + "</Project>");
	Inventory files;
	AddBuildSettings(files, fixture.Root);
	CHECK_FALSE(files.Valid);
	CHECK(files.Digest().empty());
}

TEST_CASE("Project package cache tracks and validates supported imported settings") {
	using namespace Funke::PackageBuildCache;
	PackageCacheFixture fixture;
	fixture.Write(fixture.Root / "Directory.Build.props", R"(<Project><Import Project="$(MSBuildThisFileDirectory)Version.props" /></Project>)");
	const auto imported = fixture.Root / "Version.props";
	fixture.Write(imported, "<Project><PropertyGroup><FunkeVersion>1.0.0</FunkeVersion></PropertyGroup></Project>");
	const auto stamp = std::filesystem::last_write_time(imported);
	Inventory before;
	AddBuildSettings(before, fixture.Root);
	REQUIRE(before.Valid);
	REQUIRE_FALSE(before.Digest().empty());
	bool supported = false;
	SUBCASE("same stamp imported property edit invalidates cache") {
		fixture.Write(imported, "<Project><PropertyGroup><FunkeVersion>2.0.0</FunkeVersion></PropertyGroup></Project>");
		std::filesystem::last_write_time(imported, stamp);
		supported = true;
	}
	SUBCASE("nested custom import disables cache") {
		fixture.Write(imported, R"(<Project><Import Project="custom.props" /></Project>)");
	}
	SUBCASE("imported custom task disables cache") {
		fixture.Write(imported, R"(<Project><UsingTask TaskName="Prepare" AssemblyFile="../Task.dll" /></Project>)");
	}
	Inventory after;
	AddBuildSettings(after, fixture.Root);
	CHECK(after.Valid == supported);
	CHECK(after.Digest() != before.Digest());
}

TEST_CASE("Project package cache accepts local manifests with a UTF8 BOM") {
	const std::string source = "\xEF\xBB\xBF" "return { layers = { csharp = { sources = { 'Source/**.cs' } } } }";
	CHECK(Funke::PackageManifest::HasLocalBuildInputs(source));
}

TEST_CASE("Project package cache tolerates leftover incomplete SDK directories") {
	using namespace Funke::PackageBuildCache;
	PackageCacheFixture fixture;
	const auto complete = fixture.Root / "sdk/10.0.303/MSBuild.dll";
	const auto incomplete = fixture.Root / "sdk/10.0.104/MSBuild.dll";
	fixture.Write(complete, "complete-sdk");
	std::filesystem::create_directories(incomplete.parent_path());
	Inventory before;
	AddManagedSdks(before, fixture.Root);
	REQUIRE(before.Valid);
	REQUIRE_FALSE(before.Digest().empty());
	bool valid = true;
	SUBCASE("an incomplete SDK becomes complete") { fixture.Write(incomplete, "restored-sdk"); }
	SUBCASE("a complete SDK is updated") { fixture.Write(complete, "updated-complete-sdk"); }
	SUBCASE("no complete SDK remains") { std::filesystem::remove(complete); valid = false; }
	Inventory after;
	AddManagedSdks(after, fixture.Root);
	CHECK(after.Valid == valid);
	CHECK(after.Digest() != before.Digest());
}

TEST_CASE("Project package cache validates settings beside generated targets") {
	using namespace Funke::PackageBuildCache;
	PackageCacheFixture fixture;
	Snapshot snapshot;
	snapshot.ManagedTargets = { "Pkg.Example" };
	snapshot.NativeTargets = { "Pkg.Example.Native" };
	const auto generated = fixture.Root / "generated";
	Inventory before;
	AddGeneratedBuildSettings(before, generated, snapshot);
	REQUIRE(before.Valid);
	SUBCASE("managed target source injection") {
		fixture.Write(generated / "Pkg.Example/Directory.Build.props",
			R"(<Project><ItemGroup><Compile Include="../../../Shared.cs" /></ItemGroup></Project>)");
	}
	SUBCASE("native target task injection") {
		fixture.Write(generated / "Pkg.Example.Native/Directory.Build.targets",
			R"(<Project><UsingTask TaskName="Prepare" AssemblyFile="../../../Task.dll" /></Project>)");
	}
	SUBCASE("managed user settings") { fixture.Write(generated / "Pkg.Example/Pkg.Example.csproj.user", "<Project />"); }
	SUBCASE("native user settings") { fixture.Write(generated / "Pkg.Example.Native/Pkg.Example.Native.vcxproj.user", "<Project />"); }
	Inventory after;
	AddGeneratedBuildSettings(after, generated, snapshot);
	CHECK_FALSE(after.Valid);
	CHECK(after.Digest().empty());
}
