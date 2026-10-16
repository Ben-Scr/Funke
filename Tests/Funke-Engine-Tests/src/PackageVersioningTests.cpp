#include <doctest/doctest.h>

#include "Core/PackageHost.hpp"
#include "Packages/PackageLock.hpp"
#include "Packages/PackageManifest.hpp"
#include "Packages/PackageRegistryIndex.hpp"
#include "Packages/PackageResolver.hpp"
#include "Packages/SemanticVersion.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

using namespace Funke;

namespace {
	SemanticVersion V(const char* text) {
		const auto version = SemanticVersion::Parse(text);
		REQUIRE(version.has_value());
		return *version;
	}

	bool InRange(const char* range, const char* version) {
		const auto parsed = VersionRange::Parse(range);
		REQUIRE(parsed.has_value());
		return parsed->IsSatisfiedBy(V(version));
	}

	PackageManifest Manifest(const char* name, const char* version, std::vector<PackageDependency> dependencies = {}, const char* engine = "") {
		PackageManifest manifest;
		manifest.Name = name;
		manifest.VersionText = version;
		manifest.Version = SemanticVersion::Parse(version);
		manifest.Dependencies = std::move(dependencies);
		manifest.EngineRange = engine;
		manifest.PackageDir = std::string("Packages/") + name;
		return manifest;
	}

	bool Contains(const std::vector<std::string>& values, const char* value) {
		return std::find(values.begin(), values.end(), value) != values.end();
	}

	size_t IndexOf(const std::vector<std::string>& values, const char* value) {
		return static_cast<size_t>(std::find(values.begin(), values.end(), value) - values.begin());
	}
}

TEST_CASE("SemanticVersion parses and orders per SemVer 2.0") {
	CHECK(V("1.2.3").ToString() == "1.2.3");
	CHECK(V("v2026.1.0-beta.1").ToString() == "2026.1.0-beta.1");
	CHECK(V("1.0.0+build.5").Build == "build.5");
	CHECK_FALSE(SemanticVersion::Parse("1.2").has_value());
	CHECK_FALSE(SemanticVersion::Parse("01.2.3").has_value());
	CHECK_FALSE(SemanticVersion::Parse("1.2.3-").has_value());

	// Precedence chain from the SemVer spec.
	const char* ordered[] = { "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
		"1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0" };
	for (size_t i = 0; i + 1 < std::size(ordered); ++i) {
		CAPTURE(ordered[i]);
		CHECK(V(ordered[i]) < V(ordered[i + 1]));
	}
	CHECK(V("1.0.0+a") == V("1.0.0+b"));
}

TEST_CASE("VersionRange covers caret, tilde, x-ranges, comparators, hyphen and OR") {
	CHECK(InRange("^1.2.3", "1.9.0"));
	CHECK_FALSE(InRange("^1.2.3", "2.0.0"));
	CHECK_FALSE(InRange("^1.2.3", "1.2.2"));
	CHECK(InRange("^0.2.3", "0.2.9"));
	CHECK_FALSE(InRange("^0.2.3", "0.3.0"));
	CHECK_FALSE(InRange("^0.0.3", "0.0.4"));
	CHECK(InRange("~1.2.3", "1.2.9"));
	CHECK_FALSE(InRange("~1.2.3", "1.3.0"));
	CHECK(InRange("1.x", "1.7.2"));
	CHECK_FALSE(InRange("1.x", "2.0.0"));
	CHECK(InRange("1.2", "1.2.5"));
	CHECK(InRange(">=1.0.0 <2.0.0", "1.5.0"));
	CHECK_FALSE(InRange(">=1.0.0 <2.0.0", "2.0.0"));
	CHECK(InRange(">= 1.0.0", "3.0.0"));
	CHECK(InRange("<=1.2", "1.2.9"));
	CHECK_FALSE(InRange("<1.2", "1.2.0"));
	CHECK(InRange("1.0.0 - 2.0.0", "2.0.0"));
	CHECK(InRange("1.0 - 2", "2.9.9"));
	CHECK(InRange("^1.0.0 || ^3.0.0", "3.1.0"));
	CHECK_FALSE(InRange("^1.0.0 || ^3.0.0", "2.1.0"));
	CHECK(InRange("*", "4.0.0"));
	CHECK(InRange("", "0.0.1"));

	// Pre-releases need an explicit opt-in on the same MAJOR.MINOR.PATCH.
	CHECK_FALSE(InRange("^1.0.0", "1.5.0-beta"));
	CHECK_FALSE(InRange("^1.0.0", "2.0.0-beta"));
	CHECK(InRange(">=1.5.0-beta <2.0.0", "1.5.0-rc.1"));
	CHECK_FALSE(InRange("*", "1.0.0-beta"));

	CHECK_FALSE(VersionRange::Parse("^banana").has_value());
	CHECK_FALSE(VersionRange::Parse(">=1.0.0 <").has_value());
}

TEST_CASE("PackageManifest reads names, versions, engine ranges, dependencies and layers") {
	const char* source = R"LUA(
-- A package with every dependency form.
return {
    name = "Funke.Tilemap2D",
    version = "1.4.0",
    description = 'Tile maps; "quoted"',
    engine = ">=2026.1.0 <2027.0.0",
    dependencies = {
        "Funke.Core2D",
        "Funke.Physics2D@^2.1.0",
        { name = "Funke.Serialization", version = "~1.0" },
        ["Funke.Math"] = ">=0.3.0",
    },
    layers = {
        native = { sources = { "src/**.cpp" } },
        csharp = { sources = { "Source/**.cs" }, pinvoke_dll = "Pkg.Funke.Tilemap2D.Native", allow_unsafe = true },
    },
}
)LUA";
	PackageManifest manifest;
	std::string error;
	REQUIRE_MESSAGE(PackageManifest::TryParse(source, manifest, &error), error);
	CHECK(manifest.Name == "Funke.Tilemap2D");
	REQUIRE(manifest.Version.has_value());
	CHECK(*manifest.Version == V("1.4.0"));
	CHECK(manifest.Description == "Tile maps; \"quoted\"");
	CHECK(manifest.EngineRange == ">=2026.1.0 <2027.0.0");
	REQUIRE(manifest.Dependencies.size() == 4);
	CHECK(manifest.FindDependency("Funke.Core2D")->Range == "*");
	CHECK(manifest.FindDependency("Funke.Physics2D")->Range == "^2.1.0");
	CHECK(manifest.FindDependency("Funke.Serialization")->Range == "~1.0");
	CHECK(manifest.FindDependency("Funke.Math")->Range == ">=0.3.0");
	CHECK(manifest.HasNativeLayer);
	CHECK(manifest.HasCSharpLayer);
	CHECK(manifest.CSharpAllowUnsafe);
	CHECK_FALSE(manifest.HasNativeStandaloneLayer);

	CHECK(manifest.IsCompatibleWithEngine(V("2026.4.1")));
	CHECK(manifest.IsCompatibleWithEngine(V("2026.1.0-beta.1")));
	CHECK_FALSE(manifest.IsCompatibleWithEngine(V("2027.0.0")));
}

TEST_CASE("PackageManifest rejects what it cannot read safely") {
	PackageManifest manifest;
	std::string error;
	CHECK_FALSE(PackageManifest::TryParse("return { name = \"bad\", version = \"1.0.0\" }", manifest, &error));
	CHECK_FALSE(PackageManifest::TryParse("return { name = \"Funke.A\" }", manifest, &error));
	CHECK_FALSE(PackageManifest::TryParse("return { name = \"Funke.A\", version = os.getenv(\"V\") }", manifest, &error));
	CHECK_FALSE(PackageManifest::TryParse("return { name = \"Funke.\" .. \"A\", version = \"1.0.0\" }", manifest, &error));
	CHECK_FALSE(PackageManifest::TryParse("return { name = \"Funke.A\", version = \"1.0.0\", dependencies = { \"Funke.A\" } }", manifest, &error));
	CHECK_FALSE(PackageManifest::TryParse("return { name = \"Funke.A\", version = \"1.0.0\", dependencies = { \"Funke.B@^banana\" } }", manifest, &error));
	CHECK_FALSE(PackageManifest::TryParse("return { name = \"Funke.A\", version = \"1.0.0\", engine = \">=banana\" }", manifest, &error));

	// `local m = { ... } return m`, comments and long strings are fine; a non-SemVer version still loads.
	REQUIRE(PackageManifest::TryParse("--[[ header ]]\nlocal m = { name = \"Funke.A\", version = \"1.0\", description = [[multi\nline]] }\nreturn m", manifest, &error));
	CHECK_FALSE(manifest.Version.has_value());
	CHECK(manifest.Description == "multi\nline");

	// Notepad and PowerShell's -Encoding utf8 write a BOM; Lua accepts it, so must we.
	REQUIRE_MESSAGE(PackageManifest::TryParse("\xEF\xBB\xBFreturn { name = \"Funke.Bom\", version = \"1.0.0\" }", manifest, &error), error);
	CHECK(manifest.Name == "Funke.Bom");
}

TEST_CASE("Package resolution orders dependencies first and pulls in transitive ones") {
	const std::vector<PackageManifest> available = {
		Manifest("Game.App", "1.0.0", { { "Game.Ui", "^2.0.0" } }),
		Manifest("Game.Ui", "2.3.0", { { "Game.Core", ">=1.1.0" } }),
		Manifest("Game.Core", "1.2.0"),
		Manifest("Game.Unused", "1.0.0"),
	};
	const PackageResolution resolution = ResolvePackages(available, { "Game.App" }, V("2026.1.0"));
	CHECK_FALSE(resolution.HasErrors());
	REQUIRE(resolution.LoadOrder.size() == 3);
	CHECK(IndexOf(resolution.LoadOrder, "Game.Core") < IndexOf(resolution.LoadOrder, "Game.Ui"));
	CHECK(IndexOf(resolution.LoadOrder, "Game.Ui") < IndexOf(resolution.LoadOrder, "Game.App"));
	CHECK(Contains(resolution.AutoIncluded, "Game.Core"));
	CHECK_FALSE(Contains(resolution.LoadOrder, "Game.Unused"));
}

TEST_CASE("Package resolution blocks only the failing package and its dependents") {
	const std::vector<PackageManifest> available = {
		Manifest("Game.App", "1.0.0", { { "Game.Ui", "^3.0.0" } }),
		Manifest("Game.Ui", "2.3.0"),
		Manifest("Game.Tools", "1.0.0"),
		Manifest("Game.Future", "1.0.0", {}, ">=2030.0.0"),
		Manifest("Game.NeedsMissing", "1.0.0", { { "Game.Gone", "*" } }),
		Manifest("Game.LoopA", "1.0.0", { { "Game.LoopB", "*" } }),
		Manifest("Game.LoopB", "1.0.0", { { "Game.LoopA", "*" } }),
	};
	const PackageResolution resolution = ResolvePackages(available,
		{ "Game.App", "Game.Tools", "Game.Future", "Game.NeedsMissing", "Game.LoopA", "Game.Typo" }, V("2026.1.0"));

	CHECK(resolution.HasErrors());
	CHECK(Contains(resolution.LoadOrder, "Game.Tools"));
	// Ui itself is fine, but App (its only dependent) is blocked, so nothing needs it.
	CHECK_FALSE(Contains(resolution.LoadOrder, "Game.Ui"));
	CHECK_FALSE(resolution.IsBlocked("Game.Ui"));
	for (const char* blocked : { "Game.App", "Game.Future", "Game.NeedsMissing", "Game.Gone", "Game.LoopA", "Game.LoopB", "Game.Typo" }) {
		CAPTURE(blocked);
		CHECK(resolution.IsBlocked(blocked));
		CHECK_FALSE(Contains(resolution.LoadOrder, blocked));
	}
	const auto appMessages = resolution.GetMessages("Game.App");
	REQUIRE_FALSE(appMessages.empty());
	CHECK(appMessages.front().find("requires Game.Ui ^3.0.0, but 2.3.0 is installed") != std::string::npos);
	CHECK_FALSE(resolution.GetMessages("Game.Future").empty());
}

TEST_CASE("Native package load order puts dependencies first and keeps manifest-less packages") {
	const std::vector<PackageManifest> manifests = {
		Manifest("Game.App", "1.0.0", { { "Game.Core", "*" } }),
		Manifest("Game.Core", "1.0.0"),
		Manifest("Game.Broken", "1.0.0", { { "Game.Gone", "*" } }),
	};
	const std::vector<std::string> requested = { "Game.Shipped", "Game.App", "Game.Broken" };
	const PackageResolution resolution = ResolvePackages(manifests, requested, V("2026.1.0"));
	const std::vector<std::string> order = PackageHostDetail::BuildNativeLoadOrder(resolution, requested, manifests);

	REQUIRE(order.size() == 3);
	CHECK(order[0] == "Game.Core");
	CHECK(order[1] == "Game.App");
	CHECK(order[2] == "Game.Shipped"); // shipped DLL without a manifest: loads, after the resolved ones
	CHECK_FALSE(Contains(order, "Game.Broken"));
}

TEST_CASE("Registry index reads multiple releases and picks the newest compatible one") {
	const std::string sha(64, 'a');
	const std::string json = R"({ "packages": [
		{ "name": "Funke.Tiles", "version": "2.1.0", "downloadUrl": "https://x/tiles-2.1.0.zip", "sha256": ")" + sha + R"(",
		  "engine": ">=2027.0.0", "dependencies": ["Funke.Grid@^1.0.0"], "layers": ["native", "csharp"],
		  "versions": [
			{ "version": "1.9.0", "downloadUrl": "https://x/tiles-1.9.0.zip", "sha256": ")" + sha + R"(", "minEngineVersion": "2026.1.0",
			  "dependencies": [{ "name": "Funke.Grid", "version": "^1.0.0" }] },
			{ "version": "2.0.0-beta", "downloadUrl": "https://x/tiles-2.0.0-beta.zip", "sha256": ")" + sha + R"(" },
			{ "version": "1.0.0", "downloadUrl": "https://x/tiles-1.0.0.zip", "sha256": "not-a-sha" }
		  ] },
		{ "name": "Funke.Grid", "version": "1.3.0", "downloadUrl": "https://x/grid.zip", "sha256": ")" + sha + R"(" },
		{ "name": "bad name", "version": "1.0.0", "downloadUrl": "https://x/bad.zip", "sha256": ")" + sha + R"(" }
	] })";

	std::vector<RegistryPackage> registry;
	std::string error;
	REQUIRE_MESSAGE(ParseRegistryIndex(json, registry, error), error);
	REQUIRE(registry.size() == 2);
	const RegistryPackage* tiles = FindRegistryPackage(registry, "Funke.Tiles");
	REQUIRE(tiles);
	REQUIRE(tiles->Versions.size() == 3);
	CHECK(tiles->Latest()->VersionText == "2.1.0");
	CHECK(tiles->HasNativeLayer);
	CHECK(tiles->Versions[2].VersionText == "1.9.0");
	CHECK(tiles->Versions[2].EngineRange == ">=2026.1.0");

	const RegistryPackageVersion* best = tiles->FindBest(VersionRange::Any(), V("2026.2.0"));
	REQUIRE(best);
	CHECK(best->VersionText == "1.9.0"); // 2.1.0 needs a newer engine, the beta needs an explicit opt-in
	CHECK(tiles->FindBest(*VersionRange::Parse("^2.0.0"), V("2026.2.0")) == nullptr);
}

TEST_CASE("Install plans pull dependencies, update when ranges demand it and refuse breaking updates") {
	const std::string sha(64, 'b');
	const std::string json = R"({ "packages": [
		{ "name": "Funke.Tiles", "version": "1.2.0", "downloadUrl": "https://x/t.zip", "sha256": ")" + sha + R"(", "dependencies": ["Funke.Grid@^1.4.0"] },
		{ "name": "Funke.Grid", "version": "2.0.0", "downloadUrl": "https://x/g2.zip", "sha256": ")" + sha + R"(",
		  "versions": [ { "version": "1.5.0", "downloadUrl": "https://x/g15.zip", "sha256": ")" + sha + R"(" } ] }
	] })";
	std::vector<RegistryPackage> registry;
	std::string error;
	REQUIRE(ParseRegistryIndex(json, registry, error));
	const SemanticVersion engine = V("2026.1.0");

	SUBCASE("fresh install pulls the newest dependency release inside the range") {
		const PackageInstallPlan plan = PlanRegistryInstall(registry, {}, "Funke.Tiles", "*", engine, false);
		REQUIRE(plan.IsValid());
		REQUIRE(plan.Steps.size() == 2);
		CHECK(plan.Steps[0].Name == "Funke.Grid");
		CHECK(plan.Steps[0].Version == "1.5.0");
		CHECK_FALSE(plan.Steps[0].Requested);
		CHECK(plan.Steps[1].Name == "Funke.Tiles");
		CHECK(plan.Steps[1].Requested);
	}
	SUBCASE("an installed dependency that fits stays") {
		const PackageInstallPlan plan = PlanRegistryInstall(registry, { Manifest("Funke.Grid", "1.4.2") }, "Funke.Tiles", "*", engine, false);
		REQUIRE(plan.IsValid());
		REQUIRE(plan.Steps.size() == 1);
		CHECK(plan.Steps[0].Name == "Funke.Tiles");
	}
	SUBCASE("an installed dependency that is too old is updated") {
		const PackageInstallPlan plan = PlanRegistryInstall(registry, { Manifest("Funke.Grid", "1.0.0") }, "Funke.Tiles", "*", engine, false);
		REQUIRE(plan.IsValid());
		REQUIRE(plan.Steps.size() == 2);
		CHECK(plan.Steps[0].Kind == PackageInstallStep::Action::Update);
		CHECK(plan.Steps[0].FromVersion == "1.0.0");
		CHECK(plan.Steps[0].Version == "1.5.0");
	}
	SUBCASE("updating past a range another package declares is refused") {
		const std::vector<PackageManifest> installed = {
			Manifest("Funke.Grid", "1.5.0"),
			Manifest("Funke.Tiles", "1.2.0", { { "Funke.Grid", "^1.4.0" } }),
		};
		const PackageInstallPlan plan = PlanRegistryInstall(registry, installed, "Funke.Grid", "*", engine, true);
		CHECK_FALSE(plan.IsValid());
		REQUIRE_FALSE(plan.Errors.empty());
		CHECK(plan.Errors.front().find("would break 'Funke.Tiles'") != std::string::npos);
	}
	SUBCASE("a missing package is reported") {
		const PackageInstallPlan plan = PlanRegistryInstall(registry, {}, "Funke.Nope", "*", engine, false);
		CHECK_FALSE(plan.IsValid());
	}
}

TEST_CASE("Package lock round-trips and keeps sources while versions match") {
	const std::filesystem::path root = std::filesystem::temp_directory_path()
		/ ("funke-lock-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(root);
	const std::string rootText = root.string();

	PackageLock lock;
	lock.EngineVersion = "2026.1.0";
	lock.Upsert({ "Funke.Grid", "1.5.0", "registry", "https://x/g.zip", std::string(64, 'c'), {} });
	lock.Upsert({ "Funke.Tiles", "1.2.0", "git", "https://github.com/x/tiles", "", { "Funke.Grid@^1.4.0" } });
	REQUIRE(lock.Save(rootText));

	PackageLock loaded;
	std::string error;
	REQUIRE_MESSAGE(PackageLock::TryLoad(rootText, loaded, &error), error);
	REQUIRE(loaded.Packages.size() == 2);
	CHECK(loaded.Find("Funke.Grid")->Sha256 == std::string(64, 'c'));
	CHECK(loaded.Find("Funke.Tiles")->Dependencies.front() == "Funke.Grid@^1.4.0");

	const std::vector<PackageManifest> manifests = { Manifest("Funke.Grid", "1.5.0"), Manifest("Funke.Tiles", "1.3.0") };
	const PackageLock rebuilt = PackageLock::Build({ "Funke.Grid", "Funke.Tiles" }, manifests, loaded, "2026.1.0");
	CHECK(rebuilt.Find("Funke.Grid")->Source == "registry");
	CHECK(rebuilt.Find("Funke.Tiles")->Source == "unknown"); // version changed, old source no longer describes it

	PackageLock missing;
	CHECK(PackageLock::TryLoad((root / "nothing-here").string(), missing, &error));
	CHECK(missing.Packages.empty());

	std::error_code ec;
	std::filesystem::remove_all(root, ec);
}

TEST_CASE("PackageManifest reads the engine modules a package builds on") {
	PackageManifest manifest;
	std::string error;
	REQUIRE_MESSAGE(PackageManifest::TryParse(R"LUA(
return { name = "Funke.Tilemap2D", version = "1.0.0", modules = { "Funke.2D", "Funke.2D" } }
)LUA", manifest, &error), error);
	REQUIRE(manifest.Modules.size() == 1);
	CHECK(manifest.Modules[0] == "Funke.2D");

	CHECK_FALSE(PackageManifest::TryParse(R"LUA(return { name = "Funke.A", version = "1.0.0", modules = { "Funke.4D" } })LUA", manifest, &error));
	CHECK(error.find("Funke.4D") != std::string::npos);
	CHECK_FALSE(PackageManifest::TryParse(R"LUA(return { name = "Funke.A", version = "1.0.0", modules = "Funke.2D" })LUA", manifest, &error));
	CHECK_FALSE(PackageManifest::TryParse(R"LUA(return { name = "Funke.A", version = "1.0.0", modules = { a = "Funke.2D" } })LUA", manifest, &error));
}

TEST_CASE("Packages that need a disabled engine module are blocked, with their dependents") {
	PackageManifest tilemap = Manifest("Funke.Tilemap2D", "1.0.0");
	tilemap.Modules = { "Funke.2D" };
	const PackageManifest level = Manifest("Game.Levels", "1.0.0", { { "Funke.Tilemap2D", "*" } });
	const PackageManifest ui = Manifest("Game.Ui", "1.0.0");
	const std::vector<PackageManifest> available{ tilemap, level, ui };
	const std::vector<std::string> requested{ "Game.Levels", "Game.Ui" };

	const std::vector<std::string> only3D{ "Funke.3D" };
	const PackageResolution blocked = ResolvePackages(available, requested, V("2026.1.0"), &only3D);
	CHECK(blocked.IsBlocked("Funke.Tilemap2D"));
	CHECK(blocked.IsBlocked("Game.Levels"));
	CHECK(Contains(blocked.LoadOrder, "Game.Ui"));
	bool mentionsModule = false;
	for (const std::string& message : blocked.GetMessages("Funke.Tilemap2D")) mentionsModule |= message.find("Funke.2D") != std::string::npos;
	CHECK(mentionsModule);

	const std::vector<std::string> with2D{ "Funke.2D" };
	const PackageResolution ok = ResolvePackages(available, requested, V("2026.1.0"), &with2D);
	CHECK_FALSE(ok.HasErrors());
	CHECK(Contains(ok.LoadOrder, "Game.Levels"));
	// Without a module list (tools that don't know the project) modules aren't checked.
	CHECK_FALSE(ResolvePackages(available, requested, V("2026.1.0")).HasErrors());

	const std::vector<std::string> needed = CollectRequiredModules(available, "Game.Levels");
	REQUIRE(needed.size() == 1);
	CHECK(needed[0] == "Funke.2D");
	CHECK(CollectRequiredModules(available, "Game.Ui").empty());
	const std::vector<std::string> dependents = FindModuleDependents(available, requested, "Funke.2D");
	REQUIRE(dependents.size() == 1);
	CHECK(dependents[0] == "Game.Levels");
}

TEST_CASE("Registry releases carry engine modules; unknown modules can't be installed") {
	const char* json = R"JSON({ "packages": [
		{ "name": "Funke.Tilemap2D", "version": "1.0.0", "downloadUrl": "https://example.com/t.zip",
		  "sha256": "0000000000000000000000000000000000000000000000000000000000000000", "modules": [ "Funke.2D" ] },
		{ "name": "Funke.Hologram", "version": "1.0.0", "downloadUrl": "https://example.com/h.zip",
		  "sha256": "0000000000000000000000000000000000000000000000000000000000000000", "modules": [ "Funke.4D" ] }
	] })JSON";
	std::vector<RegistryPackage> registry;
	std::string error;
	REQUIRE_MESSAGE(ParseRegistryIndex(json, registry, error), error);
	REQUIRE(registry.size() == 2);
	REQUIRE(registry[0].Latest() != nullptr);
	REQUIRE(registry[0].Latest()->Modules.size() == 1);
	CHECK(registry[0].Latest()->Modules[0] == "Funke.2D");

	const PackageInstallPlan tilemap = PlanRegistryInstall(registry, {}, "Funke.Tilemap2D", "*", V("2026.1.0"), false);
	CHECK(tilemap.IsValid());
	const PackageInstallPlan hologram = PlanRegistryInstall(registry, {}, "Funke.Hologram", "*", V("2026.1.0"), false);
	CHECK_FALSE(hologram.IsValid());
}
