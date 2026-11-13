#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Events/WindowResizeEvent.hpp"
#include <SDL3/SDL.h>
#include "Scripting/ScriptProcessTask.hpp"
#include <future>
#include <memory>
#include "Jobs/JobSystem.hpp"
#include "Memory/FrameArenas.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <filesystem>
#include <string>

using namespace Funke;

namespace {
	class LiveResizeTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Minimal();
			config.EnableTextureManager = false;
			config.SetWindowIcon = false;
			config.WindowSpecification.Width = 320;
			config.WindowSpecification.Height = 240;
			config.WindowSpecification.Fullscreen = false;
			return config;
		}

		void Start() override {
			auto* window = GetWindow();
			auto* native = window->GetNativeWindow();
			Window::PollEvents();
			int notifications = 0;
			window->SetEventCallback([&](FunkeEvent& event) {
				if (event.GetEventType() != EventType::WindowResize) return;
				++notifications;
				auto& resize = static_cast<WindowResizeEvent&>(event);
				int width = 0, height = 0;
				SDL_GetWindowSizeInPixels(native, &width, &height);
				CHECK(resize.GetWidth() == width);
				CHECK(resize.GetHeight() == height);
				// A subscriber must not re-enter the refresh render.
				RenderOnceForRefresh(true);
			});
			SDL_Event expose{};
			expose.type = SDL_EVENT_WINDOW_EXPOSED;
			expose.window.windowID = SDL_GetWindowID(native);
			expose.window.data1 = 1;
			for (int step = 1; step <= 3; ++step) {
				CHECK(SDL_SetWindowSize(native, 320 + step * 20, 240 + step * 10));
				// Do not poll: reproduce the modal loop's synchronous expose watcher.
				CHECK(SDL_PushEvent(&expose));
				CHECK(notifications == step);
				CHECK(SDL_PushEvent(&expose));
				CHECK(notifications == step);
			}
			Window::PollEvents();
			CHECK(notifications == 3);
			window->SetEventCallback({});
			Application::Quit();
		}
	};

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
		void OnQuit() override { ++QuitCount; }

		int UpdateCount = 0;
		int QuitCount = 0;
	};

	struct ScopedSmokeEnvironment {
		explicit ScopedSmokeEnvironment(bool withResources) {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path() / ("FunkeSmokeLifecycle_" + std::to_string(suffix));
			const auto contentBase = Root / "ContentBase";
			std::filesystem::create_directories(withResources ? contentBase / "FunkeAssets/Textures" : contentBase);
			Path::SetExecutableDirOverride(Path::ToUtf8(contentBase));
			Application::SetCommandLineArgs(2, Arguments);
		}

		~ScopedSmokeEnvironment() {
			Application::SetCommandLineArgs(PreviousArguments.Count, PreviousArguments.Values);
			Path::SetExecutableDirOverride(PreviousContentBase);
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path Root;
		std::string PreviousContentBase = Path::ExecutableDir();
		Application::CommandLineArgs PreviousArguments = Application::GetCommandLineArgs();
		char Executable[sizeof("Funke-Test")] = "Funke-Test";
		char SmokeArgument[sizeof("--ci-smoke-test")] = "--ci-smoke-test";
		char* Arguments[2] = { Executable, SmokeArgument };
	};

	class MinimalLifecycleTestApplication final : public Application {
	public:
		ApplicationConfig GetConfiguration() const override {
			ApplicationConfig config = ApplicationConfig::Minimal();
			config.EnableWindow = false;
			config.EnableTextureManager = false;
			config.SetWindowIcon = false;
			config.UseTargetFrameRateForMainLoop = false;
			return config;
		}

		void ConfigureScenes() override { ++ConfigureScenesCount; }
		void Start() override {
			++StartCount;
			ObserveSubsystems();
		}
		void Update() override {
			++UpdateCount;
			ObserveSubsystems();
			if (UpdateCount == 3) Application::Quit();
		}
		void OnQuit() override { ++QuitCount; }

		int ConfigureScenesCount = 0;
		int StartCount = 0;
		int UpdateCount = 0;
		int QuitCount = 0;
		bool BackgroundSystemsStayedDisabled = true;

	private:
		void ObserveSubsystems() {
			BackgroundSystemsStayedDisabled = BackgroundSystemsStayedDisabled
				&& !JobSystem::IsInitialized() && JobSystem::GetWorkerCount() == 0
				&& !FrameArenas::IsInitialized();
		}
	};
}

// Requires a desktop display and graphics backend; run explicitly with --no-skip.
TEST_CASE("Application live resize notifies before polling without duplicates" * doctest::skip()) {
	LiveResizeTestApplication application;
	CHECK(application.Run() == 0);
}

TEST_CASE("Application minimal lifecycle skips scenes workers and frame arenas") {
	MinimalLifecycleTestApplication application;
	CHECK(application.Run() == 0);
	CHECK(application.ConfigureScenesCount == 0);
	CHECK(application.StartCount == 1);
	CHECK(application.UpdateCount == 3);
	CHECK(application.QuitCount == 1);
	CHECK(application.BackgroundSystemsStayedDisabled);
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
	ScopedSmokeEnvironment environment(true);
	REQUIRE(std::filesystem::equivalent(Path::FromUtf8(Path::ResolveFunkeAssets("")),
		environment.Root / "ContentBase/FunkeAssets"));

	SmokeTestApplication application;
	CHECK(application.Run() == 0);
	CHECK(application.UpdateCount >= 3);
	CHECK(application.QuitCount == 1);
}

TEST_CASE("Application CI smoke mode fails gracefully when resources are missing") {
	ScopedSmokeEnvironment environment(false);
	REQUIRE(Path::ResolveFunkeAssets("").empty());

	SmokeTestApplication application;
	CHECK(application.Run() == 1);
	CHECK(application.UpdateCount >= 3);
	CHECK(application.QuitCount == 1);
}

TEST_CASE("Backlog completed worker can release the final task owner without joining itself") {
	for (int iteration = 0; iteration < 128; ++iteration) {
		auto task = std::make_shared<ScriptSystemProcessTaskState>();
		std::promise<void> release, finished;
		auto gate = release.get_future();
		auto done = finished.get_future();
		task->Worker = std::thread([state = task, gate = std::move(gate), &finished]() mutable {
			gate.wait();
			state->Completed.store(true, std::memory_order_release);
			state.reset();
			finished.set_value();
		});
		task.reset();
		release.set_value();
		REQUIRE(done.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	}
}

TEST_CASE("Backlog process tasks publish success and exceptions exactly once") {
	for (int mode : {0, 1, 2}) {
		auto task = ScriptProcessTasks::LaunchTask([mode]() -> Process::Result {
			if (mode == 1) throw std::runtime_error("expected worker failure");
			if (mode == 2) throw 7;
			Process::Result result;
			result.ExitCode = 0;
			result.Output = "completed";
			return result;
		});
		Process::Result result;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!task->Completed.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
		REQUIRE(task->Completed.load());
		CHECK(ScriptProcessTasks::TryTakeTaskResult(task, result));
		CHECK(result.ExitCode == (mode == 0 ? 0 : -1));
		CHECK_FALSE(result.Output.empty());
		CHECK_FALSE(task);
		CHECK_FALSE(ScriptProcessTasks::TryTakeTaskResult(task, result));
	}
}

TEST_CASE("Backlog abandoned process task does not block shutdown or retain captures") {
	std::promise<void> entered, release, finished;
	auto enteredFuture = entered.get_future();
	auto releaseFuture = release.get_future().share();
	auto finishedFuture = finished.get_future();
	auto token = std::make_shared<int>(1);
	std::weak_ptr<int> lifetime = token;
	auto task = ScriptProcessTasks::LaunchTask([token, releaseFuture, &entered, &finished]() mutable {
		entered.set_value();
		releaseFuture.wait();
		token.reset();
		finished.set_value();
		return Process::Result{};
	});
	token.reset();
	REQUIRE(enteredFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	task->Abandoned.store(true);
	task.reset();
	CHECK_FALSE(lifetime.expired());
	release.set_value();
	REQUIRE(finishedFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	CHECK(lifetime.expired());
}
