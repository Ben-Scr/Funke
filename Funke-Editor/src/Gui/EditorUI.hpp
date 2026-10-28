#pragma once

// Editor UI kit: the shared building blocks every editor panel is composed from, so panels describe
// *what* they show and the look lives in one place. Colours come from EditorTheme; never hardcode them in panels.

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace Funke::EditorUI {

	enum class Tone : uint8_t { Neutral, Accent, Success, Warning, Error, Info };

	// Text-strength colour for a tone (Neutral = disabled text colour).
	ImVec4 ToneColor(Tone tone);

	// ── Buttons ─────────────────────────────────────────────────────

	enum class ButtonVariant : uint8_t {
		Default,  // theme button colours
		Primary,  // accent fill, white label — one per view at most
		Danger,   // destructive actions (remove, delete)
		Ghost,    // no fill until hovered
	};

	bool Button(const char* label, ButtonVariant variant = ButtonVariant::Default, const ImVec2& size = ImVec2(0.0f, 0.0f));

	// Push/pop a variant's button colours for custom buttons (e.g. reference fields) that draw their own label.
	void PushButtonColors(ButtonVariant variant);
	void PopButtonColors(ButtonVariant variant);

	// Square icon button from an EditorIcons name. `active` paints the toggle-on fill.
	// `keepIconColors` keeps a coloured glyph (e.g. log level icons): the state then shows through the glyph
	// alone (full colour when active, dimmed when not) with no toggle fill.
	// size <= 0 uses the frame height. `fallbackLabel` is shown as a text button if the icon fails to load.
	bool IconButton(const char* id, const char* iconName, const char* tooltip = nullptr,
		bool active = false, bool keepIconColors = false, float size = 0.0f, const char* fallbackLabel = nullptr);

	// Text button with the same on/off fill as an active IconButton (e.g. "Gizmos", "Stats").
	bool ToggleButton(const char* label, bool active, const char* tooltip = nullptr);

	// Tooltip for the last item (shown when hovered, also while disabled).
	void Tooltip(const char* text);
	inline void Tooltip(const std::string& text) { Tooltip(text.c_str()); }

	// ── Fields ──────────────────────────────────────────────────────

	// Framed search input with a magnifier inside the frame. width < 0 fills the remaining width.
	bool SearchField(const char* id, char* buffer, std::size_t bufferSize, const char* hint = "Search...", float width = -1.0f);
	bool SearchField(const char* id, std::string& value, const char* hint = "Search...", float width = -1.0f);

	bool CompactSlider(const char* label, float* value, float minimum, float maximum, float step,
		float width, const char* format = "%.0f");

	// Button styled like a search field that opens a search UI elsewhere (e.g. the command palette).
	bool SearchLauncher(const char* id, const char* hint, const char* shortcut, const ImVec2& size);

	// ── Layout ──────────────────────────────────────────────────────

	bool NavigationItem(const char* label, bool selected, const char* iconName = nullptr);
	// Pair bar/item ends only with true returns; tab padding never reaches the selected content.
	bool BeginTabBar(const char* id, ImGuiTabBarFlags flags = 0);
	void EndTabBar();
	bool BeginTabItem(const char* label, bool* open = nullptr, ImGuiTabItemFlags flags = 0);
	void EndTabItem();

	// Call before Begin() of a floating tool window: centers it and sizes it to `screenFraction` of the main
	// viewport at the given aspect ratio each time it appears (user resizes hold while it stays open).
	void NextWindowDefaultSize(float screenFraction = 0.6f, float aspect = 16.0f / 9.0f);

	// Panel toolbar strip: consistent padding, frame-height items laid out with SameLine, hairline below.
	void BeginToolbar(const char* id);
	void EndToolbar();
	// Vertical hairline between toolbar groups; call it between two SameLine items.
	void ToolbarSeparator();

	// Disabled sections keep their expansion state but report no editable body.
	bool SectionHeader(const char* label, bool defaultOpen = true, bool* enabled = nullptr,
		bool* enabledChanged = nullptr, bool enabledMixed = false);
	// Pair EndSection only with a true return; forceOpen leaves the saved expansion state untouched.
	bool BeginSection(const char* label, const char* summary = nullptr, bool defaultOpen = true, bool forceOpen = false);
	void EndSection();

	// Keeps the caller's controls in a label-scoped, right-aligned region; narrow rows stack.
	void BeginSettingsRow(const char* label, float controlWidth = 0.0f);
	void EndSettingsRow();
	bool SettingsSwitch(const char* label, bool* value);
	bool SettingsFloat(const char* label, float* value, const char* format = "%.0f", float step = 0.0f, float fastStep = 0.0f);

	// Rounded card behind a block of content (height tracked from the previous frame). Pairs must nest.
	void BeginCard(const char* id);
	// bottomY < 0 measures to the last submitted item; pass an explicit screen Y to end the card elsewhere.
	void EndCard(float bottomY = -1.0f);

	// ── Text ────────────────────────────────────────────────────────

	// Small rounded pill (tag, layer, status). Sits on the current line; follow with SameLine if needed.
	void Badge(const char* text, Tone tone = Tone::Neutral);

	// Centered, muted placeholder for empty lists/panels.
	void EmptyState(const char* title, const char* hint = nullptr);

	// Wrapped status/validation line in a tone colour.
	void StatusText(Tone tone, const char* text);

	// ── Dialogs ─────────────────────────────────────────────────────

	// Right-aligned modal footer with equal-width buttons. `primary` gets the accent fill, `danger` the
	// destructive style (-1 for none). Bit i of `disabledMask` greys out button i. Returns the clicked index or -1.
	int DialogFooter(std::initializer_list<const char*> labels, int primary = 0, int danger = -1, uint32_t disabledMask = 0);

}
