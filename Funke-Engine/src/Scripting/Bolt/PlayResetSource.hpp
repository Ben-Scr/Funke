#pragma once

#include "Scripting/Bolt/EntityScriptSource.hpp"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace Funke::BoltScriptHost {
    // Add a public adapter in each enclosing type so private reset methods and nested types stay private.
    inline void BindPlayResetMethods(std::string& source, const std::string& unit,
        std::vector<std::pair<std::string, std::string>>& calls) {
        if (source.find("ResetOnPlay") == std::string::npos) return;
        std::vector<std::string_view> tokens;
        auto remaining = std::string_view(source);
        while (!remaining.empty()) {
            auto token = NextBoltToken(remaining);
            if (!token.empty()) tokens.push_back(token);
        }
        struct Owner {
            std::string Name;
            size_t Body, Depth;
            bool Generic, Static;
        };
        std::vector<Owner> owners;
        std::vector<std::pair<size_t, std::string>> insertions;
        std::string nameSpace, shimBody;
        const auto shim = "FkPlayResetFile" + std::to_string(std::hash<std::string>{}(unit));
        size_t depth = 0, index = 0;
        for (size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i] == "namespace" && depth == 0) {
                nameSpace.clear();
                while (++i < tokens.size() && tokens[i] != ";") nameSpace += tokens[i];
            } else if ((tokens[i] == "class" || tokens[i] == "struct") &&
                (depth == 0 || (!owners.empty() && depth == owners.back().Depth)) &&
                (i == 0 || tokens[i-1] != ":")) {
                const bool isStatic = i != 0 && tokens[i-1] == "static";
                if (++i >= tokens.size()) break;
                std::string name(tokens[i]);
                const bool generic = i+1 < tokens.size() && tokens[i+1] == "<";
                while (++i < tokens.size() && tokens[i] != "{") {}
                if (i == tokens.size()) break;
                owners.push_back({std::move(name), static_cast<size_t>(tokens[i].data()+1-source.data()),
                    ++depth, generic, isStatic});
            } else if (tokens[i] == "{") ++depth;
            else if (tokens[i] == "}") {
                if (!owners.empty() && owners.back().Depth == depth) owners.pop_back();
                if (depth) --depth;
            } else if (tokens[i] == "[" && !owners.empty() && depth == owners.back().Depth) {
                size_t j = i+1;
                if (j+2 < tokens.size() && tokens[j] == "Funke" && tokens[j+1] == ".") j += 2;
                if (j >= tokens.size() || (tokens[j] != "ResetOnPlay" && tokens[j] != "ResetOnPlayAttribute")) continue;
                while (++j < tokens.size() && tokens[j] != "]") {}
                if (j == tokens.size()) throw std::runtime_error("Unclosed ResetOnPlay attribute.");
                size_t signature = ++j;
                while (j < tokens.size() && tokens[j] != "(" && tokens[j] != "{" && tokens[j] != ";") ++j;
                const auto first = tokens.begin() + signature;
                const auto end = tokens.begin() + j;
                if (j+1 >= tokens.size() || tokens[j] != "(" || tokens[j+1] != ")" || j < signature+2 ||
                    tokens[j-2] != "void" || std::find(first,end,"async") != end ||
                    (!owners.back().Static && std::find(first,end,"static") == end) ||
                    std::any_of(owners.begin(), owners.end(), [](const auto& type) { return type.Generic; }))
                    throw std::runtime_error("ResetOnPlay requires a static synchronous parameterless void method on a non-generic type.");
                const std::string method(tokens[j-1]);
                std::string target = method;
                const auto wrapper = "FkPlayReset" + std::to_string(++index);
                for (auto owner = owners.rbegin(); owner != owners.rend(); ++owner) {
                    insertions.push_back({owner->Body, " public static void " + wrapper + "(){" + target + "();} "});
                    target = owner->Name + "." + wrapper;
                }
                shimBody += " public static void " + wrapper + "(){" + target + "();} ";
                std::string qualified = nameSpace;
                for (const auto& owner : owners) qualified += "." + owner.Name;
                calls.emplace_back(qualified + "." + method, (nameSpace.empty() ? "" : nameSpace + ".") + shim + "." + wrapper + "();");
            }
        }
        if (insertions.empty()) return;
        std::sort(insertions.begin(), insertions.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [offset, text] : insertions) source.insert(offset, text);
        source += "\npublic static class " + shim + "{" + shimBody + "}\n";
    }
}
