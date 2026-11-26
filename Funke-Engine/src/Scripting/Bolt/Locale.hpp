#pragma once
#include "Scripting/Bolt/NativeApi.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>

namespace Funke::BoltScriptHost::Locales {
    inline std::optional<std::locale> Open(std::string name) {
        if (name.empty()) return std::locale::classic();
        for (const char* suffix : {"", ".UTF-8"}) {
            try { return std::locale((name + suffix).c_str()); } catch (const std::runtime_error&) {}
        }
        std::replace(name.begin(), name.end(), '-', '_');
        try { return std::locale((name + ".UTF-8").c_str()); } catch (const std::runtime_error&) {}
        return std::nullopt;
    }

    inline const std::optional<std::locale>& Find(const std::string& name) {
        thread_local std::string previous;
        thread_local std::optional<std::locale> locale(std::locale::classic());
        if (name != previous) {
            locale = Open(name);
            previous = name;
        }
        return locale;
    }

    inline uint64_t Call(uint64_t op, void* tag, void* arguments) noexcept {
        auto* output = FrameText();
        if (!output) return 0;
        try {
            std::string name = tag ? static_cast<const char*>(tag) : "";
            const auto& found = Find(name);
            const auto locale = found.value_or(std::locale::classic());
            const auto& punctuation = std::use_facet<std::numpunct<char>>(locale);
            if (op == 0) *output = found ? name : "";
            else if (op == 1) output->assign(1, punctuation.decimal_point());
            else if (op == 2) output->assign(1, punctuation.thousands_sep());
            else {
                if (!arguments) return 0;
                const auto* words = static_cast<const uint64_t*>(arguments);
                const auto* format = reinterpret_cast<const char*>(static_cast<uintptr_t>(words[0]));
                std::string_view spec = format ? format : "";
                const char code = spec.empty() ? 'G' : spec.front();
                const char mode = static_cast<char>(std::toupper(static_cast<unsigned char>(code)));
                int precision = -1;
                if (spec.size() > 1) {
                    precision = 0;
                    for (const char c : spec.substr(1)) {
                        if (c < '0' || c > '9' || precision > 99) return 0;
                        precision = precision * 10 + c - '0';
                    }
                }
                std::ostringstream number;
                number.imbue(mode == 'N' ? locale : std::locale::classic());
                if (code >= 'A' && code <= 'Z') number << std::uppercase;
                if (mode == 'D' || mode == 'X' || ((mode == 'G' || mode == 'R') && op != 3)) {
                    if (op == 3) return 0;
                    if (mode == 'X') number << std::hex;
                    const bool negative = op == 4 && mode != 'X' && std::bit_cast<int64_t>(words[1]) < 0;
                    if (negative) number << '-';
                    if (precision > 0) number << std::setfill('0') << std::setw(precision);
                    number << (negative ? uint64_t{0} - words[1] : words[1]);
                } else {
                    auto value = op == 3 ? static_cast<long double>(std::bit_cast<double>(words[1])) :
                        op == 4 ? static_cast<long double>(std::bit_cast<int64_t>(words[1])) : static_cast<long double>(words[1]);
                    if (mode == 'F' || mode == 'N' || mode == 'P') number << std::fixed << std::setprecision(precision < 0 ? 2 : precision);
                    else if (mode == 'E') number << std::scientific << std::setprecision(precision < 0 ? 6 : precision);
                    else if (mode == 'G' || mode == 'R') number << std::setprecision(precision <= 0 ? 15 : precision);
                    else return 0;
                    number << (mode == 'P' ? value * 100.0L : value);
                    if (mode == 'P') number << " %";
                }
                *output = number.str();
                if (mode != 'N' && punctuation.decimal_point() != '.')
                    std::replace(output->begin(), output->end(), '.', punctuation.decimal_point());
            }
            return output->size();
        } catch (...) { return 0; }
    }
}
