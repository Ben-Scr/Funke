# String table editor

Open **Tools → String Tables** or the **String Tables** command-palette entry.
The dockable panel edits the project's `Assets/Localization/*.json` files using
the existing game-localization format:

```json
{
  "meta": { "code": "de", "displayName": "Deutsch" },
  "strings": { "menu.play": "Spielen", "welcome": "Hallo {0}" }
}
```

Select a language file, search keys or translations, and edit multiline UTF-8
values. The key list is the union of valid tables; **Missing only** and the
missing count identify keys absent from the selected language. An empty string
is an existing translation, matching runtime behavior. Game localization falls
back to English when available, otherwise its first language; unresolved keys
remain visible as keys. See [runtime rules](../Engine-Core/Localization.md).

Add, rename and delete affect the **selected language only**. Renaming refuses
to overwrite another key. Update script references and other languages explicitly.
Creating a language accepts a lowercase code such as `en` or `pt-br` and refuses
path traversal or an occupied destination. Save/discard the draft before changing
language or using file history.

## Saving, history and external edits

Save preserves unknown root/metadata fields and writes through the existing
atomic file writer and `AssetFileHistory`. The panel's project-session history
provides Undo/Redo; it is separate from entity-edit history. Save, Undo and Redo
reload game-localization tables. Draft keystrokes are not individual history
steps. A project change discards the panel's draft/history.

The panel rescans approximately once per second while open. A clean document
reloads external changes. A dirty document keeps its draft and blocks saving
when the baseline file changes or disappears. **Reload / discard edits** explicitly
replaces that draft. History uses the existing conflict checks before replaying
a recorded file operation. A failed history commit attempts to restore only the
panel's own write, preserving an intervening external replacement and recovery
files when rollback cannot be completed.

Invalid JSON, duplicate object keys (including equivalent escaped keys), a
non-object `strings` member, non-string translations and invalid language
metadata are rejected for editing. The diagnostics also identify duplicate
language codes across files. The editor does not silently normalize or overwrite
an invalid table; repair it externally and reload. General JSON parsing outside
this authoring path retains its existing compatibility behavior.

## Verification boundary

Document tests cover add/rename/delete, unknown-field preservation, multiline
Unicode, Undo/Redo, duplicate/invalid data, occupied destinations, external changes
and deletion conflicts. Production editor sources passed compiler checks.

### Windows editor acceptance

The local Release editor opened the panel from Tools and the command palette. A dock in the Log area survived restart. The fixture exercised key search, missing-only filtering, duplicate diagnostics, language creation, key edits, multiline values, Save, Undo and Redo. An external file replacement during a dirty edit blocked Save. Discard reloaded the external translation and the Game view displayed it in Play.
