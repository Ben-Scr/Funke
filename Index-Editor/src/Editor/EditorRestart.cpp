#include <pch.hpp>
#include "Editor/EditorRestart.hpp"

#include <algorithm>

namespace Index {

	namespace {
		// Statics on purpose: the editor layer is rebuilt by an in-process reload, the pending list is not.
		std::vector<EditorRestart::Reason> s_Pending;
		bool s_PromptRequested = false;
	}

	void EditorRestart::Request(std::string key, std::string text, Kind kind) {
		auto it = std::find_if(s_Pending.begin(), s_Pending.end(), [&key](const Reason& r) { return r.Key == key; });
		if (it != s_Pending.end()) {
			// Unchanged reasons may be re-requested every frame; only a real change prompts again.
			if (it->Text == text && it->RestartKind == kind) return;
			it->Text = std::move(text);
			it->RestartKind = kind;
		}
		else {
			s_Pending.push_back({ std::move(key), std::move(text), kind });
		}
		s_PromptRequested = true;
	}

	void EditorRestart::Withdraw(std::string_view key) {
		std::erase_if(s_Pending, [key](const Reason& r) { return r.Key == key; });
		if (s_Pending.empty()) s_PromptRequested = false;
	}

	bool EditorRestart::HasPending() {
		return !s_Pending.empty();
	}

	const std::vector<EditorRestart::Reason>& EditorRestart::GetPending() {
		return s_Pending;
	}

	EditorRestart::Kind EditorRestart::GetPendingKind() {
		const bool restart = std::any_of(s_Pending.begin(), s_Pending.end(),
			[](const Reason& r) { return r.RestartKind == Kind::Restart; });
		return restart ? Kind::Restart : Kind::Reload;
	}

	bool EditorRestart::ConsumePromptRequest() {
		return std::exchange(s_PromptRequested, false);
	}

	void EditorRestart::Clear() {
		s_Pending.clear();
		s_PromptRequested = false;
	}

}
