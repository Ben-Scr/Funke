#include "pch.hpp"
#include "Utils/Process.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string_view>

#ifdef IDX_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif IDX_PLATFORM_HAS_PROCESSES
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Index::Process {

	namespace {
		std::atomic<bool> g_CancellationActive{ false };
		std::atomic<std::uint64_t> g_CancelGeneration{ 0 };
		constexpr std::size_t kMaxCapturedOutput = 16 * 1024 * 1024;

		// Set by the streaming Run overload for the duration of the call; reads happen on the calling thread.
		thread_local const std::function<void(std::string_view)>* t_OutputSink = nullptr;

		void AppendCapturedOutput(std::string& output, const char* data, std::size_t size) {
			if (t_OutputSink && size > 0) (*t_OutputSink)(std::string_view(data, size));
			if (output.size() >= kMaxCapturedOutput) return;
			const std::size_t remaining = kMaxCapturedOutput - output.size();
			output.append(data, std::min(size, remaining));
		}

		std::string QuoteForShell(const std::string& arg) {
			if (arg.empty()) {
				return "\"\"";
			}

			bool needsQuotes = false;
			for (const char ch : arg) {
				if (std::isspace(static_cast<unsigned char>(ch)) || ch == '"' || ch == '\\') {
					needsQuotes = true;
					break;
				}
			}

			if (!needsQuotes) {
				return arg;
			}

			std::string escaped = "\"";
			size_t backslashes = 0;
			for (const char ch : arg) {
				if (ch == '\\') {
					backslashes++;
					continue;
				}

				if (ch == '"') {
					escaped.append(backslashes * 2 + 1, '\\');
					escaped.push_back('"');
					backslashes = 0;
					continue;
				}

				escaped.append(backslashes, '\\');
				backslashes = 0;
				escaped.push_back(ch);
			}

			escaped.append(backslashes * 2, '\\');
			escaped.push_back('"');
			return escaped;
		}

		std::string BuildCommandLine(const std::vector<std::string>& command) {
			std::ostringstream stream;
			for (size_t i = 0; i < command.size(); i++) {
				if (i > 0) {
					stream << ' ';
				}
				stream << QuoteForShell(command[i]);
			}
			return stream.str();
		}

#ifdef IDX_PLATFORM_WINDOWS
		std::wstring ToWide(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
			if (size <= 0) {
				return {};
			}

			std::wstring wide(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
			if (!wide.empty() && wide.back() == L'\0') {
				wide.pop_back();
			}
			return wide;
		}

		bool DrainAvailablePipe(HANDLE readPipe, std::string& output) {
			char buffer[4096];
			std::size_t drained = 0;
			constexpr std::size_t kDrainBudget = 256 * 1024;

			while (drained < kDrainBudget) {
				DWORD available = 0;
				if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
					return false;
				}
				if (available == 0) {
					return true;
				}

				const DWORD bytesToRead = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
				DWORD bytesRead = 0;
				if (!ReadFile(readPipe, buffer, bytesToRead, &bytesRead, nullptr) || bytesRead == 0) {
					return false;
				}
				AppendCapturedOutput(output, buffer, bytesRead);
				drained += bytesRead;
			}
			return true;
		}
#endif
	} // namespace

	Result Run(const std::vector<std::string>& command, const std::filesystem::path& workingDirectory, std::chrono::milliseconds timeout) {
		Result result;
		if (command.empty()) {
			result.Output = "No command specified";
			return result;
		}
		const std::uint64_t cancelGeneration = g_CancelGeneration.load(std::memory_order_acquire);
		const auto cancellationRequested = [&]() {
			return g_CancellationActive.load(std::memory_order_acquire)
				|| g_CancelGeneration.load(std::memory_order_acquire) != cancelGeneration;
		};
		if (cancellationRequested()) {
			result.Cancelled = true;
			result.Output = "Process launch cancelled";
			return result;
		}

		const bool hasTimeout = timeout.count() > 0;

#ifdef IDX_PLATFORM_WINDOWS
		SECURITY_ATTRIBUTES securityAttributes{};
		securityAttributes.nLength = sizeof(securityAttributes);
		securityAttributes.bInheritHandle = TRUE;

		HANDLE readPipe = nullptr;
		HANDLE writePipe = nullptr;
		if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
			result.Output = "Failed to create process output pipe";
			return result;
		}

		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		if (!job) {
			CloseHandle(readPipe);
			CloseHandle(writePipe);
			result.Output = "Failed to create process job object";
			return result;
		}
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
		jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(
			job, JobObjectExtendedLimitInformation, &jobLimits, sizeof(jobLimits))) {
			CloseHandle(job);
			CloseHandle(readPipe);
			CloseHandle(writePipe);
			result.Output = "Failed to configure process job object";
			return result;
		}

		const std::string commandLineUtf8 = BuildCommandLine(command);
		std::wstring commandLine = ToWide(commandLineUtf8);
		std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
		mutableCommandLine.push_back(L'\0');

		std::wstring workingDirectoryWide = workingDirectory.empty()
			? std::wstring()
			: workingDirectory.wstring();

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startupInfo.hStdOutput = writePipe;
		startupInfo.hStdError = writePipe;

		PROCESS_INFORMATION processInfo{};
		const BOOL created = CreateProcessW(
			nullptr,
			mutableCommandLine.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED,
			nullptr,
			workingDirectoryWide.empty() ? nullptr : workingDirectoryWide.c_str(),
			&startupInfo,
			&processInfo);

		CloseHandle(writePipe);

		if (!created) {
			const DWORD error = GetLastError();
			CloseHandle(job);
			CloseHandle(readPipe);
			std::ostringstream stream;
			stream << "CreateProcessW failed (" << error << ") for command: " << commandLineUtf8;
			result.Output = stream.str();
			return result;
		}
		if (!AssignProcessToJobObject(job, processInfo.hProcess)) {
			const DWORD error = GetLastError();
			TerminateProcess(processInfo.hProcess, static_cast<UINT>(-1));
			WaitForSingleObject(processInfo.hProcess, 5000);
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);
			CloseHandle(job);
			CloseHandle(readPipe);
			std::ostringstream stream;
			stream << "AssignProcessToJobObject failed (" << error << ") for command: " << commandLineUtf8;
			result.Output = stream.str();
			return result;
		}
		if (ResumeThread(processInfo.hThread) == static_cast<DWORD>(-1)) {
			const DWORD error = GetLastError();
			TerminateJobObject(job, static_cast<UINT>(-1));
			WaitForSingleObject(processInfo.hProcess, 5000);
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);
			CloseHandle(job);
			CloseHandle(readPipe);
			std::ostringstream stream;
			stream << "ResumeThread failed (" << error << ") for command: " << commandLineUtf8;
			result.Output = stream.str();
			return result;
		}

		// Drain the pipe with a deadline. PeekNamedPipe lets us check for
		// data without blocking; if no data and the deadline expires, we
		// terminate the child instead of blocking forever.
		const auto deadline = hasTimeout ? std::chrono::steady_clock::now() + timeout
		                                  : std::chrono::steady_clock::time_point::max();
		while (true) {
			if (cancellationRequested()) {
				result.Cancelled = true;
				TerminateJobObject(job, static_cast<UINT>(-1));
				break;
			}
			if (hasTimeout && std::chrono::steady_clock::now() >= deadline) {
				result.TimedOut = true;
				TerminateJobObject(job, static_cast<UINT>(-1));
				break;
			}

			DWORD available = 0;
			if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
				break; // pipe closed (child exited and write end gone)
			}
			if (available == 0) {
				// Either the child is still running with no output, or it has
				// exited. Wait briefly for either an output byte or process
				// exit before retrying.
				// Clamp to [1,50]: if the deadline elapsed between the check at the top
				// of the loop and now(), the remaining duration is negative; casting a
				// negative count to DWORD (unsigned) would wrap to ~49 days and hang
				// WaitForSingleObject far past the caller's timeout.
				const DWORD waitMs = hasTimeout
					? static_cast<DWORD>(std::clamp<long long>(
						std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count(),
						1, 50))
					: 50;
				const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, waitMs);
				if (waitResult == WAIT_OBJECT_0) {
					// Process exited; drain only bytes already available. Build
					// servers spawned by tools like dotnet/MSBuild may keep the
					// inherited write handle alive after the direct child exits.
					(void)DrainAvailablePipe(readPipe, result.Output);
					break;
				}
				continue;
			}

			if (!DrainAvailablePipe(readPipe, result.Output)) {
				break;
			}
		}
		CloseHandle(readPipe);

		if (result.TimedOut || result.Cancelled) {
			WaitForSingleObject(processInfo.hProcess, 5000);
		}
		else {
			while (true) {
				const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 50);
				if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_FAILED) break;
				if (cancellationRequested()) {
					result.Cancelled = true;
					TerminateJobObject(job, static_cast<UINT>(-1));
					WaitForSingleObject(processInfo.hProcess, 5000);
					break;
				}
				if (hasTimeout && std::chrono::steady_clock::now() >= deadline) {
					result.TimedOut = true;
					TerminateJobObject(job, static_cast<UINT>(-1));
					WaitForSingleObject(processInfo.hProcess, 5000);
					break;
				}
			}
		}
		DWORD exitCode = static_cast<DWORD>(-1);
		GetExitCodeProcess(processInfo.hProcess, &exitCode);
		result.ExitCode = static_cast<int>(exitCode);

		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
		CloseHandle(job);
		return result;
#elif !IDX_PLATFORM_HAS_PROCESSES
		(void)hasTimeout;
		(void)workingDirectory;
		result.Output = std::string("Child processes are not supported on ") + IDX_PLATFORM_NAME;
		return result;
#else
		// Build argv before fork(): post-fork code must be async-signal-safe; malloc is not safe in a multithreaded process.
		std::vector<char*> argv;
		argv.reserve(command.size() + 1);
		for (const std::string& arg : command) {
			argv.push_back(const_cast<char*>(arg.c_str()));
		}
		argv.push_back(nullptr);

		const std::string workingDirectoryStr = workingDirectory.empty() ? std::string() : workingDirectory.string();
		const char* workingDirectoryCStr = workingDirectoryStr.empty() ? nullptr : workingDirectoryStr.c_str();

		int pipeFd[2];
		if (pipe(pipeFd) != 0) {
			result.Output = "Failed to create process output pipe";
			return result;
		}

		pid_t pid = fork();
		if (pid < 0) {
			close(pipeFd[0]);
			close(pipeFd[1]);
			result.Output = "fork() failed while launching process";
			return result;
		}

		if (pid == 0) {
			if (setpgid(0, 0) != 0) {
				_exit(125);
			}
			if (workingDirectoryCStr) {
				if (chdir(workingDirectoryCStr) != 0) {
					_exit(126); // distinct from execvp's 127
				}
			}

			dup2(pipeFd[1], STDOUT_FILENO);
			dup2(pipeFd[1], STDERR_FILENO);
			close(pipeFd[0]);
			close(pipeFd[1]);

			execvp(argv[0], argv.data());
			_exit(127);
		}

		close(pipeFd[1]);
		(void)setpgid(pid, pid);
		const int pipeFlags = fcntl(pipeFd[0], F_GETFL, 0);
		if (pipeFlags >= 0) {
			(void)fcntl(pipeFd[0], F_SETFL, pipeFlags | O_NONBLOCK);
		}

		const auto deadline = hasTimeout ? std::chrono::steady_clock::now() + timeout
		                                  : std::chrono::steady_clock::time_point::max();
		char buffer[4096];
		int status = 0;
		bool childReaped = false;
		bool pipeOpen = true;
		auto terminateTree = [&]() {
			if (kill(-pid, SIGKILL) != 0 && errno == ESRCH && !childReaped) {
				(void)kill(pid, SIGKILL);
			}
		};
		auto drainPipe = [&]() {
			std::size_t drained = 0;
			constexpr std::size_t kDrainBudget = 256 * 1024;
			while (pipeOpen && drained < kDrainBudget) {
				const ssize_t bytesRead = read(pipeFd[0], buffer, sizeof(buffer));
				if (bytesRead > 0) {
					AppendCapturedOutput(result.Output, buffer, static_cast<std::size_t>(bytesRead));
					drained += static_cast<std::size_t>(bytesRead);
					continue;
				}
				if (bytesRead == 0) {
					pipeOpen = false;
					return;
				}
				if (errno == EINTR) continue;
				if (errno == EAGAIN || errno == EWOULDBLOCK) return;
				pipeOpen = false;
			}
		};
		for (;;) {
			const pid_t waitResult = waitpid(pid, &status, WNOHANG);
			if (waitResult == pid) {
				childReaped = true;
				drainPipe();
				break;
			}
			if (waitResult < 0 && errno != EINTR) {
				break;
			}
			if (cancellationRequested()) {
				result.Cancelled = true;
				terminateTree();
				break;
			}

			const auto now = std::chrono::steady_clock::now();
			if (hasTimeout && now >= deadline) {
				result.TimedOut = true;
				terminateTree();
				break;
			}

			const int waitMs = hasTimeout
				? static_cast<int>(std::clamp<long long>(
					std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count(), 1, 50))
				: 50;
			pollfd pfd{ pipeFd[0], static_cast<short>(POLLIN | POLLHUP | POLLERR), 0 };
			const int pollResult = poll(pipeOpen ? &pfd : nullptr, pipeOpen ? 1 : 0, waitMs);
			if (pollResult < 0) {
				if (errno == EINTR) continue;
				break;
			}
			if (pollResult > 0) drainPipe();
		}

		terminateTree();
		if (!childReaped) {
			for (int attempt = 0; attempt < 500; ++attempt) {
				const pid_t waitResult = waitpid(pid, &status, WNOHANG);
				if (waitResult == pid) {
					childReaped = true;
					break;
				}
				if (waitResult < 0 && errno == EINTR) continue;
				if (waitResult < 0) break;
				(void)poll(nullptr, 0, 10);
			}
		}
		drainPipe();
		close(pipeFd[0]);

		if (childReaped && WIFEXITED(status)) {
			result.ExitCode = WEXITSTATUS(status);
		} else if (childReaped && WIFSIGNALED(status)) {
			result.ExitCode = 128 + WTERMSIG(status); // shell convention
		} else {
			result.ExitCode = -1;
		}
		return result;
#endif
	}

	Result Run(const std::vector<std::string>& command, const std::filesystem::path& workingDirectory,
		std::chrono::milliseconds timeout, const std::function<void(std::string_view chunk)>& onOutput) {
		struct SinkScope {
			explicit SinkScope(const std::function<void(std::string_view)>* sink) : Previous(t_OutputSink) { t_OutputSink = sink; }
			~SinkScope() { t_OutputSink = Previous; }
			const std::function<void(std::string_view)>* Previous;
		} scope(onOutput ? &onOutput : nullptr);
		return Run(command, workingDirectory, timeout);
	}

	void CancelAll() {
		g_CancellationActive.store(true, std::memory_order_release);
		g_CancelGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	void ResetCancellation() {
		g_CancellationActive.store(false, std::memory_order_release);
		g_CancelGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	bool LaunchDetached(const std::vector<std::string>& command, const std::filesystem::path& workingDirectory) {
		unsigned long ignored = 0;
		return LaunchDetached(command, workingDirectory, ignored);
	}

	bool IsProcessRunning(unsigned long processId) {
		if (processId == 0) {
			return false;
		}
#ifdef IDX_PLATFORM_WINDOWS
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
		if (!process) {
			return false;
		}
		DWORD exitCode = 0;
		const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
		CloseHandle(process);
		return running;
#elif !IDX_PLATFORM_HAS_PROCESSES
		return false;
#else
		// EPERM: the process exists but belongs to someone else.
		return kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
#endif
	}

	bool LaunchDetached(const std::vector<std::string>& command, const std::filesystem::path& workingDirectory,
		unsigned long& outProcessId) {
		outProcessId = 0;
		if (command.empty()) {
			return false;
		}

#ifdef IDX_PLATFORM_WINDOWS
		const std::string commandLineUtf8 = BuildCommandLine(command);
		std::wstring commandLine = ToWide(commandLineUtf8);
		std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
		mutableCommandLine.push_back(L'\0');

		std::wstring workingDirectoryWide = workingDirectory.empty()
			? std::wstring()
			: workingDirectory.wstring();

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);

		PROCESS_INFORMATION processInfo{};
		const BOOL created = CreateProcessW(
			nullptr,
			mutableCommandLine.data(),
			nullptr,
			nullptr,
			FALSE,
			CREATE_NEW_PROCESS_GROUP,
			nullptr,
			workingDirectoryWide.empty() ? nullptr : workingDirectoryWide.c_str(),
			&startupInfo,
			&processInfo);

		if (!created) {
			return false;
		}

		outProcessId = processInfo.dwProcessId;
		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
		return true;
#elif !IDX_PLATFORM_HAS_PROCESSES
		(void)workingDirectory;
		return false;
#else
		std::vector<char*> argv;
		argv.reserve(command.size() + 1);
		for (const std::string& arg : command) {
			argv.push_back(const_cast<char*>(arg.c_str()));
		}
		argv.push_back(nullptr);
		char* const* argvData = argv.data();
		const std::string workingDirectoryStr = workingDirectory.empty() ? std::string() : workingDirectory.string();
		const char* workingDirectoryCStr = workingDirectoryStr.empty() ? nullptr : workingDirectoryStr.c_str();

		// The intermediate child reports the grandchild's pid; the grandchild is what keeps running.
		int pidPipe[2];
		if (pipe(pidPipe) != 0) {
			return false;
		}

		pid_t pid = fork();
		if (pid < 0) {
			close(pidPipe[0]);
			close(pidPipe[1]);
			return false;
		}

		if (pid == 0) {
			close(pidPipe[0]);
			if (setsid() < 0) _exit(126);
			const pid_t detachedPid = fork();
			if (detachedPid < 0) _exit(126);
			if (detachedPid > 0) {
				(void)!write(pidPipe[1], &detachedPid, sizeof(detachedPid));
				_exit(0);
			}
			close(pidPipe[1]);
			if (workingDirectoryCStr && chdir(workingDirectoryCStr) != 0) _exit(126);
			execvp(argvData[0], argvData);
			_exit(127);
		}

		close(pidPipe[1]);
		pid_t detachedPid = 0;
		ssize_t bytesRead = -1;
		do {
			bytesRead = read(pidPipe[0], &detachedPid, sizeof(detachedPid));
		} while (bytesRead < 0 && errno == EINTR);
		close(pidPipe[0]);

		int status = 0;
		pid_t waitResult = -1;
		do {
			waitResult = waitpid(pid, &status, 0);
		} while (waitResult < 0 && errno == EINTR);
		const bool launched = waitResult == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (launched && bytesRead == static_cast<ssize_t>(sizeof(detachedPid))) {
			outProcessId = static_cast<unsigned long>(detachedPid);
		}
		return launched;
#endif
	}

} // namespace Index::Process
