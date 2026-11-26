#include <doctest/doctest.h>
#include "Graphics/Text/GlyphPrewarmQueue.hpp"
#include <limits>
#include <vector>

TEST_CASE("Glyph prewarm budgets decoding, deduplicates and cancels") {
    Funke::GlyphPrewarmQueue queue("AAA\n\xE2\x82\xAC\xFF");
    std::vector<uint32_t> calls;
    auto glyph = [&](uint32_t cp) { calls.push_back(cp); };
    CHECK(queue.Step(glyph, 2, 1000) == 1);
    CHECK(queue.GetProcessedBytes() == 2);
    CHECK(queue.Step(glyph, 0, 1000) == 0);
    CHECK(queue.Step(glyph, 20, std::numeric_limits<double>::infinity()) == 0);
    CHECK(queue.Step(glyph, 20, 1000) == 2);
    CHECK(calls == std::vector<uint32_t>{'A', 0x20AC, 0xFFFD});
    CHECK(queue.IsDone());
    CHECK(queue.Step(glyph) == 0);
    Funke::GlyphPrewarmQueue cancelled("abc");
    cancelled.Cancel();
    CHECK(cancelled.IsDone());
    CHECK(cancelled.Step(glyph) == 0);
}

TEST_CASE("Glyph prewarm rejects oversized input and retries callback failures") {
    CHECK_THROWS_AS(Funke::GlyphPrewarmQueue(std::string(Funke::GlyphPrewarmQueue::MaxTextBytes + 1, 'x')), std::length_error);
    Funke::GlyphPrewarmQueue queue("x");
    CHECK_THROWS(queue.Step([](uint32_t) { throw std::runtime_error("failed"); }, 1, 1000));
    CHECK(queue.GetProcessedBytes() == 0);
    CHECK(queue.GetRequestedGlyphs() == 0);
    CHECK(queue.Step([](uint32_t) {}, 1, 1000) == 1);
    CHECK(queue.IsDone());
}

TEST_CASE("Glyph prewarm limits unique glyph work") {
    std::string text;
    for (uint32_t cp = 0x1000; cp <= 0x2000; ++cp) {
        text += static_cast<char>(0xE0 | (cp >> 12));
        text += static_cast<char>(0x80 | ((cp >> 6) & 63));
        text += static_cast<char>(0x80 | (cp & 63));
    }
    Funke::GlyphPrewarmQueue queue(text);
    CHECK(queue.Step([](uint32_t) {}, 5000, 1000) == 4096);
    CHECK(queue.IsDone());
    CHECK(queue.WasLimited());
}
