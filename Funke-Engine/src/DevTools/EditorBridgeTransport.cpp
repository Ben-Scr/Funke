#include "pch.hpp"
#include "DevTools/EditorBridge.hpp"

#include "Core/Log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>

#if defined(FNK_PLATFORM_WINDOWS)
#include <sddl.h>
#pragma comment(lib, "Advapi32.lib")
#elif FNK_PLATFORM_DESKTOP
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace Funke::EditorBridge {

	namespace {
		using Clock = std::chrono::steady_clock;

		// Generous for an inline input script, small enough that a stray client can't exhaust memory.
		constexpr size_t k_MaxLineBytes = 4u * 1024u * 1024u;
		constexpr int k_ReadRequestTimeoutMs = 10000;
		constexpr int k_DefaultAnswerTimeoutMs = 60000;
		// The client closes its end once it has the answer; wait that long before dropping the connection.
		constexpr int k_DrainTimeoutMs = 2000;

		int RemainingMs(Clock::time_point deadline) {
			const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
			return left > 0 ? static_cast<int>(left) : 0;
		}
	}

	struct Server::State {
		std::mutex Mutex;
		std::condition_variable Answered;
		std::deque<Incoming> Queue;
		uint64_t NextTicket = 1;
		uint64_t WaitingTicket = 0;
		bool HasAnswer = false;
		std::string Answer;
		std::atomic<bool> StopRequested{ false };
		std::string Token;
#if defined(FNK_PLATFORM_WINDOWS)
		HANDLE Pipe = INVALID_HANDLE_VALUE;
		HANDLE StopEvent = nullptr;
#elif FNK_PLATFORM_DESKTOP
		int ListenFd = -1;
		int WakeRead = -1;
		int WakeWrite = -1;
		std::string SocketPath;
#endif

		std::vector<uint64_t> Abandoned;

		// Shared by both transports: validates, queues and waits for the main thread. Empty when the client
		// left before the answer, so there is nobody to write to.
		std::string Handle(const std::string& line, const std::function<bool()>& clientConnected) {
			Request request;
			std::string error;
			if (!ParseRequest(line, request, error)) return FormatError({}, error);
			if (!TokensMatch(Token, request.Token)) return FormatError(request.Id, "unauthorized: wrong or missing token");

			const int timeoutMs = request.TimeoutMs > 0 ? request.TimeoutMs : k_DefaultAnswerTimeoutMs;
			const std::string id = request.Id;
			std::unique_lock lock(Mutex);
			const uint64_t ticket = NextTicket++;
			WaitingTicket = ticket;
			HasAnswer = false;
			Answer.clear();
			Queue.push_back({ ticket, std::move(request) });
			const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
			bool clientLeft = false;
			// A client killed mid-wait (Ctrl+C, its own timeout) must not hold the only pipe instance until the deadline.
			while (!HasAnswer && !StopRequested.load() && Clock::now() < deadline) {
				Answered.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(250)),
					[this] { return HasAnswer || StopRequested.load(); });
				if (HasAnswer || StopRequested.load()) break;
				lock.unlock();
				const bool connected = clientConnected();
				lock.lock();
				if (!connected) {
					clientLeft = true;
					break;
				}
			}
			WaitingTicket = 0;
			if (HasAnswer) return std::move(Answer);

			const auto queued = std::find_if(Queue.begin(), Queue.end(), [ticket](const Incoming& incoming) { return incoming.Ticket == ticket; });
			const bool neverStarted = queued != Queue.end();
			if (neverStarted) Queue.erase(queued);
			// Already running on the main thread: tell it nobody waits for the result any more.
			else Abandoned.push_back(ticket);
			if (clientLeft) return {};
			if (StopRequested.load()) return FormatError(id, "the editor is shutting down", true);
			return FormatError(id, neverStarted
				? "timeout: the editor did not take the command (busy, blocked by a dialog, or paused)"
				: "timeout: the command did not finish in time", true);
		}
	};

#if defined(FNK_PLATFORM_WINDOWS)
	namespace {
		std::wstring Widen(const std::string& text) {
			if (text.empty()) return {};
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<size_t>(std::max(length, 0)), L'\0');
			if (length > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
			return wide;
		}

		std::string Win32Error(DWORD code) {
			return "Win32 error " + std::to_string(code);
		}

		// Waits for an overlapped operation; false on timeout or stop (the operation is cancelled then).
		bool FinishOverlapped(HANDLE handle, OVERLAPPED& overlapped, DWORD timeoutMs, HANDLE stopEvent, DWORD& outBytes) {
			HANDLE events[2] = { overlapped.hEvent, stopEvent };
			const DWORD count = stopEvent ? 2u : 1u;
			const DWORD wait = WaitForMultipleObjects(count, events, FALSE, timeoutMs);
			if (wait != WAIT_OBJECT_0) {
				CancelIoEx(handle, &overlapped);
				GetOverlappedResult(handle, &overlapped, &outBytes, TRUE);
				return false;
			}
			return GetOverlappedResult(handle, &overlapped, &outBytes, FALSE) != FALSE;
		}

		bool WriteAll(HANDLE handle, const std::string& data, int timeoutMs, HANDLE stopEvent) {
			const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
			size_t written = 0;
			while (written < data.size()) {
				OVERLAPPED overlapped{};
				overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				const DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - written, 64u * 1024u));
				DWORD bytes = 0;
				bool ok = WriteFile(handle, data.data() + written, chunk, &bytes, &overlapped) != FALSE;
				if (!ok && GetLastError() == ERROR_IO_PENDING) ok = FinishOverlapped(handle, overlapped, static_cast<DWORD>(RemainingMs(deadline)), stopEvent, bytes);
				else if (ok) GetOverlappedResult(handle, &overlapped, &bytes, FALSE);
				CloseHandle(overlapped.hEvent);
				if (!ok || bytes == 0) return false;
				written += bytes;
			}
			return true;
		}

		// Reads up to the first '\n'; false on timeout, a closed pipe or an oversized line.
		bool ReadLine(HANDLE handle, std::string& outLine, int timeoutMs, HANDLE stopEvent) {
			const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
			std::string buffer;
			char chunk[4096];
			for (;;) {
				OVERLAPPED overlapped{};
				overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				DWORD bytes = 0;
				bool ok = ReadFile(handle, chunk, sizeof(chunk), &bytes, &overlapped) != FALSE;
				const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
				if (!ok && error == ERROR_IO_PENDING) ok = FinishOverlapped(handle, overlapped, static_cast<DWORD>(RemainingMs(deadline)), stopEvent, bytes);
				else if (ok) GetOverlappedResult(handle, &overlapped, &bytes, FALSE);
				else if (error == ERROR_MORE_DATA) ok = true;
				CloseHandle(overlapped.hEvent);
				if (!ok || bytes == 0) return false;
				buffer.append(chunk, bytes);
				if (const size_t newline = buffer.find('\n'); newline != std::string::npos) {
					outLine = buffer.substr(0, newline);
					if (!outLine.empty() && outLine.back() == '\r') outLine.pop_back();
					return true;
				}
				if (buffer.size() > k_MaxLineBytes) return false;
			}
		}

		// Only the user running the editor may open the pipe; the default DACL also lets Everyone read.
		bool MakeUserOnlySecurity(PSECURITY_DESCRIPTOR& outDescriptor) {
			outDescriptor = nullptr;
			HANDLE token = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
			DWORD size = 0;
			GetTokenInformation(token, TokenUser, nullptr, 0, &size);
			std::vector<BYTE> buffer(size);
			const bool gotUser = size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
			CloseHandle(token);
			if (!gotUser) return false;
			LPWSTR sid = nullptr;
			if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) return false;
			const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
			LocalFree(sid);
			return ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &outDescriptor, nullptr) != FALSE;
		}

		void ServeWindows(Server::State& state) {
			HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			while (!state.StopRequested.load()) {
				OVERLAPPED overlapped{};
				overlapped.hEvent = connectEvent;
				ResetEvent(connectEvent);
				bool connected = ConnectNamedPipe(state.Pipe, &overlapped) != FALSE;
				const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
				if (!connected && error == ERROR_IO_PENDING) {
					DWORD unused = 0;
					connected = FinishOverlapped(state.Pipe, overlapped, INFINITE, state.StopEvent, unused);
				}
				else if (!connected && error == ERROR_PIPE_CONNECTED) {
					connected = true;
				}
				if (state.StopRequested.load()) break;
				if (!connected) {
					DisconnectNamedPipe(state.Pipe);
					Sleep(50);
					continue;
				}

				std::string line;
				if (ReadLine(state.Pipe, line, k_ReadRequestTimeoutMs, state.StopEvent)) {
					const std::string answer = state.Handle(line, [&state]() {
						DWORD available = 0;
						return PeekNamedPipe(state.Pipe, nullptr, 0, nullptr, &available, nullptr) != FALSE;
					});
					if (!answer.empty() && WriteAll(state.Pipe, answer + "\n", 5000, state.StopEvent)) {
						// Disconnecting discards unread bytes, so let the client read and close first.
						std::string ignored;
						ReadLine(state.Pipe, ignored, k_DrainTimeoutMs, state.StopEvent);
					}
				}
				DisconnectNamedPipe(state.Pipe);
			}
			CloseHandle(connectEvent);
		}
	}
#elif FNK_PLATFORM_DESKTOP
	namespace {
		bool WaitFd(int fd, short events, int timeoutMs, int wakeFd) {
			pollfd fds[2] = { { fd, events, 0 }, { wakeFd, POLLIN, 0 } };
			const nfds_t count = wakeFd >= 0 ? 2 : 1;
			for (;;) {
				const int ready = poll(fds, count, timeoutMs);
				if (ready < 0 && errno == EINTR) continue;
				if (ready <= 0) return false;
				if (count == 2 && (fds[1].revents & POLLIN)) return false;
				return (fds[0].revents & (events | POLLHUP | POLLERR)) != 0;
			}
		}

		void DisableSigpipe(int fd) {
#if defined(SO_NOSIGPIPE)
			int one = 1;
			setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
			(void)fd;
#endif
		}

		int SendFlags() {
#if defined(MSG_NOSIGNAL)
			return MSG_NOSIGNAL;
#else
			return 0;
#endif
		}

		bool WriteAll(int fd, const std::string& data, int timeoutMs, int wakeFd) {
			const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
			size_t written = 0;
			while (written < data.size()) {
				if (!WaitFd(fd, POLLOUT, RemainingMs(deadline), wakeFd)) return false;
				const ssize_t sent = send(fd, data.data() + written, data.size() - written, SendFlags());
				if (sent < 0 && (errno == EINTR || errno == EAGAIN)) continue;
				if (sent <= 0) return false;
				written += static_cast<size_t>(sent);
			}
			return true;
		}

		bool ReadLine(int fd, std::string& outLine, int timeoutMs, int wakeFd) {
			const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
			std::string buffer;
			char chunk[4096];
			for (;;) {
				if (!WaitFd(fd, POLLIN, RemainingMs(deadline), wakeFd)) return false;
				const ssize_t received = recv(fd, chunk, sizeof(chunk), 0);
				if (received < 0 && (errno == EINTR || errno == EAGAIN)) continue;
				if (received <= 0) return false;
				buffer.append(chunk, static_cast<size_t>(received));
				if (const size_t newline = buffer.find('\n'); newline != std::string::npos) {
					outLine = buffer.substr(0, newline);
					if (!outLine.empty() && outLine.back() == '\r') outLine.pop_back();
					return true;
				}
				if (buffer.size() > k_MaxLineBytes) return false;
			}
		}

		bool FillAddress(const std::string& path, sockaddr_un& out) {
			out = sockaddr_un{};
			out.sun_family = AF_UNIX;
			if (path.size() >= sizeof(out.sun_path)) return false;
			std::memcpy(out.sun_path, path.c_str(), path.size() + 1);
			return true;
		}

		void ServePosix(Server::State& state) {
			while (!state.StopRequested.load()) {
				if (!WaitFd(state.ListenFd, POLLIN, -1, state.WakeRead)) {
					if (state.StopRequested.load()) break;
					continue;
				}
				const int client = accept(state.ListenFd, nullptr, nullptr);
				if (client < 0) continue;
				DisableSigpipe(client);
				std::string line;
				if (ReadLine(client, line, k_ReadRequestTimeoutMs, state.WakeRead)) {
					const std::string answer = state.Handle(line, [client]() {
						pollfd fd{ client, POLLIN, 0 };
						if (poll(&fd, 1, 0) <= 0) return true;
						if (fd.revents & (POLLHUP | POLLERR)) return false;
						char byte = 0;
						return recv(client, &byte, 1, MSG_PEEK | MSG_DONTWAIT) != 0;
					});
					if (!answer.empty() && WriteAll(client, answer + "\n", 5000, state.WakeRead)) {
						std::string ignored;
						ReadLine(client, ignored, k_DrainTimeoutMs, state.WakeRead);
					}
				}
				close(client);
			}
		}
	}
#endif

	Server::Server() = default;

	Server::~Server() {
		Stop();
	}

	bool Server::Start(const std::string& endpoint, const std::string& token, std::string& outError) {
		Stop();
		auto state = std::make_shared<State>();
		state->Token = token;
#if defined(FNK_PLATFORM_WINDOWS)
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!MakeUserOnlySecurity(descriptor)) {
			outError = "could not build the pipe's access rules (" + Win32Error(GetLastError()) + ")";
			return false;
		}
		SECURITY_ATTRIBUTES attributes{ sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE };
		// FIRST_PIPE_INSTANCE: fails if anyone else already owns the name. One instance = one client at a time.
		state->Pipe = CreateNamedPipeW(Widen(endpoint).c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			1, 64 * 1024, 64 * 1024, 0, &attributes);
		const DWORD createError = GetLastError();
		LocalFree(descriptor);
		if (state->Pipe == INVALID_HANDLE_VALUE) {
			outError = "could not create the pipe " + endpoint + " (" + Win32Error(createError) + ")";
			return false;
		}
		state->StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		m_State = state;
		m_Endpoint = endpoint;
		m_Thread = std::thread([state]() { ServeWindows(*state); });
		return true;
#elif FNK_PLATFORM_DESKTOP
		sockaddr_un address{};
		if (!FillAddress(endpoint, address)) {
			outError = "socket path too long: " + endpoint;
			return false;
		}
		state->ListenFd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (state->ListenFd < 0) {
			outError = std::string("socket() failed: ") + std::strerror(errno);
			return false;
		}
		unlink(endpoint.c_str());
		// Created with no permissions for group/others; chmod afterwards closes the gap on systems that ignore umask for sockets.
		const mode_t previousMask = umask(0077);
		const bool bound = bind(state->ListenFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
		umask(previousMask);
		if (!bound || chmod(endpoint.c_str(), S_IRUSR | S_IWUSR) != 0 || listen(state->ListenFd, 4) != 0) {
			outError = "could not listen on " + endpoint + ": " + std::strerror(errno);
			close(state->ListenFd);
			unlink(endpoint.c_str());
			return false;
		}
		int wake[2] = { -1, -1 };
		if (pipe(wake) != 0) {
			outError = std::string("pipe() failed: ") + std::strerror(errno);
			close(state->ListenFd);
			unlink(endpoint.c_str());
			return false;
		}
		state->WakeRead = wake[0];
		state->WakeWrite = wake[1];
		state->SocketPath = endpoint;
		m_State = state;
		m_Endpoint = endpoint;
		m_Thread = std::thread([state]() { ServePosix(*state); });
		return true;
#else
		(void)endpoint;
		outError = "the Editor Bridge is only available on desktop platforms";
		return false;
#endif
	}

	void Server::Stop() {
		if (!m_State) return;
		m_State->StopRequested.store(true);
		{
			std::lock_guard lock(m_State->Mutex);
			m_State->Answered.notify_all();
		}
#if defined(FNK_PLATFORM_WINDOWS)
		if (m_State->StopEvent) SetEvent(m_State->StopEvent);
#elif FNK_PLATFORM_DESKTOP
		if (m_State->WakeWrite >= 0) {
			const char byte = 1;
			[[maybe_unused]] const ssize_t ignored = write(m_State->WakeWrite, &byte, 1);
		}
#endif
		if (m_Thread.joinable()) m_Thread.join();
#if defined(FNK_PLATFORM_WINDOWS)
		if (m_State->Pipe != INVALID_HANDLE_VALUE) CloseHandle(m_State->Pipe);
		if (m_State->StopEvent) CloseHandle(m_State->StopEvent);
#elif FNK_PLATFORM_DESKTOP
		if (m_State->ListenFd >= 0) close(m_State->ListenFd);
		if (m_State->WakeRead >= 0) close(m_State->WakeRead);
		if (m_State->WakeWrite >= 0) close(m_State->WakeWrite);
		if (!m_State->SocketPath.empty()) unlink(m_State->SocketPath.c_str());
#endif
		m_State.reset();
		m_Endpoint.clear();
	}

	std::vector<Server::Incoming> Server::TakeRequests() {
		std::vector<Incoming> taken;
		if (!m_State) return taken;
		std::lock_guard lock(m_State->Mutex);
		taken.assign(std::make_move_iterator(m_State->Queue.begin()), std::make_move_iterator(m_State->Queue.end()));
		m_State->Queue.clear();
		return taken;
	}

	std::vector<uint64_t> Server::TakeAbandoned() {
		std::vector<uint64_t> taken;
		if (!m_State) return taken;
		std::lock_guard lock(m_State->Mutex);
		taken.swap(m_State->Abandoned);
		return taken;
	}

	bool Server::HasRequests() const {
		if (!m_State) return false;
		std::lock_guard lock(m_State->Mutex);
		return !m_State->Queue.empty();
	}

	void Server::Respond(uint64_t ticket, std::string line) {
		if (!m_State) return;
		std::lock_guard lock(m_State->Mutex);
		if (ticket == 0 || m_State->WaitingTicket != ticket) return;
		m_State->Answer = std::move(line);
		m_State->HasAnswer = true;
		m_State->Answered.notify_all();
	}

	bool Exchange(const std::string& endpoint, const std::string& line, int timeoutMs,
		std::string& outReply, std::string& outError)
	{
		outReply.clear();
		const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(timeoutMs, 1));
		const std::string message = line + "\n";
#if defined(FNK_PLATFORM_WINDOWS)
		const std::wstring name = Widen(endpoint);
		HANDLE pipe = INVALID_HANDLE_VALUE;
		for (;;) {
			// SECURITY_IDENTIFICATION: a process squatting on the name can't impersonate this user.
			pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
			if (pipe != INVALID_HANDLE_VALUE) break;
			const DWORD error = GetLastError();
			if (error != ERROR_PIPE_BUSY) {
				outError = error == ERROR_FILE_NOT_FOUND
					? "the editor is not listening (it may have closed)"
					: "could not connect to the editor (" + Win32Error(error) + ")";
				return false;
			}
			const int remaining = RemainingMs(deadline);
			if (remaining <= 0) {
				outError = "the editor is busy with another client";
				return false;
			}
			WaitNamedPipeW(name.c_str(), static_cast<DWORD>(std::min(remaining, 500)));
		}
		bool ok = WriteAll(pipe, message, RemainingMs(deadline), nullptr);
		if (!ok) outError = "could not send the request";
		else if (!ReadLine(pipe, outReply, RemainingMs(deadline), nullptr)) {
			ok = false;
			outError = RemainingMs(deadline) <= 0 ? "timed out waiting for the editor" : "the editor closed the connection";
		}
		CloseHandle(pipe);
		return ok;
#elif FNK_PLATFORM_DESKTOP
		sockaddr_un address{};
		if (!FillAddress(endpoint, address)) {
			outError = "socket path too long: " + endpoint;
			return false;
		}
		const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0) {
			outError = std::string("socket() failed: ") + std::strerror(errno);
			return false;
		}
		DisableSigpipe(fd);
		// The editor serves one client at a time; a second one sits in the listen backlog until it's done.
		if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
			outError = errno == ENOENT || errno == ECONNREFUSED
				? "the editor is not listening (it may have closed)"
				: std::string("could not connect to the editor: ") + std::strerror(errno);
			close(fd);
			return false;
		}
		bool ok = WriteAll(fd, message, RemainingMs(deadline), -1);
		if (!ok) outError = "could not send the request";
		else if (!ReadLine(fd, outReply, RemainingMs(deadline), -1)) {
			ok = false;
			outError = RemainingMs(deadline) <= 0 ? "timed out waiting for the editor" : "the editor closed the connection";
		}
		close(fd);
		return ok;
#else
		(void)endpoint;
		(void)message;
		(void)deadline;
		outError = "the Editor Bridge is only available on desktop platforms";
		return false;
#endif
	}

} // namespace Funke::EditorBridge
