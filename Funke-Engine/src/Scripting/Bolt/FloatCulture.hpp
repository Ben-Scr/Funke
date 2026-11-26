#pragma once
#include <algorithm>
#include <cstdint>
#include <locale>
#include <string>
#include <string_view>

#if defined(_WIN32) && __has_include(<icu.h>)
#include <windows.h>
#include <icu.h>
#define FK_BOLT_WINDOWS_ICU 1
#elif __has_include(<unicode/unum.h>) && __has_include(<dlfcn.h>)
#include <dlfcn.h>
#include <unicode/unum.h>
#define FK_BOLT_POSIX_ICU 1
#endif

namespace Funke::BoltScriptHost::Locales {
    template<class Char>
    inline std::string Utf8Text(std::basic_string_view<Char> value) {
        std::string result;
        for (size_t i = 0; i < value.size(); ++i) {
            uint32_t c = static_cast<uint32_t>(value[i]);
            if constexpr (sizeof(Char) == 2) {
                if (c >= 0xd800 && c <= 0xdbff && i + 1 < value.size()) {
                    uint32_t low = value[i + 1];
                    if (low >= 0xdc00 && low <= 0xdfff) { c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00; ++i; }
                }
            }
            if (c <= 0x7f) { result += static_cast<char>(c); continue; }
            if (c > 0xffff) {
                result += static_cast<char>(0xf0 | c >> 18);
                result += static_cast<char>(0x80 | ((c >> 12) & 63));
            } else if (c > 0x7ff) {
                result += static_cast<char>(0xe0 | c >> 12);
            } else {
                result += static_cast<char>(0xc0 | c >> 6);
                result += static_cast<char>(0x80 | (c & 63));
                continue;
            }
            result += static_cast<char>(0x80 | ((c >> 6) & 63));
            result += static_cast<char>(0x80 | (c & 63));
        }
        return result;
    }

    inline std::string Utf8(wchar_t c) { return Utf8Text(std::wstring_view(&c, 1)); }

    struct FloatCulture {
        std::string Decimal{"."}, Group{","}, GroupSizes{"\3"};
        std::string Negative{"-"}, Positive{"+"}, Percent{"%"}, PerMille{"‰"}, NaN{"NaN"}, Infinity{"Infinity"};
        std::string CurrencyDecimal{"."}, CurrencyGroup{","}, CurrencySizes{"\3"};
        std::string PositivePrefix{"¤"}, PositiveSuffix, NegativePrefix{"(¤"}, NegativeSuffix{")"};
        int CurrencyPlaces{2};
    };

    inline FloatCulture PlatformFloatCulture(const std::locale& locale) {
        FloatCulture data;
        const auto& number = std::use_facet<std::numpunct<wchar_t>>(locale);
        data.Decimal = Utf8(number.decimal_point());
        data.Group = Utf8(number.thousands_sep());
        data.GroupSizes = number.grouping();
        const auto& money = std::use_facet<std::moneypunct<wchar_t>>(locale);
        data.CurrencyDecimal = Utf8(money.decimal_point());
        data.CurrencyGroup = Utf8(money.thousands_sep());
        data.CurrencySizes = money.grouping();
        data.CurrencyPlaces = money.frac_digits();
        auto affixes = [&](const auto& pattern, const std::wstring& sign, std::string& prefix, std::string& suffix) {
            prefix.clear();
            suffix.clear();
            auto* side = &prefix;
            for (char field : pattern.field) {
                switch (field) {
                case std::money_base::sign: if (!sign.empty()) *side += Utf8(sign.front()); break;
                case std::money_base::symbol: *side += Utf8Text(std::wstring_view(money.curr_symbol())); break;
                case std::money_base::space: *side += ' '; break;
                case std::money_base::value: side = &suffix; break;
                }
            }
            if (sign.size() > 1) suffix += Utf8Text(std::wstring_view(sign).substr(1));
        };
        affixes(money.pos_format(), money.positive_sign(), data.PositivePrefix, data.PositiveSuffix);
        affixes(money.neg_format(), money.negative_sign(), data.NegativePrefix, data.NegativeSuffix);
        return data;
    }

#if defined(FK_BOLT_WINDOWS_ICU) || defined(FK_BOLT_POSIX_ICU)
    // Use the platform's ICU data when available, as the managed runtime does.
    // Loading it lazily preserves standalone Bolt deployments on hosts without ICU.
    struct FloatIcu {
#if defined(FK_BOLT_WINDOWS_ICU)
        HMODULE Library = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        FARPROC Find(const char* name) const { return Library ? GetProcAddress(Library, name) : nullptr; }
#else
        void* Library = [] {
#if defined(__APPLE__)
            return dlopen("/usr/lib/libicucore.A.dylib", RTLD_LAZY | RTLD_LOCAL);
#elif defined(__ANDROID__)
            return dlopen("libicu.so", RTLD_LAZY | RTLD_LOCAL);
#else
            return dlopen("libicui18n.so." U_ICU_VERSION_SHORT, RTLD_LAZY | RTLD_LOCAL);
#endif
        }();
        void* Find(const char* name) const { return Library ? dlsym(Library, name) : nullptr; }
#endif
#define FK_BOLT_ICU_NAME_LITERAL(name) #name
#define FK_BOLT_ICU_NAME(name) FK_BOLT_ICU_NAME_LITERAL(name)
#define FK_BOLT_ICU_ENTRY(function, field) decltype(&function) field = reinterpret_cast<decltype(&function)>(Find(FK_BOLT_ICU_NAME(function)))
        FK_BOLT_ICU_ENTRY(unum_open, Open);
        FK_BOLT_ICU_ENTRY(unum_close, Close);
        FK_BOLT_ICU_ENTRY(unum_getAttribute, Attribute);
        FK_BOLT_ICU_ENTRY(unum_getTextAttribute, TextAttribute);
        FK_BOLT_ICU_ENTRY(unum_getSymbol, Symbol);
#undef FK_BOLT_ICU_ENTRY
#undef FK_BOLT_ICU_NAME
#undef FK_BOLT_ICU_NAME_LITERAL

        static size_t DirectionalMark(std::string_view text) {
            if (text.starts_with("\xE2\x80\x8E") || text.starts_with("\xE2\x80\x8F")) return 3;
            return text.starts_with("\xD8\x9C") ? 2 : 0;
        }

        static std::string CurrencyAffix(std::string_view text, const std::string& symbol, const std::string& negative) {
            // NumberFormatInfo adds direction marks carried by the currency symbol and
            // negative sign. ICU also emits layout marks around the complete amount.
            std::string_view bareSign = negative;
            while (size_t mark = DirectionalMark(bareSign)) bareSign.remove_prefix(mark);
            std::string result;
            while (!text.empty()) {
                if (!symbol.empty() && text.starts_with(symbol)) {
                    result += symbol;
                    text.remove_prefix(symbol.size());
                } else if (size_t mark = DirectionalMark(text)) {
                    text.remove_prefix(mark);
                } else if (!bareSign.empty() && text.starts_with(bareSign)) {
                    result += negative;
                    text.remove_prefix(bareSign.size());
                } else {
                    result += text.front();
                    text.remove_prefix(1);
                }
            }
            return result;
        }

        void Read(const std::string& name, FloatCulture& data) const {
            if (!Open || !Close || !Attribute || !TextAttribute || !Symbol) return;
            UErrorCode error = U_ZERO_ERROR;
            UNumberFormat* format = Open(UNUM_CURRENCY, nullptr, 0, name.c_str(), nullptr, &error);
            if (U_FAILURE(error) || !format) return;
            struct FormatGuard {
                UNumberFormat* Handle;
                decltype(&unum_close) Destroy;
                ~FormatGuard() { Destroy(Handle); }
            } guard{format, Close};

            auto get = [&](auto function, auto id, bool affix = false) {
                UChar buffer[128];
                UErrorCode status = U_ZERO_ERROR;
                int count = function(format, id, buffer, 128, &status);
                if (U_FAILURE(status) || count < 0 || count > 128) return std::string{};
                if (affix) {
                    // NumberFormatInfo patterns use an ordinary space between symbol and value.
                    for (int i = 0; i < count; ++i)
                        if (buffer[i] == 0xa0 || buffer[i] == 0x202f) buffer[i] = ' ';
                }
                return Utf8Text(std::basic_string_view<UChar>(buffer, static_cast<size_t>(count)));
            };
            data.Decimal = get(Symbol, UNUM_DECIMAL_SEPARATOR_SYMBOL);
            data.Group = get(Symbol, UNUM_GROUPING_SEPARATOR_SYMBOL);
            data.Negative = get(Symbol, UNUM_MINUS_SIGN_SYMBOL);
            data.Positive = get(Symbol, UNUM_PLUS_SIGN_SYMBOL);
            data.Percent = get(Symbol, UNUM_PERCENT_SYMBOL);
            data.PerMille = get(Symbol, UNUM_PERMILL_SYMBOL);
            data.NaN = get(Symbol, UNUM_NAN_SYMBOL);
            data.Infinity = get(Symbol, UNUM_INFINITY_SYMBOL);
            data.CurrencyDecimal = get(Symbol, UNUM_MONETARY_SEPARATOR_SYMBOL);
            data.CurrencyGroup = get(Symbol, UNUM_MONETARY_GROUPING_SEPARATOR_SYMBOL);
            data.CurrencyPlaces = Attribute(format, UNUM_MIN_FRACTION_DIGITS);
            data.CurrencySizes = std::string(1, static_cast<char>(Attribute(format, UNUM_GROUPING_SIZE)));
            int secondary = Attribute(format, UNUM_SECONDARY_GROUPING_SIZE);
            if (secondary > 0) data.CurrencySizes += static_cast<char>(secondary);
            const std::string symbol = get(Symbol, UNUM_CURRENCY_SYMBOL);
            auto affix = [&](UNumberFormatTextAttribute id) {
                return CurrencyAffix(get(TextAttribute, id, true), symbol, data.Negative);
            };
            data.PositivePrefix = affix(UNUM_POSITIVE_PREFIX);
            data.PositiveSuffix = affix(UNUM_POSITIVE_SUFFIX);
            data.NegativePrefix = affix(UNUM_NEGATIVE_PREFIX);
            data.NegativeSuffix = affix(UNUM_NEGATIVE_SUFFIX);
        }
    };
#endif

    inline const FloatCulture& GetFloatCulture(const std::string& name, const std::locale& locale) {
        static const FloatCulture invariant;
        if (name.empty()) return invariant;
        thread_local std::string previous;
        thread_local FloatCulture data;
        if (name != previous) {
            data = PlatformFloatCulture(locale);
#if defined(FK_BOLT_WINDOWS_ICU) || defined(FK_BOLT_POSIX_ICU)
            static const FloatIcu icu;
            icu.Read(name, data);
#endif
            previous = name;
        }
        return data;
    }
}
#undef FK_BOLT_WINDOWS_ICU
#undef FK_BOLT_POSIX_ICU

