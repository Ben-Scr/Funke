#include <doctest/doctest.h>

#include "Core/Application.hpp"

using namespace Funke;

namespace {
	enum class LifecycleRequest {
		Reload,
		Restart,
	};

	class LifecycleTestApplication final : public Application {
	public:
		explicit LifecycleTestApplication(LifecycleRequest request)
			: m_Request(request) {}

		ApplicationConfig GetConfiguration() const override {
			ApplicationConfig config = ApplicationConfig::Headless();
			config.EnablePhysics2D = false;
			config.EnableAudio = false;
			config.EnableScripting = false;
			config.EnablePackageHost = false;
			config.JobSystemWorkerCount = 1;
			return config;
		}

		void Start() override {
			++StartCount;
			if (StartCount > 1) {
				Application::Quit();
				return;
			}

			if (m_Request == LifecycleRequest::Restart) {
				Application::Restart();
			}
			else {
				Application::Reload();
			}
		}

		void OnQuit() override {
			++QuitCount;
		}

		int StartCount = 0;
		int QuitCount = 0;

	private:
		LifecycleRequest m_Request;
	};

	class SmokeTestApplication final : public Application {
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

		void Update() override { ++UpdateCount; }

		int UpdateCount = 0;
	};
}

TEST_CASE("Application reload requests rerun the lifecycle") {
	LifecycleTestApplication application(LifecycleRequest::Reload);
	CHECK(application.Run() == 0);

	CHECK(application.StartCount == 2);
	CHECK(application.QuitCount == 2);
}

TEST_CASE("Application restart requests rerun the lifecycle") {
	LifecycleTestApplication application(LifecycleRequest::Restart);
	CHECK(application.Run() == 0);

	CHECK(application.StartCount == 2);
	CHECK(application.QuitCount == 2);
}

TEST_CASE("Application CI smoke mode exits through the normal quit path") {
	char executable[] = "Funke-Test";
	char smokeArgument[] = "--ci-smoke-test";
	char* arguments[] = { executable, smokeArgument };
	Application::SetCommandLineArgs(2, arguments);

	SmokeTestApplication application;
	CHECK(application.Run() == 0);
	CHECK(application.UpdateCount >= 3);

	Application::SetCommandLineArgs(0, nullptr);
}
