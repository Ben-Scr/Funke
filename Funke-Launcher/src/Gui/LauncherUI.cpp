#include "Gui/LauncherUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiFonts.hpp"
#include "Graphics/Texture2D.hpp"
#include "Project/LauncherRegistry.hpp"
#include "Core/Platform.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>

namespace Funke::LauncherUI {
	static bool IsLauncherStyleDark() {
		const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		return (0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z) < 0.5f;
	}

	LauncherPalette GetLauncherPalette() {
		const bool dark = IsLauncherStyleDark();
		const ImVec4 accent = dark ? ImVec4(0.357f, 0.553f, 0.937f, 1.0f) : ImVec4(0.184f, 0.420f, 0.859f, 1.0f);
		LauncherPalette p{};
		p.LogoTint      = dark ? IM_COL32_WHITE : IM_COL32_BLACK;
		p.Sidebar       = dark ? IM_COL32(22, 23, 26, 255)   : IM_COL32(236, 238, 242, 255);
		p.SidebarBorder = dark ? IM_COL32(255, 255, 255, 14) : IM_COL32(0, 0, 0, 20);
		p.NavHover      = dark ? IM_COL32(255, 255, 255, 12) : IM_COL32(0, 0, 0, 12);
		p.Card          = dark ? IM_COL32(34, 36, 41, 255)   : IM_COL32(255, 255, 255, 255);
		p.CardHover     = dark ? IM_COL32(41, 43, 49, 255)   : IM_COL32(248, 249, 252, 255);
		p.CardSelected  = dark ? IM_COL32(36, 44, 62, 255)   : IM_COL32(234, 240, 253, 255);
		p.CardBorder    = dark ? IM_COL32(255, 255, 255, 16) : IM_COL32(0, 0, 0, 22);
		p.Chip          = dark ? IM_COL32(255, 255, 255, 16) : IM_COL32(0, 0, 0, 13);
		p.Accent        = ImGui::GetColorU32(accent);
		p.AccentHover   = ImGui::GetColorU32(ImVec4(
			std::min(accent.x + 0.08f, 1.0f), std::min(accent.y + 0.08f, 1.0f), std::min(accent.z + 0.06f, 1.0f), 1.0f));
		p.AccentSoft    = ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, dark ? 0.16f : 0.12f));
		p.AccentText    = dark ? IM_COL32(157, 188, 247, 255) : ImGui::GetColorU32(accent);
		p.TextDim       = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		p.Error = dark ? ImVec4(0.949f, 0.420f, 0.400f, 1.0f) : ImVec4(0.769f, 0.200f, 0.220f, 1.0f);
		return p;
	}

	// Unscaled base size: PushFont(nullptr, size) applies global scale itself, so GetFontSize() would double-scale.
	float BaseFontSize() {
		const float base = ImGui::GetStyle().FontSizeBase;
		return base > 0.0f ? base : k_FunkeImGuiFontSize;
	}

	float IconButtonWidth(const std::string& label) {
		const ImGuiStyle& style = ImGui::GetStyle();
		return ImGui::GetFontSize() + style.ItemInnerSpacing.x
			+ ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f
			+ ImGui::GetFontSize() * 0.6f;
	}

	bool AccentButtonWithIcon(Icons::Type icon, const std::string& label, ImVec2 size) {
		const LauncherPalette pal = GetLauncherPalette();
		ImGui::PushStyleColor(ImGuiCol_Button, pal.Accent);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.AccentHover);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal.Accent);
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
		const bool clicked = Icons::ButtonWithIcon(icon, label.c_str(), size, true);
		ImGui::PopStyleColor(4);
		return clicked;
	}

	// Square button with a "⋯" glyph; there is no bundled dots icon.
	bool MoreButton(const char* id, float size) {
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::Button(id, ImVec2(size, size));
		const ImVec2 c(p0.x + size * 0.5f, p0.y + size * 0.5f);
		const float r = std::max(1.5f, size * 0.06f);
		const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		for (int k = -1; k <= 1; ++k) {
			dl->AddCircleFilled(ImVec2(c.x + k * size * 0.22f, c.y), r, col);
		}
		return clicked;
	}

	ImU32 ProjectThumbColor(std::string_view name) {
		static constexpr ImU32 k_Colors[] = {
			IM_COL32(127, 119, 221, 255), IM_COL32(29, 158, 117, 255), IM_COL32(216, 90, 48, 255),
			IM_COL32(212, 83, 126, 255),  IM_COL32(55, 138, 221, 255), IM_COL32(99, 153, 34, 255),
			IM_COL32(186, 117, 23, 255),  IM_COL32(83, 74, 183, 255),
		};
		const size_t h = std::hash<std::string_view>{}(name);
		return k_Colors[h % (sizeof(k_Colors) / sizeof(k_Colors[0]))];
	}

	// "DungeonSwiper" -> "DS", "Test Game" -> "TG", "Test54" -> "T5".
	std::string ProjectInitials(std::string_view name) {
		std::string out;
		auto pushChar = [&](size_t i) -> size_t {
			const unsigned char c = static_cast<unsigned char>(name[i]);
			size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
			len = std::min(len, name.size() - i);
			if (len == 1) out += static_cast<char>(std::toupper(c));
			else out.append(name.substr(i, len));
			return len;
		};

		size_t i = 0;
		while (i < name.size() && static_cast<unsigned char>(name[i]) < 0x80
			&& !std::isalnum(static_cast<unsigned char>(name[i]))) {
			++i;
		}
		if (i >= name.size()) return "?";

		const size_t firstLen = pushChar(i);
		for (size_t j = i + firstLen; j < name.size(); ++j) {
			const unsigned char c = static_cast<unsigned char>(name[j]);
			const unsigned char prev = static_cast<unsigned char>(name[j - 1]);
			if (c >= 0x80 || prev >= 0x80) continue;
			const bool wordStart = !std::isalnum(prev) && std::isalnum(c);
			const bool camel = std::isupper(c) != 0;
			const bool digitRun = std::isdigit(c) && !std::isdigit(prev);
			if (wordStart || camel || digitRun) {
				pushChar(j);
				break;
			}
		}
		return out;
	}

	// Pill anchored at its right edge; returns its left x.
	float DrawChipRightAligned(ImDrawList* dl, float right, float centerY, const char* text, const LauncherPalette& pal) {
		const float em = ImGui::GetFontSize();
		const ImVec2 ts = ImGui::CalcTextSize(text);
		const float h = ts.y + em * 0.4f;
		const float w = ts.x + em * 1.1f;
		const ImVec2 a(right - w, centerY - h * 0.5f);
		const ImVec2 b(right, centerY + h * 0.5f);
		dl->AddRectFilled(a, b, pal.Chip, h * 0.5f);
		dl->AddText(ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), pal.TextDim, text);
		return a.x;
	}

	bool SidebarNavItem(const char* id, Icons::Type icon, const std::string& label, bool active, const LauncherPalette& pal) {
		const float em = ImGui::GetFontSize();
		const float h = std::round(em * 2.3f);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const float w = ImGui::GetContentRegionAvail().x;
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 p1(p0.x + w, p0.y + h);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float rounding = em * 0.45f;
		if (active) {
			dl->AddRectFilled(p0, p1, pal.AccentSoft, rounding);
			dl->AddRectFilled(ImVec2(p0.x, p0.y + h * 0.28f), ImVec2(p0.x + 3.0f, p1.y - h * 0.28f), pal.Accent, 1.5f);
		}
		else if (hovered) {
			dl->AddRectFilled(p0, p1, pal.NavHover, rounding);
		}

		const ImU32 col = active ? pal.AccentText
			: ImGui::GetColorU32(ImGuiCol_Text, hovered ? 1.0f : 0.72f);
		const float iconSize = em * 1.05f;
		const float iconX = p0.x + em * 0.8f;
		Icons::Draw(dl, icon, ImVec2(iconX, p0.y + (h - iconSize) * 0.5f), iconSize, col);
		dl->AddText(ImVec2(iconX + iconSize + em * 0.7f, p0.y + (h - em) * 0.5f), col, label.c_str());
		return clicked;
	}

	// Segmented toggle glyphs: three rows for list, a 2x2 grid for grid.
	bool ViewModeButton(const char* id, bool grid, bool active, const LauncherPalette& pal) {
		const float size = ImGui::GetFrameHeight();
		if (active) {
			ImGui::PushStyleColor(ImGuiCol_Button, pal.AccentSoft);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.AccentSoft);
		}
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::Button(id, ImVec2(size, size));
		if (active) ImGui::PopStyleColor(2);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 col = active ? pal.AccentText : ImGui::GetColorU32(ImGuiCol_Text);
		const float inset = std::round(size * 0.3f);
		const ImVec2 a(p0.x + inset, p0.y + inset);
		const ImVec2 b(p0.x + size - inset, p0.y + size - inset);
		if (grid) {
			const float gap = std::max(1.0f, std::round(size * 0.08f));
			const float cx = std::round((a.x + b.x) * 0.5f);
			const float cy = std::round((a.y + b.y) * 0.5f);
			dl->AddRectFilled(a, ImVec2(cx - gap * 0.5f, cy - gap * 0.5f), col, 1.0f);
			dl->AddRectFilled(ImVec2(cx + gap * 0.5f, a.y), ImVec2(b.x, cy - gap * 0.5f), col, 1.0f);
			dl->AddRectFilled(ImVec2(a.x, cy + gap * 0.5f), ImVec2(cx - gap * 0.5f, b.y), col, 1.0f);
			dl->AddRectFilled(ImVec2(cx + gap * 0.5f, cy + gap * 0.5f), b, col, 1.0f);
		}
		else {
			const float h = b.y - a.y;
			for (int r = 0; r < 3; ++r) {
				const float y = std::round(a.y + h * (0.1f + 0.4f * r));
				dl->AddRectFilled(ImVec2(a.x, y), ImVec2(b.x, y + std::max(1.5f, h * 0.16f)), col, 1.0f);
			}
		}
		return clicked;
	}


	// Large dialog title (the OS caption already names the window; this is the in-content heading).
	void DialogTitle(const std::string& title, const std::string& subtitle) {
		ImGui::PushFont(nullptr, BaseFontSize() * 1.35f);
		ImGui::TextUnformatted(title.c_str());
		ImGui::PopFont();
		if (!subtitle.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			ImGuiDialog::Message("##Subtitle", subtitle.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.4f));
	}

	// Small muted group heading with a hairline underneath.
	void FormSection(const std::string& title) {
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.5f));
		ImGui::PushFont(nullptr, BaseFontSize() * 0.92f);
		ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().AccentText);
		ImGui::TextUnformatted(title.c_str());
		ImGui::PopStyleColor();
		ImGui::PopFont();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
			ImGui::GetColorU32(ImGuiCol_Border));
		ImGui::Dummy(ImVec2(0.0f, 4.0f));
	}

	struct FormRowState { float StartX = 0.0f; float LabelBottom = 0.0f; };
	static FormRowState s_FormRow;

	// Label (+ optional muted description) in the left column; leaves the cursor in the right column
	// with the item width set to fill it. Close with EndFormRow().
	void BeginFormRow(const std::string& label, const std::string& description, bool responsive) {
		const float avail = ImGui::GetContentRegionAvail().x;
		const bool stacked = responsive && avail < ImGui::GetFontSize() * 36.0f;
		const float labelW = stacked ? avail : std::round(avail * 0.40f);
		const float startY = ImGui::GetCursorPosY();
		s_FormRow.StartX = ImGui::GetCursorPosX();

		ImGui::BeginGroup();
		ImGui::AlignTextToFramePadding();
		if (responsive) ImGui::PushTextWrapPos(s_FormRow.StartX + labelW - ImGui::GetFontSize());
		ImGui::TextUnformatted(label.c_str());
		if (responsive) ImGui::PopTextWrapPos();
		if (!description.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, GetLauncherPalette().TextDim);
			ImGui::PushTextWrapPos(s_FormRow.StartX + labelW - ImGui::GetFontSize());
			ImGui::TextWrapped("%s", description.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
		ImGui::EndGroup();
		s_FormRow.LabelBottom = ImGui::GetCursorPosY();

		ImGui::SetCursorPos(ImVec2(s_FormRow.StartX + (stacked ? 0.0f : labelW),
			stacked ? s_FormRow.LabelBottom : startY));
		ImGui::SetNextItemWidth(stacked ? avail : avail - labelW);
	}

	void EndFormRow() {
		const float y = std::max(ImGui::GetCursorPosY(), s_FormRow.LabelBottom);
		ImGui::SetCursorPos(ImVec2(s_FormRow.StartX, y));
		ImGui::Dummy(ImVec2(0.0f, 2.0f));
	}

	void BeginSettingsSection(const char* id, const std::string& title) {
		const float em = ImGui::GetFontSize();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, GetLauncherPalette().Card);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em, em * 0.8f));
		ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::TextUnformatted(title.c_str());
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, em * 0.3f));
	}

	// Animated on/off switch in the launcher accent.
	bool ToggleSwitch(const char* id, bool* value, const LauncherPalette& pal) {
		const float frameH = ImGui::GetFrameHeight();
		const float h = std::round(frameH * 0.62f);
		const float w = std::round(h * 1.8f);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, frameH));
		if (clicked) *value = !*value;

		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID animId = ImGui::GetItemID();
		const float target = *value ? 1.0f : 0.0f;
		float t = storage->GetFloat(animId, target);
		t += (target - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
		if (std::abs(target - t) < 0.01f) t = target;
		storage->SetFloat(animId, t);

		const ImVec4 off = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive);
		const ImVec4 on = ImGui::ColorConvertU32ToFloat4(pal.Accent);
		const ImVec4 track(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t, 1.0f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 a(p.x, p.y + (frameH - h) * 0.5f);
		dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), ImGui::GetColorU32(track), h * 0.5f);
		dl->AddCircleFilled(ImVec2(a.x + h * 0.5f + t * (w - h), a.y + h * 0.5f), h * 0.5f - 2.0f, IM_COL32_WHITE);
		return clicked;
	}

	static float DialogButtonWidth(std::initializer_list<std::string> labels) {
		float buttonW = ImGui::GetFontSize() * 6.5f;
		for (const std::string& label : labels) {
			buttonW = std::max(buttonW, IconButtonWidth(label));
		}
		return buttonW;
	}

	static float DialogButtonHeight() {
		return std::round(ImGui::GetFrameHeight() + ImGui::GetFontSize() * 0.3f);
	}

	void BeginFormBody(const char* id, std::initializer_list<std::string> footerLabels) {
		const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
		const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float width = std::min(DialogButtonWidth(footerLabels), available);
		const int columns = std::max(1, static_cast<int>((available + spacing.x) / (width + spacing.x)));
		const int rows = (static_cast<int>(footerLabels.size()) + columns - 1) / columns;
		// Two spacer items around the separator, then one or more button rows.
		const float footerHeight = ImGui::GetFontSize() * 0.8f + spacing.y * 2.0f
			+ rows * DialogButtonHeight() + std::max(0, rows - 1) * spacing.y;
		ImGuiDialog::BeginBody(id, footerHeight);
	}

	// Right-aligned equal-width footer buttons; `primary` gets the accent fill. Returns the clicked index or -1.
	int DialogFooter(std::initializer_list<std::string> labels, int primary, bool primaryEnabled) {
		const float buttonW = DialogButtonWidth(labels);
		const int count = static_cast<int>(labels.size());
		const float buttonH = DialogButtonHeight();

		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.4f));
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
			ImGui::GetColorU32(ImGuiCol_Border));
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.4f));

		const LauncherPalette pal = GetLauncherPalette();
		int clicked = -1;
		int index = 0;
		for (const std::string& label : labels) {
			const float width = ImGuiDialog::FooterButton(index, count, buttonW);
			const bool isPrimary = index == primary;
			if (isPrimary) {
				ImGui::PushStyleColor(ImGuiCol_Button, pal.Accent);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.AccentHover);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal.Accent);
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
				if (!primaryEnabled) ImGui::BeginDisabled();
			}
			if (ImGui::Button(label.c_str(), ImVec2(width, buttonH))) clicked = index;
			if (isPrimary) {
				if (!primaryEnabled) ImGui::EndDisabled();
				ImGui::PopStyleColor(4);
			}
			++index;
		}
		return clicked;
	}

	void CenteredText(const char* text, ImU32 color) {
		const float avail = ImGui::GetContentRegionAvail().x;
		const float textW = ImGui::CalcTextSize(text).x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - textW) * 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
	}

	// Big title + dim subtitle on the left; returns the header height for aligning right-side buttons.
	float PageHeader(const std::string& title, const std::string& subtitle) {
		const float top = ImGui::GetCursorPosY();
		ImGui::PushFont(nullptr, BaseFontSize() * 1.6f);
		ImGui::TextUnformatted(title.c_str());
		ImGui::PopFont();
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", subtitle.c_str());
		ImGui::PopStyleColor();
		return ImGui::GetCursorPosY() - top - ImGui::GetStyle().ItemSpacing.y;
	}
	// Fills [min,max] with the project's preview image (cropped to cover) or, without one, a colored tile with initials.
	void DrawProjectThumbnail(ImDrawList* dl, ImVec2 min, ImVec2 max, const LauncherProjectEntry& entry,
		const Texture2D* thumbnail, float rounding, float initialsScale, ImDrawFlags corners)
	{
		const float boxW = max.x - min.x;
		const float boxH = max.y - min.y;
		if (thumbnail && thumbnail->IsValid() && thumbnail->GetHeight() > 0.0f && boxH > 0.0f) {
			const float imageAspect = thumbnail->GetWidth() / thumbnail->GetHeight();
			const float boxAspect = boxW / boxH;
			const ImTextureID textureId = static_cast<ImTextureID>(static_cast<intptr_t>(thumbnail->GetHandle()));
			// A portrait capture cropped into a landscape box would show only a thin middle band; pillarbox it like the Game View.
			if (boxAspect > imageAspect * 1.5f) {
				dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 255), rounding, corners);
				const float imageW = boxH * imageAspect;
				const float x0 = min.x + (boxW - imageW) * 0.5f;
				dl->AddImage(textureId, ImVec2(x0, min.y), ImVec2(x0 + imageW, max.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
				return;
			}
			float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
			if (boxAspect > imageAspect) {
				const float visible = imageAspect / boxAspect;
				v0 = (1.0f - visible) * 0.5f;
				v1 = 1.0f - v0;
			}
			else {
				const float visible = boxAspect / imageAspect;
				u0 = (1.0f - visible) * 0.5f;
				u1 = 1.0f - u0;
			}
			// Texture2D loads flipped (GL convention), so V runs bottom-up.
			dl->AddImageRounded(textureId, min, max, ImVec2(u0, 1.0f - v0), ImVec2(u1, 1.0f - v1), IM_COL32_WHITE, rounding, corners);
			return;
		}

		dl->AddRectFilled(min, max, ProjectThumbColor(entry.Name), rounding, corners);
		const std::string initials = ProjectInitials(entry.Name);
		ImGui::PushFont(nullptr, BaseFontSize() * initialsScale);
		const ImVec2 isz = ImGui::CalcTextSize(initials.c_str());
		dl->AddText(ImVec2(min.x + (boxW - isz.x) * 0.5f, min.y + (boxH - isz.y) * 0.5f),
			IM_COL32_WHITE, initials.c_str());
		ImGui::PopFont();
	}
	// Native folder dialog; returns an empty string when cancelled.
	std::string PickFolder(const std::string& title) {
		const std::optional<std::filesystem::path> picked = Platform::PickFolder({}, title);
		return picked ? Path::ToUtf8(*picked) : std::string();
	}

	// ── Relative Time Formatting ────────────────────────────────────
}
