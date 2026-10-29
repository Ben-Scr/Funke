#include "pch.hpp"
#include "Core/Platform.hpp"

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#if defined(FNK_PLATFORM_ANDROID)
	#include <SDL3/SDL_system.h>
	#include <jni.h>
#endif

#if defined(FNK_PLATFORM_WINDOWS)
	#include <shellapi.h>
	#include <shlobj.h>
#else
	#include <cstdlib>
	#include <pthread.h>
#endif

#if defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_IOS)
	#include <mach-o/dyld.h>
	#include <mach/mach.h>
#endif
#if defined(FNK_PLATFORM_WINDOWS)
	#include <psapi.h>
#endif
#if defined(FNK_PLATFORM_LINUX) || defined(FNK_PLATFORM_ANDROID)
	#include <unistd.h>
	#include <climits>
#endif

#if defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_LINUX)
	#include <spawn.h>
	#include <sys/wait.h>
	extern char** environ;
#endif

namespace Funke::Platform {

	namespace {
		std::filesystem::path FromUtf8(const char* utf8) {
			if (!utf8 || !*utf8) return {};
			const std::string_view view(utf8);
			return std::filesystem::path(std::u8string(view.begin(), view.end()));
		}

		std::string ToUtf8(const std::filesystem::path& path) {
			const std::u8string u8 = path.u8string();
			return std::string(u8.begin(), u8.end());
		}

#if FNK_PLATFORM_DESKTOP
		std::optional<std::filesystem::path> GetUserDataRootOverride() {
			std::filesystem::path root;
#if defined(FNK_PLATFORM_WINDOWS)
			// The OS environment is shared with the executable even when its CRT differs from the engine's.
			SetLastError(ERROR_SUCCESS);
			const DWORD required = GetEnvironmentVariableW(L"FUNKE_USER_DATA_ROOT", nullptr, 0);
			if (required == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
			if (required == 0) throw std::invalid_argument("FUNKE_USER_DATA_ROOT must be a non-empty absolute path");
			std::wstring value(required, L'\0');
			const DWORD copied = GetEnvironmentVariableW(L"FUNKE_USER_DATA_ROOT", value.data(), required);
			if (copied == 0 || copied >= required)
				throw std::invalid_argument("FUNKE_USER_DATA_ROOT could not be read consistently");
			value.resize(copied);
			root = std::filesystem::path(value);
#else
			const char* value = std::getenv("FUNKE_USER_DATA_ROOT");
			if (!value) return std::nullopt;
			root = FromUtf8(value);
#endif
			if (root.empty() || !root.is_absolute())
				throw std::invalid_argument("FUNKE_USER_DATA_ROOT must be a non-empty absolute path");
			return root.lexically_normal();
		}
#endif

		// RFC 8089 file URL with percent-encoding, so paths with spaces or non-ASCII survive SDL_OpenURL.
		std::string ToFileUrl(const std::filesystem::path& path) {
			const std::u8string genericU8 = path.generic_u8string();
			const std::string generic(genericU8.begin(), genericU8.end());
			std::string url = "file://";
			if (!generic.empty() && generic.front() != '/') url += '/';
			constexpr char k_Hex[] = "0123456789ABCDEF";
			for (unsigned char c : generic) {
				const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
					|| c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':';
				if (unreserved) {
					url += static_cast<char>(c);
				}
				else {
					url += '%';
					url += k_Hex[c >> 4];
					url += k_Hex[c & 0x0F];
				}
			}
			return url;
		}

		SDL_MessageBoxFlags ToSdlFlags(MessageBoxKind kind) {
			switch (kind) {
				case MessageBoxKind::Warning: return SDL_MESSAGEBOX_WARNING;
				case MessageBoxKind::Error:   return SDL_MESSAGEBOX_ERROR;
				case MessageBoxKind::Info:    break;
			}
			return SDL_MESSAGEBOX_INFORMATION;
		}

		SDL_Window* DialogParentWindow() {
			// Parenting keeps dialogs above the editor; any focused SDL window works.
			return SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardFocus() : nullptr;
		}

#if defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_LINUX)
		bool SpawnAndForget(const char* program, const std::vector<std::string>& args) {
			std::vector<char*> argv;
			argv.push_back(const_cast<char*>(program));
			for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
			argv.push_back(nullptr);
			pid_t pid = 0;
			if (posix_spawnp(&pid, program, nullptr, nullptr, argv.data(), environ) != 0) return false;
			// `open` / `xdg-open` hand off to the desktop and exit immediately; reap them so no zombie is left.
			int status = 0;
			waitpid(pid, &status, 0);
			return WIFEXITED(status) && WEXITSTATUS(status) == 0;
		}
#endif
	}

	std::filesystem::path GetExecutableDirectory() {
		return FromUtf8(SDL_GetBasePath());
	}

	std::filesystem::path GetExecutablePath() {
#if defined(FNK_PLATFORM_WINDOWS)
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;) {
			const DWORD copied = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (copied == 0) return {};
			if (copied < buffer.size()) return std::filesystem::path(buffer.data());
			if (buffer.size() >= 65536) return {};
			buffer.resize(buffer.size() * 2);
		}
#elif defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_IOS)
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::string buffer(size, '\0');
		if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
		buffer.resize(std::strlen(buffer.c_str()));
		std::error_code ec;
		const std::filesystem::path resolved = std::filesystem::weakly_canonical(FromUtf8(buffer.c_str()), ec);
		return ec ? FromUtf8(buffer.c_str()) : resolved;
#elif defined(FNK_PLATFORM_LINUX) || defined(FNK_PLATFORM_ANDROID)
		char buffer[PATH_MAX]{};
		const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if (length <= 0) return {};
		buffer[length] = '\0';
		return FromUtf8(buffer);
#else
		return {};
#endif
	}

	std::filesystem::path GetUserDataRoot() {
#if FNK_PLATFORM_DESKTOP
		if (auto root = GetUserDataRootOverride()) return *root;
#endif
#if defined(FNK_PLATFORM_WINDOWS)
		PWSTR raw = nullptr;
		std::filesystem::path result;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw) {
			result = std::filesystem::path(raw);
		}
		CoTaskMemFree(raw);
		return result;
#elif defined(FNK_PLATFORM_MACOS)
		const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
		return home ? FromUtf8(home) / "Library" / "Application Support" : std::filesystem::path{};
#elif defined(FNK_PLATFORM_LINUX)
		if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return FromUtf8(xdg);
		const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
		return home ? FromUtf8(home) / ".local" / "share" : std::filesystem::path{};
#else
		// Sandboxed platforms: the app's own writable storage; callers still append Funke/<subdir>.
		char* pref = SDL_GetPrefPath("Funke", "Funke");
		std::filesystem::path result = FromUtf8(pref);
		SDL_free(pref);
		return result;
#endif
	}

	std::filesystem::path GetUserFolder(UserFolder folder) {
		SDL_Folder sdlFolder = SDL_FOLDER_HOME;
		switch (folder) {
			case UserFolder::Home:      sdlFolder = SDL_FOLDER_HOME; break;
			case UserFolder::Desktop:   sdlFolder = SDL_FOLDER_DESKTOP; break;
			case UserFolder::Documents: sdlFolder = SDL_FOLDER_DOCUMENTS; break;
			case UserFolder::Downloads: sdlFolder = SDL_FOLDER_DOWNLOADS; break;
			case UserFolder::Music:     sdlFolder = SDL_FOLDER_MUSIC; break;
			case UserFolder::Pictures:  sdlFolder = SDL_FOLDER_PICTURES; break;
			case UserFolder::Videos:    sdlFolder = SDL_FOLDER_VIDEOS; break;
		}
		return FromUtf8(SDL_GetUserFolder(sdlFolder));
	}

	bool OpenUrl(const std::string& url) {
		if (url.empty()) return false;
		if (!SDL_OpenURL(url.c_str())) {
			FNK_CORE_WARN_TAG("Platform", "Failed to open '{}': {}", url, SDL_GetError());
			return false;
		}
		return true;
	}

	bool OpenPath(const std::filesystem::path& path) {
		if (path.empty()) return false;
#if defined(FNK_PLATFORM_WINDOWS)
		// ShellExecute takes the path directly; no URL round-trip for UNC or long paths.
		const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return reinterpret_cast<INT_PTR>(result) > 32;
#else
		return OpenUrl(ToFileUrl(path));
#endif
	}

	bool RevealInFileManager(const std::filesystem::path& path) {
		if (path.empty()) return false;
#if defined(FNK_PLATFORM_WINDOWS)
		std::error_code ec;
		if (!std::filesystem::is_directory(path, ec)) {
			const std::wstring args = L"/select,\"" + path.wstring() + L"\"";
			const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
			return reinterpret_cast<INT_PTR>(result) > 32;
		}
		return OpenPath(path);
#elif defined(FNK_PLATFORM_MACOS)
		return SpawnAndForget("open", { "-R", ToUtf8(path) });
#elif defined(FNK_PLATFORM_LINUX)
		// FileManager1 (Nautilus, Dolphin, Nemo, Thunar, ...) opens the folder with the item selected.
		if (SpawnAndForget("dbus-send", { "--session", "--print-reply", "--dest=org.freedesktop.FileManager1",
				"--type=method_call", "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1.ShowItems",
				"array:string:" + ToFileUrl(path), "string:" })) {
			return true;
		}
		std::error_code ec;
		return OpenPath(std::filesystem::is_directory(path, ec) ? path : path.parent_path());
#else
		std::error_code ec;
		return OpenPath(std::filesystem::is_directory(path, ec) ? path : path.parent_path());
#endif
	}

	const char* RevealInFileManagerLabel() {
#if defined(FNK_PLATFORM_WINDOWS)
		return "Show in Explorer";
#elif defined(FNK_PLATFORM_MACOS)
		return "Reveal in Finder";
#else
		return "Show in File Manager";
#endif
	}

	void ShowMessageBox(MessageBoxKind kind, const std::string& title, const std::string& message) {
		if (!SDL_ShowSimpleMessageBox(ToSdlFlags(kind), title.c_str(), message.c_str(), DialogParentWindow())) {
			FNK_CORE_WARN_TAG("Platform", "Message box failed ({}): {}: {}", SDL_GetError(), title, message);
		}
	}

	bool ShowConfirmDialog(MessageBoxKind kind, const std::string& title, const std::string& message,
		const std::string& yesLabel, const std::string& noLabel)
	{
		const SDL_MessageBoxButtonData buttons[] = {
			{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, noLabel.c_str() },
			{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yesLabel.c_str() },
		};
		SDL_MessageBoxData data{};
		data.flags = ToSdlFlags(kind);
		data.window = DialogParentWindow();
		data.title = title.c_str();
		data.message = message.c_str();
		data.numbuttons = static_cast<int>(std::size(buttons));
		data.buttons = buttons;
		int pressed = 0;
		if (!SDL_ShowMessageBox(&data, &pressed)) {
			FNK_CORE_WARN_TAG("Platform", "Confirm dialog failed: {}", SDL_GetError());
			return false;
		}
		return pressed == 1;
	}

	void PickFolderAsync(PathPickedFn onPicked, const std::filesystem::path& defaultLocation, const std::string& title) {
		auto* callback = new PathPickedFn(std::move(onPicked));
		const std::string location = ToUtf8(defaultLocation);
		// SDL copies the property strings before returning.
		const SDL_PropertiesID props = SDL_CreateProperties();
		if (!location.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
		if (!title.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
		if (SDL_Window* parent = DialogParentWindow()) SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, parent);
		SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER,
			[](void* userData, const char* const* fileList, int) {
				std::unique_ptr<PathPickedFn> fn(static_cast<PathPickedFn*>(userData));
				if (!fileList) {
					FNK_CORE_WARN_TAG("Platform", "Folder dialog failed: {}", SDL_GetError());
					(*fn)(std::nullopt);
					return;
				}
				(*fn)(fileList[0] ? std::optional<std::filesystem::path>(FromUtf8(fileList[0])) : std::nullopt);
			},
			callback, props);
		SDL_DestroyProperties(props);
	}

	std::optional<std::filesystem::path> PickFolder(const std::filesystem::path& defaultLocation, const std::string& title) {
		struct Shared {
			std::mutex Mutex;
			bool Done = false;
			std::optional<std::filesystem::path> Result;
		};
		auto shared = std::make_shared<Shared>();
		PickFolderAsync([shared](std::optional<std::filesystem::path> picked) {
			std::scoped_lock lock(shared->Mutex);
			shared->Result = std::move(picked);
			shared->Done = true;
		}, defaultLocation, title);
		for (;;) {
			{
				std::scoped_lock lock(shared->Mutex);
				if (shared->Done) return std::move(shared->Result);
			}
			// Portal (Linux) and Cocoa dialogs deliver their result through the event loop.
			SDL_PumpEvents();
			SDL_Delay(10);
		}
	}

	void SaveFileAsync(PathPickedFn onPicked, const std::filesystem::path& defaultPath,
		const std::vector<FileFilter>& filters, const std::string& title)
	{
		// SDL reads the filter strings until the callback runs, so they travel with it.
		struct Request {
			PathPickedFn OnPicked;
			std::vector<FileFilter> Filters;
			std::vector<SDL_DialogFileFilter> SdlFilters;
		};
		auto* request = new Request{ std::move(onPicked), filters, {} };
		for (const FileFilter& filter : request->Filters) request->SdlFilters.push_back({ filter.Name.c_str(), filter.Pattern.c_str() });

		// The Windows dialog splits folder and file name at the last separator and needs native ones.
		const std::string location = ToUtf8(std::filesystem::path(defaultPath).make_preferred());
		const SDL_PropertiesID props = SDL_CreateProperties();
		if (!location.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
		if (!title.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
		if (!request->SdlFilters.empty()) {
			SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, request->SdlFilters.data());
			SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, static_cast<Sint64>(request->SdlFilters.size()));
		}
		if (SDL_Window* parent = DialogParentWindow()) SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, parent);
		SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_SAVEFILE,
			[](void* userData, const char* const* fileList, int) {
				std::unique_ptr<Request> request(static_cast<Request*>(userData));
				if (!fileList) {
					FNK_CORE_WARN_TAG("Platform", "Save dialog failed: {}", SDL_GetError());
					request->OnPicked(std::nullopt);
					return;
				}
				request->OnPicked(fileList[0] ? std::optional<std::filesystem::path>(FromUtf8(fileList[0])) : std::nullopt);
			},
			request, props);
		SDL_DestroyProperties(props);
	}

	std::string GetPreferredLocale() {
		int count = 0;
		SDL_Locale** locales = SDL_GetPreferredLocales(&count);
		std::string result;
		if (locales && count > 0 && locales[0] && locales[0]->language) {
			result = locales[0]->language;
			if (locales[0]->country) {
				result += '-';
				result += locales[0]->country;
			}
		}
		SDL_free(locales);
		return result;
	}

	std::optional<bool> IsSystemDarkTheme() {
		if (SDL_WasInit(SDL_INIT_VIDEO)) {
			switch (SDL_GetSystemTheme()) {
				case SDL_SYSTEM_THEME_DARK:  return true;
				case SDL_SYSTEM_THEME_LIGHT: return false;
				default: break;
			}
		}
#if defined(FNK_PLATFORM_WINDOWS)
		// Before SDL video is up (editor preferences load early) read the same setting SDL would.
		DWORD appsUseLightTheme = 1;
		DWORD size = sizeof(appsUseLightTheme);
		if (RegGetValueW(HKEY_CURRENT_USER,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
				L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &appsUseLightTheme, &size) == ERROR_SUCCESS) {
			return appsUseLightTheme == 0;
		}
#endif
		return std::nullopt;
	}

	int GetLogicalCpuCount() {
		return std::max(1, SDL_GetNumLogicalCPUCores());
	}

	int GetSystemRamMiB() {
		return SDL_GetSystemRAM();
	}

	float GetProcessMemoryMiB() {
		constexpr float k_MiB = 1024.0f * 1024.0f;
#if defined(FNK_PLATFORM_WINDOWS)
		PROCESS_MEMORY_COUNTERS counters{};
		if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
			return static_cast<float>(counters.WorkingSetSize) / k_MiB;
		}
#elif defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_IOS)
		mach_task_basic_info_data_t info{};
		mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
		if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
			return static_cast<float>(info.resident_size) / k_MiB;
		}
#elif defined(FNK_PLATFORM_LINUX) || defined(FNK_PLATFORM_ANDROID)
		// statm: total and resident size in pages.
		if (std::FILE* statm = std::fopen("/proc/self/statm", "r")) {
			unsigned long totalPages = 0;
			unsigned long residentPages = 0;
			const int read = std::fscanf(statm, "%lu %lu", &totalPages, &residentPages);
			std::fclose(statm);
			if (read == 2) return static_cast<float>(residentPages) * static_cast<float>(sysconf(_SC_PAGESIZE)) / k_MiB;
		}
#endif
		return 0.0f;
	}

	void ExtendToolSearchPath() {
#if defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_LINUX)
		const char* current = std::getenv("PATH");
		std::string searchPath = (current && *current) ? current : "/usr/bin:/bin";
		const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";

		std::vector<std::string> extra;
		if (const char* dotnetRoot = std::getenv("DOTNET_ROOT"); dotnetRoot && *dotnetRoot) extra.emplace_back(dotnetRoot);
		extra.insert(extra.end(), { "/usr/local/bin", "/opt/homebrew/bin", "/usr/local/share/dotnet", "/snap/bin" });
		if (!home.empty()) {
			extra.push_back(home + "/.dotnet");
			extra.push_back(home + "/.dotnet/tools");
			extra.push_back(home + "/.local/bin");
		}

		bool changed = false;
		for (const std::string& directory : extra) {
			if ((":" + searchPath + ":").find(":" + directory + ":") != std::string::npos) continue;
			std::error_code ec;
			if (!std::filesystem::is_directory(directory, ec)) continue;
			searchPath += ":" + directory;
			changed = true;
		}
		if (changed) setenv("PATH", searchPath.c_str(), 1);

		// nethost only probes the global install locations; a dotnet-install.sh SDK lives in ~/.dotnet.
		if (!std::getenv("DOTNET_ROOT") && !home.empty()) {
			std::error_code ec;
			const bool hasGlobal = std::filesystem::exists("/usr/local/share/dotnet/dotnet", ec)
				|| std::filesystem::exists("/usr/share/dotnet/dotnet", ec)
				|| std::filesystem::exists("/usr/lib/dotnet/dotnet", ec)
				|| std::filesystem::exists("/usr/lib64/dotnet/dotnet", ec)
				|| std::filesystem::exists("/etc/dotnet/install_location", ec);
			if (!hasGlobal && std::filesystem::exists(home + "/.dotnet/dotnet", ec)) {
				setenv("DOTNET_ROOT", (home + "/.dotnet").c_str(), 1);
			}
		}
#endif
	}

	namespace {
		std::mutex s_PermissionMutex;
		std::unordered_map<std::string, PermissionState> s_PermissionStates;

		void SetPermissionState(const std::string& permission, PermissionState state) {
			std::lock_guard lock(s_PermissionMutex);
			s_PermissionStates[permission] = state;
		}

#if defined(FNK_PLATFORM_ANDROID)
		// SDL may answer from the Java UI thread.
		void SDLCALL OnPermissionResult(void*, const char* permission, bool granted) {
			if (permission) {
				SetPermissionState(permission, granted ? PermissionState::Granted : PermissionState::Denied);
			}
		}
#endif
	}

	void RequestPermission(const std::string& permission) {
		if (permission.empty()) return;
#if defined(FNK_PLATFORM_ANDROID)
		{
			std::lock_guard lock(s_PermissionMutex);
			PermissionState& state = s_PermissionStates[permission];
			if (state == PermissionState::Pending || state == PermissionState::Granted) return;
			state = PermissionState::Pending;
		}
		if (!SDL_RequestAndroidPermission(permission.c_str(), &OnPermissionResult, nullptr)) {
			FNK_CORE_WARN_TAG("Platform", "Requesting permission '{}' failed: {}", permission, SDL_GetError());
			SetPermissionState(permission, PermissionState::Denied);
		}
#else
		SetPermissionState(permission, PermissionState::Granted);
#endif
	}

	PermissionState GetPermissionState(const std::string& permission) {
#if !defined(FNK_PLATFORM_ANDROID)
		(void)permission;
		return PermissionState::Granted;
#else
		std::lock_guard lock(s_PermissionMutex);
		const auto it = s_PermissionStates.find(permission);
		return it != s_PermissionStates.end() ? it->second : PermissionState::Unknown;
#endif
	}

	void SetBackgroundAudioService(bool enabled) {
#if defined(FNK_PLATFORM_ANDROID)
		auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
		auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
		if (!env || !activity) return;
		// Looked up by name so activities without the method (other hosts) only log instead of failing to link.
		jclass activityClass = env->GetObjectClass(activity);
		jmethodID method = env->GetMethodID(activityClass, "setBackgroundAudio", "(Z)V");
		if (method) {
			env->CallVoidMethod(activity, method, enabled ? JNI_TRUE : JNI_FALSE);
		}
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			FNK_CORE_WARN_TAG("Platform", "Background audio service unavailable; is it declared in the APK (Android build option backgroundAudio)?");
		}
		env->DeleteLocalRef(activityClass);
		env->DeleteLocalRef(activity);
#else
		(void)enabled;
#endif
	}

	namespace {
		std::mutex s_MediaMutex;
		std::deque<std::pair<MediaCommand, float>> s_MediaCommands;

#if defined(FNK_PLATFORM_ANDROID)
		// Strings travel as UTF-8 byte arrays: NewStringUTF expects modified UTF-8 and rejects emoji in song titles.
		jbyteArray ToJavaBytes(JNIEnv* env, const std::string& text) {
			jbyteArray bytes = env->NewByteArray(static_cast<jsize>(text.size()));
			env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte*>(text.data()));
			return bytes;
		}

		template <typename Fn>
		void WithActivityMethod(const char* name, const char* signature, Fn&& call) {
			auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
			auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
			if (!env || !activity) return;
			jclass activityClass = env->GetObjectClass(activity);
			if (jmethodID method = env->GetMethodID(activityClass, name, signature)) {
				call(env, activity, method);
			}
			if (env->ExceptionCheck()) {
				env->ExceptionClear();
				FNK_CORE_WARN_TAG("Platform", "Media session call {} failed", name);
			}
			env->DeleteLocalRef(activityClass);
			env->DeleteLocalRef(activity);
		}
#endif
	}

	void SetMediaNowPlaying(const std::string& title, const std::string& artist, const std::string& album,
		float durationSeconds, const uint8_t* rgba, int width, int height)
	{
#if defined(FNK_PLATFORM_ANDROID)
		WithActivityMethod("setMediaNowPlaying", "([B[B[BJ[III)V", [&](JNIEnv* env, jobject activity, jmethodID method) {
			jintArray argb = nullptr;
			if (rgba && width > 0 && height > 0) {
				// Android bitmaps take packed ARGB ints.
				std::vector<jint> pixels(static_cast<size_t>(width) * height);
				for (size_t i = 0; i < pixels.size(); ++i) {
					const uint8_t* p = rgba + i * 4;
					pixels[i] = static_cast<jint>((uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]));
				}
				argb = env->NewIntArray(static_cast<jsize>(pixels.size()));
				env->SetIntArrayRegion(argb, 0, static_cast<jsize>(pixels.size()), pixels.data());
			}
			jbyteArray jTitle = ToJavaBytes(env, title);
			jbyteArray jArtist = ToJavaBytes(env, artist);
			jbyteArray jAlbum = ToJavaBytes(env, album);
			env->CallVoidMethod(activity, method, jTitle, jArtist, jAlbum, static_cast<jlong>(durationSeconds * 1000.0f),
				argb, static_cast<jint>(argb ? width : 0), static_cast<jint>(argb ? height : 0));
			env->DeleteLocalRef(jTitle);
			env->DeleteLocalRef(jArtist);
			env->DeleteLocalRef(jAlbum);
			if (argb) env->DeleteLocalRef(argb);
		});
#else
		(void)title; (void)artist; (void)album; (void)durationSeconds; (void)rgba; (void)width; (void)height;
#endif
	}

	void SetMediaPlaybackState(bool playing, float positionSeconds) {
#if defined(FNK_PLATFORM_ANDROID)
		WithActivityMethod("setMediaPlaybackState", "(ZJ)V", [&](JNIEnv* env, jobject activity, jmethodID method) {
			env->CallVoidMethod(activity, method, playing ? JNI_TRUE : JNI_FALSE, static_cast<jlong>(positionSeconds * 1000.0f));
		});
#else
		(void)playing; (void)positionSeconds;
#endif
	}

	MediaCommand PollMediaCommand(float& seekSeconds) {
		std::lock_guard lock(s_MediaMutex);
		if (s_MediaCommands.empty()) return MediaCommand::None;
		auto [command, seek] = s_MediaCommands.front();
		s_MediaCommands.pop_front();
		seekSeconds = seek;
		return command;
	}

	void PushMediaCommand(MediaCommand command, float seekSeconds) {
		std::lock_guard lock(s_MediaMutex);
		// A stuck main thread must not let a stream of headset presses grow without bound.
		if (s_MediaCommands.size() < 32) s_MediaCommands.emplace_back(command, seekSeconds);
	}

	namespace {
		std::atomic<int> s_PendingTextAction{ 0 };
	}

	bool ShowTextActions(int x, int y, int width, int height, int flags) {
#if defined(FNK_PLATFORM_ANDROID)
		if (flags == 0) return false;
		WithActivityMethod("showTextActions", "(IIIII)V", [&](JNIEnv* env, jobject activity, jmethodID method) {
			env->CallVoidMethod(activity, method, x, y, width, height, flags);
		});
		return true;
#else
		(void)x; (void)y; (void)width; (void)height; (void)flags;
		return false;
#endif
	}

	void HideTextActions() {
#if defined(FNK_PLATFORM_ANDROID)
		WithActivityMethod("hideTextActions", "()V", [](JNIEnv* env, jobject activity, jmethodID method) {
			env->CallVoidMethod(activity, method);
		});
#endif
	}

	TextAction PollTextAction() {
		return static_cast<TextAction>(s_PendingTextAction.exchange(0));
	}

	void PushTextAction(TextAction action) {
		s_PendingTextAction.store(static_cast<int>(action));
	}

	namespace {
		std::mutex s_JavaMessageMutex;
		std::deque<std::pair<std::string, std::string>> s_JavaMessages;
		// A stalled main thread must not let a chatty Java plugin grow the queue without bound.
		constexpr size_t k_MaxJavaMessages = 1024;

#if defined(FNK_PLATFORM_ANDROID)
		std::string FromJavaBytes(JNIEnv* env, jbyteArray bytes) {
			if (!bytes) return {};
			std::string text(static_cast<size_t>(env->GetArrayLength(bytes)), '\0');
			if (!text.empty()) env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(text.size()), reinterpret_cast<jbyte*>(text.data()));
			return text;
		}

		struct JavaBridge {
			jclass Class = nullptr;
			jmethodID Call = nullptr;
		};

		// Loaded through the activity's class loader: FindClass on threads Java did not start only sees system classes.
		const JavaBridge* GetJavaBridge(JNIEnv* env) {
			static std::mutex s_Mutex;
			static JavaBridge s_Bridge;
			static bool s_Resolved = false;
			std::lock_guard lock(s_Mutex);
			if (s_Resolved) return s_Bridge.Class ? &s_Bridge : nullptr;
			auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
			if (!activity) return nullptr;
			s_Resolved = true;
			jclass activityClass = env->GetObjectClass(activity);
			jmethodID getClassLoader = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
			jobject loader = getClassLoader ? env->CallObjectMethod(activity, getClassLoader) : nullptr;
			jclass loaderClass = env->FindClass("java/lang/ClassLoader");
			jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
			jstring name = env->NewStringUTF("dev.funke.player.FunkeBridge");
			auto bridge = loader && loadClass ? static_cast<jclass>(env->CallObjectMethod(loader, loadClass, name)) : nullptr;
			if (env->ExceptionCheck()) {
				env->ExceptionClear();
				bridge = nullptr;
			}
			if (bridge) {
				s_Bridge.Call = env->GetStaticMethodID(bridge, "call", "([B[B[B)[B");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (s_Bridge.Call) s_Bridge.Class = static_cast<jclass>(env->NewGlobalRef(bridge));
				env->DeleteLocalRef(bridge);
			}
			if (!s_Bridge.Class) FNK_CORE_WARN_TAG("Platform", "dev.funke.player.FunkeBridge is missing from the APK; Java calls are unavailable.");
			env->DeleteLocalRef(name);
			env->DeleteLocalRef(loaderClass);
			if (loader) env->DeleteLocalRef(loader);
			env->DeleteLocalRef(activityClass);
			env->DeleteLocalRef(activity);
			return s_Bridge.Class ? &s_Bridge : nullptr;
		}
#endif
	}

	bool CallJava(const std::string& className, const std::string& method, const std::string& argument, std::string& result) {
		result.clear();
#if defined(FNK_PLATFORM_ANDROID)
		auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
		if (!env || className.empty() || method.empty()) return false;
		const JavaBridge* bridge = GetJavaBridge(env);
		if (!bridge) return false;
		jbyteArray jClass = ToJavaBytes(env, className);
		jbyteArray jMethod = ToJavaBytes(env, method);
		jbyteArray jArgument = ToJavaBytes(env, argument);
		// FunkeBridge.call catches the target's exceptions and answers null instead.
		auto answer = static_cast<jbyteArray>(env->CallStaticObjectMethod(bridge->Class, bridge->Call, jClass, jMethod, jArgument));
		bool ok = false;
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
		} else if (answer) {
			result = FromJavaBytes(env, answer);
			ok = true;
		}
		if (answer) env->DeleteLocalRef(answer);
		env->DeleteLocalRef(jClass);
		env->DeleteLocalRef(jMethod);
		env->DeleteLocalRef(jArgument);
		return ok;
#else
		(void)className; (void)method; (void)argument;
		return false;
#endif
	}

	bool PollJavaMessage(std::string& channel, std::string& message) {
		std::lock_guard lock(s_JavaMessageMutex);
		if (s_JavaMessages.empty()) return false;
		channel = std::move(s_JavaMessages.front().first);
		message = std::move(s_JavaMessages.front().second);
		s_JavaMessages.pop_front();
		return true;
	}

	void PushJavaMessage(std::string channel, std::string message) {
		std::lock_guard lock(s_JavaMessageMutex);
		if (s_JavaMessages.size() >= k_MaxJavaMessages) s_JavaMessages.pop_front();
		s_JavaMessages.emplace_back(std::move(channel), std::move(message));
	}

	const char* SharedLibraryExtension() {
#if defined(FNK_PLATFORM_WINDOWS)
		return ".dll";
#elif defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_IOS)
		return ".dylib";
#else
		return ".so";
#endif
	}

	std::string SharedLibraryFileName(std::string_view stem) {
#if defined(FNK_PLATFORM_WINDOWS)
		return std::string(stem) + SharedLibraryExtension();
#else
		return "lib" + std::string(stem) + SharedLibraryExtension();
#endif
	}

	void* LoadSharedLibrary(const std::filesystem::path& path, std::string* error) {
#if FNK_PLATFORM_HAS_DYNAMIC_LIBRARIES
		const std::string utf8 = ToUtf8(path);
		SDL_SharedObject* library = SDL_LoadObject(utf8.c_str());
		if (!library && error) *error = SDL_GetError();
		return library;
#else
		(void)path;
		if (error) *error = std::string("Dynamic libraries are not supported on ") + FNK_PLATFORM_NAME;
		return nullptr;
#endif
	}

	void* GetSharedLibrarySymbol(void* library, const char* name) {
		if (!library || !name) return nullptr;
		return reinterpret_cast<void*>(SDL_LoadFunction(static_cast<SDL_SharedObject*>(library), name));
	}

	void UnloadSharedLibrary(void* library) {
		if (library) SDL_UnloadObject(static_cast<SDL_SharedObject*>(library));
	}

	void SetCurrentThreadName(const char* name) {
		if (!name || !*name) return;
#if defined(FNK_PLATFORM_WINDOWS)
		const int size = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
		if (size <= 0) return;
		std::wstring wide(static_cast<size_t>(size), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, name, -1, wide.data(), size);
		SetThreadDescription(GetCurrentThread(), wide.c_str());
#elif defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_IOS)
		pthread_setname_np(name);
#elif defined(FNK_PLATFORM_LINUX) || defined(FNK_PLATFORM_ANDROID)
		// Linux caps thread names at 15 characters plus the terminator.
		char truncated[16]{};
		std::strncpy(truncated, name, sizeof(truncated) - 1);
		pthread_setname_np(pthread_self(), truncated);
#else
		(void)name;
#endif
	}

}

#if defined(FNK_PLATFORM_ANDROID)
// Notification / lock-screen / headset buttons, called on the Java UI thread by FunkeAudioService.
extern "C" JNIEXPORT void JNICALL Java_dev_funke_player_FunkeAudioService_nativeMediaCommand(JNIEnv*, jclass, jint command, jlong positionMs) {
	Funke::Platform::PushMediaCommand(static_cast<Funke::Platform::MediaCommand>(command), static_cast<float>(positionMs) / 1000.0f);
}
#endif

#if defined(FNK_PLATFORM_ANDROID)
// A Cut / Copy / Paste / Select all tap in the floating text toolbar, called on the Java UI thread.
extern "C" JNIEXPORT void JNICALL Java_dev_funke_player_FunkeActivity_nativeTextAction(JNIEnv*, jclass, jint action) {
	Funke::Platform::PushTextAction(static_cast<Funke::Platform::TextAction>(action));
}

// FunkeBridge.send from any Java thread; strings arrive as UTF-8 bytes.
extern "C" JNIEXPORT void JNICALL Java_dev_funke_player_FunkeBridge_nativeSend(JNIEnv* env, jclass, jbyteArray channel, jbyteArray message) {
	Funke::Platform::PushJavaMessage(Funke::Platform::FromJavaBytes(env, channel), Funke::Platform::FromJavaBytes(env, message));
}
#endif
