# Editor state ownership

`ImGuiEditorLayer` coordinates the editor; it no longer owns several independently
mutable undo stacks and active-history pointers. `EditorHistorySession` owns those
states and receives only loaded-scene snapshots and two boundary callbacks.

| State | Owner | Boundary |
|---|---|---|
| Loaded scenes, definitions and runtime lifecycle | SceneManager | Layer calls explicit load/recovery APIs. |
| Detached prefab edit scene | ImGuiEditorLayer / PrefabInspector | Each owns its detached editing context. |
| Play/Stop snapshots, recovery outcome and selected-scene names | ImGuiEditorLayer | Pass success and restored scenes to history owner after recovery. |
| Entity selection, selection version and Inspector cache | ImGuiEditorLayer | Persistent identities reconstruct selection after scene replacement. |
| Panel visibility, docking and view state | Existing layer/panel objects | No ownership change in this extraction. |
| Per-scene and detached undo stacks, active context, suspended edit histories | EditorHistorySession | Activate/Forget/Retain/SuspendForPlay/RestoreAfterPlay; no layer pointer. |
| Pending Inspector gesture | InspectorUndoRecorder | Flush into the old stack, then Reset at a context boundary. |

Ordering is part of the contract:

1. Finish an Inspector rename before entering play. Flush pending edits to the
   current edit stack and reset the recorder before taking histories by scene name.
2. On context switch, flush/reset the previous context before activating the new
   stack. Re-selecting the same scene does not flush a second time.
3. Clear active pointers before pruning their owning histories. Forgetting a scene
   or clearing runtime histories discards the pending recorder state.
4. Restore scene contents and scene definitions first after Stop; then restore edit
   histories by scene name onto the replacement scenes. Runtime histories are cleared.
   Missing destinations or failed recovery discard suspended histories and report
   a history-ended notice when nonempty history was lost.

Public layer/bridge undo entry points are preserved. `ForScene` supports bridge
operations without changing the active Inspector context. The detached fallback
retains the editor's existing single-context policy.

`EditorHistorySessionTests` checks switch ordering, no-op reactivation, play/stop
scene replacement, failure discard and active-scene pruning. Existing `UndoTests`
exercise ordering, merge, linked histories, Inspector gestures and asset-file
restore. The remaining EDT-001 acceptance is the manual scene-switch, selection,
edit/Undo/Redo and Play/Stop comparison in a newly built matching Editor.

## Editor layer source layout

The layer's implementation is split by responsibility under
`Funke-Editor/src/Systems/`. These translation units implement the same
`ImGuiEditorLayer`; moving a method does not move or duplicate its state owner.

| Source file | Responsibility |
|---|---|
| `ImGuiEditorLayer.cpp` | Attach/detach, event handling and per-frame coordination. |
| `ImGuiEditorLayerCommands.cpp` | Docking, menus, command palette, toolbar and status bar. |
| `ImGuiEditorLayerTransforms.cpp` | Gizmo manipulation, transform capture and world-preserving reparenting. |
| `ImGuiEditorLayerHistory.cpp` | Undo context routing, shortcuts and history notices. |
| `ImGuiEditorLayerPersistence.cpp` | Scene save, autosave and asset reserialization. |
| `ImGuiEditorLayerPlayMode.cpp` | Script rebuild before play, snapshots, play entry, Stop recovery and standalone launch. |
| `ImGuiEditorLayerSelection.cpp` | Selection identities, hierarchy-root filtering, focus and active scene context. |
| `ImGuiEditorLayerEntityActions.cpp` | Copy/paste, duplicate/delete, rename, scene migration and sibling order. |
| `ImGuiEditorLayerEntityCreation.cpp` | Entity creation menus, naming and asset drops. |
| `ImGuiEditorLayerPrefabEditing.cpp` | Detached prefab edit lifecycle and applying prefab changes. |
| `ImGuiEditorLayerHierarchy.cpp` | Scene/entity hierarchy rendering, icons and drag threshold. |
| `ImGuiEditorLayerLog.cpp` | Log queue, source-location parsing, diagnostic cache and log panel. |
| `ImGuiEditorLayerBuild.cpp` | Build requests, worker progress, build panel and build profiles. |
| `ImGuiEditorLayerPanels.cpp` | Asset browser and package manager panel integration. |
| `ImGuiEditorLayerAssetInspector.cpp` | Asset inspectors and preview texture cache. |
| `ImGuiEditorLayerProjectSettings.cpp` | Settings navigation, search index and shared section controls. |
| `ImGuiEditorLayerSettingsRendering.cpp` | Display and graphics settings. |
| `ImGuiEditorLayerSettingsBranding.cpp` | Branding settings and splash preview. |
| `ImGuiEditorLayerSettingsRuntime.cpp` | Physics, build, editor, global system and threading settings. |

`ImGuiEditorLayerSettingsShared.hpp` declares the settings section and widget
helpers shared by those settings implementations. Existing chrome, Inspector,
viewport and profiling translation units keep their responsibilities.

## Asset browser source layout

`AssetBrowser` owns selection, navigation, thumbnail caches, file history and
worker lifetimes. Its implementation under `Funke-Editor/src/Gui/` is divided by
operation; the public interface and state remain in `AssetBrowser.hpp`.

| Source file | Responsibility |
|---|---|
| `AssetBrowser.cpp` | Initialization, shutdown, refresh and panel/frame coordination. |
| `AssetBrowserNavigation.cpp` | Folder tree, breadcrumb, path containment and revealing assets. |
| `AssetBrowserSearch.cpp` | Search field, recursive search worker and result publication. |
| `AssetBrowserSelection.cpp` | Selection, range/box selection state and keyboard shortcuts. |
| `AssetBrowserGrid.cpp` | Grid and tile rendering, thumbnails, sprite slices and box selection input. |
| `AssetBrowserMenus.cpp` | Background and asset context menus. |
| `AssetBrowserActions.cpp` | Copy/cut/paste, duplication and in-editor asset/entity drops. |
| `AssetBrowserNativeDragDrop.cpp` | Windows OLE file drag objects, capture and drag lifecycle. |
| `AssetBrowserFileOperations.cpp` | File history integration, registry bookkeeping, scene naming and native script mirror paths. |
| `AssetBrowserRename.cpp` | Rename input/commit and script/scene rename synchronization. |
| `AssetBrowserCreation.cpp` | Asset creation, templates and name-collision prompts. |
| `AssetBrowserImport.cpp` | Import planning, file copy worker, registration and progress. |
| `AssetBrowserDeletion.cpp` | Delete confirmation, deletion worker, result application and progress. |
| `AssetBrowserExternal.cpp` | External opening, file-manager reveal, clipboard paths and shell-thread tracking. |

`AssetBrowserInternal.hpp` declares the shared implementation helpers without
adding mutable state. Import, deletion and search workers still publish results
through the browser's existing queues and mutexes. Shutdown joins those workers
and outstanding shell launches before clearing history and GPU-backed caches;
thumbnail invalidation still runs at the existing frame boundary.

## Verification and Windows asset-history fix, 20 November 2026

`EditorHistorySessionTests` now restores real transform edits and redo branches
through three scene replacements. It reverses loaded-scene order, changes live
play-state values, verifies the original scenes can expire, and checks Undo/Redo
against the restored persistent entity IDs. A second test checks that a pending
Inspector edit is committed before suspension while detached-prefab history
remains independent.

An actual failure in the existing folder-deletion history test exposed a Windows
path-length defect: the nested recovery/transaction directories could exceed
MAX_PATH even when the user's asset path was short enough. AssetFileHistory now
uses extended Windows paths at filesystem-call boundaries, including copying,
fingerprinting, exclusive moves, link checks and cleanup. Logical registry paths
and containment comparisons keep their original spelling. The new regression
uses a 215-character project root and checks bytes, metadata, GUID resolution,
empty directories and three Undo/Redo cycles.

A second regression reproduced an Undo failure after the original files were
parked. Rollback restored the user's files but retained a temporary transaction
directory. The transaction now removes its temporary copies when rollback
succeeds and preserves the originals for recovery if rollback itself fails.
The focused Editor suite passed 85 cases and 1,184 assertions. The Release
Editor was rebuilt against the existing Engine and opened the material reference
project through its normal startup path.

A separate live Editor run saved a persistent entity at (17, 23), entered prefab
edit mode, saved the prefab with a different transform and reopened it. Play
changed the entity to (−90, 120). Stop restored the authored transform and
Undo/Redo restored the same persistent ID. A deliberate CS1029 compile failure
preserved the callable revision, scene bytes, prefab and Undo/Redo history.
Opening a missing scene reported an error. Switching to another scene and back
preserved the saved data. Repairing the source activated revision 3. This run
invoked the actual Editor command handlers and checked their resulting state.
See the [acceptance record](Acceptance-2026-11-20.md) for evidence.

The separate named-pipe runner in `scripts/ci/backlog_editor_acceptance.py`
covers the unsaved dirty-state workflow on hosts with Editor bridge access.

### Windows scene switch and inspector history

On 26 November 2026 the Windows Release editor switched from First to Second with a selected entity. Both fixtures intentionally reused its persistent ID. Editing the second entity’s Transform 2D position from 30 to 36.6 created one history step. Undo restored 30, Redo restored 36.6, and Play/Stop preserved the authored value in Second. Transform 2D uses its registered descriptors for position, rotation and scale so Inspector gestures participate in scene history.
