#pragma once

#include "Scripting/Bolt/EntityScriptSource.hpp"
#include <algorithm>
#include <stdexcept>
#include <span>

namespace Funke::BoltScriptHost {
    struct ConsoleParameter {
        std::string Type, Name, Default;
    };

    inline std::vector<std::string> ConsoleEnumNames(std::string_view type, std::string_view source) {
        if (const auto dot = type.rfind('.'); dot != type.npos) type.remove_prefix(dot + 1);
        while (!source.empty()) {
            if (NextBoltToken(source) != "enum" || NextBoltToken(source) != type) continue;
            while (!source.empty() && NextBoltToken(source) != "{") {}
            std::vector<std::string> names;
            bool first = true;
            while (!source.empty()) {
                const auto token = NextBoltToken(source);
                if (token.empty() || token == "}") break;
                if (first && token != ",") names.emplace_back(token);
                first = token == ",";
            }
            return names;
        }
        return {};
    }

    inline std::string ConsoleAdapter(const std::string& method, const std::string& resultType,
        const std::vector<ConsoleParameter>& params, const std::string& adapter, std::string_view userSource, std::string_view sdkSource) {
        std::string code = " private static string? " + adapter + "(string[] words){";
        std::string call = method + "(";
        const bool raw = params.size() == 1 && params[0].Type == "string[]";
        if (!raw && (params.empty() || params.back().Type != "string"))
            code += "if(words.Length>" + std::to_string(params.size()) + ") throw System.ArgumentException(\"Too many arguments.\");";
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& p = params[i];
            const auto index = std::to_string(i);
            if (i) call += ",";
            if (raw) { call += "words"; continue; }
            const auto variable = "value" + index;
            call += variable;
            code += p.Type + " " + variable + "; if(words.Length<=" + index + "){";
            if (p.Default.empty()) code += "throw System.ArgumentException(\"Missing argument '" + p.Name + "'.\");";
            else code += variable + "=" + p.Default + ";";
            code += "} else { string word = words[" + index + "];";
            if (p.Type == "string") {
                if (i + 1 == params.size()) code += "for(int j=" + std::to_string(i+1) + "; j<words.Length; j++) word+=\" \"+words[j];";
                code += variable + "=word;";
            } else if (p.Type == "bool") code += variable + "=Funke.FkConsoleArgs.Bool(word);";
            else if (p.Type == "int" || p.Type == "long" || p.Type == "float" || p.Type == "double")
                code += "if(!" + p.Type + ".TryParse(word," + variable + ")) throw System.ArgumentException(\"Invalid " + p.Type + " for '" + p.Name + "'.\");";
            else if (p.Type == "Vector2" || p.Type == "Funke.Vector2" || p.Type == "Vector3" || p.Type == "Funke.Vector3")
                code += variable + "=Funke.FkConsoleArgs." + (p.Type.ends_with("2") ? "Vec2" : "Vec3") + "(word);";
            else {
                auto options = ConsoleEnumNames(p.Type, userSource);
                if (options.empty()) options = ConsoleEnumNames(p.Type, sdkSource);
                code += "int enumValue=0; if(int.TryParse(word,enumValue)) " + variable + "=(" + p.Type + ")enumValue;";
                for (const auto& option : options)
                    code += "else if(word.ToLower()==\"" + option + "\".ToLower()) " + variable + "=" + p.Type + "." + option + ";";
                code += "else throw System.ArgumentException(\"Invalid " + p.Type + " for '" + p.Name + "'.\");";
            }
            code += "}";
        }
        call += ")";
        if (resultType == "void") return code + call + ";return null;}";
        if (resultType == "string" || resultType == "string?") return code + "return " + call + ";}";
        return code + "return (string)" + call + ";}";
    }

    // Record the source tokens before inserting generated members so strings, comments and offsets stay intact.
    struct ConsoleDeclaration { std::string NameLiteral, DescriptionLiteral, Usage; };

    inline std::string BindConsoleAttributes(std::string& source, std::string_view sdkSource,
        std::vector<ConsoleDeclaration>* declarations = nullptr) {
        if (source.find("DevCommand") == source.npos) return {};
        std::vector<std::string_view> tokens;
        std::string_view tail(source);
        while (!tail.empty()) {
            auto t = NextBoltToken(tail);
            if (!t.empty()) tokens.push_back(t);
        }
        struct Edit { size_t Offset, Length; std::string Text; };
        std::vector<Edit> edits;
        std::string ns, setup;
        size_t serial = 0;
        for (size_t i = 0; i + 2 < tokens.size(); ++i) {
            if (tokens[i] == "namespace") {
                ns.clear();
                for (++i; i < tokens.size() && tokens[i] != ";" && tokens[i] != "{"; ++i) ns += tokens[i];
                continue;
            }
            if (tokens[i] != "class" && tokens[i] != "struct") continue;
            const std::string owner = std::string(tokens[++i]);
            while (++i < tokens.size() && tokens[i] != "{") {}
            if (i == tokens.size()) break;
            const size_t body = static_cast<size_t>(tokens[i].data() - source.data()) + 1;
            std::string wrappers, registerCalls;
            int depth = 1;
            for (++i; i < tokens.size() && depth; ++i) {
                if (tokens[i] == "{") { ++depth; continue; }
                if (tokens[i] == "}") { --depth; continue; }
                if (depth != 1 || tokens[i] != "[") continue;
                size_t cursor = i + 1;
                if (cursor + 2 < tokens.size() && tokens[cursor] == "Funke" && tokens[cursor+1] == ".") cursor += 2;
                if (cursor == tokens.size() || (tokens[cursor] != "DevCommand" && tokens[cursor] != "DevCommandAttribute")) continue;
                std::string name, description = "\"\"";
                for (++cursor; cursor < tokens.size() && tokens[cursor] != "]"; ++cursor) {
                    if (tokens[cursor].front() != '"') continue;
                    if (name.empty() && !(cursor >= 2 && tokens[cursor-2] == "Description")) name = tokens[cursor];
                    else description = tokens[cursor];
                }
                if (cursor == tokens.size()) throw std::runtime_error("Unclosed DevCommand attribute.");
                edits.push_back({static_cast<size_t>(tokens[i].data()-source.data()),
                    static_cast<size_t>(tokens[cursor].data()+1-tokens[i].data()), ""});
                const size_t signature = cursor + 1;
                while (++cursor < tokens.size() && tokens[cursor] != "(" && tokens[cursor] != ";" && tokens[cursor] != "{") {}
                if (cursor >= tokens.size() || tokens[cursor] != "(" || cursor < signature + 2) continue;
                const auto flags = std::span(tokens).subspan(signature, cursor-signature);
                if (std::find(flags.begin(), flags.end(), "static") == flags.end()) continue;
                const std::string method(tokens[cursor-1]);
                if (method == ">") continue;
                std::string returnType(tokens[cursor-2]);
                if (returnType == "?" && cursor > 2) returnType = std::string(tokens[cursor-3]) + "?";
                if (name.empty() || name == "\"\"") name = "\"" + method + "\"";
                std::vector<ConsoleParameter> parameters;
                while (++cursor < tokens.size() && tokens[cursor] != ")") {
                    size_t start = cursor;
                    while (cursor < tokens.size() && tokens[cursor] != "=" && tokens[cursor] != "," && tokens[cursor] != ")") ++cursor;
                    if (cursor < start + 2 || cursor == tokens.size()) throw std::runtime_error("Invalid DevCommand parameter.");
                    ConsoleParameter parameter;
                    for (size_t k = start; k + 1 < cursor; ++k) parameter.Type += tokens[k];
                    parameter.Name = tokens[cursor-1];
                    if (tokens[cursor] == "=") {
                        for (++cursor; cursor < tokens.size() && tokens[cursor] != "," && tokens[cursor] != ")"; ++cursor)
                            parameter.Default += tokens[cursor];
                    }
                    parameters.push_back(std::move(parameter));
                    if (cursor == tokens.size() || tokens[cursor] == ")") break;
                }
                const auto adapter = "FkConsoleCommand" + std::to_string(++serial);
                wrappers += ConsoleAdapter(method, returnType, parameters, adapter, source, sdkSource);
                std::string usage;
                for (const auto& parameter : parameters) {
                    if (!usage.empty()) usage += " ";
                    if (parameter.Type == "string[]") usage += "[args...]";
                    else usage += (parameter.Default.empty() ? "<" : "[") + parameter.Name + ":" + parameter.Type + (parameter.Default.empty() ? ">" : "]");
                }
                if (declarations) declarations->push_back({name, description, usage});
                registerCalls += " Funke.DevConsole.FkRegister(" + name + "," + adapter + "," + description + ",\"" + usage + "\");";
            }
            --i;
            if (registerCalls.empty()) continue;
            wrappers += " private static bool FkConsoleReady; public static void FkSetupDevCommands(){if(FkConsoleReady)return;"
                + registerCalls + "FkConsoleReady=true;} ";
            edits.push_back({body,0,std::move(wrappers)});
            setup += (ns.empty() ? owner : ns + "." + owner) + ".FkSetupDevCommands();";
        }
        std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.Offset > b.Offset; });
        for (const auto& e : edits) source.replace(e.Offset, e.Length, e.Text);
        return setup;
    }
}
