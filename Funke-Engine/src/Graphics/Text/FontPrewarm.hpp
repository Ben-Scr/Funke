#pragma once
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/Text/GlyphPrewarmQueue.hpp"

namespace Funke {
    // A request is bound to one font size/generation. Never retains a raw Font pointer.
    class FontPrewarm {
    public:
        FontPrewarm(FontHandle font, std::string_view text) : font_(font), queue_(text) {}
        bool IsDone() const { return queue_.IsDone(); }
        bool WasLimited() const { return queue_.WasLimited(); }
        bool WasInvalidated() const { return invalidated_; }
        void Cancel() { queue_.Cancel(); }
        // Main thread, after the font has loaded. An unloaded/replaced font cancels the request.
        size_t Step(size_t maxCodepoints = 32, double milliseconds = 1.0) {
            if (IsDone()) return 0;
            Font* font = FontManager::GetFont(font_);
            if (!font || !font->IsLoaded()) { invalidated_ = true; Cancel(); return 0; }
            const size_t requested = queue_.Step([font](uint32_t cp) { font->GetGlyph(cp); }, maxCodepoints, milliseconds);
            FontManager::EnforceMemoryBudget();
            return requested;
        }
    private:
        FontHandle font_;
        GlyphPrewarmQueue queue_;
        bool invalidated_ = false;
    };
}
