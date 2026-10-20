#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

using namespace Funke;

namespace {
	// A project folder with an Android profile (plus a profile define) and one for the editor's own platform.
	struct PlatformTestProject {
		std::filesystem::path Root;
		BuildPlatform Host = BuildPlatformSupport::HostDesktopPlatform().value_or(BuildPlatform::Windows);

		PlatformTestProject() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path() / ("FunkeEditorScriptPlatform_" + std::to_string(suffix));
			std::filesystem::create_directories(Root / "BuildProfiles");

			FunkeBuildProfile android;
			android.Name = "Android";
			android.Platform = BuildPlatform::Android;
			android.RenderBackend = FunkeProject::RenderBackend::Vulkan;
			android.Defines = { "STORE_BUILD" };
			REQUIRE(android.Save(Path::ToUtf8(Root / "BuildProfiles" / "Android.build")));

			FunkeBuildProfile desktop;
			desktop.Name = "Desktop";
			desktop.Platform = Host;
			REQUIRE(desktop.Save(Path::ToUtf8(Root / "BuildProfiles" / "Desktop.build")));
		}

		~PlatformTestProject() {
			ProjectManager::SetCurrentProject(std::unique_ptr<FunkeProject>());
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}

		std::unique_ptr<FunkeProject> MakeProject(const std::string& activeProfile) const {
			auto project = std::make_unique<FunkeProject>();
			project->Name = "PlatformTest";
			project->RootDirectory = Path::ToUtf8(Root);
			project->CsprojPath = Path::ToUtf8(Root / "PlatformTest.csproj");
			project->ActiveBuildProfileName = activeProfile;
			project->CustomDefines = { "MY_GAME" };
			return project;
		}
	};

	struct EditorHostScope {
		bool Previous = Application::IsEditor();
		explicit EditorHostScope(bool editor) { Application::SetEditorHost(editor); }
		~EditorHostScope() { Application::SetEditorHost(Previous); }
	};

	bool HasDefine(const std::string& defines, const std::string& name) {
		const std::string separated = "%3B" + defines + "%3B";
		return separated.find("%3B" + name + "%3B") != std::string::npos;
	}
}

TEST_CASE("The editor script target is the active build profile's platform and defines") {
	PlatformTestProject fixture;

	const FunkeProject::EditorScriptTarget android = fixture.MakeProject("Android")->GetEditorScriptTarget();
	CHECK(android.Platform == BuildPlatform::Android);
	CHECK(android.Simulated);
	CHECK(android.ProfileName == "Android");
	REQUIRE(android.ProfileDefines.size() == 1);
	CHECK(android.ProfileDefines[0] == "STORE_BUILD");

	const FunkeProject::EditorScriptTarget desktop = fixture.MakeProject("Desktop")->GetEditorScriptTarget();
	CHECK(desktop.Platform == fixture.Host);
	CHECK_FALSE(desktop.Simulated);

	const FunkeProject::EditorScriptTarget none = fixture.MakeProject("")->GetEditorScriptTarget();
	CHECK(none.Platform == fixture.Host);
	CHECK_FALSE(none.Simulated);
	CHECK(none.ProfileName.empty());

	const FunkeProject::EditorScriptTarget missing = fixture.MakeProject("Deleted")->GetEditorScriptTarget();
	CHECK(missing.Platform == fixture.Host);
	CHECK_FALSE(missing.Simulated);
}

TEST_CASE("Editor script compiles use the simulated platform's define, ship contexts keep the host's") {
	PlatformTestProject fixture;
	ProjectManager::SetCurrentProject(fixture.MakeProject("Android"));
	const std::string hostDefine = FunkeProject::GetManagedPlatformDefine(fixture.Host);

	const std::string editor = FunkeProject::BuildManagedDefineConstants("FUNKE_EDITOR");
	CHECK(HasDefine(editor, "FUNKE_EDITOR"));
	CHECK(HasDefine(editor, "FUNKE_ANDROID"));
	CHECK_FALSE(HasDefine(editor, hostDefine));
	CHECK(HasDefine(editor, "MY_GAME"));
	CHECK(HasDefine(editor, "STORE_BUILD"));

	const std::string unsimulated = FunkeProject::BuildManagedDefineConstants("FUNKE_EDITOR", false);
	CHECK(HasDefine(unsimulated, hostDefine));
	CHECK_FALSE(HasDefine(unsimulated, "FUNKE_ANDROID"));
	CHECK_FALSE(HasDefine(unsimulated, "STORE_BUILD"));

	const std::string build = FunkeProject::BuildManagedDefineConstants("FUNKE_BUILD");
	CHECK(HasDefine(build, hostDefine));
	CHECK_FALSE(HasDefine(build, "FUNKE_ANDROID"));

	ProjectManager::SetCurrentProject(fixture.MakeProject(""));
	CHECK(FunkeProject::BuildManagedDefineConstants("FUNKE_EDITOR") == unsimulated);
}

TEST_CASE("FunkeDefines.props shows IntelliSense the simulated platform") {
	PlatformTestProject fixture;
	const std::unique_ptr<FunkeProject> project = fixture.MakeProject("Android");
	REQUIRE(project->WriteManagedDefinesProps());
	const std::string props = File::ReadAllText(Path::ToUtf8(fixture.Root / "Packages" / "FunkeDefines.props"));
	CHECK(props.find(";FUNKE_ANDROID") != std::string::npos);
	CHECK(props.find(";STORE_BUILD") != std::string::npos);
	CHECK(props.find(";" + FunkeProject::GetManagedPlatformDefine(fixture.Host)) == std::string::npos);
}

TEST_CASE("Application.Platform reports the simulated platform in the editor and the real one elsewhere") {
	CHECK(ScriptBindings::ToManagedPlatform(BuildPlatform::Windows) == 1);
	CHECK(ScriptBindings::ToManagedPlatform(BuildPlatform::Linux) == 2);
	CHECK(ScriptBindings::ToManagedPlatform(BuildPlatform::MacOS) == 3);
	CHECK(ScriptBindings::ToManagedPlatform(BuildPlatform::Android) == 4);
	CHECK(ScriptBindings::ToManagedPlatform(BuildPlatform::IOS) == 5);
	CHECK(ScriptBindings::ToManagedPlatform(BuildPlatform::Web) == 6);
	CHECK(ScriptBindings::ToManagedPlatform(BuildPlatform::VisionOS) == 7);

	PlatformTestProject fixture;
	ProjectManager::SetCurrentProject(fixture.MakeProject("Android"));
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	REQUIRE(bindings.Application_GetPlatform != nullptr);

	{
		EditorHostScope editor(true);
		CHECK(bindings.Application_GetPlatform() == ScriptBindings::ToManagedPlatform(BuildPlatform::Android));
	}
	{
		EditorHostScope runtime(false);
		CHECK(bindings.Application_GetPlatform() == ScriptBindings::ToManagedPlatform(fixture.Host));
	}
}
