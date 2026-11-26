# Prefab variants

In the Asset Browser, right-click a `.prefab` and choose **Create Variant**. The
new sibling file receives an available `Variant` name (numbered on collision),
its own GUID and the base prefab's GUID as `PrefabRef`. File-history Undo/Redo
includes the metadata file, preserving identity. Renaming the base through the
Asset Browser keeps the reference valid.

The Prefab Inspector opens the expanded variant and shows the base filename.
Native components are marked **Inherited** or **Local override**; the reset icon restores
the inherited component, removes a locally added component, or restores a locally
removed inherited component. Changes use the existing Inspector save workflow.
Full prefab edit mode can change existing child values too. The Scripts group shows
a summary badge for script assignments, fields and managed component values; the
new component-reset buttons apply to native components.

The writer preserves the existing `PrefabRef`, root `Overrides` and per-source-ID
`EntityOverrides` format; it does not flatten inheritance into a copied tree.
A null top-level component override represents a local removal. Adding, deleting
or reparenting inherited entities is not supported by this format and refuses save
instead of silently losing the hierarchy edit.

Typed entity references are serialized using stable prefab source IDs, including
UI targets, event targets/entity arguments and typed script fields/list entries.
Asset GUIDs and unrelated numeric values are left unchanged. Custom serializers
with entity references must use `Detail::MapWrittenEntityReference` when writing
persistent IDs and the scene's reference-remapping contract when loading.

Saving a base in the editor refreshes direct instances and loaded descendants of
its variant inheritance chain transactionally, retaining instance-local overrides.
External disk changes resolve the previous descendant baseline using the old base
source without rewriting disk/cache data. The existing recovery path stages and
validates replacements before publishing scene changes. In-memory prefab template
invalidation also follows transitive inheritance dependencies.

An already open detached variant whose base changed refuses to save against its
stale baseline. Reopen the variant against the new base; preserve any unsaved work
before closing it. This rejection is logged in the Console. In play mode, saving
assets does not hot-propagate changes into the running scene.

`PrefabVariantAuthoringTests` covers root/child overrides, deleted base components,
local component removal, multi-level inheritance, internal entity references,
base rename, creation Undo/Redo, saves without flattening, stale-base refusal and
both editor-save/external-change propagation to loaded scenes. The test is in the
Editor suite because it invokes the actual nonvisual authoring/recovery code.

### Windows editor acceptance

On 26 November 2026 the Windows Release editor created a variant through the Project panel. After leaving the inline name editor, Edit → Undo removed its tile and Redo restored it. The Inspector showed inherited components on a sparse base, a local override after changing Flip X, and inherited values after reset. Removing and restoring the Sprite Renderer worked through the Inspector. A child position edit survived autosave as `EntityOverrides["102"]["Transform2D.posX"]`. Reopening after a base edit showed the new inherited root position. Two fixtures with inherited and replaced script attachments displayed the corresponding Scripts badges. The editor regression suite checks that saving an unchanged sparse variant writes an empty `Overrides` object and that editing one property writes just that property.
