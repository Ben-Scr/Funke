#pragma once

#include "Core/PlatformDetection.hpp"

#include <imgui.h>

namespace Funke::EditorShortcuts {

	// ImGui maps physical Command to ImGuiMod_Ctrl when macOS behavior is enabled.
#ifdef FNK_PLATFORM_MACOS
	inline constexpr ImGuiKeyChord k_Rename = ImGuiKey_Enter;
	inline constexpr ImGuiKeyChord k_Delete = ImGuiMod_Ctrl | ImGuiKey_Backspace;
	inline constexpr ImGuiKeyChord k_OpenAsset = ImGuiMod_Ctrl | ImGuiKey_DownArrow;
	inline constexpr ImGuiKeyChord k_NavigateUp = ImGuiMod_Ctrl | ImGuiKey_UpArrow;
	inline constexpr ImGuiKeyChord k_Profiler = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P;
	inline constexpr const char* k_RenameLabel = "Return";
	inline constexpr const char* k_DeleteLabel = "Cmd+Backspace";
	inline constexpr const char* k_OpenAssetLabel = "Cmd+Down";
	inline constexpr const char* k_NavigateUpLabel = "Cmd+Up";
	inline constexpr const char* k_ProfilerLabel = "Cmd+Shift+P";
	inline constexpr const char* k_SaveLabel = "Cmd+S";
	inline constexpr const char* k_CopyLabel = "Cmd+C";
	inline constexpr const char* k_CutLabel = "Cmd+X";
	inline constexpr const char* k_PasteLabel = "Cmd+V";
	inline constexpr const char* k_DuplicateLabel = "Cmd+D";
	inline constexpr const char* k_UndoLabel = "Cmd+Z";
	inline constexpr const char* k_RedoLabel = "Cmd+Shift+Z";
	inline constexpr const char* k_CommandPaletteLabel = "Cmd+K";
#else
	inline constexpr ImGuiKeyChord k_Rename = ImGuiKey_F2;
	inline constexpr ImGuiKeyChord k_Delete = ImGuiKey_Delete;
	inline constexpr ImGuiKeyChord k_OpenAsset = ImGuiKey_Enter;
	inline constexpr ImGuiKeyChord k_NavigateUp = ImGuiKey_Backspace;
	inline constexpr ImGuiKeyChord k_Profiler = ImGuiMod_Ctrl | ImGuiKey_F6;
	inline constexpr const char* k_RenameLabel = "F2";
	inline constexpr const char* k_DeleteLabel = "Delete";
	inline constexpr const char* k_OpenAssetLabel = "Enter";
	inline constexpr const char* k_NavigateUpLabel = "Backspace";
	inline constexpr const char* k_ProfilerLabel = "Ctrl+F6";
	inline constexpr const char* k_SaveLabel = "Ctrl+S";
	inline constexpr const char* k_CopyLabel = "Ctrl+C";
	inline constexpr const char* k_CutLabel = "Ctrl+X";
	inline constexpr const char* k_PasteLabel = "Ctrl+V";
	inline constexpr const char* k_DuplicateLabel = "Ctrl+D";
	inline constexpr const char* k_UndoLabel = "Ctrl+Z";
	inline constexpr const char* k_RedoLabel = "Ctrl+Y";
	inline constexpr const char* k_CommandPaletteLabel = "Ctrl+K";
#endif

	inline bool IsDeletePressed() {
		return ImGui::IsKeyChordPressed(k_Delete)
			|| ImGui::IsKeyChordPressed(ImGuiKey_Delete)
			|| ImGui::IsKeyChordPressed(ImGuiKey_KeypadDecimal);
	}

	inline bool IsRenamePressed() {
#ifdef FNK_PLATFORM_MACOS
		if (ImGui::IsKeyChordPressed(ImGuiKey_KeypadEnter)) return true;
#endif
		return ImGui::IsKeyChordPressed(k_Rename);
	}

	inline bool IsOpenAssetPressed() {
#ifndef FNK_PLATFORM_MACOS
		if (ImGui::IsKeyChordPressed(ImGuiKey_KeypadEnter)) return true;
#endif
		return ImGui::IsKeyChordPressed(k_OpenAsset);
	}
}
