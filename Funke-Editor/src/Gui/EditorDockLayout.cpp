#include "Gui/EditorDockLayout.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <charconv>
#include <climits>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace Funke::EditorDockLayout {

	void WindowSettings::Install() {
		ImGuiSettingsHandler handler;
		handler.TypeName = "FunkeLayout";
		handler.TypeHash = ImHashStr(handler.TypeName);
		handler.UserData = this;
		handler.ClearAllFn = handler.ReadInitFn = [](ImGuiContext*, ImGuiSettingsHandler* handler) {
			auto& state = *static_cast<WindowSettings*>(handler->UserData);
			state.m_Saved.clear();
			state.m_HasSaved = false;
			state.m_PendingTabs.clear();
		};
		handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) -> void* {
			if (std::strcmp(name, "Windows") != 0) return nullptr;
			auto& state = *static_cast<WindowSettings*>(handler->UserData);
			state.m_HasSaved = true;
			return &state;
		};
		handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
			const std::string_view text(line);
			const size_t separator = text.rfind('=');
			if (separator == std::string_view::npos || separator == 0) return;
			const auto value = text.substr(separator + 1);
			if (value != "0" && value != "1") return;
			static_cast<WindowSettings*>(entry)->m_Saved[std::string(text.substr(0, separator))] = value == "1";
		};
		handler.ApplyAllFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler) {
			auto& state = *static_cast<WindowSettings*>(handler->UserData);
			for (auto* window = ctx->SettingsWindows.begin(); window; window = ctx->SettingsWindows.next_chunk(window)) {
				if (auto* node = ImGui::DockBuilderGetNode(window->DockId)) {
					auto& group = state.m_PendingTabs[node->ID];
					group.Selected = node->SelectedTabId;
					group.Order[window->ID] = window->DockOrder;
				}
			}
			if (state.m_HasSaved && state.m_Apply) state.m_Apply(state.m_Saved);
		};
		handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* output) {
			auto& state = *static_cast<WindowSettings*>(handler->UserData);
			if (state.m_Capture) {
				state.m_Saved = state.m_Capture();
				state.m_HasSaved = true;
			}
			if (!state.m_HasSaved) return;
			output->append("[FunkeLayout][Windows]\n");
			for (const auto& [name, open] : state.m_Saved) {
				if (name.empty() || name.find_first_of("\r\n") != std::string::npos) continue;
				output->appendf("%s=%d\n", name.c_str(), open ? 1 : 0);
			}
			output->append("\n");
		};
		ImGui::AddSettingsHandler(&handler);
		ImGuiContextHook hook;
		hook.Type = ImGuiContextHookType_EndFramePost;
		hook.UserData = this;
		hook.Callback = [](ImGuiContext*, ImGuiContextHook* hook) {
			static_cast<WindowSettings*>(hook->UserData)->RestoreTabs();
		};
		ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
	}

	void WindowSettings::RestoreTabs() {
		// Reopened tools may join a node one frame later than its already-live windows. ImGui only
		// sorts newly arriving tabs together, so restore the complete group once its tabs are present.
		for (auto it = m_PendingTabs.begin(); it != m_PendingTabs.end();) {
			ImGuiDockNode* node = ImGui::DockBuilderGetNode(it->first);
			if (!node || node->Windows.empty() || node->IsHiddenTabBar() || node->IsNoTabBar()) {
				it = m_PendingTabs.erase(it);
				continue;
			}
			ImGuiTabBar* bar = node->TabBar;
			if (!bar || std::any_of(node->Windows.begin(), node->Windows.end(), [bar](ImGuiWindow* window) {
				return window->Active && !ImGui::TabBarFindTabByID(bar, window->TabId);
			})) { ++it; continue; }
			const TabGroup& saved = it->second;
			const auto order = [&saved](const ImGuiTabItem& tab) {
				const auto found = saved.Order.find(tab.Window ? tab.Window->ID : 0);
				return found != saved.Order.end() && found->second >= 0 ? static_cast<int>(found->second) : INT_MAX;
			};
			if (bar->Tabs.Size > 1) {
				std::stable_sort(bar->Tabs.begin(), bar->Tabs.end(), [&](const auto& a, const auto& b) { return order(a) < order(b); });
			}
			for (int i = 0; i < bar->Tabs.Size; ++i) {
				if (auto* window = bar->Tabs[i].Window) window->DockOrder = static_cast<short>(i);
			}
			if (const auto* selected = ImGui::TabBarFindTabByID(bar, saved.Selected)) {
				node->SelectedTabId = bar->SelectedTabId = bar->NextSelectedTabId = saved.Selected;
				const ImGuiWindow* focused = ImGui::GetCurrentContext()->NavWindow;
				if (focused && focused->RootWindow->DockNode == node) ImGui::FocusWindow(selected->Window);
			}
			bar->WantLayout = true;
			it = m_PendingTabs.erase(it);
		}
	}

	WindowSettings* WindowSettings::Get() {
		const ImGuiSettingsHandler* handler = ImGui::FindSettingsHandler("FunkeLayout");
		return handler ? static_cast<WindowSettings*>(handler->UserData) : nullptr;
	}

	void WindowSettings::SetCallbacks(std::function<WindowVisibility()> capture, std::function<void(const WindowVisibility&)> apply) {
		m_Capture = std::move(capture);
		m_Apply = std::move(apply);
		if (m_HasSaved && m_Apply) m_Apply(m_Saved);
	}

	void WindowSettings::ClearCallbacks() {
		// Keep a final snapshot for the context layer's shutdown save, after the editor has detached.
		if (m_Capture) {
			m_Saved = m_Capture();
			m_HasSaved = true;
		}
		m_Capture = {};
		m_Apply = {};
	}

	namespace {
		constexpr const char* k_MainPanels[] = { "Entities", "Inspector", "Editor View", "Game View", "Project", "Log" };

		ImGuiID GetDockId(const char* windowName) {
			if (const ImGuiWindow* window = ImGui::FindWindowByName(windowName)) {
				return window->DockNode != nullptr ? window->DockNode->ID : window->DockId;
			}
			const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(windowName));
			return settings != nullptr ? settings->DockId : 0;
		}

		// Extents in font heights so the panels follow DPI and editor zoom; clamped so a small window keeps its centre.
		float SplitRatio(float extentEm, float available, float minRatio, float maxRatio) {
			if (available <= 0.0f) return minRatio;
			return std::clamp(ImGui::GetFontSize() * extentEm / available, minRatio, maxRatio);
		}
	}

	Preview ParsePreview(std::string_view settings) {
		struct Node {
			ImVec2 Size;
			ImVec2 Position = ImVec2(0, 0);
			ImVec2 Extent = ImVec2(1, 1);
			int Children[2] = { -1, -1 };
			char Split = 0;
			bool Dockspace = false;
			bool Central = false;
		};
		const auto parseNumber = [](std::string_view text, unsigned int& value, int base = 10) {
			if (base == 16 && text.starts_with("0x")) text.remove_prefix(2);
			const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
			return result.ec == std::errc() && result.ptr == text.data() + text.size();
		};
		std::vector<Node> nodes;
		std::unordered_map<unsigned int, int> indices;
		std::istringstream input{ std::string(settings) };
		std::string line;
		bool inDocking = false;
		int root = -1;
		while (std::getline(input, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.starts_with('[')) {
				inDocking = line == "[Docking][Data]";
				continue;
			}
			if (!inDocking) continue;
			std::istringstream fields(line);
			std::string field;
			fields >> field;
			if (field != "DockSpace" && field != "DockNode") continue;
			if (nodes.size() >= 4096) return {};
			Node node;
			node.Dockspace = field == "DockSpace";
			unsigned int id = 0;
			unsigned int parent = 0;
			while (fields >> field) {
				const size_t separator = field.find('=');
				if (separator == std::string::npos) continue;
				const std::string_view key(field.data(), separator);
				const std::string_view value(field.data() + separator + 1, field.size() - separator - 1);
				if (key == "ID" || key == "Parent") {
					if (!parseNumber(value, key == "ID" ? id : parent, 16)) return {};
				} else if (key == "Size" || key == "SizeRef") {
					const size_t comma = value.find(',');
					unsigned int x = 0, y = 0;
					if (comma == std::string_view::npos || !parseNumber(value.substr(0, comma), x)
						|| !parseNumber(value.substr(comma + 1), y) || x == 0 || y == 0) return {};
					node.Size = ImVec2(static_cast<float>(x), static_cast<float>(y));
				} else if (key == "Split") {
					if (value != "X" && value != "Y") return {};
					node.Split = value.front();
				} else if (key == "CentralNode") {
					node.Central = value == "1";
				}
			}
			if (id == 0 || indices.contains(id) || node.Size.x <= 0 || node.Size.y <= 0) return {};
			const int index = static_cast<int>(nodes.size());
			if (parent != 0) {
				const auto found = indices.find(parent);
				if (found == indices.end()) return {};
				Node& parentNode = nodes[found->second];
				if (parentNode.Children[1] != -1) return {};
				parentNode.Children[parentNode.Children[0] == -1 ? 0 : 1] = index;
			} else if (node.Dockspace && (root < 0 || node.Size.x * node.Size.y > nodes[root].Size.x * nodes[root].Size.y)) {
				root = index;
			}
			indices.emplace(id, index);
			nodes.push_back(node);
		}
		if (root < 0) return {};
		Preview result;
		result.Size = nodes[root].Size;
		std::vector<int> pending{ root };
		while (!pending.empty()) {
			const Node node = nodes[pending.back()];
			pending.pop_back();
			if (node.Children[0] == -1) {
				if (node.Split != 0) return {};
				result.Panes.push_back({ node.Position, node.Extent, node.Central });
				continue;
			}
			if (node.Children[1] == -1 || node.Split == 0) return {};
			Node& first = nodes[node.Children[0]];
			Node& second = nodes[node.Children[1]];
			const int axis = node.Split == 'X' ? 0 : 1;
			const float ratio = first.Size[axis] / (first.Size[axis] + second.Size[axis]);
			first.Position = second.Position = node.Position;
			first.Extent = second.Extent = node.Extent;
			first.Extent[axis] *= ratio;
			second.Position[axis] += first.Extent[axis];
			second.Extent[axis] *= 1.0f - ratio;
			pending.push_back(node.Children[1]);
			pending.push_back(node.Children[0]);
		}
		return result;
	}

	bool HasDockedPanels(ImGuiID dockspaceId) {
		for (const char* name : k_MainPanels) {
			const ImGuiID dockId = GetDockId(name);
			if (dockId == 0) continue;
			if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId); node != nullptr && ImGui::DockNodeGetRootNode(node)->ID == dockspaceId) {
				return true;
			}
		}
		return false;
	}

	void BuildDefault(ImGuiID dockspaceId, ImVec2 size) {
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, size);

		const float leftRatio = SplitRatio(25.0f, size.x, 0.12f, 0.3f);
		const float rightRatio = SplitRatio(26.0f, size.x * (1.0f - leftRatio), 0.12f, 0.3f);
		const float bottomRatio = SplitRatio(20.0f, size.y, 0.2f, 0.45f);

		// Each split hands the central node to the opposite side, so the views end up central.
		ImGuiID rest = 0;
		ImGuiID centre = 0;
		ImGuiID views = 0;
		ImGuiID log = 0;
		const ImGuiID left = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, leftRatio, nullptr, &rest);
		const ImGuiID right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, rightRatio, nullptr, &centre);
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, bottomRatio, nullptr, &views);
		const ImGuiID project = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.5f, nullptr, &log);

		ImGui::DockBuilderDockWindow("Entities", left);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("Editor View", views);
		ImGui::DockBuilderDockWindow("Game View", views);
		ImGui::DockBuilderDockWindow("Animator", views);
		ImGui::DockBuilderDockWindow("Project", project);
		ImGui::DockBuilderDockWindow("Animation", project);
		ImGui::DockBuilderDockWindow("Log", log);
		ImGui::DockBuilderFinish(dockspaceId);
	}

}
