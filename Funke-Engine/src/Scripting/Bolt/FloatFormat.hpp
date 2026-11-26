#pragma once

#include "Scripting/Bolt/FloatCulture.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Funke::BoltScriptHost::Locales {
    inline std::vector<bool> GroupStarts(int length, const std::string& widths) {
        std::vector<bool> starts(length, false);
        int position = length;
        for (size_t i = 0; !widths.empty();) {
            int width = static_cast<unsigned char>(widths[i]);
            if (width == 0 || width == CHAR_MAX || position <= width) break;
            position -= width;
            starts[position] = true;
            if (i + 1 < widths.size()) ++i;
        }
        return starts;
    }

    inline std::string Currency(float value, std::string_view format, const FloatCulture& culture) {
        int decimals = culture.CurrencyPlaces;
        if (format.size() > 1) {
            auto digit = std::from_chars(format.data() + 1, format.data() + format.size(), decimals);
            if (digit.ec != std::errc() || decimals < 0) return {};
        }
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::fixed << std::setprecision(decimals) << std::fabs(value);
        std::string number = stream.str();
        const int count = static_cast<int>(number.find('.') == std::string::npos ? number.size() : number.find('.'));
        const auto starts = GroupStarts(count, culture.CurrencySizes);
        const auto separator = culture.CurrencyGroup;
        std::string amount;
        for (int i = 0; i < count; ++i) {
            if (starts[i]) amount += separator;
            amount += number[i];
        }
        if (decimals > 0) amount += culture.CurrencyDecimal + number.substr(count + 1);
        return std::signbit(value) ? culture.NegativePrefix + amount + culture.NegativeSuffix :
            culture.PositivePrefix + amount + culture.PositiveSuffix;
    }

    struct FloatDigits {
        std::string Digits;
        int Point{};
        bool IsZero() const { return Digits.empty(); }

        explicit FloatDigits(float value) {
            if (value == 0) return;
            // System.Single builds custom formats from seven significant decimal digits.
            char buffer[32];
            auto end = std::to_chars(buffer, buffer + sizeof(buffer), std::fabs(value), std::chars_format::scientific, 6).ptr;
            auto power = std::find(buffer, end, 'e');
            for (auto p = buffer; p != power; ++p) if (*p != '.') Digits += *p;
            if (power < end) {
                const char* first = power + 1;
                if (*first == '+') ++first;
                std::from_chars(first, static_cast<const char*>(end), Point);
            }
            ++Point;
        }

        void Round(int keep) {
            if (keep >= static_cast<int>(Digits.size()) || IsZero()) return;
            if (keep < 0) { Digits.clear(); Point = 0; return; }
            // The custom-format rounding step rounds a decimal midpoint away from zero.
            bool up = Digits[keep] >= '5';
            Digits.resize(keep);
            while (up && !Digits.empty() && Digits.back() == '9') Digits.pop_back();
            if (up && Digits.empty()) { Digits = "1"; ++Point; }
            else if (up) ++Digits.back();
            else if (Digits.empty()) Point = 0;
        }

        char At(int i) const { return i >= 0 && i < static_cast<int>(Digits.size()) ? Digits[i] : '0'; }
    };

    inline std::array<std::string_view, 3> Sections(std::string_view format) {
        std::array<std::string_view, 3> parts{};
        size_t start = 0, part = 0;
        char quote = 0;
        for (size_t i = 0; i < format.size(); ++i) {
            const char c = format[i];
            if (quote) { if (c == quote) quote = 0; continue; }
            if (c == '\\') { if (i + 1 < format.size()) ++i; continue; }
            if (c == '"' || c == '\'') { quote = c; continue; }
            if (c == ';') {
                parts[part++] = format.substr(start, i - start);
                if (part == parts.size()) return parts;
                start = i + 1;
            }
        }
        parts[part] = format.substr(start);
        return parts;
    }

    struct CustomToken { char Kind; std::string_view Text; };
    struct CustomPattern {
        std::vector<CustomToken> Tokens;
        int Whole{}, Fraction{}, MinimumWhole{}, MinimumFraction{}, Scale{};
        bool Group{}, Scientific{};

        explicit CustomPattern(std::string_view format) {
            bool decimal = false;
            int lastComma = -1, commas = 0;
            for (size_t i = 0; i < format.size();) {
                size_t start = i;
                char c = format[i++];
                if (c == '\'' || c == '"') {
                    size_t finish = format.find(c, i);
                    Tokens.push_back({'l', format.substr(i, finish == format.npos ? finish : finish - i)});
                    i = finish == format.npos ? format.size() : finish + 1;
                } else if (c == '\\') {
                    if (i < format.size()) Tokens.push_back({'l', format.substr(i++, 1)});
                } else if (c == '0' || c == '#') {
                    if (decimal) { ++Fraction; if (c == '0') MinimumFraction = Fraction; }
                    else { ++Whole; if (c == '0' && MinimumWhole == 0) MinimumWhole = Whole; }
                    Tokens.push_back({decimal ? 'f' : 'w', {}});
                } else if (c == '.') {
                    if (!decimal) Tokens.push_back({'.', {}});
                    decimal = true;
                } else if (c == ',') {
                    if (!decimal && Whole > 0) {
                        if (Whole == lastComma) ++commas;
                        else { if (lastComma >= 0) Group = true; lastComma = Whole; commas = 1; }
                    }
                } else if (c == '%' || format.substr(start, 3) == "‰") {
                    if (c == '%') Scale += 2;
                    else { Scale += 3; i = start + 3; }
                    Tokens.push_back({c == '%' ? '%' : 'p', {}});
                } else if ((c == 'e' || c == 'E') && !Scientific) {
                    size_t j = i;
                    if (j < format.size() && (format[j] == '+' || format[j] == '-')) ++j;
                    size_t first = j;
                    while (j < format.size() && format[j] == '0') ++j;
                    if (j != first) { i = j; Scientific = true; }
                    Tokens.push_back({Scientific ? 'e' : 'l', format.substr(start, i - start)});
                } else Tokens.push_back({'l', format.substr(start, 1)});
            }
            if (lastComma == Whole) Scale -= 3 * commas;
            else if (lastComma >= 0) Group = true;
            if (MinimumWhole) MinimumWhole = Whole - MinimumWhole + 1;
        }

        std::string Render(FloatDigits digits, const FloatCulture& culture) const {
            int exponent = 0;
            if (Scientific && !digits.IsZero()) { exponent = digits.Point - Whole; digits.Point = Whole; }
            const int whole = (std::max)(MinimumWhole, digits.IsZero() ? 0 : digits.Point);
            int fraction = Fraction;
            while (fraction > MinimumFraction && digits.At(digits.Point + fraction - 1) == '0') --fraction;
            const auto groups = GroupStarts(whole, Group ? culture.GroupSizes : "");
            std::string result;
            int left = 0, right = 0;
            auto emit = [&](int i) {
                const int index = whole - Whole + i;
                if (index < 0) return;
                if (groups[index]) result += culture.Group;
                result += digits.At(digits.Point - Whole + i);
            };
            bool extra = false;
            for (const auto& token : Tokens) {
                if (!extra && (token.Kind == 'w' || token.Kind == '.')) {
                    for (int i = Whole - whole; i < 0; ++i) emit(i);
                    extra = true;
                }
                switch (token.Kind) {
                case 'w': emit(left++); break;
                case 'f': if (right < fraction) result += digits.At(digits.Point + right); ++right; break;
                case '.': if (fraction > 0) result += culture.Decimal; break;
                case '%': result += culture.Percent; break;
                case 'p': result += culture.PerMille; break;
                case 'l': result += token.Text; break;
                case 'e': {
                    result += token.Text.front();
                    if (exponent < 0) result += culture.Negative;
                    else if (token.Text[1] == '+') result += culture.Positive;
                    std::string power = std::to_string(std::abs(exponent));
                    size_t width = std::min<size_t>(10, token.Text.size() - token.Text.find('0'));
                    if (power.size() < width) result.append(width - power.size(), '0');
                    result += power;
                    break;
                }
                }
            }
            return result;
        }
    };

    inline std::string FormatFloat(float value, std::string_view format, const FloatCulture& culture) {
        if (!std::isfinite(value)) return std::isnan(value) ? culture.NaN : std::signbit(value) ? culture.Negative + culture.Infinity : culture.Infinity;
        if (!format.empty() && (format[0] == 'C' || format[0] == 'c') && format.find_first_not_of("0123456789", 1) == format.npos)
            return Currency(value, format, culture);
        const auto sections = Sections(format);
        FloatDigits digits(value);
        size_t index = digits.IsZero() ? 2 : std::signbit(value) ? 1 : 0;
        if (sections[index].empty()) index = 0;
        CustomPattern pattern(sections[index]);
        if (!digits.IsZero()) {
            digits.Point += pattern.Scale;
            digits.Round(pattern.Scientific ? pattern.Whole + pattern.Fraction : digits.Point + pattern.Fraction);
            if (digits.IsZero()) {
                size_t zeroSection = sections[2].empty() ? 0 : 2;
                if (index != zeroSection) { index = zeroSection; pattern = CustomPattern(sections[index]); }
            }
        }
        std::string result = pattern.Render(digits, culture);
        if (index == 0 && std::signbit(value) && (!digits.IsZero() || !result.empty())) result.insert(0, culture.Negative);
        return result;
    }
}

