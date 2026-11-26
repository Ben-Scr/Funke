#pragma once
#include "Scripting/Bolt/EntityScriptSource.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace Funke::BoltScriptHost {
    inline std::string BindJobQueries(std::string source) {
        if (source.find("IJobQuery") == source.npos)
            return source;
        std::vector<std::string_view> tokens;
        std::string_view rest = source;
        while (!rest.empty()) {
            auto token = NextBoltToken(rest);
            if (!token.empty())
                tokens.push_back(token);
        }
        auto at = [&](size_t i) { return i < tokens.size() ? tokens[i] : std::string_view{}; };
        struct Injection {
            size_t Position, Length;
            std::string Code;
        };
        std::vector<Injection> edits;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (at(i) != "struct" || at(i + 2) != ":")
                continue;
            size_t brace = i + 3;
            bool query = false;
            while (brace < tokens.size() && at(brace) != "{") {
                query |= at(brace) == "IJobQuery";
                ++brace;
            }
            if (!query || brace == tokens.size())
                continue;
            std::string with, without;
            int enabled = 1;
            size_t prefix = i;
            while (prefix &&
                   (at(prefix - 1) == "public" || at(prefix - 1) == "internal" || at(prefix - 1) == "private"))
                --prefix;
            while (prefix && at(prefix - 1) == "]") {
                size_t begin = prefix - 1;
                while (begin && at(begin) != "[")
                    --begin;
                if (at(begin) != "[")
                    break;
                size_t attr = begin + 1;
                while (at(attr + 1) == ".")
                    attr += 2;
                if (at(attr) == "WithAll" || at(attr) == "WithoutAll") {
                    auto& expression = at(attr) == "WithAll" ? with : without;
                    for (size_t j = attr + 2; j < prefix - 1;) {
                        if (at(j) == ")" || at(j) == ",") {
                            ++j;
                            continue;
                        }
                        if (at(j) == "typeof" && at(j + 1) == "(")
                            j += 2;
                        std::string type(at(j++));
                        while (at(j) == ".") {
                            type += "." + std::string(at(j + 1));
                            j += 2;
                        }
                        if (type.empty())
                            throw std::runtime_error("Expected component type in job filter");
                        if (!expression.empty())
                            expression += "+\"|\"+";
                        expression += "Funke.FunkeApiTypes.NativeKey<" + type + ">()";
                    }
                } else if (at(attr) == "EnabledFilter") {
                    auto mode = at(prefix - 3);
                    if (mode == "EnabledOnly" || mode == "1")
                        enabled = 1;
                    else if (mode == "DisabledOnly" || mode == "2")
                        enabled = 2;
                    else if (mode == "IncludeDisabled" || mode == "0")
                        enabled = 0;
                    else
                        throw std::runtime_error("Unknown IJobQuery enabled filter");
                }
                if (at(attr) == "WithAll" || at(attr) == "WithoutAll" || at(attr) == "EnabledFilter") {
                    auto start = static_cast<size_t>(at(begin).data() - source.data());
                    auto length = at(prefix - 1).data() + 1 - at(begin).data();
                    auto whitespace = source.substr(start, length);
                    for (auto& ch : whitespace)
                        if (ch != '\n' && ch != '\r')
                            ch = ' ';
                    edits.push_back({start, static_cast<size_t>(length), std::move(whitespace)});
                }
                prefix = begin;
            }
            std::string writes, reads, args;
            struct Param {
                std::string Type;
                bool Read;
            };
            std::vector<Param> params;
            bool index = false, execute = false;
            size_t end = brace + 1;
            for (int depth = 1; end < tokens.size() && depth; ++end) {
                if (at(end) == "{")
                    ++depth;
                else if (at(end) == "}")
                    --depth;
                else if (depth == 1 && at(end) == "Execute" && at(end + 1) == "(") {
                    if (execute)
                        throw std::runtime_error("IJobQuery requires one Execute method");
                    execute = true;
                    size_t pos = end + 2;
                    if (at(pos) == "int" && at(pos + 2) == ",") {
                        index = true;
                        pos += 3;
                    }
                    while (pos < tokens.size() && at(pos) != ")") {
                        bool read = at(pos) == "const";
                        if (read)
                            ++pos;
                        std::string type(at(pos++));
                        while (at(pos) == ".") {
                            type += "." + std::string(at(pos + 1));
                            pos += 2;
                        }
                        if (at(pos) != "&")
                            throw std::runtime_error("IJobQuery Execute expects component references");
                        pos += 2;
                        auto& names = read ? reads : writes;
                        names += (names.empty() ? "" : "+\"|\"+") + std::string("Funke.FunkeApiTypes.NativeKey<") +
                                 type + ">()";
                        params.push_back({type, read});
                        if (at(pos) == ")")
                            break;
                        if (at(pos++) != ",")
                            throw std::runtime_error("Invalid IJobQuery component parameter");
                    }
                }
            }
            if (!execute || params.empty())
                throw std::runtime_error("IJobQuery requires Execute with component references");
            size_t numWrites = std::count_if(params.begin(), params.end(), [](const auto& p) { return !p.Read; });
            size_t writeIndex = 0, readIndex = numWrites;
            if (index)
                args = "row";
            for (const auto& param : params) {
                if (!args.empty())
                    args += ",";
                const auto slot = param.Read ? readIndex++ : writeIndex++;
                args += "*(" + param.Type + "*)cells[row*" + std::to_string(params.size()) + "+" +
                        std::to_string(slot) + "]";
            }
            std::string code = " public: string FkWrites(){return " + (writes.empty() ? "\"\"" : writes) +
                               ";} string FkReads(){return " + (reads.empty() ? "\"\"" : reads) +
                               ";} void FkExecute(int row,void** cells){Execute(" + args + ");}";
            auto rangeLoop=[](const std::string& arguments) {
                const std::string invoke="Execute("+arguments+");";
                return "if(token.CanBeCanceled){for(int row=first;row<end;row++){token.ThrowIfCancellationRequested();"+invoke+
                    "}}else{for(int row=first;row<end;row++){"+invoke+"}}";
            };
            code+=" void FkRunRows(int first,int end,void** cells,void* dense,System.Threading.CancellationToken token){";
            if (params.size()==1) {
                std::string denseArgs=index?"row,":"";
                denseArgs+="(("+params[0].Type+"*)dense)[row]";
                code+="if(dense!=null){"+rangeLoop(denseArgs)+"return;}";
            }
            code+=rangeLoop(args)+"}";
            code += " string FkWith(){return " + (with.empty() ? "\"\"" : with) + ";} string FkWithout(){return " +
                    (without.empty() ? "\"\"" : without) + ";} int FkEnableMode(){return " + std::to_string(enabled) +
                    ";}";
            code += " private static ulong fkPlan;private static string fkPlanScene; public: ulong FkOpenPlan(string scene){"
                    "if(fkPlan!=0ul && fkPlanScene==scene){ulong view=Funke.FunkeApiTypes.OpenJobPlan((ulong)FkEnableMode(),fkPlan);"
                    "if(view!=18446744073709551615ul)return view;}"
                    "fkPlan=Funke.FunkeApiTypes.RegisterJobPlan(scene+\"\\n\"+FkWrites()+\"\\n\"+FkReads()+\"\\n\"+FkWith()+\"\\n\"+FkWithout());"
                    "fkPlanScene=scene;return Funke.FunkeApiTypes.OpenJobPlan((ulong)FkEnableMode(),fkPlan);}";
            edits.push_back({static_cast<size_t>(at(end - 1).data() - source.data()), 0, std::move(code)});
            i = end - 1;
        }
        std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.Position > b.Position; });
        for (const auto& edit : edits)
            source.replace(edit.Position, edit.Length, edit.Code);
        return source;
    }
} // namespace Funke::BoltScriptHost
