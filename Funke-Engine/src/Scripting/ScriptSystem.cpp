#include "pch.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ManagedScriptCompiler.hpp"
#include "Scripting/ScriptCoreArtifact.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Components/Tags.hpp"
#if FUNKE_WITH_2D
#include "Physics/Collision2D.hpp"
#endif
#if FUNKE_WITH_3D
#include "Physics3D/PhysicsWorld3D.hpp"
#endif
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Serialization/Json.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>

#include "Core/Platform.hpp"

#if defined(FNK_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Funke {
	NativeScriptHost ScriptSystem::m_NativeHost;

	NativeScriptHost& ScriptSystem::GetNativeHost() { return m_NativeHost; }

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
			if (Completed.load(std::memory_order_acquire)) {
				// Worker function has already returned; just reap the thread.
				Worker.join();
			} else {
				Abandoned.store(true, std::memory_order_release);
				Worker.detach();
			}
		}
	};

	namespace {
		using ProcessTaskState = ScriptSystemProcessTaskState;

		std::string GetNativeLibraryFilename(const std::string& targetName)
		{
			return Platform::SharedLibraryFileName(targetName);
		}

		std::filesystem::path NormalizeNativePath(std::filesystem::path path)
		{
			std::error_code ec;
			if (std::filesystem::exists(path)) {
				const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
				if (!ec) {
					return canonicalPath;
				}
			}

			return path.lexically_normal();
		}

		std::filesystem::path ResolveProjectNativeDLLPath(const FunkeProject& project)
		{
			return NormalizeNativePath(Path::FromUtf8(project.GetNativeDllPath()));
		}

		std::filesystem::path ResolveStandaloneNativeDLLPath(const std::filesystem::path& executableDirectory)
		{
			return NormalizeNativePath(
				executableDirectory / ".." / "Funke-NativeScripts" / GetNativeLibraryFilename("Funke-NativeScripts"));
		}

		std::string GetActiveNativeBuildConfig()
		{
			return FunkeProject::GetActiveBuildConfiguration();
		}

		std::string TrimWhitespace(std::string value)
		{
			const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			});
			const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			}).base();

			if (first >= last) {
				return {};
			}

			return std::string(first, last);
		}

		std::string ToLowerCopy(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

#if defined(FNK_PLATFORM_WINDOWS)
		std::optional<std::filesystem::path> NormalizeExecutablePath(std::filesystem::path candidate, const char* executableName)
		{
			if (candidate.empty()) {
				return std::nullopt;
			}

			std::error_code ec;
			if (std::filesystem::is_directory(candidate, ec) && !ec) {
				candidate /= executableName;
			}

			ec.clear();
			if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
				return std::nullopt;
			}

			ec.clear();
			const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, ec);
			return ec ? candidate.lexically_normal() : canonical;
		}

		std::optional<std::filesystem::path> GetEnvironmentExecutablePath(const char* variableName, const char* executableName)
		{
			const char* value = std::getenv(variableName);
			if (value == nullptr || value[0] == '\0') {
				return std::nullopt;
			}

			return NormalizeExecutablePath(std::filesystem::path(value), executableName);
		}

		std::optional<std::filesystem::path> SearchExecutableOnPath(const char* executableName)
		{
			char stackBuffer[MAX_PATH]{};
			char* filePart = nullptr;
			DWORD length = SearchPathA(nullptr, executableName, nullptr, MAX_PATH, stackBuffer, &filePart);
			if (length > 0 && length < MAX_PATH) {
				return NormalizeExecutablePath(std::filesystem::path(stackBuffer), executableName);
			}

			if (length >= MAX_PATH) {
				std::string dynamicBuffer(static_cast<size_t>(length) + 1, '\0');
				length = SearchPathA(nullptr, executableName, nullptr,
					static_cast<DWORD>(dynamicBuffer.size()), dynamicBuffer.data(), &filePart);
				if (length > 0 && length < dynamicBuffer.size()) {
					dynamicBuffer.resize(length);
					return NormalizeExecutablePath(std::filesystem::path(dynamicBuffer), executableName);
				}
			}

			return std::nullopt;
		}

		void AppendCMakeCandidate(std::vector<std::filesystem::path>& candidates,
			const char* environmentVariable,
			const std::filesystem::path& relativePath)
		{
			const char* root = std::getenv(environmentVariable);
			if (root == nullptr || root[0] == '\0') {
				return;
			}

			candidates.emplace_back(std::filesystem::path(root) / relativePath);
		}

		std::optional<std::filesystem::path> ResolveVisualStudioCMakeWithVswhere()
		{
			const char* programFilesX86 = std::getenv("ProgramFiles(x86)");
			if (programFilesX86 == nullptr || programFilesX86[0] == '\0') {
				return std::nullopt;
			}

			const std::filesystem::path vswherePath =
				std::filesystem::path(programFilesX86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
			auto normalizedVswhere = NormalizeExecutablePath(vswherePath, "vswhere.exe");
			if (!normalizedVswhere) {
				return std::nullopt;
			}

			Process::Result result = Process::Run({
				normalizedVswhere->string(),
				"-latest",
				"-requires", "Microsoft.VisualStudio.Component.VC.CMake.Project",
				"-property", "installationPath"
			});

			if (!result.Succeeded()) {
				return std::nullopt;
			}

			const std::string installPath = TrimWhitespace(result.Output);
			if (installPath.empty()) {
				return std::nullopt;
			}

			return NormalizeExecutablePath(
				std::filesystem::path(installPath) / "Common7" / "IDE" / "CommonExtensions" /
					"Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe",
				"cmake.exe");
		}

		std::optional<std::string> ResolveCMakeExecutable()
		{
			static std::optional<std::string> cachedCMakeExecutable;
			if (cachedCMakeExecutable) {
				return cachedCMakeExecutable;
			}

			for (const char* variableName : { "FUNKE_CMAKE_PATH", "CMAKE_COMMAND", "CMAKE_EXE" }) {
				if (auto path = GetEnvironmentExecutablePath(variableName, "cmake.exe")) {
					cachedCMakeExecutable = Path::ToUtf8(*path);
					return cachedCMakeExecutable;
				}
			}

			if (auto path = SearchExecutableOnPath("cmake.exe")) {
				cachedCMakeExecutable = Path::ToUtf8(*path);
				return cachedCMakeExecutable;
			}

			if (auto path = ResolveVisualStudioCMakeWithVswhere()) {
				cachedCMakeExecutable = Path::ToUtf8(*path);
				return cachedCMakeExecutable;
			}

			std::vector<std::filesystem::path> candidates;
			AppendCMakeCandidate(candidates, "ProgramFiles", std::filesystem::path("CMake") / "bin" / "cmake.exe");
			AppendCMakeCandidate(candidates, "ProgramFiles(x86)", std::filesystem::path("CMake") / "bin" / "cmake.exe");

			for (const char* version : { "2022", "2019" }) {
				for (const char* edition : { "Enterprise", "Professional", "Community", "BuildTools" }) {
					AppendCMakeCandidate(candidates, "ProgramFiles",
						std::filesystem::path("Microsoft Visual Studio") / version / edition / "Common7" /
							"IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe");
					AppendCMakeCandidate(candidates, "ProgramFiles(x86)",
						std::filesystem::path("Microsoft Visual Studio") / version / edition / "Common7" /
							"IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe");
				}
			}

			for (const std::filesystem::path& candidate : candidates) {
				if (auto path = NormalizeExecutablePath(candidate, "cmake.exe")) {
					cachedCMakeExecutable = Path::ToUtf8(*path);
					return cachedCMakeExecutable;
				}
			}

			return std::nullopt;
		}
#else
		std::optional<std::string> ResolveCMakeExecutable()
		{
			return std::string("cmake");
		}
#endif

		std::vector<std::string> GetManagedWatchPatterns()
		{
			return {
				".cs",
				".csproj",
				".props",
				".targets",
				".sln"
			};
		}

		std::vector<std::string> BuildManagedWatchTargets(
			const std::filesystem::path& scriptsDirectory,
			const std::filesystem::path& projectFile,
			const std::filesystem::path& solutionFile = {})
		{
			std::vector<std::string> targets;
			auto appendTarget = [&targets](const std::filesystem::path& path) {
				if (!path.empty()) {
					targets.push_back(Path::ToUtf8(path));
				}
			};

			appendTarget(scriptsDirectory);
			appendTarget(projectFile);
			appendTarget(solutionFile);

			const std::filesystem::path projectRoot = projectFile.parent_path();
			if (!projectRoot.empty()) {
				appendTarget(projectRoot / "Directory.Build.props");
				appendTarget(projectRoot / "Directory.Build.targets");
			}

			return targets;
		}

		std::vector<std::string> GetNativeWatchPatterns()
		{
			return {
				".c",
				".cc",
				".cpp",
				".cxx",
				".h",
				".hh",
				".hpp",
				".hxx",
				".inl",
				".ipp",
				".cmake",
				"CMakeLists.txt"
			};
		}

		bool IsNativeScriptBootstrapFile(const std::filesystem::path& path)
		{
			return Path::ToUtf8(path.filename()) == "NativeScriptExports.cpp";
		}

		bool IsNativeScriptSourceExtension(const std::filesystem::path& path)
		{
			std::string extension = Path::ToUtf8(path.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});

			return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx";
		}

		bool HasNativeScriptSourceFiles(const std::filesystem::path& sourceDirectory)
		{
			std::error_code ec;
			if (sourceDirectory.empty() || !std::filesystem::exists(sourceDirectory, ec) || ec) {
				return false;
			}

			for (std::filesystem::recursive_directory_iterator it(sourceDirectory, std::filesystem::directory_options::skip_permission_denied, ec), end;
				 it != end;
				 it.increment(ec)) {
				if (ec) {
					ec.clear();
					continue;
				}

				if (!it->is_regular_file(ec) || ec) {
					ec.clear();
					continue;
				}

				const std::filesystem::path path = it->path();
				if (IsNativeScriptBootstrapFile(path)) {
					continue;
				}

				if (IsNativeScriptSourceExtension(path)) {
					return true;
				}
			}

			return false;
		}

		std::vector<std::string> BuildNativeWatchTargets(
			const std::filesystem::path& projectDirectory,
			const std::filesystem::path& sourceDirectory = {},
			const std::filesystem::path& assetsDirectory = {})
		{
			std::vector<std::string> targets;
			auto appendTarget = [&targets](const std::filesystem::path& path) {
				if (!path.empty()) {
					targets.push_back(Path::ToUtf8(path));
				}
			};

			appendTarget(sourceDirectory);
			appendTarget(assetsDirectory);
			appendTarget(projectDirectory / "Include");
			appendTarget(projectDirectory / "CMakeLists.txt");
			appendTarget(projectDirectory / "FunkeNativeScripts.cmake");

			if (targets.empty()) {
				appendTarget(projectDirectory);
			}

			return targets;
		}

		bool ShouldIgnoreBuildInputDirectory(const std::filesystem::path& directoryPath)
		{
			const std::string name = ToLowerCopy(Path::ToUtf8(directoryPath.filename()));
			return name == ".git"
				|| name == ".vs"
				|| name == ".idea"
				|| name == "bin"
				|| name == "bin-int"
				|| name == "obj"
				|| name == "build"
				|| name == "out";
		}

		bool MatchesBuildInputPattern(const std::filesystem::path& filePath, const std::vector<std::string>& patterns)
		{
			if (patterns.empty()) {
				return true;
			}

			const std::string fileName = ToLowerCopy(Path::ToUtf8(filePath.filename()));
			const std::string extension = ToLowerCopy(Path::ToUtf8(filePath.extension()));

			for (const std::string& pattern : patterns) {
				const std::string normalized = ToLowerCopy(pattern);
				if (normalized.empty()) {
					continue;
				}

				if (normalized.front() == '.') {
					if (extension == normalized) {
						return true;
					}
				}
				else if (fileName == normalized) {
					return true;
				}
			}

			return false;
		}

		void UpdateLatestBuildInputTime(
			const std::filesystem::path& filePath,
			std::filesystem::file_time_type& latestWriteTime,
			bool& foundInput)
		{
			std::error_code ec;
			const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(filePath, ec);
			if (ec) {
				return;
			}

			if (!foundInput || writeTime > latestWriteTime) {
				latestWriteTime = writeTime;
				foundInput = true;
			}
		}

		bool TryGetLatestBuildInputTime(
			const std::vector<std::string>& targets,
			const std::vector<std::string>& patterns,
			std::filesystem::file_time_type& latestWriteTime)
		{
			bool foundInput = false;

			for (const std::string& targetString : targets) {
				if (targetString.empty()) {
					continue;
				}

				const std::filesystem::path target(targetString);
				std::error_code ec;
				if (std::filesystem::is_directory(target, ec) && !ec) {
					for (std::filesystem::recursive_directory_iterator it(target, std::filesystem::directory_options::skip_permission_denied, ec), end;
						 it != end;
						 it.increment(ec)) {
						if (ec) {
							ec.clear();
							continue;
						}

						if (it->is_directory(ec)) {
							if (!ec && ShouldIgnoreBuildInputDirectory(it->path())) {
								it.disable_recursion_pending();
							}
							ec.clear();
							continue;
						}

						if (!it->is_regular_file(ec) || ec || !MatchesBuildInputPattern(it->path(), patterns)) {
							ec.clear();
							continue;
						}

						UpdateLatestBuildInputTime(it->path(), latestWriteTime, foundInput);
					}

					continue;
				}

				ec.clear();
				if (!std::filesystem::is_regular_file(target, ec) || ec || !MatchesBuildInputPattern(target, patterns)) {
					continue;
				}

				UpdateLatestBuildInputTime(target, latestWriteTime, foundInput);
			}

			return foundInput;
		}

		bool BuildArtifactIsStale(
			const std::vector<std::string>& inputTargets,
			const std::vector<std::string>& inputPatterns,
			const std::filesystem::path& artifactPath)
		{
			std::filesystem::file_time_type latestInputWriteTime{};
			if (!TryGetLatestBuildInputTime(inputTargets, inputPatterns, latestInputWriteTime)) {
				return false;
			}

			std::error_code ec;
			if (artifactPath.empty() || !std::filesystem::exists(artifactPath, ec) || ec) {
				return true;
			}

			const std::filesystem::file_time_type artifactWriteTime = std::filesystem::last_write_time(artifactPath, ec);
			if (ec) {
				return true;
			}

			return latestInputWriteTime > artifactWriteTime;
		}

		// "<assembly>.defines" holds the C# defines the editor built that assembly with.
		std::string ReadScriptDefinesStamp(const std::string& assemblyPath)
		{
			if (assemblyPath.empty()) return {};
			const std::string stampPath = assemblyPath + ".defines";
			return File::Exists(stampPath) ? File::ReadAllText(stampPath) : std::string();
		}

		void WriteScriptDefinesStamp(const std::string& assemblyPath, const std::string& defines)
		{
			if (assemblyPath.empty() || defines.empty()) return;
			if (!File::WriteAllText(assemblyPath + ".defines", defines)) {
				FNK_WARN_TAG("ScriptSystem", "Could not write the script defines stamp next to '{}'.", assemblyPath);
			}
		}

		template <typename TFunc>
		void ForEachLoadedScene(TFunc&& func)
		{
			Application* app = Application::GetInstance();
			if (!app || !app->GetSceneManager()) {
				return;
			}

			SceneManager::ForeachLoadedScene(std::forward<TFunc>(func));
		}

		void RemovePendingFieldValuesForClass(ScriptComponent& scriptComp, const std::string& className)
		{
			if (className.empty()) {
				return;
			}

			for (const ScriptInstance& instance : scriptComp.Scripts) {
				if (instance.GetClassName() == className) {
					return;
				}
			}

			const std::string prefix = className + ".";
			for (auto it = scriptComp.PendingFieldValues.begin(); it != scriptComp.PendingFieldValues.end(); ) {
				if (it->first.rfind(prefix, 0) == 0) {
					it = scriptComp.PendingFieldValues.erase(it);
				}
				else {
					++it;
				}
			}
			scriptComp.PendingDynamicComponentValues.erase(className);
		}

		std::vector<std::string> GetActiveGlobalSystemClassNames()
		{
			std::vector<std::string> classNames;
			FunkeProject* project = ProjectManager::GetCurrentProject();
			if (!project) {
				return classNames;
			}

			for (const auto& registration : project->GlobalSystems) {
				if (registration.Active && !registration.ClassName.empty()) {
					classNames.push_back(registration.ClassName);
				}
			}
			return classNames;
		}

		void InitializeRegisteredGlobalSystems()
		{
			ScriptEngine::InitializeGlobalSystems(GetActiveGlobalSystemClassNames());
		}

		struct ScriptSceneScope {
			explicit ScriptSceneScope(Scene& scene)
				: ExecutionScope(scene)
				, Previous(ScriptEngine::GetScene())
			{
				ScriptEngine::SetScene(&scene);
			}

			~ScriptSceneScope()
			{
				ScriptEngine::SetScene(Previous);
			}

			SceneExecutionScope ExecutionScope;
			Scene* Previous = nullptr;
		};

		uint64_t ToManagedEntityID(Scene& scene, EntityHandle entity)
		{
			// Prefer persistent UUID so C# `collision.OtherEntity == myCachedEntity` matches across creation paths.
			uint64_t entityID = static_cast<uint64_t>(static_cast<uint32_t>(entity));
			if (scene.IsValid(entity)) {
				const uint64_t persistentID = scene.GetEntityPersistentID(entity);
				if (persistentID != 0) {
					entityID = persistentID;
				}
			}
			return entityID;
		}

		struct ManagedScriptTeardownState {
			uint32_t Handle = 0;
			bool WasEnabled = false;
		};

		struct NativeScriptTeardownState {
			NativeScript* Instance = nullptr;
		};

		ManagedScriptTeardownState CaptureManagedTeardownState(const ScriptInstance& instance)
		{
			return {
				instance.GetGCHandle(),
				instance.HasEnabled()
			};
		}

		NativeScriptTeardownState CaptureNativeTeardownState(const ScriptInstance& instance)
		{
			return { instance.GetNativePtr() };
		}

		void InvokeManagedScriptTeardown(Scene& scene, const ManagedScriptTeardownState& state)
		{
			if (state.Handle == 0 || !ScriptEngine::IsInitialized()) {
				return;
			}

			ScriptSceneScope sceneScope(scene);
			if (state.WasEnabled) {
				ScriptEngine::InvokeOnDisable(state.Handle);
			}
			ScriptEngine::InvokeOnDestroy(state.Handle);
			ScriptEngine::DestroyScriptInstance(state.Handle);
		}

		void InvokeNativeScriptTeardown(Scene& scene, const NativeScriptTeardownState& state, NativeScriptHost& nativeHost)
		{
			if (!state.Instance) {
				return;
			}

			ScriptSceneScope sceneScope(scene);
			if (nativeHost.IsLoaded()) {
				nativeHost.DestroyInstance(state.Instance);
			}
		}

		void TeardownManagedScriptInstance(Scene& scene, ScriptInstance& instance)
		{
			if (!instance.HasManagedInstance()) {
				return;
			}

			// Unbind() before callbacks so the GCHandle is dropped even if IsInitialized() flips during teardown.
			const ManagedScriptTeardownState state = CaptureManagedTeardownState(instance);
			instance.Unbind();
			InvokeManagedScriptTeardown(scene, state);
		}

		void TeardownNativeScriptInstance(Scene& scene, ScriptInstance& instance, NativeScriptHost& nativeHost)
		{
			if (!instance.HasNativeInstance()) {
				return;
			}

			const NativeScriptTeardownState state = CaptureNativeTeardownState(instance);
			instance.Unbind();
			InvokeNativeScriptTeardown(scene, state, nativeHost);
		}

		template<typename Fn>
		bool TryInvokeNativeScript(const ScriptInstance& instance, const char* callbackName, Fn&& callback)
		{
			// Copy now: the callback may reallocate the entity's Scripts vector, leaving `instance` dangling.
			const std::string className = instance.GetClassName();
			try {
				callback();
				return true;
			}
			catch (const std::exception& e) {
				FNK_CORE_ERROR_TAG("ScriptSystem", "Native script '{}' failed during {}: {}",
					className, callbackName, e.what());
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("ScriptSystem", "Native script '{}' failed during {} with an unknown exception",
					className, callbackName);
			}

			return false;
		}

		bool IsTaskRunning(const std::shared_ptr<ProcessTaskState>& task)
		{
			return task && !task->Completed.load(std::memory_order_acquire);
		}

		std::shared_ptr<ProcessTaskState> LaunchTask(std::function<Process::Result()> work)
		{
			auto task = std::make_shared<ProcessTaskState>();
			task->StartedAt = std::chrono::steady_clock::now();

			// Capture by weak_ptr so abandoning during shutdown doesn't stall on the worker.
			std::weak_ptr<ProcessTaskState> weakTask = task;
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

		bool TryTakeTaskResult(std::shared_ptr<ProcessTaskState>& task, Process::Result& outResult)
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

		bool NativeBuildNeedsConfigure(const std::filesystem::path& nativeProjectDirectory, const std::filesystem::path& buildDirectory)
		{
			std::error_code ec;
			const std::filesystem::path cachePath = buildDirectory / "CMakeCache.txt";
			if (!std::filesystem::exists(cachePath, ec) || ec) {
				return true;
			}

			const auto cacheWriteTime = std::filesystem::last_write_time(cachePath, ec);
			if (ec) {
				return true;
			}

			for (const char* name : { "CMakeLists.txt", "FunkeNativeScripts.cmake" }) {
				ec.clear();
				const auto writeTime = std::filesystem::last_write_time(nativeProjectDirectory / name, ec);
				if (!ec && writeTime > cacheWriteTime) return true;
			}
			return false;
		}

		void AbandonTask(std::shared_ptr<ProcessTaskState>& task, std::string_view description)
		{
			if (!task) {
				return;
			}

			const bool stillRunning = !task->Completed.load(std::memory_order_acquire);
			if (stillRunning) {
				task->Abandoned.store(true, std::memory_order_release);
				FNK_CORE_WARN_TAG("ScriptSystem", "{} is still running during teardown; detaching so shutdown stays non-blocking", description);
			}

			task.reset();
		}

		// Snapshot before user callbacks run — callbacks can invalidate the EnTT view.
		std::vector<EntityHandle> CollectScriptEntities(Scene& scene)
		{
			std::vector<EntityHandle> result;
			auto view = scene.GetRegistry().view<ScriptComponent>(entt::exclude<DisabledTag>);
			result.reserve(view.size_hint());
			for (auto entity : view) {
				result.push_back(entity);
			}
			return result;
		}

		// Funke-based walk with re-fetch each step; user callbacks can add/remove scripts or destroy the entity.
		template <typename Fn>
		void ForEachScriptOnEntitySafe(Scene& scene, EntityHandle entity, Fn&& fn)
		{
			for (size_t i = 0; ; ++i) {
				if (!scene.IsValid(entity) || !scene.HasComponent<ScriptComponent>(entity)) {
					return;
				}
				auto& scriptComp = scene.GetComponent<ScriptComponent>(entity);
				if (i >= scriptComp.Scripts.size()) {
					return;
				}
				if (!fn(scriptComp, scriptComp.Scripts[i], i)) {
					return;
				}
			}
		}

		ScriptInstance* FindManagedScriptInstance(Scene& scene, EntityHandle entity, uint32_t handle)
		{
			if (handle == 0 || !scene.IsValid(entity) || !scene.HasComponent<ScriptComponent>(entity)) {
				return nullptr;
			}

			auto& scripts = scene.GetComponent<ScriptComponent>(entity).Scripts;
			auto it = std::find_if(scripts.begin(), scripts.end(),
				[handle](const ScriptInstance& instance) { return instance.GetGCHandle() == handle; });
			return it != scripts.end() ? &(*it) : nullptr;
		}

		ScriptInstance* FindNativeScriptInstance(Scene& scene, EntityHandle entity, NativeScript* nativePtr)
		{
			if (nativePtr == nullptr || !scene.IsValid(entity) || !scene.HasComponent<ScriptComponent>(entity)) {
				return nullptr;
			}

			auto& scripts = scene.GetComponent<ScriptComponent>(entity).Scripts;
			auto it = std::find_if(scripts.begin(), scripts.end(),
				[nativePtr](const ScriptInstance& instance) { return instance.GetNativePtr() == nativePtr; });
			return it != scripts.end() ? &(*it) : nullptr;
		}

		// MUST run after GCHandle is set but before InvokeAwake/InvokeStart so OnAwake/OnStart see inspector-assigned values.
		// Read the live managed instance's current field values back into
		// PendingFieldValues so they survive a managed-assembly reload. Without
		// this, recompile resets editor-set fields to type-defaults: the values
		// live only on the instance after ApplyPendingFieldValues applied (and
		// erased) them, and the reload destroys the instance before the new one
		// is created. Capturing here also preserves runtime-modified fields.
		void SnapshotManagedFieldsToPending(ScriptComponent& scriptComp, const ScriptInstance& instance) {
			if (!instance.HasManagedInstance()) return;
			// Only capture in EDIT mode. This exists to preserve inspector-assigned
			// field values across an edit-mode recompile; doing it during PLAY would
			// copy RUNTIME-mutated values into the persistent PendingFieldValues, which
			// then bake into the pre-play snapshot and survive Stop — play-mode changes
			// must revert. (A play-mode hot-reload therefore re-applies authored values
			// rather than preserving runtime state, which is the correct trade-off.)
			if (Application::GetIsPlaying()) return;
			auto& callbacks = ScriptEngine::GetCallbacks();
			if (!callbacks.GetScriptFields) return;

			const char* rawJson = callbacks.GetScriptFields(static_cast<int32_t>(instance.GetGCHandle()));
			if (!rawJson || !*rawJson) return;

			Json::Value fields;
			if (!Json::TryParse(rawJson, fields, nullptr) || !fields.IsArray()) return;

			const std::string prefix = instance.GetClassName() + ".";
			for (const Json::Value& field : fields.GetArray()) {
				if (!field.IsObject()) continue;
				const Json::Value* nameNode = field.FindMember("name");
				const Json::Value* valueNode = field.FindMember("value");
				if (!nameNode || !valueNode) continue;
				const std::string name = nameNode->AsStringOr();
				if (name.empty()) continue;
				// Overwrite: the live instance is the freshest source — a stale
				// pending value (from a value applied earlier this session)
				// would otherwise shadow a runtime change.
				scriptComp.PendingFieldValues[prefix + name] = valueNode->AsStringOr();
			}
		}

		void ApplyPendingFieldValues(ScriptComponent& scriptComp, ScriptInstance& instance) {
			if (instance.HasNativeInstance()) {
				const std::string prefix = instance.GetClassName() + ".";
				for (const auto& [key, value] : scriptComp.PendingFieldValues) {
					if (key.size() > prefix.size() && key.rfind(prefix, 0) == 0) {
						ScriptSystem::GetNativeHost().SetFieldValue(instance.GetNativePtr(), key.substr(prefix.size()), value);
					}
				}
				return;
			}
			if (!instance.HasManagedInstance()) return;
			auto& callbacks = ScriptEngine::GetCallbacks();
			if (!callbacks.SetScriptField) return;

			// Snapshot before invoking setters: user property setters can add/remove scripts or
			// components, which invalidates scriptComp, its map, and instance.
			const std::string className = instance.GetClassName();
			const auto gcHandle = static_cast<int32_t>(instance.GetGCHandle());
			const std::string prefix = className + ".";
			std::vector<std::pair<std::string, std::string>> toApply;
			for (auto it = scriptComp.PendingFieldValues.begin(); it != scriptComp.PendingFieldValues.end(); ) {
				if (it->first.rfind(prefix, 0) == 0) {
					toApply.emplace_back(it->first.substr(prefix.size()), std::move(it->second));
					it = scriptComp.PendingFieldValues.erase(it);
				} else {
					++it;
				}
			}
			for (const auto& [fieldName, value] : toApply) {
				callbacks.SetScriptField(gcHandle, fieldName.c_str(), value.c_str());
			}
			if (!toApply.empty()) {
				FNK_CORE_INFO_TAG("ScriptSystem",
					"Applied {} pending field value(s) for {}", toApply.size(), className);
			}
		}
	}

	void ScriptSystem::Awake(Scene& scene)
	{
		m_LastScene = &scene;

		// Skipped here so launcher doesn't lock Funke-ScriptCore.dll while editor rebuilds it.
		auto* app = Application::GetInstance();
		if (app && !app->GetConfiguration().EnableScripting) {
			return;
		}

		++m_ActiveSystemCount;
		if (m_PollingOwner == nullptr) {
			m_PollingOwner = this;
		}
		if (m_ActiveSystemCount > 1) {
			// First scene's Awake handles engine/assembly init; subsequent scenes still fire OnAwake.
			if (ScriptEngine::IsInitialized())
			{
				const auto restoredCount = ScriptEngine::RestoreDynamicComponentsForScene(scene);
				if (restoredCount > 0) {
					FNK_INFO_TAG("ScriptSystem",
						"Restored {} dynamic component row(s) for scene '{}' before script Awake.",
						restoredCount, scene.GetName());
				}

				ScriptSceneScope sceneScope(scene);
				const auto entities = CollectScriptEntities(scene);
				for (EntityHandle entity : entities)
				{
					ForEachScriptOnEntitySafe(scene, entity,
						[entity](ScriptComponent& scriptComp, ScriptInstance& instance, size_t) -> bool {
							if (instance.GetClassName().empty()) return true;
							if (instance.GetType() == ScriptType::Native) return true;

							if (!instance.IsBound()) {
								instance.Bind(entity);
							}
							if (!instance.HasManagedInstance() && ScriptEngine::ClassExists(instance.GetClassName()))
							{
								uint32_t handle = ScriptEngine::CreateScriptInstance(instance.GetClassName(), entity);
								if (handle != 0) {
									instance.SetGCHandle(handle);
								}
							}
							if (instance.HasManagedInstance()) {
								// Capture handle before apply/invoke — re-entrant AddScript reallocates Scripts and dangles `instance`.
								const uint32_t awakeHandle = instance.GetGCHandle();
								ApplyPendingFieldValues(scriptComp, instance);
								ScriptEngine::InvokeAwake(awakeHandle);
							}
							return true;
						});
				}
			}
			return;
		}

		auto exeDir = Path::FromUtf8(Path::ExecutableDir());
		FunkeProject* project = ProjectManager::GetCurrentProject();

		const bool engineWasFreshlyInitialized = !ScriptEngine::IsInitialized();
		if (engineWasFreshlyInitialized) {
			ScriptEngine::Init();
		}

		if (m_CoreAssemblyPath.empty())
		{
			const std::filesystem::path coreAssembly = ScriptCoreArtifact::FindFirstCompleteDll({
				exeDir,
				exeDir / ".." / "Funke-ScriptCore",
			});
			if (!coreAssembly.empty()) {
				m_CoreAssemblyPath = Path::ToUtf8(coreAssembly);
			}
		}

		if (!ScriptEngine::IsInitialized() && std::filesystem::exists(Path::FromUtf8(m_CoreAssemblyPath)))
			ScriptEngine::LoadCoreAssembly(m_CoreAssemblyPath);

		if (m_UserAssemblyPath.empty())
		{
			if (project)
			{
				auto userDll = Path::FromUtf8(project->GetUserAssemblyOutputPath());
				m_UserAssemblyPath = Path::ToUtf8(userDll);
				if (std::filesystem::exists(userDll)) {
					m_UserAssemblyPath = Path::ToUtf8(std::filesystem::canonical(userDll));
				}
				else {
					// Scan bin/<config>/ subdirs: editor and runtime may be built with different configs, so the configured path may miss.
					std::filesystem::path binRoot = Path::FromUtf8(project->RootDirectory) / "bin";
					std::error_code binEc;
					if (std::filesystem::exists(binRoot, binEc) && !binEc) {
						const std::string targetFile = project->Name + ".dll";
						for (const auto& entry : std::filesystem::directory_iterator(binRoot, binEc)) {
							std::error_code itEc;
							if (binEc) { binEc.clear(); break; }
							if (!entry.is_directory(itEc) || itEc) continue;
							const std::filesystem::path candidate = entry.path() / targetFile;
							if (std::filesystem::exists(candidate, itEc) && !itEc) {
								m_UserAssemblyPath = Path::ToUtf8(std::filesystem::canonical(candidate));
								FNK_INFO_TAG("ScriptSystem",
									"User assembly resolved via bin/ scan: {} (configured path '{}' did not exist)",
									m_UserAssemblyPath, Path::ToUtf8(userDll));
								break;
							}
						}
					}
				}
			}
			else
			{
				auto sandboxPath = exeDir / ".." / "Funke-Sandbox" / "Funke-Sandbox.dll";
				m_UserAssemblyPath = Path::ToUtf8(sandboxPath);
				if (std::filesystem::exists(sandboxPath))
					m_UserAssemblyPath = Path::ToUtf8(std::filesystem::canonical(sandboxPath));
			}
		}

		if (!ScriptEngine::HasUserAssembly() && !m_UserAssemblyPath.empty()
			&& std::filesystem::exists(Path::FromUtf8(m_UserAssemblyPath))) {
			ScriptEngine::LoadUserAssembly(m_UserAssemblyPath);
		}
		InitializeRegisteredGlobalSystems();

		// Host identity owns editor tooling because Dist also ships an Editor.

		if (project)
		{
			auto scriptsDir = Path::FromUtf8(project->AssetsDirectory);
			auto csproj = Path::FromUtf8(project->CsprojPath);
			if (Application::IsEditor() && std::filesystem::exists(csproj))
			{
				std::vector<std::string> managedWatchTargets =
					BuildManagedWatchTargets(scriptsDir, csproj, Path::FromUtf8(project->SlnPath));
				m_SandboxProjectPath = Path::ToUtf8(std::filesystem::canonical(csproj));
				m_ScriptWatcher.Watch(
					managedWatchTargets,
					GetManagedWatchPatterns(),
					[this]() { RebuildAndReloadScripts(); });

				// Without a stamp the assembly was built the old way, for the editor's own platform.
				m_ScriptDefines = ReadScriptDefinesStamp(m_UserAssemblyPath);
				if (m_ScriptDefines.empty()) m_ScriptDefines = FunkeProject::BuildManagedDefineConstants("FUNKE_EDITOR", false);
				if (BuildArtifactIsStale(managedWatchTargets, GetManagedWatchPatterns(), Path::FromUtf8(m_UserAssemblyPath))) {
					RebuildAndReloadScripts();
				}
				else {
					PollScriptDefines();
				}
			}
		}
		else
		{
			auto sandboxProjectDir = exeDir / ".." / ".." / ".." / "Funke-Sandbox";
			auto sandboxSourceDir = sandboxProjectDir / "Source";
			auto sandboxCsproj = sandboxProjectDir / "Funke-Sandbox.csproj";
			if (Application::IsEditor() && std::filesystem::exists(sandboxCsproj))
			{
				std::vector<std::string> managedWatchTargets =
					BuildManagedWatchTargets(sandboxSourceDir, sandboxCsproj);
				m_SandboxProjectPath = Path::ToUtf8(std::filesystem::canonical(sandboxCsproj));
				m_ScriptWatcher.Watch(
					managedWatchTargets,
					GetManagedWatchPatterns(),
					[this]() { RebuildAndReloadScripts(); });

				if (BuildArtifactIsStale(managedWatchTargets, GetManagedWatchPatterns(), Path::FromUtf8(m_UserAssemblyPath))) {
					RebuildAndReloadScripts();
				}
			}
		}

		if (project)
		{
			const bool hasNativeScriptSources = project->HasNativeScriptSources();
			if (Application::IsEditor() && hasNativeScriptSources) {
				project->EnsureNativeScriptProjectFiles();
			}

			m_NativeProjectDirectory = Path::ToUtf8(NormalizeNativePath(Path::FromUtf8(project->NativeScriptsDir)));
			m_NativeSourceDirectory = Path::ToUtf8(NormalizeNativePath(Path::FromUtf8(project->NativeSourceDir)));
			const std::filesystem::path nativeDll = ResolveProjectNativeDLLPath(*project);
			m_NativeDLLPath = Path::ToUtf8(nativeDll);
			m_NativeTargetName = project->GetNativeScriptTargetName();
			if (std::filesystem::exists(nativeDll))
			{
				m_NativeHost.LoadDLL(m_NativeDLLPath);
			}
			if (Application::IsEditor() && !m_NativeProjectDirectory.empty())
			{
				std::vector<std::string> nativeWatchTargets =
					BuildNativeWatchTargets(Path::FromUtf8(m_NativeProjectDirectory), Path::FromUtf8(m_NativeSourceDirectory),
						Path::FromUtf8(project->AssetsDirectory));
				m_NativeWatcher.Watch(
					nativeWatchTargets,
					GetNativeWatchPatterns(),
					[this]() { RebuildAndReloadNativeScripts(); });

				if (hasNativeScriptSources && BuildArtifactIsStale(nativeWatchTargets, GetNativeWatchPatterns(), Path::FromUtf8(m_NativeDLLPath))) {
					RebuildAndReloadNativeScripts();
				}
			}
		}
		else
		{
			auto nativeProjectDir = exeDir / ".." / ".." / ".." / "Funke-NativeScripts";
			auto nativeSourceDir = nativeProjectDir / "Source";
			const bool hasNativeScriptSources = HasNativeScriptSourceFiles(nativeSourceDir);
			m_NativeProjectDirectory = Path::ToUtf8(NormalizeNativePath(nativeProjectDir));
			m_NativeSourceDirectory = Path::ToUtf8(NormalizeNativePath(nativeSourceDir));

			const std::filesystem::path nativeDll = ResolveStandaloneNativeDLLPath(exeDir);
			m_NativeDLLPath = Path::ToUtf8(nativeDll);
			m_NativeTargetName = "Funke-NativeScripts";
			if (std::filesystem::exists(nativeDll))
			{
				m_NativeHost.LoadDLL(m_NativeDLLPath);
			}
			if (Application::IsEditor() && !m_NativeProjectDirectory.empty())
			{
				std::vector<std::string> nativeWatchTargets =
					BuildNativeWatchTargets(Path::FromUtf8(m_NativeProjectDirectory), Path::FromUtf8(m_NativeSourceDirectory));
				m_NativeWatcher.Watch(
					nativeWatchTargets,
					GetNativeWatchPatterns(),
					[this]() { RebuildAndReloadNativeScripts(); });

				if (hasNativeScriptSources && BuildArtifactIsStale(nativeWatchTargets, GetNativeWatchPatterns(), Path::FromUtf8(m_NativeDLLPath))) {
					RebuildAndReloadNativeScripts();
				}
			}
		}

		if (engineWasFreshlyInitialized) {
			FNK_INFO_TAG("ScriptSystem", "Script system initialized");
		}

		// Eager OnAwake dispatch for first scene; C# side guards against double-fire on reload.
		if (ScriptEngine::IsInitialized())
		{
			const auto restoredCount = ScriptEngine::RestoreDynamicComponentsForScene(scene);
			if (restoredCount > 0) {
				FNK_INFO_TAG("ScriptSystem",
					"Restored {} dynamic component row(s) for scene '{}' before script Awake.",
					restoredCount, scene.GetName());
			}

			ScriptSceneScope sceneScope(scene);
			const auto entities = CollectScriptEntities(scene);
			for (EntityHandle entity : entities)
			{
				ForEachScriptOnEntitySafe(scene, entity,
					[entity](ScriptComponent& scriptComp, ScriptInstance& instance, size_t) -> bool {
						if (instance.GetClassName().empty()) return true;
						if (instance.GetType() == ScriptType::Native) return true;

						if (!instance.IsBound()) {
							instance.Bind(entity);
						}
						if (!instance.HasManagedInstance() && ScriptEngine::ClassExists(instance.GetClassName()))
						{
							uint32_t handle = ScriptEngine::CreateScriptInstance(instance.GetClassName(), entity);
							if (handle != 0) {
								instance.SetGCHandle(handle);
							}
						}
						if (instance.HasManagedInstance()) {
							// Apply BEFORE InvokeAwake so OnAwake sees fields; capture handle first — AddScript in OnAwake reallocates Scripts and dangles `instance`.
							const uint32_t awakeHandle = instance.GetGCHandle();
							ApplyPendingFieldValues(scriptComp, instance);
							ScriptEngine::InvokeAwake(awakeHandle);
						}
						return true;
					});
			}
		}
	}

	void ScriptSystem::PollScriptDefines()
	{
		if (!Application::IsEditor() || m_SandboxProjectPath.empty() || m_RebuildTask) return;
		const auto now = std::chrono::steady_clock::now();
		if (now - m_LastDefinesPoll < std::chrono::seconds(1)) return;
		m_LastDefinesPoll = now;
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) return;
		const std::string defines = FunkeProject::BuildManagedDefineConstants("FUNKE_EDITOR");
		if (defines == m_ScriptDefines) return;

		// IntelliSense follows too; a profile edited in the Build Profiles panel does not save project.json.
		(void)project->WriteManagedDefinesProps();
		const FunkeProject::EditorScriptTarget target = project->GetEditorScriptTarget();
		FNK_INFO_TAG("ScriptSystem", "C# defines changed: scripts now compile as {}{}.",
			FunkeBuildProfile::PlatformDisplayName(target.Platform),
			target.ProfileName.empty() ? std::string() : " (build profile '" + target.ProfileName + "')");
		RebuildAndReloadScripts();
	}

	void ScriptSystem::RebuildAndReloadScripts()
	{
		// Drop rebuilds during shutdown — late watcher events would lock the .dll mid-teardown.
		if (Application::IsShuttingDown()) {
			m_RebuildQueued = false;
			return;
		}
		if (m_RebuildTask) {
			m_RebuildQueued = true;
			return;
		}
		if (m_SandboxProjectPath.empty()) {
			m_RebuildQueued = false;
			if (ProjectManager::GetCurrentProject()) {
				m_ManagedRebuildError.clear();
				return;
			}
			m_ManagedRebuildError = "Cannot rebuild C# scripts: managed project path is not configured.";
			FNK_ERROR_TAG("ScriptSystem", "Cannot rebuild C# scripts: managed project path is not configured.");
			return;
		}

		FNK_INFO_TAG("ScriptSystem", "Rebuilding C# scripts...");
		m_ManagedRebuildError.clear();
		const std::string sandboxProjectPath = m_SandboxProjectPath;
		// Snapshot FunkeProject statics on the main thread so the worker
		// can't race with a project reload that mutates them mid-build (M9).
		const std::string buildConfig = FunkeProject::GetActiveBuildConfiguration();
		if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
			m_ManagedBuildOutputPath = project->GetUserAssemblyOutputPath(buildConfig);
		}
		else {
			m_ManagedBuildOutputPath = m_UserAssemblyPath;
		}
		const char* primarySymbol = Application::IsEditor() ? "FUNKE_EDITOR" : "FUNKE_BUILD";
		const std::string defineConstants = FunkeProject::BuildManagedDefineConstants(primarySymbol);
		m_ScriptDefines = defineConstants;
		m_BuildingScriptDefines = defineConstants;
		const std::string defineConstantsArg = "-p:DefineConstants=" + defineConstants;
		// FunkeCodegenEnabled=false: hot-reload must not regenerate CodegenComponents.cpp — the running engine has the old C++ layout and a size mismatch would crash on next ComponentTypes<T> init.
		const char* codegenArg = "-p:FunkeCodegenEnabled=false";
		const bool inProcess = ManagedScriptCompiler::EnsureLoaded();
		ManagedScriptCompiler::Request compileRequest{
			sandboxProjectPath, buildConfig, { codegenArg, defineConstantsArg }, m_ManagedBuildOutputPath };
		m_RebuildTask = LaunchTask([sandboxProjectPath, buildConfig, defineConstantsArg, codegenArg, inProcess, compileRequest]() {
			if (inProcess) {
				const ManagedScriptCompiler::Result compiled = ManagedScriptCompiler::Compile(compileRequest);
				if (compiled.Status == ManagedScriptCompiler::Status::Compiled) {
					FNK_INFO_TAG("ScriptSystem", "C# scripts compiled in-process in {:.0f} ms{}", compiled.ElapsedMs,
						compiled.Evaluated ? " (project re-evaluated)" : "");
					Process::Result result;
					result.ExitCode = 0;
					return result;
				}
				if (compiled.Status == ManagedScriptCompiler::Status::Failed) {
					Process::Result result;
					result.ExitCode = 1;
					result.Output = compiled.Diagnostics;
					return result;
				}
				FNK_INFO_TAG("ScriptSystem", "Running dotnet build: {}", compiled.Message);
			}
			return Process::Run({
				"dotnet",
				"build",
				sandboxProjectPath,
				"-c", buildConfig,
				"--nologo",
				"-v", "q",
				"-nodeReuse:false",
				"-p:UseSharedCompilation=false",
				codegenArg,
				defineConstantsArg
			}, {}, std::chrono::minutes(5));
		});
	}

	Process::Result ScriptSystem::BuildNativeScripts(const std::string& projectDirectory,
		const std::string& targetName, const std::string& configuration, const std::string& profile,
		const std::string& buildFolder, const std::vector<std::string>& configureOptions)
	{
		const std::filesystem::path buildDirectory = Path::FromUtf8(projectDirectory) / buildFolder;
		const auto cmake = ResolveCMakeExecutable();
		if (!cmake) {
			Process::Result result{};
			result.Output = "CMake was not found. Install the Visual Studio C++ CMake tools, add cmake to PATH, or set FUNKE_CMAKE_PATH.";
			return result;
		}
		Process::Result configureResult{};
		if (!configureOptions.empty() || NativeBuildNeedsConfigure(Path::FromUtf8(projectDirectory), buildDirectory)) {
			std::vector<std::string> command = {
				*cmake, "-B", Path::ToUtf8(buildDirectory), "-S", projectDirectory,
				"-DCMAKE_BUILD_TYPE=" + configuration, "-DFUNKE_BUILD_PROFILE=" + profile
			};
#ifdef FNK_PLATFORM_WINDOWS
			if (!std::filesystem::exists(buildDirectory / "CMakeCache.txt") && !std::getenv("CMAKE_GENERATOR")) {
				if (const char* programFiles = std::getenv("ProgramFiles(x86)")) {
					const auto vswhere = Path::FromUtf8(programFiles) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
					if (std::filesystem::exists(vswhere)) {
						const auto visualStudio = Process::Run({ Path::ToUtf8(vswhere), "-latest", "-products", "*",
							"-version", "[17.0,18.0)", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
							"-property", "installationPath" }, {}, std::chrono::seconds(15));
						if (visualStudio.Succeeded() && !TrimWhitespace(visualStudio.Output).empty()) {
							command.insert(command.end(), { "-G", "Visual Studio 17 2022", "-T", "v143", "-A",
#if defined(_M_ARM64)
								"ARM64"
#else
								"x64"
#endif
							});
						}
					}
				}
			}
#endif
			command.insert(command.end(), configureOptions.begin(), configureOptions.end());
			configureResult = Process::Run(command, projectDirectory, std::chrono::minutes(5));
			if (!configureResult.Succeeded()) return configureResult;
		}
		std::vector<std::string> command = { *cmake, "--build", Path::ToUtf8(buildDirectory), "--config", configuration };
		if (!targetName.empty()) {
			command.push_back("--target");
			command.push_back(targetName);
		}
		Process::Result result = Process::Run(command, projectDirectory, std::chrono::minutes(5));
		result.Output = configureResult.Output + result.Output;
		return result;
	}

	void ScriptSystem::RebuildAndReloadNativeScripts()
	{
		// See RebuildAndReloadScripts — same shutdown-window race.
		if (Application::IsShuttingDown()) {
			m_NativeRebuildQueued = false;
			return;
		}
		if (m_NativeRebuildTask) {
			m_NativeRebuildQueued = true;
			return;
		}
		if (m_NativeProjectDirectory.empty()) {
			m_NativeRebuildQueued = false;
			return;
		}

		FunkeProject* project = ProjectManager::GetCurrentProject();
		const bool hasNativeScriptSources = project
			? project->HasNativeScriptSources()
			: HasNativeScriptSourceFiles(Path::FromUtf8(m_NativeSourceDirectory));

		if (!hasNativeScriptSources) {
			m_NativeRebuildError.clear();
			if (m_NativeHost.IsLoaded()) {
				ForEachLoadedScene([this](Scene& loadedScene) { TeardownNativeScripts(loadedScene); });
				m_NativeHost.UnloadDLL();
				FNK_INFO_TAG("ScriptSystem", "Native script sources removed; unloaded native script DLL");
			}
			return;
		}

		if (project) {
			project->EnsureNativeScriptProjectFiles();
			m_NativeProjectDirectory = Path::ToUtf8(NormalizeNativePath(Path::FromUtf8(project->NativeScriptsDir)));
			m_NativeSourceDirectory = Path::ToUtf8(NormalizeNativePath(Path::FromUtf8(project->NativeSourceDir)));
			m_NativeTargetName = project->GetNativeScriptTargetName();
		}

		FNK_INFO_TAG("ScriptSystem", "Rebuilding native scripts...");
		m_NativeRebuildError.clear();
		const std::string nativeProjectDirectory = m_NativeProjectDirectory;
		const std::string buildConfig = GetActiveNativeBuildConfig();
		const std::string nativeTargetName = m_NativeTargetName;
		m_NativeRebuildTask = LaunchTask([nativeProjectDirectory, buildConfig, nativeTargetName]() {
			return BuildNativeScripts(nativeProjectDirectory, nativeTargetName, buildConfig);
		});
	}

	bool ScriptSystem::RequestRebuildAndReloadAll()
	{
		const bool wasRebuilding = IsRebuilding();

		RebuildAndReloadScripts();
		RebuildAndReloadNativeScripts();

		return IsRebuilding() || wasRebuilding;
	}

	bool ScriptSystem::IsRebuilding() const
	{
		return m_RebuildTask != nullptr || m_NativeRebuildTask != nullptr;
	}

	std::string ScriptSystem::GetLastRebuildOutput()
	{
		if (m_ManagedRebuildError.empty()) return m_NativeRebuildError;
		if (m_NativeRebuildError.empty()) return m_ManagedRebuildError;
		return m_ManagedRebuildError + "\n" + m_NativeRebuildError;
	}

	uint64_t ScriptSystem::GetCompletedRebuildCount()
	{
		return m_CompletedRebuildCount;
	}

	bool ScriptSystem::ReloadManagedAssemblies()
	{
		if (!ScriptEngine::IsInitialized()) {
			return false;
		}

		// Same order as the hot-reload path in OnPreRender: without the teardown,
		// every ScriptInstance keeps a stale non-zero GC handle after the unload, so
		// Update() never recreates it and all managed scripts silently stop running.
		ForEachLoadedScene([](Scene& loadedScene) { TeardownManagedScripts(loadedScene); });
		if (!ScriptEngine::ReloadAssemblies()) {
			if (ScriptEngine::HasUserAssembly()) {
				InitializeRegisteredGlobalSystems();
				FNK_ERROR_TAG("ScriptSystem", "User assembly could not be reloaded; scripts continue with the previous working assembly.");
			}
			else {
				FNK_ERROR_TAG("ScriptSystem", "User assembly could not be reloaded; managed scripts are unavailable until the next successful build.");
			}
			return false;
		}

		InitializeRegisteredGlobalSystems();
		PrefabTemplateCache::Get().InvalidateAll();
		FNK_INFO_TAG("ScriptSystem", "User assembly reloaded");
		return true;
	}

	void ScriptSystem::TeardownManagedScripts(Scene& scene)
	{
		if (!ScriptEngine::IsInitialized()) {
			return;
		}

		// RAII guard defers any watcher-triggered rebuild until teardown completes, preventing LoadUserAssembly while live GCHandles exist.
		struct TeardownGuard {
			TeardownGuard()  { ScriptSystem::m_TeardownInProgress.store(true,  std::memory_order_release); }
			~TeardownGuard() { ScriptSystem::m_TeardownInProgress.store(false, std::memory_order_release); }
		} guard;

		std::vector<EntityHandle> entities;
		auto view = scene.GetRegistry().view<ScriptComponent>();
		entities.reserve(view.size()); // single-component view: size() is exact, not a hint
		for (auto entity : view) entities.push_back(entity);

		for (EntityHandle entity : entities) {
			if (!scene.GetRegistry().valid(entity)) continue;
			if (!scene.GetRegistry().all_of<ScriptComponent>(entity)) continue;

			std::vector<ManagedScriptTeardownState> scripts;
			auto& scriptComp = scene.GetRegistry().get<ScriptComponent>(entity);
			scripts.reserve(scriptComp.Scripts.size());
			for (const ScriptInstance& instance : scriptComp.Scripts) {
				if (instance.HasManagedInstance()) {
					// Snapshot live field values BEFORE teardown so a managed
					// reload (recompile) re-applies them instead of resetting
					// to type-defaults. See SnapshotManagedFieldsToPending.
					SnapshotManagedFieldsToPending(scriptComp, instance);
					scripts.push_back(CaptureManagedTeardownState(instance));
				}
			}

			for (const ManagedScriptTeardownState& script : scripts) {
				if (script.Handle == 0) continue;
				if (!scene.GetRegistry().valid(entity)) continue;
				if (scene.GetRegistry().all_of<ScriptComponent>(entity)) {
					auto& currentScripts = scene.GetRegistry().get<ScriptComponent>(entity).Scripts;
					if (auto it = std::find_if(currentScripts.begin(), currentScripts.end(),
						[handle = script.Handle](const ScriptInstance& instance) { return instance.GetGCHandle() == handle; });
						it != currentScripts.end()) {
						TeardownManagedScriptInstance(scene, *it);
						continue;
					}
				}
			}
		}

	}

	void ScriptSystem::TeardownNativeScripts(Scene& scene)
	{
		// Mirror TeardownManagedScripts — same race surface, different language.
		struct TeardownGuard {
			TeardownGuard()  { ScriptSystem::m_TeardownInProgress.store(true,  std::memory_order_release); }
			~TeardownGuard() { ScriptSystem::m_TeardownInProgress.store(false, std::memory_order_release); }
		} guard;

		std::vector<EntityHandle> entities;
		auto view = scene.GetRegistry().view<ScriptComponent>();
		entities.reserve(view.size()); // single-component view: size() is exact, not a hint
		for (auto entity : view) entities.push_back(entity);

		for (EntityHandle entity : entities) {
			if (!scene.GetRegistry().valid(entity)) continue;
			if (!scene.GetRegistry().all_of<ScriptComponent>(entity)) continue;

			std::vector<NativeScriptTeardownState> scripts;
			auto& scriptComp = scene.GetRegistry().get<ScriptComponent>(entity);
			scripts.reserve(scriptComp.Scripts.size());
			for (const ScriptInstance& instance : scriptComp.Scripts) {
				if (instance.HasNativeInstance()) {
					scripts.push_back(CaptureNativeTeardownState(instance));
				}
			}

			for (const NativeScriptTeardownState& script : scripts) {
				if (!script.Instance) continue;
				if (!scene.GetRegistry().valid(entity)) continue;
				if (scene.GetRegistry().all_of<ScriptComponent>(entity)) {
					auto& currentScripts = scene.GetRegistry().get<ScriptComponent>(entity).Scripts;
					if (auto it = std::find_if(currentScripts.begin(), currentScripts.end(),
						[ptr = script.Instance](const ScriptInstance& instance) { return instance.GetNativePtr() == ptr; });
						it != currentScripts.end()) {
						TeardownNativeScriptInstance(scene, *it, m_NativeHost);
						continue;
					}
				}
			}
		}

	}

	bool ScriptSystem::RemoveScript(Entity entity, size_t index)
	{
		Scene* scene = entity.GetScene();
		if (scene == nullptr) {
			return false;
		}

		if (!entity.HasComponent<ScriptComponent>()) {
			return false;
		}

		auto& scriptComp = entity.GetComponent<ScriptComponent>();
		if (index >= scriptComp.Scripts.size()) {
			return false;
		}

		const ScriptInstance& instance = scriptComp.Scripts[index];
		const std::string removedClassName = instance.GetClassName();
		const ManagedScriptTeardownState managedState = CaptureManagedTeardownState(instance);
		const NativeScriptTeardownState nativeState = CaptureNativeTeardownState(instance);

		scriptComp.Scripts.erase(scriptComp.Scripts.begin() + static_cast<ptrdiff_t>(index));
		RemovePendingFieldValuesForClass(scriptComp, removedClassName);

		// Also remove the paired DynamicComponentStorage row for IComponent C# structs; Scripts-only removal would leave it dangling.
		auto& componentRegistry = SceneManager::GetComponentRegistry();
		if (const ComponentInfo* info = componentRegistry.FindBySerializedName(removedClassName)) {
			if (info->isDynamic && info->remove) {
				info->remove(entity);
			}
		}

		InvokeManagedScriptTeardown(*scene, managedState);
		InvokeNativeScriptTeardown(*scene, nativeState, m_NativeHost);

		scene->MarkDirty();
		return true;
	}

	void ScriptSystem::RemoveAllScripts(Entity entity)
	{
		if (entity.GetScene() == nullptr || !entity.HasComponent<ScriptComponent>()) {
			return;
		}

		auto& scriptComp = entity.GetComponent<ScriptComponent>();
		for (size_t index = scriptComp.Scripts.size(); index > 0; --index) {
			RemoveScript(entity, index - 1);
		}

		scriptComp.PendingFieldValues.clear();
	}

	void ScriptSystem::SetScriptsEnabled(Entity entity, bool enabled)
	{
		Scene* scene = entity.GetScene();
		if (scene == nullptr || !entity.HasComponent<ScriptComponent>() || !ScriptEngine::IsInitialized()) {
			return;
		}

		ScriptSceneScope sceneScope(*scene);
		const EntityHandle entityHandle = entity.GetHandle();
		// An entity that was disabled since load has no instances yet. Create them now (scene field values, OnAwake,
		// OnEnable) instead of on the next ScriptSystem pass, so a caller that enables the entity and then sets a field
		// through GetComponent<T>() gets a live instance whose value the scene data no longer overwrites.
		const bool createMissing = enabled && Application::GetIsPlaying() && scene->IsSystemEnabled<ScriptSystem>();
		ForEachScriptOnEntitySafe(*scene, entityHandle,
			[&](ScriptComponent& scriptComp, ScriptInstance& instance, size_t) -> bool {
			if (createMissing && !instance.HasAnyInstance()
				&& (instance.GetType() == ScriptType::Managed || instance.GetType() == ScriptType::Unknown)
				&& !instance.GetClassName().empty()
				&& ScriptEngine::ClassExists(instance.GetClassName()))
			{
				if (!instance.IsBound()) {
					instance.Bind(entityHandle);
				}
				const uint32_t created = ScriptEngine::CreateScriptInstance(instance.GetClassName(), entityHandle);
				if (created == 0) {
					return true;
				}
				instance.SetGCHandle(created);
				// Setters and OnAwake can add or remove scripts, which reallocates Scripts and dangles `instance`.
				ApplyPendingFieldValues(scriptComp, instance);
				ScriptEngine::InvokeAwake(created);
				ScriptInstance* live = FindManagedScriptInstance(*scene, entityHandle, created);
				if (live && !live->HasEnabled()) {
					live->MarkEnabled();
					ScriptEngine::InvokeOnEnable(created);
				}
				return true;
			}
			if (!instance.HasManagedInstance()) {
				return true;
			}

			const uint32_t handle = instance.GetGCHandle();
			if (enabled && !instance.HasEnabled()) {
				instance.MarkEnabled();
				ScriptEngine::InvokeOnEnable(handle);
			}
			else if (!enabled && instance.HasEnabled()) {
				instance.MarkDisabled();
				ScriptEngine::InvokeOnDisable(handle);
			}
			return true;
		});
	}

	void ScriptSystem::CreateScriptInstancesNow(Scene& scene, EntityHandle root)
	{
		if (!Application::GetIsPlaying() || !ScriptEngine::IsInitialized() || m_TeardownInProgress
			|| !scene.IsValid(root) || !scene.IsSystemEnabled<ScriptSystem>()) {
			return;
		}

		// Disabled entities stay asleep until enabled (SetScriptsEnabled), as in the scene-start pass.
		std::vector<EntityHandle> entities;
		std::vector<EntityHandle> pending{ root };
		while (!pending.empty()) {
			const EntityHandle entity = pending.back();
			pending.pop_back();
			if (!scene.IsValid(entity) || scene.HasComponent<DisabledTag>(entity)) continue;
			if (scene.HasComponent<ScriptComponent>(entity)) entities.push_back(entity);
			for (EntityHandle child : scene.GetEntity(entity).GetChildren()) pending.push_back(child);
		}
		if (entities.empty()) return;

		ScriptSceneScope sceneScope(scene);
		struct Created { EntityHandle Entity; uint32_t Handle; };
		std::vector<Created> created;
		// Every instance exists before any field is applied: a field pointing at another script of the subtree
		// resolves through GetComponent and would read null otherwise.
		for (EntityHandle entity : entities) {
			ForEachScriptOnEntitySafe(scene, entity,
				[&](ScriptComponent&, ScriptInstance& instance, size_t) -> bool {
				if (instance.HasAnyInstance() || instance.GetClassName().empty()
					|| (instance.GetType() != ScriptType::Managed && instance.GetType() != ScriptType::Unknown)
					|| !ScriptEngine::ClassExists(instance.GetClassName())) {
					return true;
				}
				if (!instance.IsBound()) {
					instance.Bind(entity);
				}
				const uint32_t handle = ScriptEngine::CreateScriptInstance(instance.GetClassName(), entity);
				if (handle != 0) {
					instance.SetGCHandle(handle);
					created.push_back({ entity, handle });
				}
				return true;
			});
		}

		// Re-find each instance after user code (setters, OnAwake) ran: it may have added scripts or destroyed entities.
		for (const Created& c : created) {
			if (ScriptInstance* instance = FindManagedScriptInstance(scene, c.Entity, c.Handle)) {
				ApplyPendingFieldValues(scene.GetComponent<ScriptComponent>(c.Entity), *instance);
			}
		}
		for (const Created& c : created) {
			if (FindManagedScriptInstance(scene, c.Entity, c.Handle)) {
				ScriptEngine::InvokeAwake(c.Handle);
			}
		}
		for (const Created& c : created) {
			if (!scene.IsValid(c.Entity) || scene.HasComponent<DisabledTag>(c.Entity)) continue;
			ScriptInstance* live = FindManagedScriptInstance(scene, c.Entity, c.Handle);
			if (live && !live->HasEnabled()) {
				live->MarkEnabled();
				ScriptEngine::InvokeOnEnable(c.Handle);
			}
		}
	}

#if FUNKE_WITH_2D
	namespace {
		enum class CollisionDispatchPhase {
			Enter,
			Stay,
			Exit
		};

		void DispatchCollision2D(const Collision2D& collision, CollisionDispatchPhase phase)
		{
			if (!ScriptEngine::IsInitialized()) {
				return;
			}

			const auto dispatchToEntity = [&](Scene* selfScene, EntityHandle self, Scene* otherScene, EntityHandle other) {
				if (!selfScene || !selfScene->IsValid(self)
					|| !selfScene->HasComponent<ScriptComponent>(self)
					|| selfScene->HasComponent<DisabledTag>(self)) {
					return;
				}

				Scene& scene = *selfScene;
				ScriptSceneScope sceneScope(scene);
				const uint64_t selfID = ToManagedEntityID(scene, self);
				const uint64_t otherID = otherScene && otherScene->IsValid(other)
					? ToManagedEntityID(*otherScene, other) : 0;
				const uint64_t entityAID = collision.sceneA && collision.sceneA->IsValid(collision.entityA)
					? ToManagedEntityID(*collision.sceneA, collision.entityA) : 0;
				const uint64_t entityBID = collision.sceneB && collision.sceneB->IsValid(collision.entityB)
					? ToManagedEntityID(*collision.sceneB, collision.entityB) : 0;

				// Funke-based walk with re-fetch — collision callbacks are
				// user code that can AddScript (vector reallocation),
				// RemoveScript, or Destroy(self).
				ForEachScriptOnEntitySafe(scene, self,
					[&, phase](ScriptComponent&, ScriptInstance& instance, size_t) -> bool {
						if (!instance.HasManagedInstance() || !instance.HasEnabled()) {
							return true;
						}

						if (phase == CollisionDispatchPhase::Stay && !ScriptEngine::WantsCallback(instance,
							collision.isTrigger ? ScriptCallback_TriggerStay2D : ScriptCallback_CollisionStay2D)) {
							return true;
						}

						if (collision.isTrigger) {
							ScriptEngine::InvokeTrigger2D(instance.GetGCHandle(), static_cast<int32_t>(phase), selfID, otherID, entityAID, entityBID);
							return true;
						}

						switch (phase) {
						case CollisionDispatchPhase::Enter:
							ScriptEngine::InvokeCollisionEnter2D(instance.GetGCHandle(), selfID, otherID, entityAID, entityBID, collision.contactPoint.x, collision.contactPoint.y);
							break;
						case CollisionDispatchPhase::Stay:
							ScriptEngine::InvokeCollisionStay2D(instance.GetGCHandle(), selfID, otherID, entityAID, entityBID, collision.contactPoint.x, collision.contactPoint.y);
							break;
						case CollisionDispatchPhase::Exit:
							ScriptEngine::InvokeCollisionExit2D(instance.GetGCHandle(), selfID, otherID, entityAID, entityBID, collision.contactPoint.x, collision.contactPoint.y);
							break;
						}
						return true;
					});
			};

			collision.ForEachParticipant(dispatchToEntity);
		}
	}

	void ScriptSystem::DispatchCollisionEnter2D(const Collision2D& collision)
	{
		DispatchCollision2D(collision, CollisionDispatchPhase::Enter);
	}

	void ScriptSystem::DispatchCollisionStay2D(const Collision2D& collision)
	{
		DispatchCollision2D(collision, CollisionDispatchPhase::Stay);
	}

	void ScriptSystem::DispatchCollisionExit2D(const Collision2D& collision)
	{
		DispatchCollision2D(collision, CollisionDispatchPhase::Exit);
	}
#endif

#if FUNKE_WITH_3D
	void ScriptSystem::DispatchCollision3D(const Collision3D& collision, CollisionPhase3D phase)
	{
		Scene* scene = collision.OwningScene;
		if (!ScriptEngine::IsInitialized() || !scene) {
			return;
		}

		const auto dispatchToEntity = [&](EntityHandle self, EntityHandle other, const Vec3& normalTowardSelf) {
			if (!scene->IsValid(self)
				|| !scene->HasComponent<ScriptComponent>(self)
				|| scene->HasComponent<DisabledTag>(self)) {
				return;
			}

			ScriptSceneScope sceneScope(*scene);
			const uint64_t selfID = ToManagedEntityID(*scene, self);
			const uint64_t otherID = scene->IsValid(other) ? ToManagedEntityID(*scene, other) : 0;
			ForEachScriptOnEntitySafe(*scene, self,
				[&](ScriptComponent&, ScriptInstance& instance, size_t) -> bool {
					const bool skipStay = phase == CollisionPhase3D::Stay && !ScriptEngine::WantsCallback(instance,
						collision.IsTrigger ? ScriptCallback_TriggerStay3D : ScriptCallback_CollisionStay3D);
					if (instance.HasManagedInstance() && instance.HasEnabled() && !skipStay) {
						ScriptEngine::InvokeCollision3D(instance.GetGCHandle(), static_cast<int32_t>(phase), collision.IsTrigger,
							selfID, otherID, collision.ContactPoint, normalTowardSelf);
					}
					return true;
				});
		};

		// Collision3D::Normal points from A to B.
		dispatchToEntity(collision.EntityA, collision.EntityB, -collision.Normal);
		if (collision.EntityB != collision.EntityA) {
			dispatchToEntity(collision.EntityB, collision.EntityA, collision.Normal);
		}
	}
#endif

	void ScriptSystem::OnPreRender(Scene& scene)
	{
		(void)scene;
		if (m_PollingOwner == nullptr) {
			m_PollingOwner = this;
		}
		if (m_PollingOwner != this) {
			return;
		}

		// Skip file watcher polling while a script is being created/renamed
		if (!m_SuppressRecompile && m_AutoRecompileEnabled) {
			m_ScriptWatcher.Poll(1.0f);
			m_NativeWatcher.Poll(1.0f);
			PollScriptDefines();
		}

		bool anyRebuilding = false;

		Process::Result result;
		if (TryTakeTaskResult(m_RebuildTask, result))
		{
			m_ManagedRebuildError.clear();
			if (result.Succeeded())
			{
				const std::filesystem::path outputPath = Path::FromUtf8(m_ManagedBuildOutputPath);
				std::error_code outputEc;
				if (m_ManagedBuildOutputPath.empty()
					|| !std::filesystem::is_regular_file(outputPath, outputEc)
					|| outputEc)
				{
					m_ManagedRebuildError = "C# build succeeded, but the assembly was not found: " + m_ManagedBuildOutputPath;
					FNK_ERROR_TAG("ScriptSystem", "C# build succeeded, but assembly was not found at '{}'.", m_ManagedBuildOutputPath);
				}
				else
				{
					const std::filesystem::path canonicalOutput = std::filesystem::canonical(outputPath, outputEc);
					m_UserAssemblyPath = Path::ToUtf8(outputEc ? outputPath : canonicalOutput);
					ForEachLoadedScene([this](Scene& loadedScene) { TeardownManagedScripts(loadedScene); });
					if (ScriptEngine::LoadUserAssembly(m_UserAssemblyPath)) {
						InitializeRegisteredGlobalSystems();
						// Managed reload can invalidate cached component layouts.
						PrefabTemplateCache::Get().InvalidateAll();
						if (Application::IsEditor()) WriteScriptDefinesStamp(m_UserAssemblyPath, m_BuildingScriptDefines);
						FNK_INFO_TAG("ScriptSystem", "C# scripts rebuilt and reloaded from '{}'", m_UserAssemblyPath);
					}
					else {
						m_ManagedRebuildError = "C# build succeeded, but the rebuilt assembly could not be loaded.";
						if (ScriptEngine::HasUserAssembly()) {
							InitializeRegisteredGlobalSystems();
							m_ManagedRebuildError += " Scripts continue with the previous working assembly.";
						}
						FNK_ERROR_TAG("ScriptSystem", "{}", m_ManagedRebuildError);
					}
				}
			}
			else
			{
				m_ManagedRebuildError = result.Output.empty() ? "C# build failed; see the log" : result.Output;
				FNK_ERROR_TAG("ScriptSystem", "C# build failed (exit code {})", result.ExitCode);
				if (!result.Output.empty()) {
					FNK_ERROR_TAG("ScriptSystem", "Compiler output:\n{}", result.Output);
				}
			}
			++m_CompletedRebuildCount;

			if (m_RebuildQueued) {
				if (!m_TeardownInProgress.load(std::memory_order_acquire)) {
					m_RebuildQueued = false;
					RebuildAndReloadScripts();
				}
			}
		}

		if (TryTakeTaskResult(m_NativeRebuildTask, result))
		{
			m_NativeRebuildError.clear();
			if (result.Succeeded())
			{
				const std::filesystem::path exeDir = Path::FromUtf8(Path::ExecutableDir());
				if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
					m_NativeDLLPath = Path::ToUtf8(ResolveProjectNativeDLLPath(*project));
				}
				else {
					m_NativeDLLPath = Path::ToUtf8(ResolveStandaloneNativeDLLPath(exeDir));
				}

				if (!std::filesystem::exists(Path::FromUtf8(m_NativeDLLPath))) {
					FNK_ERROR_TAG("ScriptSystem", "Native scripts built, but DLL was not found at '{}'", m_NativeDLLPath);
					m_NativeRebuildError = "Native scripts built, but the library was not found at: " + m_NativeDLLPath;
				}
				else {
					// Swap only after the replacement DLL exists so failed rebuilds keep the previous host alive.
					ForEachLoadedScene([this](Scene& loadedScene) {
						for (auto [entity, component] : loadedScene.GetRegistry().view<ScriptComponent>().each()) {
							(void)entity;
							if (Application::GetIsPlaying()) continue;
							for (const ScriptInstance& instance : component.Scripts) {
								if (!instance.HasNativeInstance()) continue;
								const std::string prefix = instance.GetClassName() + ".";
								for (const auto& [name, value] : m_NativeHost.GetFieldValues(instance.GetNativePtr())) {
									component.PendingFieldValues[prefix + name] = value;
								}
							}
						}
						TeardownNativeScripts(loadedScene);
					});

					if (!m_NativeHost.LoadDLL(m_NativeDLLPath)) {
						FNK_ERROR_TAG("ScriptSystem", "Native scripts built, but failed to load '{}'", m_NativeDLLPath);
						m_NativeRebuildError = "Native scripts built, but the library could not be loaded: " + m_NativeDLLPath;
						FNK_WARN_TAG("ScriptSystem", "Keeping the previous native script DLL loaded; instances will be recreated on the next update.");
					}
					else {
						PrefabTemplateCache::Get().InvalidateAll();
						FNK_INFO_TAG("ScriptSystem", "Native scripts rebuilt and reloaded");
					}
				}
			}
			else
			{
				FNK_ERROR_TAG("ScriptSystem", "Native build failed (exit code {})", result.ExitCode);
				if (!result.Output.empty()) {
					FNK_ERROR_TAG("ScriptSystem", "{}", result.Output);
				}
				m_NativeRebuildError = result.Output.empty() ? "Native build failed; see the log" : result.Output;
			}
			++m_CompletedRebuildCount;

			if (m_NativeRebuildQueued) {
				// See managed branch above — same teardown race rationale.
				if (!m_TeardownInProgress.load(std::memory_order_acquire)) {
					m_NativeRebuildQueued = false;
					RebuildAndReloadNativeScripts();
				}
			}
		}

		(void)anyRebuilding;
	}

	bool ScriptSystem::IsScriptRebuildRunning() const
	{
		return IsTaskRunning(m_RebuildTask);
	}

	bool ScriptSystem::IsNativeRebuildRunning() const
	{
		return IsTaskRunning(m_NativeRebuildTask);
	}

	float ScriptSystem::GetActiveRebuildElapsedSeconds() const
	{
		const bool nativeRunning = IsTaskRunning(m_NativeRebuildTask);
		const bool managedRunning = IsTaskRunning(m_RebuildTask);
		if (!nativeRunning && !managedRunning) {
			return 0.0f;
		}

		const auto startTime = nativeRunning ? m_NativeRebuildTask->StartedAt : m_RebuildTask->StartedAt;
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();
	}

	void ScriptSystem::Update(Scene& scene)
	{
		FUNKE_PROFILE_SCOPE("Scripts");
		m_LastScene = &scene;
		ScriptSceneScope sceneScope(scene);
		const bool managedRuntimeReady = ScriptEngine::IsInitialized();

		// Snapshot: user Update() may add/remove ScriptComponents and entities.
		const auto entities = CollectScriptEntities(scene);

		for (EntityHandle entity : entities)
		{
			ForEachScriptOnEntitySafe(scene, entity,
				[&, entity](ScriptComponent& scriptCompRef, ScriptInstance& instanceRef, size_t) -> bool {
					// Use pointers so we can rebind after any user callback that reallocates ScriptComponent::Scripts.
					ScriptInstance* instance = &instanceRef;
					ScriptComponent* scriptComp = &scriptCompRef;
					(void)scriptComp;

					if (instance->GetClassName().empty()) return true;

					if (!instance->IsBound())
						instance->Bind(entity);

					if (!instance->HasAnyInstance())
					{
						const ScriptType scriptType = instance->GetType();
						const bool canUseManaged = scriptType == ScriptType::Managed || scriptType == ScriptType::Unknown;
						const bool canUseNative = scriptType == ScriptType::Native || scriptType == ScriptType::Unknown;

						if (canUseManaged && managedRuntimeReady && ScriptEngine::ClassExists(instance->GetClassName()))
						{
							uint32_t handle = ScriptEngine::CreateScriptInstance(
								instance->GetClassName(), entity);
							if (handle != 0)
								instance->SetGCHandle(handle);
						}
						if (!instance->HasAnyInstance()
							&& canUseNative
							&& !IsTaskRunning(m_NativeRebuildTask)
							&& m_NativeHost.IsLoaded())
						{
							NativeScript* native = m_NativeHost.CreateInstance(
								instance->GetClassName(), entity, &scene);
							if (native)
								instance->SetNativePtr(native);
						}

						if (!instance->HasAnyInstance())
							return true;

						// Capture before apply: property setters may AddScript, reallocating Scripts and dangling `instance`.
						const bool isManaged = instance->GetType() == ScriptType::Managed && instance->HasManagedInstance();
						const uint32_t awakeHandle = instance->GetGCHandle();
						ApplyPendingFieldValues(*scriptComp, *instance);
						if (isManaged) {
							instance = FindManagedScriptInstance(scene, entity, awakeHandle);
							if (!instance) return true;
							ScriptEngine::InvokeAwake(awakeHandle);
							// Rebind: user Awake may have AddScript'd, reallocating Scripts.
							instance = FindManagedScriptInstance(scene, entity, awakeHandle);
							if (!instance) return true;
						}
					}

					if (instance->GetType() == ScriptType::Managed)
					{
						if (!managedRuntimeReady || !instance->HasManagedInstance()) {
							return true;
						}

						const uint32_t handle = instance->GetGCHandle();
						if (!instance->HasEnabled())
						{
							instance->MarkEnabled();
							ScriptEngine::InvokeOnEnable(handle);
							instance = FindManagedScriptInstance(scene, entity, handle);
							if (!instance) return true;
						}

						if (!instance->HasStarted())
						{
							instance->MarkStarted();
							ScriptEngine::InvokeStart(handle);
							instance = FindManagedScriptInstance(scene, entity, handle);
							if (!instance) return true;
						}
						if (ScriptEngine::WantsCallback(*instance, ScriptCallback_Update)) ScriptEngine::InvokeUpdate(handle);
						// `instance` not used after this point; no rebind needed.
					}
					else if (instance->GetType() == ScriptType::Native)
					{
						if (IsTaskRunning(m_NativeRebuildTask) || !instance->HasNativeInstance()) {
							return true;
						}

						NativeScript* nativeInstance = instance->GetNativePtr();
						if (!nativeInstance) {
							instance->Unbind();
							return true;
						}

						if (!instance->HasStarted())
						{
							if (!TryInvokeNativeScript(*instance, "Start", [nativeInstance]() {
								nativeInstance->Start();
							})) {
								m_NativeHost.DestroyInstance(nativeInstance);
								// Rebind before Unbind — user Start may have realloc'd Scripts.
								instance = FindNativeScriptInstance(scene, entity, nativeInstance);
								if (instance) instance->Unbind();
								return true;
							}
							instance = FindNativeScriptInstance(scene, entity, nativeInstance);
							if (!instance) return true;
							instance->MarkStarted();
						}

						if (!TryInvokeNativeScript(*instance, "Update", [nativeInstance]() {
							nativeInstance->Update();
						})) {
							m_NativeHost.DestroyInstance(nativeInstance);
							instance = FindNativeScriptInstance(scene, entity, nativeInstance);
							if (instance) instance->Unbind();
							return true;
						}
					}
					return true;
				});
		}
	}

	// Skips bind/start bookkeeping; instance must already be Bound + Started by Update.
	void ScriptSystem::FixedUpdate(Scene& scene)
	{
		FUNKE_PROFILE_SCOPE("Scripts.FixedUpdate");
		// ScriptSceneScope required: GetScene() is read by every NativeBindings call; wrong scene with additive scenes would silently no-op or null-deref.
		ScriptSceneScope sceneScope(scene);
		const bool managedRuntimeReady = ScriptEngine::IsInitialized();

		const auto entities = CollectScriptEntities(scene);
		for (EntityHandle entity : entities)
		{
			ForEachScriptOnEntitySafe(scene, entity,
				[&](ScriptComponent&, ScriptInstance& instance, size_t) -> bool {
					if (instance.GetClassName().empty() || !instance.IsBound() || !instance.HasStarted()) return true;

					if (instance.GetType() == ScriptType::Managed)
					{
						if (!managedRuntimeReady || !instance.HasManagedInstance()) return true;
						if (ScriptEngine::WantsCallback(instance, ScriptCallback_FixedUpdate)) ScriptEngine::InvokeFixedUpdate(instance.GetGCHandle());
					}
					else if (instance.GetType() == ScriptType::Native)
					{
						if (IsTaskRunning(m_NativeRebuildTask) || !instance.HasNativeInstance()) return true;
						NativeScript* nativeInstance = instance.GetNativePtr();
						if (!nativeInstance) return true;
						if (!TryInvokeNativeScript(instance, "FixedUpdate", [nativeInstance]() {
							nativeInstance->FixedUpdate();
						})) {
							m_NativeHost.DestroyInstance(nativeInstance);
							// Rebind before Unbind — user FixedUpdate may have realloc'd Scripts.
							ScriptInstance* rebound = FindNativeScriptInstance(scene, entity, nativeInstance);
							if (rebound) rebound->Unbind();
						}
					}
					return true;
				});
		}
	}

	void ScriptSystem::OnDestroy(Scene& scene)
	{
		if (m_PollingOwner == this) {
			m_PollingOwner = nullptr;
		}

		if (ScriptEngine::IsInitialized()) {
			TeardownManagedScripts(scene);
		}
		TeardownNativeScripts(scene);

		if (m_ActiveSystemCount > 0) {
			--m_ActiveSystemCount;
		}
		if (m_ActiveSystemCount > 0) {
			return;
		}

		m_ScriptWatcher.Stop();
		m_NativeWatcher.Stop();

		AbandonTask(m_RebuildTask, "Managed script rebuild");
		AbandonTask(m_NativeRebuildTask, "Native script rebuild");
		m_RebuildQueued = false;
		m_NativeRebuildQueued = false;

		if (Application::IsShuttingDown() && ScriptEngine::IsInitialized()) {
			ScriptEngine::Shutdown();
		}

		m_NativeHost.UnloadDLL();
		m_LastScene = nullptr;
		m_CoreAssemblyPath.clear();
		m_UserAssemblyPath.clear();
		m_SandboxProjectPath.clear();
		m_ManagedBuildOutputPath.clear();
		m_NativeProjectDirectory.clear();
		m_NativeSourceDirectory.clear();
		m_NativeDLLPath.clear();
		m_NativeTargetName.clear();
		m_ManagedRebuildError.clear();
		m_NativeRebuildError.clear();
		m_SuppressRecompile = false;
		m_PollingOwner = nullptr;
	}

} // namespace Funke
