# Glyph prewarming

`Graphics/Text/FontPrewarm.hpp` provides opt-in preparation of a known UTF-8 text
for an already loaded `FontHandle`. The handle identifies the baked size as well
as the slot generation. A request never retains a raw font pointer across frames.

```cpp
#include "Graphics/Text/FontPrewarm.hpp"

// Keep this request across loading-screen frames. Use the same font/size as the text.
Funke::FontPrewarm request(fontHandle, localizedText);
// Main/render thread, after the font has loaded:
request.Step(32, 1.0); // At most 32 decoded characters and about 1 ms per frame.
// Stop stepping when request.IsDone(); request.Cancel() abandons remaining work.
```

Each request accepts at most 1 MiB of input and visits at most 4,096 distinct
printable codepoints. Repeated characters and controls count against the per-step
decoding budget but do not cause repeated glyph requests. Invalid UTF-8 uses the
normal replacement character. `WasLimited()` reports the unique-character cap;
oversized input throws `std::length_error`. Zero/nonfinite/nonpositive budgets do
no work. A callback exception leaves the character pending.

`FontPrewarm` calls the same `Font::GetGlyph` path as rendering, sharing glyphs,
fallbacks, atlas pages and the existing per-font Unicode cache limits. It enforces
the FontManager atlas budget after each step. An unloaded, evicted or replaced
handle ends the request with `WasInvalidated() == true`. Create a new request
after changing the font, baked size or language; cancellation retains already
cached glyphs. A finished request means its bounded input was processed, not that
the font supports every requested character or that cache pressure cannot evict it.

The time budget is cooperative: a single rasterization, atlas allocation or upload
cannot be interrupted and may exceed it. There are no background GPU calls and
no change to the ordinary lazy-loading default.

`Font::GetGlyphCacheStats()` exposes cumulative lazy-miss rasterization attempts,
uploaded glyphs, CPU rasterization milliseconds and CPU upload-submission
milliseconds. The latter includes page allocation, row padding and submission,
including unsuccessful attempts. It is **not GPU execution time**. Initial font
baking is excluded; stats reset with the font/cache lifetime. Compare snapshots
before/after preparation and first rendering, alongside `GetAtlasByteSize()`.
The queue tests do not establish a measured reduction in rendering spikes; that
requires an actual GPU run using the same text, font and baked size.
