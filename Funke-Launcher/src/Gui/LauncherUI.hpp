#pragma once

#include "Gui/Icons.hpp"
#include <imgui.h>
#include <initializer_list>
#include <string>
#include <string_view>

namespace Funke {
	class Texture2D;
	struct LauncherProjectEntry;
}

namespace Funke::LauncherUI {
	struct LauncherPalette {
		ImU32 LogoTint;
		ImU32 Sidebar;
		ImU32 SidebarBorder;
		ImU32 NavHover;
		ImU32 Card;
		ImU32 CardHover;
		ImU32 CardSelected;
		ImU32 CardBorder;
		ImU32 Chip;
		ImU32 Accent;
		ImU32 AccentHover;
		ImU32 AccentSoft;
		ImU32 AccentText;
		ImU32 TextDim;
		ImVec4 Error;
	};

	LauncherPalette GetLauncherPalette();
	float BaseFontSize();
	float IconButtonWidth(const std::string& label);
	bool AccentButtonWithIcon(Icons::Type icon, const std::string& label, ImVec2 size);
	bool MoreButton(const char* id, float size);
	ImU32 ProjectThumbColor(std::string_view name);
	std::string ProjectInitials(std::string_view name);
	float DrawChipRightAligned(ImDrawList* dl, float right, float centerY, const char* text, const LauncherPalette& pal);
	bool SidebarNavItem(const char* id, Icons::Type icon, const std::string& label, bool active, const LauncherPalette& pal);
	bool ViewModeButton(const char* id, bool grid, bool active, const LauncherPalette& pal);
	void DialogTitle(const std::string& title, const std::string& subtitle = {});
	void FormSection(const std::string& title);
	void BeginFormRow(const std::string& label, const std::string& description = {}, bool responsive = false);
	void EndFormRow();
	void BeginSettingsSection(const char* id, const std::string& title);
	bool ToggleSwitch(const char* id, bool* value, const LauncherPalette& pal);
	void BeginFormBody(const char* id, std::initializer_list<std::string> footerLabels);
	int DialogFooter(std::initializer_list<std::string> labels, int primary, bool primaryEnabled = true);
	void CenteredText(const char* text, ImU32 color);
	float PageHeader(const std::string& title, const std::string& subtitle);
	void DrawProjectThumbnail(ImDrawList* dl, ImVec2 min, ImVec2 max, const LauncherProjectEntry& entry, const Texture2D* thumbnail, float rounding, float initialsScale, ImDrawFlags corners = ImDrawFlags_RoundCornersAll);
	std::string PickFolder(const std::string& title);
}
