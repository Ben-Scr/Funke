#include "RuntimeConsoleLayer.hpp"
#include "RuntimeImGuiHost.hpp"

#include "Core/Application.hpp"
#include "Core/EngineModules.hpp"
#include "Core/Input.hpp"
#include "Core/KeyCode.hpp"
#include "Core/PlatformShortcuts.hpp"
#include "DevTools/DevConsole.hpp"
#include "DevTools/PhysicsDebugOverlay.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace Funke {

	void RuntimeConsoleLayer::OnAttach(Application& app) {
		m_ImGuiAcquired = RuntimeImGuiHost::Acquire(app.GetWindow());
	}

	void RuntimeConsoleLayer::OnDetach(Application&) {
		DevConsole::SetOpen(false);
		if (m_ImGuiAcquired) {
			RuntimeImGuiHost::Release();
			m_ImGuiAcquired = false;
		}
	}

	bool RuntimeConsoleLayer::DetectThreeFingerTap() const {
		if (Input::GetTouchCount() != 3) return false;
		bool anyBegan = false;
		for (int i = 0; i < 3; ++i) {
			Touch touch;
			if (!Input::GetTouch(i, touch) || touch.Phase == TouchPhase::Ended) return false;
			anyBegan |= touch.Phase == TouchPhase::Began;
		}
		// Fires once, on the frame the third finger lands.
		return anyBegan;
	}

	void RuntimeConsoleLayer::Toggle() {
		DevConsole::SetOpen(!DevConsole::IsOpen());
		if (DevConsole::IsOpen()) {
			m_FocusInput = true;
			m_HistoryCursor = -1;
			m_CommandNames.clear();
			for (const DevCommandInfo& info : DevConsole::ListCommands()) m_CommandNames.push_back(info.Name);
		}
	}

	void RuntimeConsoleLayer::OnUpdate(Application&, float) {
		if (!DevConsole::IsEnabled()) return;
		// Diagnostic shortcuts must not depend on the console window or its keyboard focus.
		if (PlatformShortcuts::IsPressed(PlatformShortcuts::k_PhysicsDebug)) TogglePhysicsDebug();
		if (!m_ImGuiAcquired || !RuntimeImGuiHost::IsInitialized()) return;
		if (PlatformShortcuts::IsPressed(PlatformShortcuts::k_DevConsole) || DetectThreeFingerTap()) {
			if (DevConsole::IsOpen()) m_CloseRequested = true;
			else Toggle();
		}
		if (DevConsole::IsOpen() && Input::GetKeyDown(KeyCode::Esc)) m_CloseRequested = true;
	}

	void RuntimeConsoleLayer::TogglePhysicsDebug() {
		// Share the command's feedback with the console history and runtime log.
		DevConsole::Execute("physics.debug toggle");
	}

	void RuntimeConsoleLayer::DrawControls() {
		const std::string consoleLabel = std::string("Console (") + PlatformShortcuts::k_DevConsoleLabel + ")";
		if (ImGui::Button(consoleLabel.c_str())) {
			if (DevConsole::IsOpen()) m_CloseRequested = true;
			else Toggle();
		}
		ImGui::SameLine();
		const std::string physicsLabel = std::string("Physics: ") + (PhysicsDebugOverlay::IsEnabled() ? "On (" : "Off (")
			+ PlatformShortcuts::k_PhysicsDebugLabel + ")";
		if (ImGui::Button(physicsLabel.c_str())) TogglePhysicsDebug();
		if (!EngineModules::IsActive(EngineModule::Physics2D) && !EngineModules::IsActive(EngineModule::Physics3D)) {
			ImGui::TextDisabled("No physics module enabled in this build.");
		}
		else if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Shows collider outlines and contacts. The scene needs colliders to draw.");
		}
	}

	int RuntimeConsoleLayer::InputCallback(ImGuiInputTextCallbackData* data) {
		auto* self = static_cast<RuntimeConsoleLayer*>(data->UserData);
		switch (data->EventFlag) {
		case ImGuiInputTextFlags_CallbackHistory: {
			if (self->m_InputHistory.empty()) return 0;
			const int count = static_cast<int>(self->m_InputHistory.size());
			if (data->EventKey == ImGuiKey_UpArrow) {
				self->m_HistoryCursor = self->m_HistoryCursor < 0 ? count - 1 : std::max(0, self->m_HistoryCursor - 1);
			}
			else if (data->EventKey == ImGuiKey_DownArrow && self->m_HistoryCursor >= 0) {
				self->m_HistoryCursor = self->m_HistoryCursor + 1 >= count ? -1 : self->m_HistoryCursor + 1;
			}
			data->DeleteChars(0, data->BufTextLen);
			if (self->m_HistoryCursor >= 0) data->InsertChars(0, self->m_InputHistory[static_cast<size_t>(self->m_HistoryCursor)].c_str());
			return 0;
		}
		case ImGuiInputTextFlags_CallbackCompletion: {
			// Completes the command name (first word) to the longest common prefix of the matches.
			const std::string text(data->Buf, static_cast<size_t>(data->BufTextLen));
			if (text.find(' ') != std::string::npos) return 0;
			std::vector<const std::string*> matches;
			for (const std::string& name : self->m_CommandNames) {
				if (name.rfind(text, 0) == 0) matches.push_back(&name);
			}
			if (matches.empty()) return 0;
			std::string prefix = *matches.front();
			for (const std::string* match : matches) {
				size_t common = 0;
				while (common < prefix.size() && common < match->size() && prefix[common] == (*match)[common]) ++common;
				prefix.resize(common);
			}
			if (matches.size() == 1) prefix += ' ';
			else {
				std::string list;
				for (const std::string* match : matches) list += (list.empty() ? "" : "  ") + *match;
				DevConsole::Print(list);
			}
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, prefix.c_str());
			return 0;
		}
		default:
			return 0;
		}
	}

	void RuntimeConsoleLayer::Submit() {
		const std::string line(m_Buffer);
		m_Buffer[0] = '\0';
		m_HistoryCursor = -1;
		if (line.find_first_not_of(" \t") == std::string::npos) return;
		if (m_InputHistory.empty() || m_InputHistory.back() != line) m_InputHistory.push_back(line);
		DevConsole::Execute(line);
	}

	void RuntimeConsoleLayer::OnPreRender(Application&) {
		if (!m_ImGuiAcquired || !RuntimeImGuiHost::IsInitialized()) return;
		RuntimeImGuiHost::BeginFrame();
		if (!DevConsole::IsEnabled()) return;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (!DevConsole::IsOpen()) {
			const float margin = ImGui::GetStyle().WindowPadding.x;
			ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin,
				viewport->WorkPos.y + viewport->WorkSize.y - margin), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
			ImGui::SetNextWindowBgAlpha(0.80f);
			constexpr ImGuiWindowFlags k_ControlFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
			if (ImGui::Begin("##FunkeDevControls", nullptr, k_ControlFlags)) DrawControls();
			ImGui::End();
			return;
		}
		const float height = std::max(ImGui::GetFrameHeight() * 8.0f, viewport->WorkSize.y * 0.4f);
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
		ImGui::SetNextWindowBgAlpha(0.92f);
		constexpr ImGuiWindowFlags k_Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
		if (ImGui::Begin("##FunkeDevConsole", nullptr, k_Flags)) {
			DrawControls();
			ImGui::Separator();
			const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
			if (ImGui::BeginChild("##History", ImVec2(0.0f, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
				const ImVec4 inputColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
				const ImVec4 errorColor(1.0f, 0.42f, 0.40f, 1.0f);
				for (const DevConsole::HistoryEntry& entry : DevConsole::GetHistory()) {
					const bool colored = entry.Type != DevConsole::HistoryEntry::Kind::Output;
					if (colored) ImGui::PushStyleColor(ImGuiCol_Text, entry.Type == DevConsole::HistoryEntry::Kind::Error ? errorColor : inputColor);
					ImGui::TextUnformatted(entry.Text.c_str());
					if (colored) ImGui::PopStyleColor();
				}
				if (const uint64_t revision = DevConsole::GetHistoryRevision(); revision != m_ShownRevision) {
					m_ShownRevision = revision;
					ImGui::SetScrollHereY(1.0f);
				}
			}
			ImGui::EndChild();

			ImGui::SetNextItemWidth(-1.0f);
			if (m_FocusInput) {
				ImGui::SetKeyboardFocusHere();
				m_FocusInput = false;
			}
			constexpr ImGuiInputTextFlags k_InputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory
				| ImGuiInputTextFlags_CallbackCompletion;
			if (ImGui::InputTextWithHint("##Command", "Command (help lists them, Tab completes, Esc closes)",
					m_Buffer, sizeof(m_Buffer), k_InputFlags, &RuntimeConsoleLayer::InputCallback, this)) {
				Submit();
				m_FocusInput = true;
			}
		}
		ImGui::End();
	}

	void RuntimeConsoleLayer::OnPostRender(Application&) {
		if (!m_ImGuiAcquired || !RuntimeImGuiHost::IsInitialized()) return;
		RuntimeImGuiHost::EndFrame();
		// Closed after the frame, so the game never sees the closing key (Esc would open its pause menu).
		if (m_CloseRequested) {
			m_CloseRequested = false;
			DevConsole::SetOpen(false);
		}
	}

} // namespace Funke
