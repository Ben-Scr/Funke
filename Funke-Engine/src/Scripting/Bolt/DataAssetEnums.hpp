#pragma once

#include "Scripting/Bolt/EntityScriptSource.hpp"
#include <unordered_map>

namespace Funke::BoltScriptHost {
    struct DataEnum {
        std::string Name, Underlying = "int";
        std::vector<std::string> Members;
        bool Flags = false;
    };
    using DataEnums = std::unordered_map<std::string, DataEnum>;

    // Read member names and their declared integer type. Bolt evaluates all values,
    // including aliases, flags and constant expressions, when describing the asset.
    inline void CollectDataEnums(std::string_view source, DataEnums& result) {
        std::string scope, nextType;
        std::vector<size_t> scopes;
        bool flags = false;
        while (!source.empty()) {
            const auto token = NextBoltToken(source);
            if (token == "namespace") {
                scope.clear();
                for (auto word = NextBoltToken(source); !word.empty() && word != ";"; word = NextBoltToken(source))
                    scope += word;
            } else if (token == "class" || token == "struct") {
                nextType = NextBoltToken(source);
            } else if (token == "{") {
                scopes.push_back(scope.size());
                if (!nextType.empty()) scope += (scope.empty() ? "" : ".") + nextType;
                nextType.clear();
            } else if (token == "}" && !scopes.empty()) {
                scope.resize(scopes.back());
                scopes.pop_back();
            } else if (token == "[") {
                for (auto attr = NextBoltToken(source); !attr.empty() && attr != "]"; attr = NextBoltToken(source))
                    if (attr == "Flags" || attr == "FlagsAttribute") flags = true;
            } else if (token == "enum") {
                DataEnum found;
                found.Name = scope.empty() ? "" : scope + ".";
                found.Name += NextBoltToken(source);
                auto part = NextBoltToken(source);
                if (part == ":") {
                    found.Underlying = NextBoltToken(source);
                    part = NextBoltToken(source);
                }
                if (part != "{") continue;
                found.Flags = flags;
                flags = false;
                bool member = true, payload = false;
                int nesting = 0;
                while (!(part = NextBoltToken(source)).empty() && part != "}") {
                    if (member && part != ",") {
                        found.Members.emplace_back(part);
                        auto tail = source;
                        if (NextBoltToken(tail) == "(") payload = true;
                    }
                    if (part == "(") ++nesting;
                    if (part == ")") --nesting;
                    member = part == "," && nesting == 0;
                }
                if (!payload && !found.Members.empty()) result.insert_or_assign(found.Name, std::move(found));
            } else if (token == ";" || token == "=") {
                flags = false;
            }
        }
    }

    inline const DataEnum* DataEnumFor(const DataEnums& enums, std::string_view owner, std::string_view name) {
        std::string scope(owner);
        for (;;) {
            auto it = enums.find(scope.empty() ? std::string(name) : scope + "." + std::string(name));
            if (it != enums.end()) return &it->second;
            if (scope.empty()) break;
            const auto dot = scope.rfind('.');
            scope.resize(dot == scope.npos ? 0 : dot);
        }
        const DataEnum* match = nullptr;
        const auto suffix = "." + std::string(name);
        for (const auto& [type, definition] : enums) {
            if (!type.ends_with(suffix)) continue;
            if (match) return nullptr;
            match = &definition;
        }
        return match;
    }
}
