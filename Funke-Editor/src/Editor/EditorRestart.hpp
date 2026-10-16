#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	// Changes that only take effect after an editor reload register here. The editor layer shows one
	// "Reload the editor?" prompt in the main window (Reload Now / Later) and keeps a status-bar hint while
	// anything is pending, so no panel opens its own restart dialog.
	class EditorRestart {
	public:
		enum class Kind {
			Reload,  // in-process reload: modules, packages
			Restart  // also recreates the window and GPU device: render backend, editor font
		};

		struct Reason {
			std::string Key;
			std::string Text;
			Kind RestartKind = Kind::Reload;
		};

		// Same key replaces the earlier text, so toggling a setting twice leaves one line; repeating an identical
		// request is a no-op, so callers may sync every frame.
		static void Request(std::string key, std::string text, Kind kind = Kind::Reload);
		// The setting is back to the running state; drops the reason without prompting.
		static void Withdraw(std::string_view key);

		static bool HasPending();
		static const std::vector<Reason>& GetPending();
		static Kind GetPendingKind();
		// True once after each Request, so the prompt opens again for a new change after "Later".
		static bool ConsumePromptRequest();
		// Called when the reload starts; the reloaded editor applies everything that was pending.
		static void Clear();
	};

}
