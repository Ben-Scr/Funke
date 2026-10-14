---
name: index-comment-style
description: Use when writing, editing, compressing, or reviewing comments in the Index engine (C++ or C#) — including doc comments, XML-doc summary comments, inline notes, and "should this comment exist at all" decisions. Triggers on "comment", "// why", "doc comment", "XML doc", "verbose comments", "comment cleanup", "explain this code", "trim comments", or any time you are about to write a `//` / `/* */` block in `Index-Engine/`, `Index-Editor/`, `Index-Runtime/`, `Index-Launcher/`, `Index-ScriptCore/`, or a package layer. Encodes Index's terse "why-not-what" comment philosophy: default to no comment, one line when a comment earns its place, delete narration and banners. Complements the `code-style` skill (which covers naming / formatting). Consult this BEFORE writing a multi-line comment block.
---

# Skill: Index Comment Style

How comments are written in the Index engine. The bar is high: **most code gets no comment**, and the ones that exist are one terse line that says something the code cannot.

## The core rule

A comment explains **WHY**, never **WHAT**. The code already says what it does. A comment earns its place only by adding information the reader cannot get from the code itself:

- a non-obvious **rationale** ("why this and not the obvious alternative"),
- an **ordering / sequencing constraint** ("must run before X because X mutates shared state"),
- a **footgun / gotcha** that will bite the next editor,
- an **invariant** the code relies on but doesn't enforce,
- a **workaround** for an external bug (OS / driver / library / ABI),
- a **unit / domain** that isn't obvious from the type (pixels vs world units, etc.).

If a comment isn't doing one of those, delete it.

## Length

- **One line.** Two only if genuinely irreducible (e.g. two independent constraints).
- If you're writing a **3rd consecutive comment line, stop** — you're narrating the code. A run of 4+ comment lines is almost always an essay to cut down to one line or zero.
- No paragraphs. No multi-sentence build-up. State the fact and move on.

## Delete on sight

- Comments that **narrate the next line(s)**: `// loop over entities and draw each` above an obvious loop.
- Comments that **restate an identifier**: `// the renderer` above `auto* renderer = ...`.
- **Decorative banners / ASCII dividers**: `// ─────────`, `// ===== Section =====`. (A single short `// Section` label is fine if it genuinely aids navigation in a long function; the divider art is not.)
- **Doc comments that echo the signature**: `/// Returns the width.` over `int GetWidth()`.
- **History / changelog narration**: `// Changed this to fix the thing in the other PR`.
- Standard language/library behavior explanations ("std::vector grows by doubling…").

## Style

- **English**, always — match the codebase. Never translate comments to another language.
- Terse and declarative. Drop filler ("basically", "note that", "here we", "this function").
- Place the comment directly above the construct it explains.
- A trailing inline `code; // x` is fine for a quick per-line note; don't grow it into a block.

## C# XML doc (`///` summary comments)

- Keep only if it states a **real contract** the caller can't infer (thread-safety, ownership, "call X before Y", null/empty semantics).
- Compress to a **one-line summary**. Delete the boilerplate param/returns lines that just restate the names.
- If it only echoes the method name/signature, delete it entirely.

## ⚠️ Escape-sequence trap (hard-won)

When a comment needs to mention an escape sequence, **write it as a word** — `newline`, `tab`, `backslash` — **never put a literal `\n`, `\t`, or `\\` in the comment text.**

Reason: when this comment is later edited or compressed (by a person or a tool whose replacement string is JSON/escaped), the `\n` can decode into a **real newline** and tear the comment across two physical lines. The second line is then no longer a comment → it breaks compilation, often cascading (a torn comment above an `enum`/`struct` takes down every file that includes the header). This actually happened during a bulk comment pass and broke the engine build.

```cpp
// BAD  — a later edit can turn the \n into a real line break:
// Wire format: items joined by '\n', backslashes escaped as '\\'.

// GOOD — wording carries the meaning, nothing to mis-decode:
// Wire format: items joined by newlines; embedded newline/backslash chars are backslash-escaped.
```

## Hard rules (never break)

- **Never delete or alter a line of real code** while editing a comment. Comment-only edits must keep the file compiling.
- Never leave a dangling `/*` or `*/`. Don't touch `//` or `/* */` that live **inside string literals** (e.g. WGSL shader source in `R"WGSL(...)"`, glob patterns like `"Source/*.cpp"`) — those aren't comments.
- Commented-out code: delete it if it's clearly dead; leave it only if it's an intentional reference/toggle.
- After any bulk comment change, **build** (`build-engine` skill) — a torn comment or an accidentally-eaten code line only shows up at compile time. A `/* */` balance check catches unterminated blocks but not single-line `//` tears.

## Before / after (real engine examples)

**DELETE — pure narration:**
```cpp
// Push the scene's stored editor-time field values onto the freshly-created
// managed instance. Mirrors how ScriptComponent flushes PendingFieldValues …
void ApplyFieldOverrides(Scene& scene)        // ← signature already says this; delete the block
```

**COMPRESS — keep the one real constraint (33 lines → 1):**
```cpp
// MUST precede the render call: SetSize above mutates the shared MainViewport
// that UI systems fall back to, so publishing the region first pins layout to renderSize.
```

**COMPRESS — keep the gotcha (8 lines → 1):**
```cpp
// Sprites-only: only the sprite pipeline honours wireframe; overlays would paint filled quads over it.
```

## Quick test before keeping a comment

Ask: *"Would a competent engineer reading this code already know this?"* If yes → delete. If the answer is a non-obvious why/constraint/footgun → keep it, in one line.
