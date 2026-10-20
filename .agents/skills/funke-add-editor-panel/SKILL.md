---
name: funke-add-editor-panel
description: Use when adding or changing an ImGui editor panel / window in Funke-Editor. Triggers on "add an editor panel", "new panel", "editor window", "tool window", "Tools menu", "command palette entry", "ImGuiEditorLayer", "OnAttach / OnDetach / OnPreRender", "m_Show…", "EditorUI", "SetNextWindowAsNativeDialog", "NextWindowDefaultSize", "EmptyState", "ProfilerPanel / PackageManagerPanel / BuildProfilesPanel / EditorPreferencesPanel / SpriteEditorPanel", "worker thread in a panel", "panel freezes the editor", or work in `Funke-Editor/src/Gui/*Panel.{hpp,cpp}` and `Funke-Editor/src/Systems/ImGuiEditorLayer*.{hpp,cpp}`. Covers the panel class shape (Initialize / Render(bool* pOpen) / Shutdown), window setup conventions, the ImGuiEditorLayer integration sites (member + flag, OnAttach, OnDetach, the render call in OnPreRender, menu + command palette), lazy-init panels, and the async worker pattern (shared_ptr state + mutex + atomic + std::thread + main-thread poll).
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

`Funke-Editor/src/Gui/MyPanel.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>

namespace Funke {

	class MyPanel {
	public:
		void Initialize();          // one-time setup; no ImGui calls
		void Shutdown();            // join workers, free resources; no ImGui calls
		void Render(bool* pOpen);   // every frame; pOpen flips false on the title-bar X

	private:
		void RefreshIfDirty();
		void RenderToolbar();
		void RenderList();

		std::vector<std::string> m_Items;
		bool m_Dirty = true;
		char m_SearchBuffer[256]{};

		std::string m_StatusMessage;
		bool m_StatusIsError = false;
	};

} // namespace Funke
```

`Funke-Editor/src/Gui/MyPanel.cpp`:

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
- **Native dialog:** `ImGuiImplWebGPU::SetNextWindowAsNativeDialog()` (`Funke-Engine/src/Gui/ImGuiImplWebGPU.hpp`) must be the call **immediately before** `Begin`/`BeginPopupModal`. Used by Profiler, Build Profiles, Preferences, Package Manager. Dockable views (Sprite Editor, Log) skip it.
- **Always pair `Begin` with `End`**, including the early-return path where `Begin` returned false.
- Per-frame cost: cache + dirty flag (`m_Dirty`, `m_ManifestsDirty` in `PackageManagerPanel`) instead of scanning disk each frame. Instrument with the profiler if a panel drops frames (`funke-add-profiler`).

## Integration sites in ImGuiEditorLayer

### 1 — Member + visibility flag
`Funke-Editor/src/Systems/ImGuiEditorLayer.hpp`: add `#include "Gui/MyPanel.hpp"` next to the other `Gui/*Panel.hpp` includes, and in the private section (near `m_ShowProfiler` / `m_ProfilerPanel`):

```cpp
bool    m_ShowMyPanel = false;
MyPanel m_MyPanel;
```

### 2 — Initialize: `ImGuiEditorLayer::OnAttach` (`ImGuiEditorLayer.cpp`)
Next to `m_ProfilerPanel.Initialize(); m_SpriteEditorPanel.Initialize();`:
```cpp
m_MyPanel.Initialize();
```
**Or lazy-init** on first show when setup is expensive or needs a project — the `RenderBuildProfilesPanel` pattern (`ImGuiEditorLayerPanels.cpp`):
```cpp
void ImGuiEditorLayer::RenderMyPanel() {
	if (!m_ShowMyPanel) return;
	if (!m_MyPanelInitialized) { m_MyPanel.Initialize(); m_MyPanelInitialized = true; }
	m_MyPanel.Render(&m_ShowMyPanel);
}
```

### 3 — Shutdown: `ImGuiEditorLayer::OnDetach` (`ImGuiEditorLayer.cpp`)
Next to `m_ProfilerPanel.Shutdown(); m_SpriteEditorPanel.Shutdown(); m_EditorPreferencesPanel.Shutdown();` (these run after the layer's own build/reserialize workers are joined and before WebGPU teardown — a panel owning GPU textures must release them here). Panels with workers go with `m_PackageManagerPanel.Shutdown()` in the "panels → icons → FBOs" block.
```cpp
m_MyPanel.Shutdown();
```

### 4 — Render: `ImGuiEditorLayer::OnPreRender` (`ImGuiEditorLayerChrome.cpp`)
Two spots:
- **Inside** `ImGui::BeginDisabled(m_BuildState > 0 || m_ReserializeState > 0)` … `EndDisabled()` with `RenderBuildPanel(); RenderBuildProfilesPanel(); RenderPackageManagerPanel();` — use this if the panel must be greyed out while a build/reserialize runs.
- **After** `EndDisabled()` with the Profiler / Sprite Editor — always interactive:
```cpp
if (m_ShowMyPanel) {
	m_MyPanel.Render(&m_ShowMyPanel);
}
```
(Global shortcuts live here too, e.g. `ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F6)` toggles the profiler.)

### 5 — Entry points (`ImGuiEditorLayer.cpp`)
- **Main menu** — `RenderMainMenu`: menus are `Application`, `Project` (Build, Build Profiles…, Project Settings, Package Manager), `Edit` (Undo/Redo, Preferences...), `Tools` (`ImGui::MenuItem("Profiler", "Ctrl+F6", &m_ShowProfiler);`). Gate project-dependent items with `hasProject`.
- **Command palette** (Ctrl+K) — `RenderCommandPalette`: add `{ "My Panel", nullptr, hasProject, [&]() { m_ShowMyPanel = true; } },` to the `commands[]` array.

Missing site 4 is the classic bug: it compiles, the menu flips the bool, nothing appears. New `.cpp/.hpp` files under `Funke-Editor/src/` are picked up by the `src/**.cpp` glob once premake regenerates; `scripts\dev\Build.ps1 Editor` does that automatically (`funke-build-engine`).

## Async work — the long-task pattern

ImGui runs on the main thread; anything that can exceed a frame (file I/O over many files, `dotnet`/`cmake`/Python processes, network, blocking Win32 dialogs) goes to a worker. `PackageManagerPanel` has six instances of this pattern (`AutomationTaskState`, `RegistryFetchTaskState`, `CloudInstallTaskState`, `GitInstallTaskState`, `NewPackageTaskState`, `DiskInstallTaskState`):

```cpp
struct FetchTaskState {
	std::mutex Mutex;
	std::atomic<bool> Running{ false };
	bool Finished = false;
	bool Success = false;
	std::string Error;
	std::string Result;
};
std::shared_ptr<FetchTaskState> m_FetchTask;
std::thread m_FetchWorker;

void MyPanel::StartFetch() {
	if (m_FetchTask && m_FetchTask->Running.load()) return;
	if (m_FetchWorker.joinable()) m_FetchWorker.join();
	m_FetchTask = std::make_shared<FetchTaskState>();
	m_FetchTask->Running.store(true);
	auto state = m_FetchTask;                    // capture the shared_ptr, never `this`
	m_FetchWorker = std::thread([state]() {
		std::string result, error; bool ok = /* long work */ true;
		std::scoped_lock lock(state->Mutex);
		state->Result = std::move(result); state->Error = std::move(error);
		state->Success = ok; state->Finished = true;
		state->Running.store(false);
	});
}

void MyPanel::PollFetch() {                      // call at the top of Render
	if (!m_FetchTask) return;
	bool finished = false;
	{ std::scoped_lock lock(m_FetchTask->Mutex); finished = m_FetchTask->Finished; }
	if (!finished) return;
	{ std::scoped_lock lock(m_FetchTask->Mutex); m_FetchTask->Finished = false; }
	if (m_FetchWorker.joinable()) m_FetchWorker.join();
	// consume Result/Error on the main thread: touch project state, mark dirty, set status
}

void MyPanel::Shutdown() {
	if (m_FetchWorker.joinable()) m_FetchWorker.join();
}
```

Rules (from `PackageManagerPanel`):
- **Snapshot under the lock, act outside it.** Copy fields out, release, then do work.
- **Project/engine state changes happen on the main thread** in the poll (e.g. `InstallToProject` + post-install automation after a git clone), never in the worker.
- **`Shutdown` joins every `std::thread` and waits every `std::future`** before clearing state — destroying a joinable thread is `std::terminate`, and `std::async` futures block in their destructor.
- **Blocking Win32 dialogs** (`IFileOpenDialog::Show`) run on their own worker that owns a COM STA — see `HandleDiskInstall` / `DiskInstallTaskState`.
- **Progress UI** for long jobs is the shared build-progress popup owned by the chrome: expose `bool GetActiveLoadingPopup(std::string& title, std::string& stage, float& progress)` like `PackageManagerPanel` and hook it where `OnPreRender` queries `m_PackageManagerPanel.GetActiveLoadingPopup(...)`.

## Pitfalls

- **ImGui calls in `Initialize`/`Shutdown`** — run outside a frame; defer to `Render`.
- **`SetNextWindowAsNativeDialog` not immediately before `Begin`** — another `SetNextWindow*`-consuming call in between eats it.
- **Window title collisions** — the ImGui title is the ID and the `imgui.ini` key; keep it unique and stable (rename = users lose saved docking for it). Use `"Title##id"` to disambiguate.
- **Worker captures `this`** — races panel destruction. Capture the `shared_ptr` state.
- **Forgetting `Shutdown`** in `OnDetach` — hang or `std::terminate` on editor close.
- **Opening a popup from inside another ID scope** — set a request flag (`m_OpenRenamePopup`, `m_OpenDeletePopup`) and call `OpenPopup` where the `BeginPopupModal` lives (BuildProfilesPanel / EditorPreferencesPanel do this).

## Related skills
`funke-add-profiler` (ProfilerPanel + instrumentation), `funke-author-package` (PackageManagerPanel internals), `funke-inspector-ruleset` (Inspector drawing, not panels), `funke-build-and-ship` (Build / Build Profiles panels' semantics), `funke-code-style`, `funke-comment-style`, `funke-build-engine`.
