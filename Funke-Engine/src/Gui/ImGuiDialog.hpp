#pragma once

#include <imgui.h>

#include <algorithm>

namespace Funke::ImGuiDialog {

	inline thread_local int t_BodyDepth = 0;

	struct WorkArea {
		ImVec2 Position;
		ImVec2 Size;
	};

	inline WorkArea GetWorkArea(ImVec2 center) {
		for (const ImGuiPlatformMonitor& monitor : ImGui::GetPlatformIO().Monitors) {
			if (center.x >= monitor.MainPos.x && center.y >= monitor.MainPos.y
				&& center.x < monitor.MainPos.x + monitor.MainSize.x && center.y < monitor.MainPos.y + monitor.MainSize.y)
				return { monitor.WorkPos, monitor.WorkSize };
		}
		const auto* viewport = ImGui::GetMainViewport();
		return { viewport->WorkPos, viewport->WorkSize };
	}

	inline bool BeginModal(const char* title, bool* open = nullptr, ImGuiWindowFlags flags = 0) {
		const ImVec2 work = GetWorkArea(ImGui::GetMainViewport()->GetCenter()).Size;
		ImVec2 minSize = ImGui::GetStyle().WindowMinSize;
		if (flags & ImGuiWindowFlags_AlwaysAutoResize)
			minSize.x = std::min(std::max(minSize.x, ImGui::GetFontSize() * 32.0f), work.x * 0.9f);
		ImGui::SetNextWindowSizeConstraints(minSize, ImVec2(work.x * 0.9f, work.y * 0.85f));
		if (!ImGui::BeginPopupModal(title, open, flags)) return false;
		const ImVec2 position = ImGui::GetWindowPos();
		const ImVec2 size = ImGui::GetWindowSize();
		const WorkArea area = GetWorkArea(ImVec2(position.x + size.x * 0.5f, position.y + size.y * 0.5f));
		const ImVec2 bounded(
			std::clamp(position.x, area.Position.x, area.Position.x + std::max(0.0f, area.Size.x - size.x)),
			std::clamp(position.y, area.Position.y, area.Position.y + std::max(0.0f, area.Size.y - size.y)));
		if (bounded.x != position.x || bounded.y != position.y) ImGui::SetWindowPos(bounded);
		return true;
	}

	// A non-negative footer height makes the body fill a user-resizable dialog.
	// The default keeps content-driven sizing for message/confirmation dialogs.
	inline bool BeginBody(const char* id, float footerHeight = -1.0f) {
		const auto& style = ImGui::GetStyle();
		const bool fillWindow = footerHeight >= 0.0f;
		float bodyHeight = 0.0f;
		if (fillWindow) {
			bodyHeight = std::max(ImGui::GetTextLineHeight(),
				ImGui::GetContentRegionAvail().y - footerHeight - style.ItemSpacing.y);
		}
		else {
			const float footer = ImGui::GetFrameHeight() * 3.0f + style.ItemSpacing.y * 8.0f + style.WindowPadding.y;
			const float workHeight = GetWorkArea(ImGui::GetWindowPos()).Size.y;
			const float height = std::max(ImGui::GetTextLineHeight(), std::min(workHeight * 0.6f,
				workHeight * 0.85f - ImGui::GetCursorPosY() - footer));
			ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, height));
		}
		const bool reset = ImGui::IsWindowAppearing();
		const bool visible = ImGui::BeginChild(id, ImVec2(0.0f, bodyHeight),
			fillWindow ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY,
			ImGuiWindowFlags_NoBackground);
		if (reset) ImGui::SetScrollY(0.0f);
		ImGui::PushTextWrapPos(0.0f);
		++t_BodyDepth;
		return visible;
	}

	inline void EndBody() {
		--t_BodyDepth;
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
	}

	inline float FooterButton(int index, int count, float preferredWidth) {
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		// Begin() latches the window padding. A style override may already have
		// been popped, so use the actual content bounds to avoid growing auto-sized dialogs.
		const float left = ImGui::GetCursorStartPos().x;
		const float available = std::max(1.0f, ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - left);
		const float width = std::min(preferredWidth, available);
		const int columns = std::max(1, static_cast<int>((available + gap) / (width + gap)));
		if (index % columns != 0) ImGui::SameLine();
		else {
			const int rowCount = std::min(columns, count - index);
			ImGui::SetCursorPosX(left + std::max(0.0f, available - width * rowCount - gap * (rowCount - 1)));
		}
		return width;
	}

	inline void NextMessageWindow() {
		const ImVec2 available = GetWorkArea(ImGui::GetMainViewport()->GetCenter()).Size;
		const float width = std::min(ImGui::GetFontSize() * 32.0f, available.x * 0.9f);
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(available.x * 0.9f, available.y * 0.85f));
	}

	inline void Message(const char* id, const char* text) {
		if (!text || !*text) return;
		if (t_BodyDepth > 0) {
			ImGui::TextUnformatted(text);
			return;
		}
		const ImGuiStyle& style = ImGui::GetStyle();
		const float lineHeight = ImGui::GetTextLineHeight();
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float wrapWidth = std::max(1.0f, width - style.ScrollbarSize);
		const float textHeight = ImGui::CalcTextSize(text, nullptr, false, wrapWidth).y;
		const float workHeight = GetWorkArea(ImGui::GetWindowPos()).Size.y;
		const float footerHeight = ImGui::GetFrameHeight() * 3.0f + style.ItemSpacing.y * 8.0f + style.WindowPadding.y * 2.0f;
		const float remainingHeight = workHeight * 0.85f - ImGui::GetCursorPosY() - footerHeight;
		const float maxHeight = std::max(lineHeight, std::min({ lineHeight * 12.0f, workHeight * 0.4f, remainingHeight }));
		if (textHeight <= maxHeight) {
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
			return;
		}

		const bool resetScroll = ImGui::IsWindowAppearing();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool visible = ImGui::BeginChild(id, ImVec2(width, maxHeight), ImGuiChildFlags_None,
			ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoBackground);
		ImGui::PopStyleVar();
		if (visible) {
			if (resetScroll) ImGui::SetScrollY(0.0f);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();
	}

	inline void Error(const char* id, const char* text, const ImVec4& color, const char* copyLabel) {
		if (!text || !*text) return;
		ImGui::PushID(id);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		Message("##Details", text);
		ImGui::PopStyleColor();
		if (ImGui::SmallButton(copyLabel)) ImGui::SetClipboardText(text);
		ImGui::PopID();
	}

}
