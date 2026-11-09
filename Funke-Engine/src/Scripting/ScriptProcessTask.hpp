#pragma once
#include "Utils/Process.hpp"
#include "Profiling/Profiler.hpp"
#include <functional>
#include <memory>
#include <exception>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace Funke {
	struct ScriptSystemProcessTaskState {
		std::mutex Mutex;
		std::optional<Process::Result> Result;
		std::atomic<bool> Completed{ false };
		std::atomic<bool> Abandoned{ false };
		std::chrono::steady_clock::time_point StartedAt{ std::chrono::steady_clock::now() };

		std::thread Worker;

		~ScriptSystemProcessTaskState() {
			if (!Worker.joinable()) {
				return;
			}
			if (Worker.get_id() != std::this_thread::get_id() && Completed.load(std::memory_order_acquire)) {
				// Completed publishes the result; joining still waits for the worker to return.
				Worker.join();
			} else {
				Abandoned.store(true, std::memory_order_release);
				Worker.detach();
			}
		}
	};

	namespace ScriptProcessTasks {
		inline std::shared_ptr<ScriptSystemProcessTaskState> LaunchTask(std::function<Process::Result()> work)
		{
			auto task = std::make_shared<ScriptSystemProcessTaskState>();
			task->StartedAt = std::chrono::steady_clock::now();

			// Capture by weak_ptr so abandoning during shutdown doesn't stall on the worker.
			std::weak_ptr<ScriptSystemProcessTaskState> weakTask = task;
			task->Worker = std::thread([weakTask, work = std::move(work)]() mutable {
				FUNKE_PROFILE_THREAD_NAME("ScriptProcess");
				Process::Result result{};
				try {
					result = work();
				}
				catch (const std::exception& e) {
					result.ExitCode = -1;
					result.Output = e.what();
				}
				catch (...) {
					result.ExitCode = -1;
					result.Output = "Unknown exception while running background rebuild task.";
				}

				if (auto state = weakTask.lock()) {
					if (!state->Abandoned.load(std::memory_order_acquire)) {
						std::scoped_lock lock(state->Mutex);
						state->Result = std::move(result);
					}
					state->Completed.store(true, std::memory_order_release);
				}
			});

			return task;
		}

		inline bool TryTakeTaskResult(std::shared_ptr<ScriptSystemProcessTaskState>& task, Process::Result& outResult)
		{
			if (!task || !task->Completed.load(std::memory_order_acquire)) {
				return false;
			}

			if (task->Abandoned.load(std::memory_order_acquire)) {
				task.reset();
				return false;
			}

			{
				std::scoped_lock lock(task->Mutex);
				outResult = task->Result.value_or(Process::Result{});
			}

			task.reset();
			return true;
		}

	}

}
