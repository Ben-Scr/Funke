---
name: funke-comment-style
description: "Write or review comments in Funke engine C++ and C#, including XML docs and bulk comment cleanup."
---

# Comments in Funke code

A comment earns its place when removing it would hide a constraint. Keep the reason for an unusual order, unit, lifetime, ABI workaround or caller obligation. Omit narration, signature paraphrases, decorative dividers and change history. Keep an intentional toggle or reference when its purpose is clear.

Write in English. Usually one line directly above the relevant code is enough. For example, `// Publish the viewport before layout reads its size.` A short trailing note also works for a unit. In C# XML docs, keep caller-visible contracts such as ownership, call order, thread safety or null behavior. Drop boilerplate parameter and return descriptions.

For comment-only edits, preserve executable code, directives and string contents, including embedded WGSL and glob patterns. Spell out “newline” and “backslash” in prose. Escaped replacement text has previously split comments across physical lines. After bulk edits, inspect the diff for torn lines or unbalanced delimiters and compile the affected targets once.
