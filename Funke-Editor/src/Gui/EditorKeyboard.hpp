#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace Funke::EditorKeyboard {
	inline void ReserveGameNavigation() {
		auto& g = *GImGui;
		auto* window = ImGui::GetCurrentWindow();
		window->Flags |= ImGuiWindowFlags_NoNavInputs;
		if (g.NavWindow != window) return;
		// Cancel requests already queued before the game window acquired focus this frame.
		ImGui::NavMoveRequestCancel();
		g.NavInitRequest = false;
		g.NavInitResult = {};
		g.NavCursorVisible = false;
	}

	namespace {
		inline void FocusSliderOnTab(ImGuiID id) {
			auto& g = *GImGui;
			if (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_FromTabbing))
				g.NavActivateFlags &= ~ImGuiActivateFlags_PreferInput;
		}

		inline int SliderKeyboardDirection(ImGuiID id) {
			auto& g = *GImGui;
			const auto& io = g.IO;
			if (g.NavId != id || g.NavWindow != ImGui::GetCurrentWindow() || io.WantTextInput
				|| io.KeyCtrl || io.KeyAlt || io.KeySuper || io.KeyShift
				|| (g.CurrentItemFlags & (ImGuiItemFlags_Disabled | static_cast<ImGuiItemFlags>(ImGuiItemFlags_ReadOnly))) || ImGui::TempInputIsActive(id)
				|| (g.ActiveId != 0 && (g.ActiveId != id || g.ActiveIdSource == ImGuiInputSource_Mouse))) return 0;
			int direction = 0;
			bool activate = false;
			for (ImGuiKey key : { ImGuiKey_A, ImGuiKey_D, ImGuiKey_LeftArrow, ImGuiKey_RightArrow }) {
				ImGui::SetKeyOwner(key, id);
				if (ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat | ImGuiInputFlags_RepeatRateNavTweak, id)) {
					activate = true;
					direction += (key == ImGuiKey_A || key == ImGuiKey_LeftArrow) ? -1 : 1;
				}
			}
			if (activate) {
				ImGui::NavMoveRequestCancel();
				ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
				g.ActiveIdSource = ImGuiInputSource_Keyboard;
				g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
				g.NavCursorVisible = true;
			}
			return direction;
		}
		struct SuppressNativeSliderTweak {
			ImGuiID Id;
			bool Suppressed;
			explicit SuppressNativeSliderTweak(ImGuiID id) : Id(id) {
				Suppressed = GImGui->ActiveId == id && GImGui->ActiveIdSource == ImGuiInputSource_Keyboard
					&& !ImGui::TempInputIsActive(id) && GImGui->NavActivateId != id
					&& (ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_RightArrow)
						|| ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_D));
				if (Suppressed) GImGui->ActiveIdSource = ImGuiInputSource_None;
			}
			~SuppressNativeSliderTweak() {
				if (Suppressed && GImGui->ActiveId == Id && GImGui->ActiveIdSource == ImGuiInputSource_None)
					GImGui->ActiveIdSource = ImGuiInputSource_Keyboard;
			}
		};

	}

	inline bool SliderFloat(const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags) {
		const ImGuiID id = ImGui::GetID(label);
		FocusSliderOnTab(id);
		const int direction = SliderKeyboardDirection(id);
		const float previous = *value;
		if (direction) {
			const int precision = ImParseFormatPrecision(format, 3);
			const float quantum = std::pow(10.0f, -static_cast<float>(precision));
			const float step = std::max(quantum, std::round((maximum - minimum) / (100.0f * quantum)) * quantum);
			*value = std::clamp(*value + direction * step, minimum, maximum);
		}
		SuppressNativeSliderTweak suppress(id);
		const bool changed = ImGui::SliderFloat(label, value, minimum, maximum, format, flags);
		if (*value != previous) ImGui::MarkItemEdited(id);
		return changed || *value != previous;
	}

	inline bool SliderInt(const char* label, int* value, int minimum, int maximum, const char* format, ImGuiSliderFlags flags) {
		const ImGuiID id = ImGui::GetID(label);
		FocusSliderOnTab(id);
		const int direction = SliderKeyboardDirection(id);
		const int previous = *value;
		if (direction) *value = static_cast<int>(std::clamp(static_cast<long long>(*value) + direction,
			static_cast<long long>(minimum), static_cast<long long>(maximum)));
		SuppressNativeSliderTweak suppress(id);
		const bool changed = ImGui::SliderInt(label, value, minimum, maximum, format, flags);
		if (*value != previous) ImGui::MarkItemEdited(id);
		return changed || *value != previous;
	}

	inline bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0) {
		if (!ImGui::BeginCombo(label, preview, flags)) return false;
		auto& g = *GImGui;
		const auto& io = g.IO;
		if (g.NavWindow == ImGui::GetCurrentWindow() && !io.WantTextInput && !io.KeyCtrl
			&& !io.KeyAlt && !io.KeySuper && !io.KeyShift && !g.NavMoveSubmitted) {
			for (ImGuiKey key : { ImGuiKey_W, ImGuiKey_A }) {
				ImGui::SetKeyOwner(key, g.CurrentWindow->ID);
				if (ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat, g.CurrentWindow->ID)) {
					const ImGuiDir direction = key == ImGuiKey_W ? ImGuiDir_Up : ImGuiDir_Down;
					ImGui::NavMoveRequestSubmit(direction, direction, ImGuiNavMoveFlags_None, ImGuiScrollFlags_KeepVisibleEdgeY);
					break;
				}
			}
		}
		return true;
	}

	inline void EndCombo() {
		const bool focused = GImGui->NavWindow == ImGui::GetCurrentWindow();
		ImGui::EndCombo();
		if (focused) GImGui->NavMoveFlags &= ~ImGuiNavMoveFlags_WrapMask_;
	}
}
