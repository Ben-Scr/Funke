# Font fallback families

In **Project Settings → Graphics → Font Fallbacks**, add font assets in lookup
order. The primary font on each Text Renderer remains unchanged. For a missing
Unicode codepoint, the engine tries the configured fallback fonts in order and
uses the first font containing that glyph. If all fonts lack it, the primary
font's existing replacement-character/`?` behavior applies.

The ordered `fontFallbackAssets` project setting stores stable font GUID strings.
Zero and duplicate GUIDs are ignored; at most 16 entries are retained. The list
is flat, so font-to-font recursion and cycles cannot be configured. Project
settings apply it through `FontManager::SetFallbackFonts`. Runtime changes made
directly through that native API do not themselves save the project file.
The existing project-GUID dependency scan includes these fonts in asset pruning.

## Metrics, atlases and lifetime

Each glyph retains its selected font's atlas and UVs. Its advance, dimensions and
offsets are scaled to the primary font's pixel size. Kerning applies only when
both glyphs use the same font. Family ascent/descent/leading encompass the
configured fonts, so a fallback with larger metrics can increase line height
even for a line containing only primary-font glyphs. Measurement, text layout
and caret calculations use these same font APIs.

Fallback fonts are cached at the normal quantized bake sizes and retained by
ordinary font-manager usage. Loading/reloading/unloading fonts or changing the
family advances a revision, invalidating cached renderer layouts. The bounded
per-primary fallback-metric cache holds up to 65,536 glyph entries; ordinary glyph
atlas limits still apply. Exhausted caches use replacement glyphs.

Fallback loading and first-use glyph rasterization/upload can perform synchronous
work. Prepare representative text before an interactive scene when needed; see
[glyph prewarming](../Engine-Core/Glyph-Prewarming.md). Missing/unreadable font
assets are skipped. Ship font files under licenses that permit distribution.

This feature provides coverage selection. It does not implement bidirectional
layout, grapheme-aware editing, combining-mark positioning, OpenType shaping or
color emoji.

## Verification boundary

The source-built Dawn Null resource test uses IBM Plex Sans Condensed and a CJK
system font to check Latin, Greek, Cyrillic and Chinese codepoints, a genuinely
missing codepoint, separate atlases, scaled advances, family metrics, mixed-font
kerning, plain/rich layout agreement, invalidation after unload and consistent replacement/kerning when the fallback-metric cache is exhausted. The fixture
can use `FUNKE_LOADING_CJK_FONT` instead of the Windows system font.

On Windows, the rebuilt editor displayed Latin, Greek, Cyrillic and Chinese text across three lines on a Radeon RX 7800 XT with both D3D12 and Vulkan. U+10FFFF displayed the replacement glyph. A Windows Development export from the local Launcher SDK preserved both font assets and rendered the same text. The older beta SDK at 67e570bf packaged the files but displayed replacement glyphs for the fallback characters. In a single-viewport test build, Project Settings found the fallback section by name, added a font through the picker and persisted the reordered GUIDs. The input field placed an inserted character between a Chinese and Cyrillic glyph after keyboard navigation. Replacing the CJK font with Plex while the editor was running switched the text to replacement characters. Restoring the font restored the glyphs. An unknown GUID ahead of the CJK font was skipped. Other target platforms still need verification.
