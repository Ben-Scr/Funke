#include <pch.hpp>
#include "Gui/GlassBackdrop.hpp"
#include "Gui/EditorTheme.hpp"
#include "Editor/EditorPreferences.hpp"

#include "Core/Platform/Win32Titlebar.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Funke::GlassBackdrop {

	namespace {
		void DrawPanelSurface(ImDrawList* dl, ImRect rect, float scale) {
			const bool dark = EditorTheme::IsDark();
			const float radius = 12.0f * scale;
			rect.Expand(-3.0f * scale);
			if (rect.GetWidth() < radius * 2 || rect.GetHeight() < radius * 2) return;
			for (int i = 4; i > 0; --i) {
				const float spread = i * scale;
				dl->AddRect(ImVec2(rect.Min.x - spread, rect.Min.y - spread + scale),
					ImVec2(rect.Max.x + spread, rect.Max.y + spread + scale),
					ImGui::GetColorU32(ImVec4(0, 0, 0, dark ? 0.06f : 0.035f)), radius + spread, 0, scale);
			}
			const int start = dl->VtxBuffer.Size;
			dl->AddRectFilled(rect.Min, rect.Max, IM_COL32_WHITE, radius);
			const ImVec4 top = dark ? ImVec4(0.25f, 0.255f, 0.26f, 1) : ImVec4(0.97f, 0.975f, 0.98f, 1);
			const ImVec4 bottom = dark ? ImVec4(0.12f, 0.125f, 0.13f, 1) : ImVec4(0.77f, 0.785f, 0.80f, 1);
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, start, dl->VtxBuffer.Size,
				rect.Min, ImVec2(rect.Min.x + rect.GetWidth() * 0.18f, rect.Max.y),
				ImGui::ColorConvertFloat4ToU32(top), ImGui::ColorConvertFloat4ToU32(bottom));
			const float lip = 2.0f * scale;
			dl->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(ImVec4(1, 1, 1, dark ? 0.30f : 0.85f)), radius, 0, lip);
			dl->AddRect(ImVec2(rect.Min.x + lip, rect.Min.y + lip), ImVec2(rect.Max.x - lip, rect.Max.y - lip),
				ImGui::GetColorU32(ImVec4(0, 0, 0, dark ? 0.15f : 0.08f)), radius - lip, 0, scale);
		}

		void DrawRim(ImDrawList* dl, const ImRect& rect, float rounding, float response, float press) {
			if (rect.GetWidth() < 4.0f || rect.GetHeight() < 4.0f) return;
			const bool dark = EditorTheme::IsDark();
			const float scale = ImGui::GetFontSize() / 15.0f;
			if (rect.GetHeight() <= ImGui::GetFrameHeight() * 1.5f) {
				const int sheenStart = dl->VtxBuffer.Size;
				dl->AddRectFilled(rect.Min, rect.Max, IM_COL32_WHITE, rounding);
				for (int i = sheenStart; i < dl->VtxBuffer.Size; ++i) {
					ImDrawVert& vertex = dl->VtxBuffer[i];
					const float coverage = (vertex.col >> IM_COL32_A_SHIFT) / 255.0f;
					const float t = std::clamp((vertex.pos.y - rect.Min.y) / rect.GetHeight(), 0.0f, 1.0f);
					const float alpha = (dark ? 0.045f : 0.10f) * (1.0f - t) * (1.0f - press * 0.7f);
					vertex.col = ImGui::GetColorU32(ImVec4(1, 1, 1, alpha * coverage));
				}
			}
			const int first = dl->VtxBuffer.Size;
			dl->AddRect(rect.Min, rect.Max, IM_COL32_WHITE, rounding, 0, std::max(1.0f, scale));
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const float reach = std::max(50.0f * scale, std::min(rect.GetWidth(), 240.0f * scale));
			for (int i = first; i < dl->VtxBuffer.Size; ++i) {
				ImDrawVert& vertex = dl->VtxBuffer[i];
				const float edgeAlpha = (vertex.col >> IM_COL32_A_SHIFT) / 255.0f;
				const float t = std::clamp((vertex.pos.y - rect.Min.y) / rect.GetHeight(), 0.0f, 1.0f);
				float reflection = 0.0f;
				if (response > 0.001f && ImGui::IsMousePosValid(&mouse)) {
					const float dx = (vertex.pos.x - mouse.x) / reach;
					const float dy = (vertex.pos.y - mouse.y) / reach;
					reflection = std::exp(-2.0f * (dx * dx + dy * dy)) * response;
				}
				const float alpha = ((dark ? 0.28f : 0.65f) * (1.0f - t) + 0.08f
					+ reflection * 0.35f + response * 0.08f) * (1.0f - press * 0.40f);
				vertex.col = ImGui::GetColorU32(ImVec4(1, 1, 1, alpha * edgeAlpha));
			}
			const float inset = std::max(1.0f, scale);
			dl->AddRect(ImVec2(rect.Min.x + inset, rect.Min.y + inset),
				ImVec2(rect.Max.x - inset, rect.Max.y - inset),
				ImGui::GetColorU32(ImVec4(0, 0, 0, (dark ? 0.16f : 0.12f) + press * 0.12f)),
				std::max(0.0f, rounding - inset));
		}

	}

	ImVec4 TitlebarColor(bool focused) {
		if (!EditorTheme::IsDark()) {
			return focused ? ImVec4(0.92f, 0.925f, 0.93f, 1) : ImVec4(0.94f, 0.945f, 0.95f, 1);
		}
		return focused ? ImVec4(0.15f, 0.155f, 0.16f, 1) : ImVec4(0.13f, 0.135f, 0.14f, 1);
	}

	void SyncViewportCaptions(bool glass) {
		// Viewport ID -> HWND it was tinted on; a recreated platform window needs the colour again.
		static std::unordered_map<ImGuiID, void*> s_Tinted;
		static bool s_WasDark = true;
		const bool dark = EditorTheme::IsDark();
		if (!glass && s_Tinted.empty()) return;

		const ImVec4 c = TitlebarColor(true);
		const Color caption(c.x, c.y, c.z, glass ? 1.0f : 0.0f);
		const Color none(0.0f, 0.0f, 0.0f, 0.0f);
		const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		const Color captionText(text.x, text.y, text.z, 1.0f);
		const ImGuiViewport* main = ImGui::GetMainViewport();

		std::unordered_map<ImGuiID, void*> live;
		for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
			if (viewport == main || !viewport->PlatformHandleRaw || (viewport->Flags & ImGuiViewportFlags_NoDecoration)) {
				continue;
			}
			const auto it = s_Tinted.find(viewport->ID);
			const bool tinted = it != s_Tinted.end() && it->second == viewport->PlatformHandleRaw;
			if (glass != tinted || (glass && dark != s_WasDark)) {
				Win32Titlebar::SetColors(viewport->PlatformHandleRaw, caption, glass ? captionText : none, caption);
			}
			if (glass) {
				live.emplace(viewport->ID, viewport->PlatformHandleRaw);
			}
		}
		s_Tinted = std::move(live);
		s_WasDark = dark;
	}

	void Draw() {
		ImGuiViewport* main = ImGui::GetMainViewport();
		if (!main || main->Size.x <= 0.0f || main->Size.y <= 0.0f || (main->Flags & ImGuiViewportFlags_IsMinimized)) {
			return;
		}

		ImDrawList* dl = ImGui::GetBackgroundDrawList(main);
		const bool dark = EditorTheme::IsDark();
		const ImVec2 min = main->Pos;
		const ImVec2 max(min.x + main->Size.x, min.y + main->Size.y);
		dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(
			dark ? ImVec4(0.075f, 0.08f, 0.085f, 1) : ImVec4(0.72f, 0.735f, 0.75f, 1)));

	}

	bool IsEnabled() {
		const EditorThemeMode mode = EditorPreferences::GetThemeMode();
		return mode == EditorThemeMode::Glass || mode == EditorThemeMode::GlassLight;
	}

	void DrawWindowEdges() {
		if (!IsEnabled()) return;
		ImGuiContext& context = *ImGui::GetCurrentContext();
		for (ImGuiWindow* window : context.Windows) {
			// ImGui also marks docked panels as child windows.
			if (!window->Active || window->Hidden || window->IsFallbackWindow || window->DockNodeAsHost
				|| ((window->Flags & ImGuiWindowFlags_ChildWindow) && !window->DockIsActive)
				|| (window->Flags & (ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_Tooltip))) continue;
			const ImRect rect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y));
			if (window->DockIsActive && window->Viewport == ImGui::GetMainViewport()) {
				ImDrawList* background = ImGui::GetBackgroundDrawList(window->Viewport);
				background->PushClipRect(rect.Min, rect.Max, true);
				DrawPanelSurface(background, rect, ImGui::GetStyle().FontScaleDpi);
				background->PopClipRect();
				continue;
			}
			window->DrawList->PushClipRect(rect.Min, rect.Max, false);
			DrawRim(window->DrawList, ImRect(ImVec2(rect.Min.x + 0.5f, rect.Min.y + 0.5f),
				ImVec2(rect.Max.x - 0.5f, rect.Max.y - 0.5f)), window->WindowRounding,
				0.0f, 0.0f);
			window->DrawList->PopClipRect();
		}
	}

	void DrawLastItemSurface(ImVec2 min, ImVec2 max) {
		if (!IsEnabled() || !ImGui::IsItemVisible()) return;
		const ImGuiContext& context = *ImGui::GetCurrentContext();
		const ImGuiID id = ImGui::GetItemID();
		if (!id) return;
		const bool disabled = (context.LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
		const bool hovered = !disabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
		const bool held = !disabled && ImGui::IsItemActive();
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID hoverId = ImHashStr("##GlassHover", 0, id);
		const ImGuiID pressId = ImHashStr("##GlassPress", 0, id);
		const ImGuiID frameId = ImHashStr("##GlassFrame", 0, id);
		const bool continuous = storage->GetInt(frameId, -2) == ImGui::GetFrameCount() - 1;
		const float ease = 1.0f - std::exp(-std::min(ImGui::GetIO().DeltaTime, 0.1f) * 16.0f);
		float response = continuous ? storage->GetFloat(hoverId) : 0.0f;
		float press = continuous ? storage->GetFloat(pressId) : 0.0f;
		response += ((hovered ? 1.0f : 0.0f) - response) * ease;
		press += ((held ? 1.0f : 0.0f) - press) * ease;
		storage->SetFloat(hoverId, response);
		storage->SetFloat(pressId, press);
		storage->SetInt(frameId, ImGui::GetFrameCount());
		DrawRim(ImGui::GetWindowDrawList(), ImRect(min, max),
			ImGui::GetStyle().FrameRounding, response, press);
	}

	void DrawLastItemSurface() {
		DrawLastItemSurface(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	}

}
