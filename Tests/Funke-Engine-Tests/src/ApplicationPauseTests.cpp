#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Scene/SceneManager.hpp"

#include <string>

using namespace Funke;

namespace {
	class PauseBoundaryTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			ApplicationConfig config = ApplicationConfig::Headless();
			config.EnablePhysics2D = false;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene("Pause Boundary").SetAsStartupScene();
		}

		void Update() override {
			Sequence += 'U';
			++UpdateCount;
			if (UpdateCount == 1) {
				StartupCompleteAtPause = Application::IsStartupLoadComplete();
				Application::Pause(true);
				PauseReportedInUpdate = Application::IsPaused();
			}
			else {
				Application::Quit();
			}
		}

		void OnPaused() override {
			Sequence += 'P';
			++PausedCount;
			Application::Pause(false);
		}

		std::string Sequence;
		int UpdateCount = 0;
		int PausedCount = 0;
		bool StartupCompleteAtPause = false;
		bool PauseReportedInUpdate = false;
	};
}

TEST_CASE("Application pause changes take effect on frame boundaries") {
	PauseBoundaryTestApplication application;
	application.Run();

	CHECK(application.Sequence == "UPU");
	CHECK(application.UpdateCount == 2);
	CHECK(application.PausedCount == 1);
	CHECK(application.StartupCompleteAtPause);
	CHECK(application.PauseReportedInUpdate);
}
