# Localization

This page explains how Funke handles **localization** — translating interface text into multiple languages, choosing a language from a JSON file, switching it at runtime, and downloading languages (and the CJK font) on demand. It also covers the file format, the lookup API, and how to add a language of your own.

If you just want the short version: each language is a JSON file of `key → text`. You look a string up by key with `FNK_TR("some.key")` and set the active language with `Localization::SetLanguage("de")`. Any label that re-reads its key picks up the new language on the next frame, falling back to English for anything it can't find — and to the raw key if even English is missing it, so gaps are visible rather than fatal.

**Making a game?** Your game has its own string tables in `Assets/Localization/`, a C# API (`Localization.Get`, `Localization.SetLanguage`, …) and a **Localized Text** component — jump to [Localizing your game](#localizing-your-game). The rest of this page describes the engine's own (Launcher) tables, which share the file format.

---

## What it is

Funke's localization system is a small, self-contained engine subsystem (namespace `Funke::Localization`, in `Funke-Engine/src/Localization/`) that maps **keys to translated strings**. Each language is a JSON file: a `meta` block describing the language, plus a flat `strings` object of `key → translated text`.

At startup the engine loads the **English fallback table**, scans the languages it can find on disk, and reads the user's saved preference. From then on, a lookup like `Localization::Get("launcher.settings.title")` returns the active language's text for that key — or the English text if the active language is missing it — or, as a last resort, the raw key itself.

English (`en`) is the **built-in fallback**: it is always present, is never downloaded, and is the language every other one falls back to. Other languages ship as bundled JSON files and/or are listed in a manifest for on-demand download.

The bundled localization data ships under `Funke-Runtime/FunkeAssets/Localization/`, which a post-build step copies next to every runnable binary:

```
FunkeAssets/
  Localization/
    en.json          ← English (built-in fallback, always present)
    de.json          ← Deutsch (bundled)
    ja.json          ← Japanese (日本語) (bundled; needs the CJK font)
    manifest.json    ← lists downloadable languages + the CJK font
```

> **Two separate tables.** `Funke::Localization` (this part of the page) holds the engine's own strings: every Funke binary calls `Localization::Initialize()` at startup, but only the **Launcher** reads from it, and all of its keys are in the `launcher.*` namespace. Games use a second, independent table, `Funke::GameLocalization`, loaded from the project's `Assets/Localization/` folder — see [Localizing your game](#localizing-your-game). Launcher keys and game keys never mix, and the two remember their language in different places.

---

## Supported languages

Funke ships three languages out of the box. Two distinctions matter: which ones are **bundled** (work offline, no download) versus listed only for download, and which need the large **CJK font** to render their glyphs.

| Code | Display name | How it ships | Needs CJK font |
| --- | --- | --- | --- |
| **en** | English | Built-in fallback — always present, never downloaded | No |
| **de** | Deutsch | Bundled on disk (also has a download URL for updates) | No |
| **ja** | Japanese (日本語) | Bundled on disk (also has a download URL for updates) | **Yes** |

`en` is special: it is the hard-coded fallback table, it is **not** listed in `manifest.json`, and the language list always sorts it first (the rest sort by display name). `de` and `ja` are bundled so the Launcher works offline; their manifest URLs are an *update* path, not the only way to get them. Only `ja` sets `requiresCjkFont`, because Latin/German glyphs are already in the default UI font but Japanese kana and ideographs are not (see [On-demand downloads and the CJK font](#on-demand-downloads-and-the-cjk-font)).

The shipped `de` and `ja` files are intentionally **partial** — they translate the strings that matter most and lean on English for the rest. That is by design; the fallback chain makes it safe.

---

## The translation file format

A language file has two top-level objects: a `meta` block describing the language, and a `strings` object mapping dotted keys to translated text. Placeholders use `{0}`, `{1}`, … positional markers.

```json
{
  "meta": {
    "code": "de",
    "displayName": "Deutsch",
    "fallback": "en",
    "schemaVersion": 1
  },
  "strings": {
    "launcher.settings.title": "Launcher-Einstellungen",
    "launcher.list.size_prefix": "Größe: {0}",
    "launcher.delete.message": "Projekt '{0}' löschen?"
  }
}
```

| Field | Meaning |
| --- | --- |
| **`meta.code`** | The language code (`en`, `de`, `ja`). This is the identity used everywhere — file lookups, the preference file, `SetLanguage`. |
| **`meta.displayName`** | The human-readable name shown in a language picker (e.g. `"Japanese (日本語)"`). |
| **`meta.fallback`** | The language to fall back to; in practice always `en`. |
| **`meta.schemaVersion`** | Format version (currently `1`). |
| **`strings`** | A flat object of `key → translated string`. Only string-valued members are loaded; anything else is skipped. |

When a file is found, only its `meta` block is parsed up front, so a language picker can be populated cheaply without loading every table. The full `strings` table is loaded lazily the moment that language becomes active. Values are substituted at lookup time with `std::vformat`, so `{0}`/`{1}` map to the arguments you pass to `Localization::Format`.

> **`en.json` is the canonical key set.** English is the fallback for every other language, and missing keys resolve *through* it, so a translation file should use the **same keys** as `en.json` — only the values change. If `de.json` lacks a key, the German UI shows the English string; if English also lacks it, the UI shows the raw key (e.g. `launcher.settings.title`) and the engine logs the miss once. A renamed or mistyped key simply won't be found. Keep `en.json` complete and treat its keys as the source of truth.

---

## Choosing and switching the language

### At startup

`Initialize()` reads the saved preference from `locale.json` (at `%LocalAppData%/Funke/locale.json` on Windows). If there is no usable preference — or the saved language is unknown or not actually installed — it falls back to `en` and writes the resolved choice back out. From then on, the active table is whatever the preference says.

### Auto-detecting the system language

`Localization::GetSystemLanguage()` probes the OS UI locale (Windows `GetUserDefaultLocaleName`, or the POSIX `LC_ALL` / `LC_MESSAGES` / `LANG` environment variables), normalizes it to a bare lowercase tag, and returns the matching language code — **but only if that language is already installed**. A system locale that maps to a not-yet-downloaded language quietly returns `en`; it does not auto-trigger a download. The Launcher uses this to offer an "Auto (system)" option that re-pins to the OS language on each launch.

### The fallback chain and missing keys

Every `Get()` lookup walks the same chain:

1. The **active** language's table.
2. The **English** fallback table.
3. On a complete miss: the engine records the key, logs a one-time warning tagged `Localization`, and returns the **raw key string** itself.

That last step means a missing key is never an exception or a blank — it surfaces visibly as the key text (`launcher.settings.title`), which makes gaps easy to spot during development.

### Switching at runtime

`Localization::SetLanguage(code)` does the right thing based on the language's status:

- **Installed** → swaps the active table immediately, persists the new preference, and (for a CJK language) makes sure the CJK font is present — fetching it if not, which can require a restart.
- **Available** (listed in the manifest but not on disk) → records a pending switch and kicks off a download; the switch is applied when the download finishes.
- **Downloading / DownloadFailed**, an unknown code, or the already-current code → no-op.

Switching to an already-installed, non-CJK language is **live**: the active string table is swapped in place, any registered change callbacks fire, and there is no restart. (The CJK font is the one exception — see below.)

---

## On-demand downloads and the CJK font

`manifest.json` (schema version 1) lists the languages and the font that can be fetched at runtime. Each language entry carries a GitHub-release `url`, a `sha256`, a `sizeBytes`, and `requiresCjkFont`. A separate `fonts.cjk` entry describes the shared CJK font — **Noto Sans CJK** (`NotoSansCJK-Regular.ttc`, ~19 MB). The font is large enough that it is *not* bundled; it is downloaded the first time it's needed.

Two download paths exist:

- **A language JSON.** Selecting an **Available** language queues its `<code>.json` for download to `%LocalAppData%/Funke/Localization/<code>.json`. (Because `de`/`ja` are bundled, this path is really their *update* path; it mainly matters for any server-only languages added later.)
- **The CJK font.** Whenever a CJK language is activated and the font isn't already on disk, Funke queues `NotoSansCJK-Regular.ttc` to `%LocalAppData%/Funke/Fonts/NotoSansCJK/NotoSansCJK-Regular.ttc`. This happens even for **Japanese, which ships bundled** — the `ja.json` is already present, but its font is not, so selecting Japanese for the first time still triggers the ~19 MB download.

Downloads run on a single background worker, one job at a time (the actual transfer is delegated to the bundled `Funke-PackageTool`). The host loop calls `Localization::Poll()` each frame to finalize a finished job — which rebuilds the language list, marks the language **Installed**, and applies any pending switch. Each file is staged to a temporary sibling and renamed into place only after its byte length (when the manifest gives one) and **SHA-256** (when the manifest gives one) check out — so a failed or tampered download never overwrites the existing file.

> **A CJK language needs a restart the first time.** The UI font atlas is baked once at startup; there is no dynamic glyph loading, so a CJK font downloaded mid-session can't be merged into the live atlas — its characters would render as boxes until the next launch. Because of this, fetching the CJK font sets a **restart-required** flag, and the Launcher shows a "Restart Required" prompt that can relaunch the app. Ordinary (non-CJK) language switches never require a restart.

A note on platforms: SHA-256 verification is implemented with BCrypt on **Windows**; on other platforms the hash check is not yet implemented, so any manifest entry carrying a `sha256` will fail to verify there. In practice the download path is Windows-centric today — which is consistent with localization being a Launcher feature for now.

User-downloaded files always **override** bundled ones on a code collision, so an updated `de.json` pulled into `%LocalAppData%/Funke/Localization/` takes precedence over the bundled copy.

---

## Using it from code

The public C++ surface lives in `Funke-Engine/src/Localization/Localization.hpp`, all in namespace `Funke::Localization`:

| Function | What it does |
| --- | --- |
| **`Get(key)`** | Returns the active translation for `key`, falling back to English, then to the raw key. |
| **`GetFallback(key)`** | Looks up `key` in the **English table only**, ignoring the active language. Used for UI shown *before* a freshly-selected language's font is loaded (e.g. the CJK restart prompt), where localized text would render as missing-glyph boxes. |
| **`Format(key, args...)`** | Looks up `key` and substitutes `{0}`/`{1}`/… with the arguments. Header-only template over `std::vformat`; returns the raw template on a bad format spec rather than throwing. |
| **`FormatFallback(key, args...)`** | The English-fallback counterpart of `Format` (built on `GetFallback`). |
| **`GetAvailableLanguages()`** | The cached list of `LanguageInfo { Code, DisplayName, Status, RequiresCjkFont }`. |
| **`GetCurrentLanguage()`** | The active language code. |
| **`GetSystemLanguage()`** | The OS UI language code, if it's installed; otherwise `"en"`. |
| **`SetLanguage(code)`** | Switches language (or queues a download — see above). |
| **`RequestLanguageDownload(code)`** | Kicks off an async download for an *Available* language (plus the CJK font if required); no-op if a download is already running or the language isn't *Available*. `SetLanguage` calls this for you, so you rarely call it directly. |
| **`GetActiveDownload()`** | A snapshot of any in-flight or recently-finished download (`Code`, `Stage`, `Progress`, `Running`, `Failed`, `RestartRequired`, `Error`) for progress UI. |
| **`Poll()`** | Pumped every frame by the host loop to finalize downloads. **Without a pump, a finished download never transitions** — applies any pending switch and rebuilds the installed list. |
| **`Initialize()`** | Loads the fallback, scans languages, applies the saved preference. Called once at startup by the engine entry point. |
| **`RegisterChangeCallback(fn)` / `UnregisterChangeCallback(h)`** | Run `fn` whenever the active language changes; the handle unregisters it. |
| **`FNK_TR(key)`** | Macro alias for `Get(key)` — the idiomatic way to read a string. |

(`Get`, `GetFallback`, and the rest are `FUNKE_API`-exported symbols; `Format` and `FormatFallback` are header-only templates with no exported symbol.)

A typical call site just re-reads its keys every frame, so live language switches are picked up automatically:

```cpp
#include "Localization/Localization.hpp"
using namespace Funke;

// A label — re-fetched each frame, so it re-localizes on a language switch.
ImGui::TextUnformatted(FNK_TR("launcher.settings.title").c_str());

// A formatted string with positional args ({0}, {1}).
std::string msg = Localization::Format("launcher.asset_library.engine_too_old",
                                       requiredVersion, currentVersion);

// UI that must stay readable before a new font has loaded → use the English fallback.
ImGui::TextUnformatted(Localization::GetFallback("launcher.settings.language.restart_required").c_str());

// Switch language at runtime (live for an installed, non-CJK language).
if (userPickedGerman)
    Localization::SetLanguage("de");
```

Two things to keep in mind. First, `Get()`/`GetFallback()` return a **reference** into the engine's internal tables; **don't hold that reference across a language change** — switching languages rebuilds the active table and can invalidate it. Copy the string if you need to keep it. Second, the change-callback hook is live (it fires on every switch) but the shipped Launcher doesn't register one — it simply re-queries every label through `FNK_TR` each frame, which is the recommended pattern.

These functions only reach the Launcher's tables. Game code — C# scripts and native game code alike — uses the game table described next.

---

## Adding a new language

Adding a language is drop-in — no code changes, no rebuild:

1. **Copy `en.json`** to `<code>.json` (e.g. `fr.json`), using the language's code.
2. **Edit the `meta` block**: set `code` to the same code and `displayName` to the name to show in a picker. Keep `fallback` as `"en"` and `schemaVersion` as `1`.
3. **Translate the `strings` values**, leaving the keys exactly as they are. Any key you omit falls back to English.
4. **Drop the file** into either the bundled folder (`Funke-Runtime/FunkeAssets/Localization/`) to ship it, or the user folder (`%LocalAppData%/Funke/Localization/`) to test it locally. The engine scans both on startup, and **user-dir files override bundled ones** on a matching code.

The new language appears automatically: the scan reads each file's `meta` block, records the language, and the next `GetAvailableLanguages()` includes it with status **Installed**. Languages listed in `manifest.json` but not present on disk show up as **Available** (downloadable) instead.

---

## Localizing your game

A game's translations are separate from the Launcher's: their own table (`Funke::GameLocalization` natively, `Funke.Localization` in C#), their own folder in the project, and the player's choice stored with the game's save data.

### Language files

Put one file per language into **`Assets/Localization/`** — the same format as above:

```
MyGame/
  Assets/
    Localization/
      en.json    ← fallback
      de.json
      fr.json
```

```json
{
  "meta": { "code": "de", "displayName": "Deutsch" },
  "strings": {
    "menu.play": "Spiel starten",
    "hud.score": "Punkte: {0:N1}"
  }
}
```

- Only `*.json` files directly in that folder count. `meta.code` names the language; without it the file name is used (`fr.json` → `fr`), and without `meta.displayName` pickers show the code. `fallback` and `schemaVersion` are optional here.
- The **fallback** language is `en` when the game has an `en.json`, otherwise its first language by code. Lookups go **current language → fallback → the key itself**, and a missing key is logged once (tag `Localization`).
- The files load when the project opens — in the editor, the built game and `funke.exe` — and **always ship** in builds (the build's asset pruning keeps the whole folder). The editor reloads them as soon as you save one, also during Play.
- Text Renderers load Unicode glyphs on demand from their assigned font. Choose a font containing the characters in your translations: the bundled IBM Plex fonts cover Latin Extended, but Greek, Cyrillic and CJK require another font. Missing characters use the font's replacement glyph or `?`; malformed UTF-8 also renders as replacement characters. There is no automatic fallback-font chain or complex-script shaping/bidirectional layout. This game-text path is separate from the Launcher's ImGui font atlas and its CJK-font restart requirement. See [Known Limitations](../Known-Limitations.md) and [font caching](../Assets/Assets.md#loading-caching-and-handles).

### Which language the game starts in

1. The language the player picked with `Localization.SetLanguage` — stored in **`PlayerPrefs`** under `Funke.Language`, so it lives in the game's player-data folder (`Application.PersistentDataPath`) and follows `--prefs-profile` / isolated build profiles like any other save data.
2. Otherwise the **OS language**, if the game has it: an exact match (`pt-BR`), then the bare language (`de` for `de-AT`), then a regional variant (`pt-PT` → `pt-BR`).
3. Otherwise the fallback.

Editor Play Mode and the built game share the player-data folder, so a language chosen while playing in the editor is also the built game's start language on that machine.

### The Localized Text component

Add **Localized Text** (category UI) to an entity with a **Text Renderer** (Add Component adds the Text Renderer for you) and set its **Key**. During Play — and always in a built game — the component writes the translation into the Text Renderer, and writes it again whenever the language changes or the files are reloaded. A script may still set the text itself; that text stays until the key or the language changes.

Outside Play the Text Renderer keeps the text you typed, so translations never leak into saved scenes or prefabs. The inspector's read-only **Translation** row shows what the key resolves to in the current language (or `(missing key)`).

In scene JSON the component is `"LocalizedText": { "key": "menu.play" }`.

### From C#

```csharp
using Funke;
using Funke.UI;

title.Text = Localization.Get("menu.title");
score.Text = Localization.Format("hud.score", points);   // {0}, {1} … formatted for the language's culture

// A language picker:
foreach (LanguageInfo language in Localization.AvailableLanguages)
    AddButton(language.DisplayName, () => Localization.SetLanguage(language.Code));

Localization.LanguageChanged += RefreshLabels;          // unsubscribe in OnDisable/OnDestroy
GetComponent<LocalizedText>()!.Key = "menu.continue";   // switch a Localized Text to another key
```

| Member | What it does |
| --- | --- |
| **`Get(key)`** | The translation; the fallback's when the current language lacks it; the key itself when no language has it. Cached until the language changes. |
| **`Format(key, params args)`** | `Get` plus .NET composite formatting (`{0}`, `{0:N1}`, `{{` for a brace) with `Culture`. A malformed translation is returned unformatted instead of throwing. |
| **`Has(key)`** | True when the current or the fallback language has the key. |
| **`CurrentLanguage`** / **`FallbackLanguage`** | Language codes; empty when the game has no language files. |
| **`SystemLanguage`** | The OS UI language as a bare code (`"de"`), whether or not the game has it; `"en"` when unknown. |
| **`AvailableLanguages`** | `LanguageInfo(Code, DisplayName)` for every file, fallback first, then by display name. |
| **`SetLanguage(code)`** | Switches immediately and stores the choice in PlayerPrefs; `false` (nothing changes) for a language the game doesn't have. |
| **`Culture`** | `CultureInfo` of the current language for your own number/date formatting; invariant when .NET doesn't know the code. |
| **`LanguageChanged`** | Raised after a language switch and after the editor reloaded edited files. |
| **`PreferenceKey`** | `"Funke.Language"`, the PlayerPrefs key holding the choice. |

Call it from the main thread (scripts, systems), not from jobs.

### From native code

`Localization/GameLocalization.hpp` offers the same table to C++: `GameLocalization::Get`, `Format` (`std::format` placeholders), `Has`, `SetLanguage`, `GetCurrentLanguage`, `GetAvailableLanguages`, `RegisterChangeCallback` and `GetRevision` (bumped by every language switch or reload). `SetLanguage` there does not store the player's choice; only the C# `Localization.SetLanguage` does.

### Checking translations

`funke validate` reports language files that aren't valid JSON or lack a `strings` object, Localized Text keys that the fallback language doesn't have (they would show the raw key) or that sit on an entity without a Text Renderer, and — as warnings — keys a language hasn't translated yet or has but the fallback doesn't (typos, leftovers).

---

## Summary

- Funke localization maps **keys to translated strings**, one **JSON file per language** (a `meta` block plus a flat `strings` object), in namespace `Funke::Localization`.
- **English (`en`) is the always-present fallback**; lookups resolve active → English → raw key, so missing keys are visible, never fatal.
- Three languages ship: **en**, **de** (bundled), and **ja** (bundled, needs the **CJK font**); `manifest.json` lists `de`/`ja` and the on-demand ~19 MB Noto Sans CJK font.
- The active language is read from **`%LocalAppData%/Funke/locale.json`**, can auto-detect the **system language**, and switches **live** via `SetLanguage` — except the CJK font, which **requires a restart** because the font atlas is baked at startup.
- Read strings with **`FNK_TR(key)`** / `Localization::Get`, use **`GetFallback`** for pre-font-load UI, format with **`Localization::Format(key, args...)`**, and pump **`Poll()`** each frame. These engine tables serve the **Launcher**.
- **Add a language** by dropping a `<code>.json` with a `meta` block into the bundled or user `Localization` folder — it auto-appears, with the user dir overriding bundled.
- **Games** keep their own tables in **`Assets/Localization/<code>.json`** (fallback `en` or the first language), read them from C# with **`Localization.Get` / `Format`**, let the **Localized Text** component fill Text Renderers, and switch with **`Localization.SetLanguage`**, which remembers the choice in **PlayerPrefs**; the game otherwise starts in the OS language when it has it. Unicode text needs a font containing the requested glyphs; game Text Renderers load them on demand.

## Related pages

- [Assets](../Assets/Assets.md) — the locale JSON files are content shipped next to each binary, like other engine assets.
- [Startup Processes](Startup-Processes.md) — the engine boot sequence, where `Localization::Initialize()` runs.
- [Third-Party Libraries](../Reference/Third-Party-Libraries.md) — the bundled OFL fonts and the on-demand Noto Sans CJK font.
- [Known Limitations](../Known-Limitations.md) — remaining font, text-shaping and localization-editor limits.
