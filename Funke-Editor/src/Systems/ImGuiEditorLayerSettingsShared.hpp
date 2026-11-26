#pragma once

#include "Systems/ImGuiEditorLayer.hpp"
#include "Gui/EditorUI.hpp"

namespace Funke::EditorSettingsUI {
	bool SectionVisible(ImGuiEditorLayer::SettingsCategory category, const char* section, const std::string& filterLower);

	class SettingsSection {
	public:
		SettingsSection(const char* label, const std::string& filterLower, const std::string& summary = {})
			: m_Open(EditorUI::BeginSection(label, summary.empty() ? nullptr : summary.c_str(), true, !filterLower.empty())) {}

		~SettingsSection() { if (m_Open) EditorUI::EndSection(); }
		SettingsSection(const SettingsSection&) = delete;
		SettingsSection& operator=(const SettingsSection&) = delete;
		explicit operator bool() const { return m_Open; }

	private:
		bool m_Open;
	};

	bool SettingsInt(const char* label, int* value);
	bool SettingsCombo(const char* label, int* value, const char* const items[], int count);
	bool SettingsSliderFloat(const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags = ImGuiSliderFlags_None);
	bool SettingsSliderInt(const char* label, int* value, int minimum, int maximum, const char* format, ImGuiSliderFlags flags = ImGuiSliderFlags_None);
	bool SettingsDragFloat(const char* label, float* value, float speed, float minimum, float maximum, const char* format, ImGuiSliderFlags flags = ImGuiSliderFlags_None);
	bool SettingsColor(const char* label, float color[3]);
}
