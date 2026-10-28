#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Editor/SceneRecovery.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/File.hpp"

#include <chrono>
#include <filesystem>
#include <memory>

using namespace Funke;

namespace {
	class RecoveryTestApplication final : public Application {
	public:
		explicit RecoveryTestApplication(std::filesystem::path root) : m_Root(std::move(root)) {}

		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Headless();
			config.EnablePhysics2D = false;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene("Recovery")
				.OnLoad([](Scene& scene) { scene.CreateEntity("Original"); })
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
		}

		void Start() override {
			SetIsPlaying(false);
			Scene* scene = SceneManager::GetActiveScene();
			REQUIRE(scene);
			scene->MarkDirty();
			SceneRecovery::SnapshotDirtyScenes();
			const auto snapshot = m_Root / ".funke" / "Recovery" / "Recovery.scene";
			REQUIRE(std::filesystem::is_regular_file(snapshot));
			const auto original = File::ReadAllBytes(snapshot.string());
			REQUIRE_FALSE(original.empty());
			CHECK(scene->IsDirty());

			const auto blockedTemporary = snapshot.string() + ".tmp";
			std::filesystem::create_directory(blockedTemporary);
			scene->CreateEntity("Unsaved edit");
			scene->MarkDirty();
			SceneRecovery::SnapshotDirtyScenes();
			CHECK(std::filesystem::is_regular_file(snapshot));
			CHECK(File::ReadAllBytes(snapshot.string()) == original);
			CHECK(scene->IsDirty());

			std::error_code error;
			std::filesystem::remove(blockedTemporary, error);
			SceneRecovery::SnapshotDirtyScenes();
			CHECK(File::ReadAllBytes(snapshot.string()) != original);
			CHECK(scene->IsDirty());

			scene->ClearDirty();
			SceneRecovery::SnapshotDirtyScenes();
			CHECK_FALSE(std::filesystem::exists(snapshot));
			Completed = true;
			Quit();
		}

		bool Completed = false;

	private:
		std::filesystem::path m_Root;
	};
}

TEST_CASE("Recovery preserves the last successful snapshot after a write failure") {
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root = std::filesystem::temp_directory_path() / ("FunkeRecoveryTest_" + std::to_string(suffix));
	struct Cleanup {
		std::filesystem::path Root;
		~Cleanup() {
			ProjectManager::SetCurrentProject(nullptr);
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}
	} cleanup{ root };
	std::filesystem::create_directories(root / "Assets");
	auto project = std::make_unique<FunkeProject>();
	project->Name = "Recovery Test";
	project->RootDirectory = root.string();
	project->AssetsDirectory = (root / "Assets").string();
	ProjectManager::SetCurrentProject(std::move(project));
	RecoveryTestApplication app(root);
	CHECK(app.Run() == 0);
	CHECK(app.Completed);
}
