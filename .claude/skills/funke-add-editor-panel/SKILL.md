---
name: funke-add-editor-panel
description: "Add or change a Funke Editor ImGui panel, including window lifecycle, menus, command palette entries and background work."
---

# Add an Editor Panel

Editor panels are plain classes in `Funke-Editor/src/Gui/`, owned by value in `ImGuiEditorLayer` and drawn from its `OnPreRender`. Reference panels:

| Panel | Shape worth copying |
|---|---|
| `Gui/ProfilerPanel.{hpp,cpp}` | Pure view over engine state; `Initialize/Render(bool*)/Shutdown`; compiled stub when `FUNKE_PROFILER_ENABLED` is off |
| `Gui/BuildProfilesPanel.{hpp,cpp}` | Lazy-initialized on first open; dirty-flag rescans; status strip; deferred popup opens |
| `Gui/EditorPreferencesPanel.{hpp,cpp}` | First-frame-open hook (`m_WasOpenLastFrame`); deferred modal opens (`m_OpenSaveLayoutRequest`/`m_OpenDeleteLayoutRequest`); search-filtered sections (`PreferenceSectionVisible`) |
| `Gui/SpriteEditorPanel.{hpp,cpp}` | Opened with data (`OpenTexture`), result handed back (`TakeSlicesApplied()`) |
| `Gui/PackageManagerPanel.{hpp,cpp}` | Async workers; `Render()` without `pOpen` — the layer wraps `Begin/End` |

Some windows (Log, Build, Project Settings, Project/Asset browser) are **not** classes — they're `ImGuiEditorLayer::Render…Panel()` methods in `Systems/ImGuiEditorLayerPanels.cpp`. Prefer a class in `Gui/` for anything new and non-trivial; `ImGuiEditorLayerPanels.cpp` is already ~4.6k lines.

## Class skeleton

In `Gui/MyPanel.hpp` declare `Initialize()`, `Shutdown()` and `Render(bool* pOpen)`. Initialize and shut down outside the ImGui frame. Keep cached data and its dirty flag on the panel. The render method owns the window unless the layer already wraps it, as it does for `PackageManagerPanel`.

```cpp
#include "Gui/MyPanel.hpp"

#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Project/ProjectManager.hpp"

namespace Funke {

	void MyPanel::Render(bool* pOpen) {
		if (!pOpen || !*pOpen) return;

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();   // floating tool window in its own OS window
		EditorUI::NextWindowDefaultSize();
		if (!ImGui::Begin("My Panel", pOpen)) {
			ImGui::End();
			return;
		}

		if (!ProjectManager::GetCurrentProject()) {
			EditorUI::EmptyState("No project open.", "Open a project to use this panel.");
			ImGui::End();
			return;
		}

		RefreshIfDirty();
		RenderToolbar();
		RenderList();
		ImGui::End();
	}

} // namespace Funke
```

Conventions:
- **PCH:** `Funke-Editor/premake5.lua` force-includes `EditorPch.hpp` (ImGui, Vec2, Scene/entt are already in it). Include your own header first; don't add churny headers to `EditorPch.hpp` (rule in its header comment).
- **Look:** compose from `Gui/EditorUI.hpp` (`Button(label, ButtonVariant)`, `SearchField`, `BeginToolbar/EndToolbar`, `BeginCard/EndCard`, `Badge`, `EmptyState`, `StatusText(Tone, …)`, `DialogFooter`, `Tooltip`) and `Gui/ImGuiUtils.hpp` (`ToggleSwitch`, `Switch`, …). Colours come from `EditorTheme` — never hardcode them in a panel.
- **Native dialog:** `ImGuiImplWebGPU::SetNextWindowAsNativeDialog()` (`Funke-Engine/src/Gui/ImGuiImplWebGPU.hpp`) sets the next window's class. Call it before the matching `Begin`/`BeginPopupModal`, with no intervening window begin; other setup such as `EditorUI::NextWindowDefaultSize()` may follow it. Used by Profiler, Build Profiles, Preferences, Package Manager. Dockable views (Sprite Editor, Log) skip it.
- **Always pair `Begin` with `End`**, including the early-return path where `Begin` returned false.
- Per-frame cost: cache + dirty flag (`m_Dirty`, `m_ManifestsDirty` in `PackageManagerPanel`) instead of scanning disk each frame. Instrument with the profiler if a panel drops frames (`funke-add-profiler`).

## Integration sites in ImGuiEditorLayer

1. Add the panel member and its visibility flag to `ImGuiEditorLayer.hpp`.
2. Call `Initialize()` in `OnAttach`, or on first show if setup is expensive or needs a project. `RenderBuildProfilesPanel` shows lazy initialization.
3. Call `Shutdown()` in `OnDetach` after layer workers finish and before WebGPU teardown. Panels with workers belong in the panels → icons → FBOs shutdown block. Release their GPU textures there.
4. In `OnPreRender`, draw the visible panel with `m_MyPanel.Render(&m_ShowMyPanel)`. Put it within the build/reserialize disabled block when those operations should block interaction. Global shortcuts are handled in the chrome.
5. Expose the same flag in `RenderMainMenu` and the Ctrl+K `RenderCommandPalette` in `ImGuiEditorLayerCommands.cpp`. Gate project-dependent commands with `hasProject`. Build new panel sources with the Editor target once the Premake graph includes them.

## Async work — the long-task pattern

Follow the worker and poll implementation in `PackageManagerPanel` for file scans, subprocesses, network work and blocking dialogs. Reject overlapping starts, join any completed worker before reuse, and capture shared task state by value. Publish results and completion under its mutex. Poll from `Render` and consume each result once.
- **Snapshot under the lock, act outside it.** Copy fields out, release, then do work.
- **Project/engine state changes happen on the main thread** in the poll (e.g. `InstallToProject` + post-install automation after a git clone), never in the worker.
- **`Shutdown` joins every `std::thread` and waits every `std::future`** before clearing state — destroying a joinable thread is `std::terminate`, and `std::async` futures block in their destructor.
- **Blocking Win32 dialogs** (`IFileOpenDialog::Show`) run on their own worker that owns a COM STA — see `HandleDiskInstall` / `DiskInstallTaskState`.
- **Progress UI** for long jobs is the shared build-progress popup owned by the chrome: expose `bool GetActiveLoadingPopup(std::string& title, std::string& stage, float& progress)` like `PackageManagerPanel` and hook it where `OnPreRender` queries `m_PackageManagerPanel.GetActiveLoadingPopup(...)`.

## Pitfalls

- **Window title collisions** — the ImGui title is the ID and the `imgui.ini` key; keep it unique and stable (rename = users lose saved docking for it). Use `"Title##id"` to disambiguate.
- **Opening a popup from inside another ID scope** — set a request flag (`m_OpenRenamePopup`, `m_OpenDeletePopup`) and call `OpenPopup` where the `BeginPopupModal` lives (BuildProfilesPanel / EditorPreferencesPanel do this).

## Related skills
`funke-add-profiler` (ProfilerPanel + instrumentation), `funke-author-package` (PackageManagerPanel internals), `funke-inspector-ruleset` (Inspector drawing, not panels), `funke-build-and-ship` (Build / Build Profiles panels' semantics), `funke-code-style`, `funke-comment-style`, `funke-build-engine`.
