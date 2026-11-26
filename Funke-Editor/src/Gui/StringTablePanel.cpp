#include "Gui/StringTablePanel.hpp"
#include "Gui/EditorUI.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace Funke {
	void StringTablePanel::Shutdown() {
		m_History.Clear(); m_Directory.clear(); m_Document = {}; m_Selected.clear(); m_Text.clear();
	}
	void StringTablePanel::Scan() {
		m_Files.clear(); m_Keys.clear(); m_Diagnostics.clear();
		std::map<std::string, std::filesystem::path> codes;
		std::error_code ec;
		for (const auto& file : File::ListFiles(m_Directory, false, ec)) {
			auto extension = Path::ToUtf8(file.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (extension != ".json") continue;
			m_Files.push_back(file);
		}
		std::sort(m_Files.begin(), m_Files.end());
		for (const auto& file : m_Files) {
			StringTableDocument doc;
			if (!doc.Open(file)) { m_Diagnostics.push_back(Path::ToUtf8(file.filename()) + ": " + doc.Error()); continue; }
			Json::Value root;
			Json::TryParseUnique(File::ReadAllText(Path::ToUtf8(file)), root);
			std::string code = Path::ToUtf8(file.stem());
			if (const auto* meta = root.FindMember("meta"))
				if (const auto* value = meta->FindMember("code"); value && !value->AsStringOr().empty()) code = value->AsStringOr();
			if (!codes.emplace(code, file).second) m_Diagnostics.push_back("Duplicate language code: " + code);
			for (const auto& [key, value] : doc.Strings()->GetObject()) m_Keys.insert(key);
		}
		m_Document.Refresh();
		m_NextScan = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	}
	void StringTablePanel::Render(bool* open) {
		if (!open || !*open) return;
		EditorUI::NextWindowDefaultSize();
		if (!ImGui::Begin("String Tables", open)) { ImGui::End(); return; }
		const auto* project = ProjectManager::GetCurrentProject();
		if (!project) { EditorUI::EmptyState("No project open.", "Open a project to edit translations."); ImGui::End(); return; }
		const auto directory = Path::FromUtf8(project->RootDirectory) / "Assets" / "Localization";
		if (directory != m_Directory) {
			Shutdown(); m_Directory = directory; m_History.Initialize(directory.parent_path()); Scan();
		}
		if (std::chrono::steady_clock::now() >= m_NextScan) {
			const auto before = m_Document.Strings() ? Json::Stringify(*m_Document.Strings()) : std::string{};
			Scan();
			if (!m_Document.IsDirty() && m_Document.Strings() && before != Json::Stringify(*m_Document.Strings())) m_Selected.clear();
		}
		ImGui::BeginDisabled(m_Document.IsDirty());
		if (ImGui::BeginCombo("Language file", Path::ToUtf8(m_Document.Path().filename()).c_str())) {
			for (const auto& file : m_Files) if (ImGui::Selectable(Path::ToUtf8(file.filename()).c_str(), file == m_Document.Path())) {
				m_Document.Open(file); m_Selected.clear();
			}
			ImGui::EndCombo();
		}
		if (ImGui::CollapsingHeader("Create language")) {
			ImGui::InputText("Code", m_NewCode, sizeof(m_NewCode));
			ImGui::InputText("Display name", m_NewName, sizeof(m_NewName));
			if (EditorUI::Button("Create")) { m_Document.Create(m_Directory, m_NewCode, m_NewName); m_Selected.clear(); }
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(!m_Document.IsDirty() || !m_Document.IsValid() || m_Document.HasConflict());
		if (EditorUI::Button("Save")) { m_Document.Save(m_History); Scan(); }
		ImGui::EndDisabled(); ImGui::SameLine();
		if (EditorUI::Button("Reload / discard edits") && !m_Document.Path().empty()) { m_Document.Open(m_Document.Path()); m_Selected.clear(); Scan(); }
		ImGui::BeginDisabled(m_Document.IsDirty());
		ImGui::SameLine(); ImGui::BeginDisabled(!m_History.CanUndo());
		if (EditorUI::Button("Undo")) { try { m_Status = m_History.Undo(); m_Selected.clear(); Scan(); } catch (const std::exception& e) { m_Status = e.what(); } }
		ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(!m_History.CanRedo());
		if (EditorUI::Button("Redo")) { try { m_Status = m_History.Redo(); m_Selected.clear(); Scan(); } catch (const std::exception& e) { m_Status = e.what(); } }
		ImGui::EndDisabled(); ImGui::EndDisabled();
		if (!m_Status.empty()) ImGui::TextWrapped("%s", m_Status.c_str());
		if (!m_Document.Error().empty()) ImGui::TextWrapped("%s", m_Document.Error().c_str());
		if (!m_Diagnostics.empty() && ImGui::TreeNode("Table diagnostics")) {
			for (const auto& message : m_Diagnostics) ImGui::TextWrapped("%s", message.c_str());
			ImGui::TreePop();
		}
		const auto* strings = m_Document.Strings();
		if (!strings) { ImGui::End(); return; }
		ImGui::InputTextWithHint("##search", "Search keys or translations", m_Search, sizeof(m_Search));
		ImGui::Checkbox("Missing translations only", &m_MissingOnly);
		std::set<std::string> keys = m_Keys;
		for (const auto& [key, value] : strings->GetObject()) keys.insert(key);
		const auto missing = std::count_if(keys.begin(), keys.end(), [&](const auto& key) { return !strings->FindMember(key); });
		ImGui::Text("%zu keys, %zu missing in this language", keys.size(), static_cast<size_t>(missing));
		ImGui::TextWrapped("Missing keys use the runtime fallback (en, otherwise first language by code). Empty translations remain empty. Rename and delete affect this language file.");
		if (ImGui::BeginChild("Keys", ImVec2(0, 200), ImGuiChildFlags_Borders)) {
			for (const auto& key : keys) {
				const auto* value = strings->FindMember(key);
				const std::string text = value ? value->AsStringOr() : std::string{};
				if (m_MissingOnly && value) continue;
				if (*m_Search && key.find(m_Search) == std::string::npos && text.find(m_Search) == std::string::npos) continue;
				ImGui::PushID(key.c_str());
				if (ImGui::Selectable((key + (value ? "" : "  (missing)")).c_str(), m_Selected == key)) {
					m_Selected = key; m_Text.assign(std::max<size_t>(text.size() + 1, 256), 0); std::memcpy(m_Text.data(), text.data(), text.size());
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::InputText("Key", m_NewKey, sizeof(m_NewKey));
		if (EditorUI::Button("Add key") && *m_NewKey) {
			if (strings->FindMember(m_NewKey)) m_Status = "That key already exists.";
			else { m_Document.Set(m_NewKey, ""); m_Selected = m_NewKey; m_Text.assign(256, 0); m_Status.clear(); }
		}
		if (!m_Selected.empty()) {
			ImGui::SameLine();
			if (EditorUI::Button("Rename selected") && m_Document.Rename(m_Selected, m_NewKey)) m_Selected = m_NewKey;
			ImGui::SameLine();
			if (EditorUI::Button("Delete selected")) { m_Document.Remove(m_Selected); m_Selected.clear(); }
		}
		if (!m_Selected.empty()) {
			ImGui::TextUnformatted(m_Selected.c_str());
			auto resize = [](ImGuiInputTextCallbackData* data) -> int {
				auto& buffer = *static_cast<std::vector<char>*>(data->UserData);
				buffer.resize(static_cast<size_t>(data->BufSize)); data->Buf = buffer.data(); return 0;
			};
			if (ImGui::InputTextMultiline("Translation", m_Text.data(), m_Text.size(), ImVec2(-1, 110),
				ImGuiInputTextFlags_CallbackResize, resize, &m_Text)) m_Document.Set(m_Selected, m_Text.data());
		}
		ImGui::End();
	}
}
