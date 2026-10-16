#include "Gui/ScriptEditorWindows.hpp"

#include "Core/Application.hpp"
#include "Core/Log.hpp"
#include "Gui/EditorUI.hpp"
#include "Scripting/ScriptEditorGui.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace Funke {

	namespace ScriptEditorWindowsInternal {
		StackDepth& CurrentDepth() {
			static StackDepth depth;
			return depth;
		}

		void UnwindTo(const StackDepth& target) {
			StackDepth& depth = CurrentDepth();
			while (depth.Indents.size() > target.Indents.size()) {
				ImGui::Unindent(depth.Indents.back());
				depth.Indents.pop_back();
			}
			for (; depth.Ids > target.Ids; --depth.Ids) ImGui::PopID();
			for (; depth.Groups > target.Groups; --depth.Groups) ImGui::EndGroup();
		}
	}

	namespace {
		using ScriptEditorWindowsInternal::CurrentDepth;

		const char* OrDefault(const char* format, const char* fallback) {
			return format && *format ? format : fallback;
		}

		void Text(const char* text, int style) {
			switch (static_cast<EditorGuiTextStyle>(style)) {
			case EditorGuiTextStyle::Disabled: ImGui::TextDisabled("%s", text); break;
			case EditorGuiTextStyle::Heading: ImGui::SeparatorText(text); break;
			case EditorGuiTextStyle::Wrapped: ImGui::TextWrapped("%s", text); break;
			case EditorGuiTextStyle::Normal:
			default: ImGui::TextUnformatted(text); break;
			}
		}
		int Button(const char* label, float width, float height) {
			return ImGui::Button(label, ImVec2(width, height)) ? 1 : 0;
		}
		int Toggle(const char* label, int value) {
			bool on = value != 0;
			ImGui::Checkbox(label, &on);
			return on ? 1 : 0;
		}
		float Slider(const char* label, float value, float min, float max, const char* format) {
			ImGui::SliderFloat(label, &value, min, max, OrDefault(format, "%.3f"));
			return value;
		}
		float VerticalSlider(const char* label, float width, float height, float value, float min, float max, const char* format) {
			const float frame = ImGui::GetFrameHeight();
			ImGui::VSliderFloat(label, ImVec2(width > 0.0f ? width : frame, height > 0.0f ? height : frame * 6.0f),
				&value, min, max, OrDefault(format, "%.2f"));
			return value;
		}
		float FloatField(const char* label, float value, float speed) {
			ImGui::DragFloat(label, &value, speed > 0.0f ? speed : 0.1f);
			return value;
		}
		int TextField(const char* label, char* buffer, int capacity) {
			return ImGui::InputText(label, buffer, static_cast<size_t>(capacity)) ? 1 : 0;
		}
		void ProgressBar(float fraction, float width, float height, const char* overlay) {
			// An empty overlay hides ImGui's default percentage text.
			ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(width != 0.0f ? width : -FLT_MIN, height), overlay ? overlay : "");
		}
		void Layout(int op, float value) {
			auto& depth = CurrentDepth();
			switch (static_cast<EditorGuiLayoutOp>(op)) {
			case EditorGuiLayoutOp::Separator: ImGui::Separator(); break;
			case EditorGuiLayoutOp::SameLine: ImGui::SameLine(0.0f, value >= 0.0f ? value : -1.0f); break;
			case EditorGuiLayoutOp::Spacing: ImGui::Dummy(ImVec2(0.0f, value > 0.0f ? value : ImGui::GetStyle().ItemSpacing.y)); break;
			case EditorGuiLayoutOp::BeginGroup: ImGui::BeginGroup(); ++depth.Groups; break;
			case EditorGuiLayoutOp::EndGroup:
				// Unmatched ends are ignored rather than corrupting the stack.
				if (depth.Groups > 0) { ImGui::EndGroup(); --depth.Groups; }
				break;
			case EditorGuiLayoutOp::Indent: ImGui::Indent(value); depth.Indents.push_back(value); break;
			case EditorGuiLayoutOp::Unindent:
				if (!depth.Indents.empty()) { ImGui::Unindent(depth.Indents.back()); depth.Indents.pop_back(); }
				break;
			case EditorGuiLayoutOp::PopId:
				if (depth.Ids > 0) { ImGui::PopID(); --depth.Ids; }
				break;
			}
		}
		void PushId(const char* id) {
			ImGui::PushID(id);
			++CurrentDepth().Ids;
		}
		float Query(int what) {
			switch (static_cast<EditorGuiQuery>(what)) {
			case EditorGuiQuery::IsPlaying: return Application::GetIsPlaying() ? 1.0f : 0.0f;
			case EditorGuiQuery::AvailableWidth: return ImGui::GetContentRegionAvail().x;
			case EditorGuiQuery::LineHeight: return ImGui::GetFrameHeight();
			}
			return 0.0f;
		}

		const EditorGuiBackend k_Backend{ &Text, &Button, &Toggle, &Slider, &VerticalSlider, &FloatField, &TextField,
			&ProgressBar, &Layout, &PushId, &Query };

		double NowSeconds() {
			using Clock = std::chrono::steady_clock;
			return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
		}

		std::string SettingsPath(const std::string& projectRoot) {
			return (std::filesystem::path(projectRoot) / ".funke" / "ScriptWindows.json").string();
		}
	}

	void ScriptEditorWindows::Initialize() {
		ScriptEditorGui::SetBackend(&k_Backend);
		m_Initialized = true;
	}

	void ScriptEditorWindows::Shutdown() {
		if (!m_Initialized) return;
		for (const std::string& type : m_Open) ScriptEngine::CloseEditorWindow(type.c_str());
		ScriptEditorGui::SetBackend(nullptr);
		m_Initialized = false;
	}

	void ScriptEditorWindows::LoadProject(const std::string& projectRoot) {
		m_ProjectRoot = projectRoot;
		m_Open.clear();
		m_LastRefresh = -1.0;
		if (projectRoot.empty()) return;
		const std::string path = SettingsPath(projectRoot);
		if (!File::Exists(path)) return;
		Json::Value root;
		if (!Json::TryParse(File::ReadAllText(path), root) || !root.IsObject()) return;
		if (const Json::Value* open = root.FindMember("open"); open && open->IsArray()) {
			for (const Json::Value& type : open->GetArray()) {
				if (type.IsString() && !type.AsStringOr().empty()) m_Open.push_back(type.AsStringOr());
			}
		}
	}

	void ScriptEditorWindows::Save() const {
		if (m_ProjectRoot.empty()) return;
		Json::Value root = Json::Value::MakeObject();
		Json::Value open = Json::Value::MakeArray();
		for (const std::string& type : m_Open) open.Append(Json::Value(type));
		root.AddMember("open", std::move(open));
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(m_ProjectRoot) / ".funke", ec);
		if (!File::WriteAllText(SettingsPath(m_ProjectRoot), Json::Stringify(root, true))) {
			FNK_WARN_TAG("Editor", "Could not save the open script windows to {}", SettingsPath(m_ProjectRoot));
		}
	}

	void ScriptEditorWindows::Refresh(bool force) {
		const double now = NowSeconds();
		if (!force && m_LastRefresh >= 0.0 && now - m_LastRefresh < 1.0) return;
		m_LastRefresh = now;
		m_Types = ParseScriptWindowTypes(ScriptEngine::GetEditorWindowTypes());
	}

	bool ScriptEditorWindows::IsOpen(const std::string& type) const {
		return std::find(m_Open.begin(), m_Open.end(), type) != m_Open.end();
	}

	void ScriptEditorWindows::Open(const std::string& type) {
		if (IsOpen(type)) {
			ImGui::SetWindowFocus((TitleOf(type) + "###ScriptWindow_" + type).c_str());
			return;
		}
		m_Open.push_back(type);
		Save();
	}

	void ScriptEditorWindows::Close(const std::string& type) {
		const auto it = std::find(m_Open.begin(), m_Open.end(), type);
		if (it == m_Open.end()) return;
		m_Open.erase(it);
		ScriptEngine::CloseEditorWindow(type.c_str());
		Save();
	}

	std::string ScriptEditorWindows::TitleOf(const std::string& type) const {
		for (const WindowType& entry : m_Types) {
			if (entry.Type == type) return entry.Title;
		}
		return type.substr(type.find_last_of('.') + 1);
	}

	void ScriptEditorWindows::DrawMenuItems() {
		Refresh();
		if (m_Types.empty()) return;
		ImGui::Separator();
		for (const WindowType& entry : m_Types) {
			// "Audio/Audio Mixer" → submenu Audio, item Audio Mixer.
			std::vector<std::string> parts = SplitMenuPath(entry.Menu);
			if (parts.empty()) parts.push_back(entry.Title);
			int opened = 0;
			bool visible = true;
			for (size_t i = 0; i + 1 < parts.size(); ++i) {
				if (!ImGui::BeginMenu(parts[i].c_str())) { visible = false; break; }
				++opened;
			}
			if (visible) {
				const bool isOpen = IsOpen(entry.Type);
				if (ImGui::MenuItem(parts.back().c_str(), nullptr, isOpen)) {
					if (isOpen) Close(entry.Type);
					else Open(entry.Type);
				}
			}
			for (int i = 0; i < opened; ++i) ImGui::EndMenu();
		}
	}

	void ScriptEditorWindows::Render() {
		if (m_Open.empty()) return;
		Refresh();
		// Copy: closing from inside the loop edits m_Open.
		const std::vector<std::string> open = m_Open;
		for (const std::string& type : open) {
			bool keepOpen = true;
			ImVec2 size(420.0f, 320.0f);
			for (const WindowType& entry : m_Types) {
				if (entry.Type != type) continue;
				if (entry.Width > 0.0f) size.x = entry.Width;
				if (entry.Height > 0.0f) size.y = entry.Height;
			}
			ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
			const std::string id = TitleOf(type) + "###ScriptWindow_" + type;
			if (ImGui::Begin(id.c_str(), &keepOpen)) {
				const ScriptEditorWindowsInternal::StackDepth before = CurrentDepth();
				const int result = ScriptEngine::DrawEditorWindow(type.c_str());
				ScriptEditorWindowsInternal::UnwindTo(before);
				if (result == 0) {
					EditorUI::EmptyState("Window not available",
						"Its class is not in the loaded scripts. Build the scripts, or install the package that provides it.");
				}
				else if (result < 0) {
					ImGui::Spacing();
					ImGui::TextDisabled("OnGUI threw an exception; see the Console.");
				}
			}
			ImGui::End();
			if (!keepOpen) Close(type);
		}
	}

}
