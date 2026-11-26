#pragma once
#include "Graphics/Text/TextEncoding.hpp"
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace Funke {
    // Bounded, incremental UTF-8 decoding. The callback must run on the render/main thread.
    class GlyphPrewarmQueue {
    public:
        static constexpr size_t MaxTextBytes = 1024 * 1024;
        static constexpr size_t MaxUniqueGlyphs = 4096;
        explicit GlyphPrewarmQueue(std::string_view text) {
            if (text.size() > MaxTextBytes) throw std::length_error("Prewarm text exceeds 1 MiB");
            text_ = text;
        }
        bool IsDone() const { return cursor_ == text_.size(); }
        bool WasLimited() const { return limited_; }
        size_t GetProcessedBytes() const { return cursor_; }
        size_t GetRequestedGlyphs() const { return seen_.size(); }
        void Cancel() { text_.clear(); cursor_ = 0; seen_.clear(); }

        // Budget includes decoding repeated/control characters. One callback is indivisible.
        template<class RequestGlyph>
        size_t Step(RequestGlyph&& request, size_t maxCodepoints = 32, double milliseconds = 1.0) {
            if (maxCodepoints == 0 || !std::isfinite(milliseconds) || milliseconds <= 0) return 0;
            const auto start = std::chrono::steady_clock::now();
            size_t requested = 0;
            for (size_t scanned = 0; scanned < maxCodepoints && !IsDone(); ++scanned) {
                if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= milliseconds) break;
                uint32_t cp; int length;
                TextEncoding::DecodeUtf8(text_, cursor_, cp, length);
                if (cp >= 32 && !seen_.contains(cp)) {
                    if (seen_.size() == MaxUniqueGlyphs) { limited_ = true; cursor_ = text_.size(); break; }
                    request(cp); // On failure/exception this character remains pending.
                    seen_.insert(cp);
                    ++requested;
                }
                cursor_ += static_cast<size_t>(length);
            }
            return requested;
        }
    private:
        std::string text_;
        size_t cursor_ = 0;
        bool limited_ = false;
        std::unordered_set<uint32_t> seen_;
    };
}
