#pragma once
#include "Core/Export.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Portable OS services. SDL3 covers most of them; the few Windows paths that
// stay native (shell "select in Explorer") live behind the same calls.
namespace Index::Platform {

	// ── Paths ───────────────────────────────────────────────────────────

	// Directory of the running executable (the Resources folder inside a macOS bundle). Empty on failure.
	INDEX_API std::filesystem::path GetExecutableDirectory();
	// Full path of the running executable; empty on Web or failure.
	INDEX_API std::filesystem::path GetExecutablePath();

	// Writable per-user root for engine data: %LOCALAPPDATA% on Windows, ~/Library/Application Support on Apple,
	// $XDG_DATA_HOME (~/.local/share) on Linux, the app's private storage on Android / Web.
	INDEX_API std::filesystem::path GetUserDataRoot();

	enum class UserFolder : uint8_t { Home, Desktop, Documents, Downloads, Music, Pictures, Videos };
	// Empty when the platform has no such folder (mobile, Web).
	INDEX_API std::filesystem::path GetUserFolder(UserFolder folder);

	// ── Shell ───────────────────────────────────────────────────────────

	INDEX_API bool OpenUrl(const std::string& url);
	// Opens a file with its default application, or a folder in the file manager.
	INDEX_API bool OpenPath(const std::filesystem::path& path);
	// Shows the file manager with the item selected where supported, else opens its parent folder.
	INDEX_API bool RevealInFileManager(const std::filesystem::path& path);
	// Menu label for RevealInFileManager: "Show in Explorer", "Reveal in Finder" or "Show in File Manager".
	INDEX_API const char* RevealInFileManagerLabel();

	// ── Dialogs ─────────────────────────────────────────────────────────

	enum class MessageBoxKind : uint8_t { Info, Warning, Error };
	// Blocking; works before any window exists.
	INDEX_API void ShowMessageBox(MessageBoxKind kind, const std::string& title, const std::string& message);
	// Blocking yes/no prompt; returns true for yes.
	INDEX_API bool ShowConfirmDialog(MessageBoxKind kind, const std::string& title, const std::string& message,
		const std::string& yesLabel = "Yes", const std::string& noLabel = "No");

	// Empty optional when the user cancelled. The callback may run on any thread; SDL needs the main
	// thread to keep pumping events while the dialog is open.
	using PathPickedFn = std::function<void(std::optional<std::filesystem::path>)>;
	INDEX_API void PickFolderAsync(PathPickedFn onPicked, const std::filesystem::path& defaultLocation = {},
		const std::string& title = {});
	// Blocks the calling (main) thread until the dialog closes, pumping OS events meanwhile.
	INDEX_API std::optional<std::filesystem::path> PickFolder(const std::filesystem::path& defaultLocation = {},
		const std::string& title = {});

	// Pattern lists extensions without dots, ';'-separated: "anim", "png;jpg".
	struct FileFilter {
		std::string Name;
		std::string Pattern;
	};
	// "Save as" dialog. defaultPath is folder + suggested file name. The OS does not append the filter's extension
	// everywhere, so callers enforce it.
	INDEX_API void SaveFileAsync(PathPickedFn onPicked, const std::filesystem::path& defaultPath = {},
		const std::vector<FileFilter>& filters = {}, const std::string& title = {});

	// ── Environment ─────────────────────────────────────────────────────

	// BCP 47 tag of the user's first preferred locale ("de-DE", "en"); empty when unknown.
	INDEX_API std::string GetPreferredLocale();
	// Empty optional when the OS exposes no theme preference.
	INDEX_API std::optional<bool> IsSystemDarkTheme();
	INDEX_API int GetLogicalCpuCount();
	INDEX_API int GetSystemRamMiB();
	// Resident memory (working set) of this process in MiB; 0 when the platform does not report it.
	INDEX_API float GetProcessMemoryMiB();

	// Finder, the Dock and desktop launchers start apps with a minimal PATH. Appends the usual install
	// locations of dotnet, cmake, git and code editors (Homebrew, /usr/local, ~/.dotnet, snap) so child
	// processes find them, and points DOTNET_ROOT at a per-user .NET install. No-op on Windows.
	INDEX_API void ExtendToolSearchPath();

	// ── Permissions ─────────────────────────────────────────────────────

	enum class PermissionState : uint8_t { Unknown, Pending, Granted, Denied };
	// Asks for an Android runtime permission ("android.permission.READ_MEDIA_AUDIO"); the answer arrives
	// asynchronously, poll GetPermissionState. Every other platform grants immediately.
	// The permission must also be declared in the APK manifest (Android build profile option "permissions").
	INDEX_API void RequestPermission(const std::string& permission);
	INDEX_API PermissionState GetPermissionState(const std::string& permission);

	// Android: starts/stops the foreground media service that keeps a backgrounded app playing audio (and shows
	// its "now playing" notification). No-op elsewhere. Use AudioManager::SetPlayInBackground instead of calling this.
	INDEX_API void SetBackgroundAudioService(bool enabled);

	// ── Media session (Android notification / lock screen / headset buttons) ─────

	enum class MediaCommand : int { None = 0, Play = 1, Pause = 2, Next = 3, Previous = 4, Seek = 5 };
	// What the "now playing" notification shows. rgba (row 0 at top) may be null; no-op off Android.
	INDEX_API void SetMediaNowPlaying(const std::string& title, const std::string& artist, const std::string& album,
		float durationSeconds, const uint8_t* rgba, int width, int height);
	INDEX_API void SetMediaPlaybackState(bool playing, float positionSeconds);
	// Buttons pressed in the notification, on the lock screen or on a headset, oldest first. Main thread.
	INDEX_API MediaCommand PollMediaCommand(float& seekSeconds);
	// Called by the platform glue (any thread).
	INDEX_API void PushMediaCommand(MediaCommand command, float seekSeconds);

	// ── Text selection toolbar (Android's floating Cut / Copy / Paste / Select all) ─────

	enum class TextAction : int { None = 0, Cut = 1, Copy = 2, Paste = 3, SelectAll = 4 };
	enum TextActionFlags : int { TextActionCut = 1, TextActionCopy = 2, TextActionPaste = 4, TextActionSelectAll = 8 };
	// Shows the toolbar above a rect in window pixels. False where there is no system toolbar (desktop uses shortcuts).
	INDEX_API bool ShowTextActions(int x, int y, int width, int height, int flags);
	INDEX_API void HideTextActions();
	INDEX_API TextAction PollTextAction();
	INDEX_API void PushTextAction(TextAction action);

	// ── Java bridge (Android code that packages ship in the APK) ─────

	// Calls `public static String method(String)` on a Java class through dev.index.player.IndexBridge.
	// False when the class or method is missing, it threw, or off Android.
	INDEX_API bool CallJava(const std::string& className, const std::string& method, const std::string& argument,
		std::string& result);
	// Messages Java posted with IndexBridge.send(channel, message), oldest first. Main thread.
	INDEX_API bool PollJavaMessage(std::string& channel, std::string& message);
	// Called by the platform glue (any thread).
	INDEX_API void PushJavaMessage(std::string channel, std::string message);

	// ── Dynamic libraries ───────────────────────────────────────────────

	// "Pkg.Foo" -> "Pkg.Foo.dll" / "libPkg.Foo.so" / "libPkg.Foo.dylib".
	INDEX_API std::string SharedLibraryFileName(std::string_view stem);
	INDEX_API const char* SharedLibraryExtension();

	// Null on failure; *error receives the reason when provided. iOS / Web link packages statically and always fail here.
	INDEX_API void* LoadSharedLibrary(const std::filesystem::path& path, std::string* error = nullptr);
	INDEX_API void* GetSharedLibrarySymbol(void* library, const char* name);
	INDEX_API void UnloadSharedLibrary(void* library);

	// ── Threads ─────────────────────────────────────────────────────────

	// Shows up in debuggers and profilers; no-op where unsupported.
	INDEX_API void SetCurrentThreadName(const char* name);

}
