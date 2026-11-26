#pragma once

#include "Scripting/Bolt/EntityScriptSource.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>
#include <vector>

namespace Funke::BoltScriptHost {
    // A suspension stores the next block and the invocation's locals. The blocks themselves compile with the native
    // JIT.
    class CoroutineLowering {
        std::string_view m_Source;
        std::vector<std::string_view> m_Tokens;
        size_t m_Pos = 0;
        std::map<std::string, std::string> m_Locals;
        std::map<std::string, std::string> m_Bindings;
        std::string BindLocal(const std::string& name, std::string type) {
            const auto field = "Local" + std::to_string(m_Locals.size());
            m_Locals.emplace(field, std::move(type));
            m_Bindings[name] = field;
            return field;
        }
        std::vector<std::string> m_Blocks;
        struct Cleanup {
            int Parent, Entry;
            std::string Handlers;
        };
        int m_Catch = 0;
        bool m_ExternalToken = false, m_FrameWait = false, m_TimeWait = false, m_FixedWait = false;
        std::vector<Cleanup> m_Cleanups;
        std::vector<int> m_BlockRegions;
        int m_Region = 0;
        std::string m_Unwind, m_ReturnType;
        std::vector<std::string> m_Methods;
        struct Edit {
            size_t Start, End;
            std::string Text;
        };
        std::vector<Edit> m_Edits;

        size_t Offset(size_t i) const {
            return i == m_Tokens.size() ? m_Source.size() : m_Tokens.at(i).data() - m_Source.data();
        }
        std::string_view At(size_t i) const { return i < m_Tokens.size() ? m_Tokens[i] : std::string_view{}; }
        [[noreturn]] void Error(const std::string& why) const {
            const auto line =
                1 + std::count(m_Source.begin(), m_Source.begin() + Offset(std::min(m_Pos, m_Tokens.size())), '\n');
            throw std::runtime_error("line " + std::to_string(line) + ": " + why);
        }
        size_t Close(size_t i, std::string_view open, std::string_view close) const {
            if (At(i) != open)
                Error("expected " + std::string(open));
            int depth = 1;
            while (++i < m_Tokens.size()) {
                if (At(i) == open)
                    ++depth;
                else if (At(i) == close && --depth == 0)
                    return i;
            }
            Error("missing " + std::string(close));
        }
        std::string Slice(size_t i, size_t j, bool locals = true) const {
            if (i == j)
                return {};
            std::string result;
            auto start = Offset(i);
            for (size_t k = i; locals && k < j; ++k) {
                if ((k == i || At(k - 1) != ".") && m_Bindings.contains(std::string(At(k)))) {
                    result += m_Source.substr(start, Offset(k) - start);
                    result += "fkFrame." + m_Bindings.at(std::string(At(k)));
                    start = Offset(k) + At(k).size();
                }
            }
            result += m_Source.substr(start, Offset(j - 1) + At(j - 1).size() - start);
            return result;
        }
        std::string Jump(int to) const {
            auto jump = "fkFrame.Pc=" + std::to_string(to) + "; continue;";
            if (m_Region && m_BlockRegions.at(to) != m_Region)
                jump = m_Unwind + "(fkFrame," + std::to_string(m_BlockRegions[to]) + "," + std::to_string(to) +
                       ");continue;";
            return jump;
        }
        int Block(std::string code = {}) {
            m_BlockRegions.push_back(m_Region);
            m_Blocks.push_back(std::move(code));
            return static_cast<int>(m_Blocks.size()) - 1;
        }
        std::string Condition() {
            const auto end = Close(m_Pos, "(", ")");
            auto value = Slice(m_Pos + 1, end);
            m_Pos = end + 1;
            return value;
        }
        static bool Identifier(std::string_view text) {
            return !text.empty() && (std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_');
        }
        std::string ExpressionStatement(size_t start, size_t end) {
            if (start == end)
                return {};
            size_t name = start + 1;
            while (name < end) {
                if (At(name) == "." && Identifier(At(name + 1))) {
                    name += 2;
                    continue;
                }
                if (At(name) == "<") {
                    size_t i = name + 1;
                    int depth = 1;
                    for (; i < end && depth; i++) {
                        if (At(i) == "<")
                            ++depth;
                        else if (At(i) == ">")
                            --depth;
                    }
                    if (depth)
                        break;
                    name = i;
                    continue;
                }
                if (At(name) == "[" && At(name + 1) == "]") {
                    name += 2;
                    continue;
                }
                if (At(name) == "?") {
                    ++name;
                    continue;
                }
                break;
            }
            if (Identifier(At(start)) && Identifier(At(name)) && name < end &&
                (name + 1 == end || At(name + 1) == "=")) {
                auto local = std::string(At(name));
                auto type = Slice(start, name, false);
                if (type == "var")
                    Error("declare coroutine locals with an explicit type");
                local = BindLocal(local, type);
                if (name + 1 == end)
                    return "fkFrame." + local + " = default;";
                return "fkFrame." + local + " = " + Slice(name + 2, end) + ";";
            }
            return Slice(start, end) + ";";
        }

        void Statement(int in, int out, int onBreak, int onContinue) {
            const auto token = At(m_Pos);
            if (token == "{") {
                auto outer = m_Bindings;
                ++m_Pos;
                int current = in;
                while (At(m_Pos) != "}") {
                    if (At(m_Pos).empty())
                        Error("unterminated coroutine block");
                    int next = Block();
                    Statement(current, next, onBreak, onContinue);
                    current = next;
                }
                ++m_Pos;
                m_Blocks[current] = Jump(out);
                m_Bindings.swap(outer);
                return;
            }
            if (token == "try") {
                const auto start = ++m_Pos;
                const auto closeTry = Close(start, "{", "}");
                size_t next = closeTry + 1;
                struct Handler {
                    size_t Open, Body, End;
                };
                std::vector<Handler> handlers;
                while (At(next) == "catch") {
                    size_t open = next + 1, body = At(open) == "(" ? Close(open, "(", ")") + 1 : open;
                    next = Close(body, "{", "}") + 1;
                    handlers.push_back({open, body, next});
                }
                const int parent = m_Region;
                int cleanupRegion = parent, cleanup = -1, finish = -1;
                const bool hasFinally = At(next) == "finally";
                if (hasFinally) {
                    cleanup = Block();
                    finish = Block();
                    m_Cleanups.push_back({parent, cleanup, {}});
                    cleanupRegion = static_cast<int>(m_Cleanups.size());
                } else if (handlers.empty())
                    Error("expected catch or finally after try");
                m_Region = cleanupRegion;
                int bodyRegion = cleanupRegion;
                if (!handlers.empty()) {
                    m_Cleanups.push_back({cleanupRegion, -1, {}});
                    bodyRegion = static_cast<int>(m_Cleanups.size());
                }
                m_Region = bodyRegion;
                const int body = Block();
                m_Blocks[in] = "fkFrame.Region=" + std::to_string(bodyRegion) + ";fkFrame.Pc=" + std::to_string(body) +
                               ";continue;";
                Statement(body, out, onBreak, onContinue);
                m_Region = cleanupRegion;
                std::string dispatch;
                for (const auto& handler : handlers) {
                    const int entry = Block();
                    auto bindings = m_Bindings;
                    const auto savedCatch = m_Catch;
                    m_Catch = bodyRegion;
                    const auto slot = "Caught" + std::to_string(bodyRegion);
                    m_Locals.emplace(slot, "Bolt.Runtime.ExceptionState");
                    dispatch += " catch";
                    std::string capture;
                    if (handler.Body != handler.Open) {
                        const auto close = handler.Body - 1;
                        size_t arg = handler.Open + 1;
                        if (At(arg) == "const")
                            ++arg;
                        size_t typeEnd = arg + 1;
                        while (At(typeEnd) == ".")
                            typeEnd += 2;
                        const auto type = Slice(arg, typeEnd, false);
                        if (At(typeEnd) == "&")
                            ++typeEnd;
                        if (typeEnd != close) {
                            if (typeEnd + 1 != close || !Identifier(At(typeEnd)))
                                Error("invalid catch parameter");
                            const auto local = BindLocal(std::string(At(typeEnd)), type);
                            capture = "fkFrame." + local + "=caught;";
                        }
                        dispatch += "(" + type + "& caught)";
                    }
                    dispatch += "{fkFrame." + slot + "=fkFrame.Error;" + capture +
                                "fkFrame.Pc=" + std::to_string(entry) + ";return;}";
                    m_Pos = handler.Body;
                    Statement(entry, out, onBreak, onContinue);
                    m_Catch = savedCatch;
                    m_Bindings.swap(bindings);
                }
                if (!handlers.empty())
                    m_Cleanups[bodyRegion - 1].Handlers = "try{try{fkFrame.Error.Rethrow();}" + dispatch + "}catch{}";
                m_Region = parent;
                m_Pos = next;
                if (hasFinally) {
                    ++m_Pos;
                    Statement(cleanup, finish, onBreak, onContinue);
                    const auto suffix = std::to_string(cleanupRegion);
                    m_Blocks[finish] =
                        m_Unwind + "(fkFrame,fkFrame.ExitRegion" + suffix + ",fkFrame.ExitPc" + suffix + ");continue;";
                }
                return;
            }
            if (token == "if" || token == "while") {
                ++m_Pos;
                const auto condition = Condition();
                int yes = Block(), no = out;
                if (token == "while")
                    Statement(yes, in, out, in);
                else {
                    Statement(yes, out, onBreak, onContinue);
                    if (At(m_Pos) == "else") {
                        ++m_Pos;
                        no = Block();
                        Statement(no, out, onBreak, onContinue);
                    }
                }
                m_Blocks[in] = condition=="true" ? Jump(yes) : condition=="false" ? Jump(no) : "if (" + condition + ") {" + Jump(yes) + "}" + Jump(no);
                return;
            }
            if (token == "for") {
                auto outer = m_Bindings;
                const auto end = Close(++m_Pos, "(", ")");
                const auto init = ++m_Pos;
                while (m_Pos < end && At(m_Pos) != ";")
                    ++m_Pos;
                const auto initialize = ExpressionStatement(init, m_Pos);
                const auto cond = ++m_Pos;
                while (m_Pos < end && At(m_Pos) != ";")
                    ++m_Pos;
                if (m_Pos >= end)
                    Error("invalid coroutine for-loop");
                const std::string condition = cond == m_Pos ? "true" : Slice(cond, m_Pos);
                const auto increment = Slice(m_Pos + 1, end);
                m_Pos = end + 1;
                const int test = Block(), body = Block(), step = Block(increment + ";" + Jump(test));
                m_Blocks[in] = initialize + Jump(test);
                m_Blocks[test] = "if (" + condition + ") {" + Jump(body) + "}" + Jump(out);
                Statement(body, step, out, step);
                m_Bindings.swap(outer);
                return;
            }
            const size_t begin = m_Pos;
            while (!At(m_Pos).empty() && At(m_Pos) != ";" && At(m_Pos) != "}") {
                if (At(m_Pos) == "{")
                    Error("unsupported structured statement in native coroutine: " + std::string(token));
                ++m_Pos;
            }
            if (At(m_Pos) != ";")
                Error("expected ';' in coroutine");
            const size_t end = m_Pos++;
            if (token == "return") {
                if (At(begin + 1) == "await" && m_ReturnType != "void") {
                    const auto task = "Returned" + std::to_string(in);
                    m_Locals.emplace(task, "System.Threading.Tasks.Task<" + m_ReturnType + ">");
                    const int check =
                        Block("if(!fkFrame." + task + ".IsCompleted)return true;fkFrame.Completion.SetResult(fkFrame." +
                              task + ".GetResult());" + Jump(1));
                    m_Blocks[in] = "fkFrame." + task + "=" + Slice(begin + 2, end) + ";" + Jump(check);
                    return;
                }
                if ((m_ReturnType == "void") != (end == begin + 1))
                    Error("coroutine return value does not match its Task");
                m_Blocks[in] = (end == begin + 1 ? std::string{}
                                                 : "fkFrame.Completion.SetResult(" + Slice(begin + 1, end) + ");") +
                               Jump(1);
            } else if (token == "throw" && end == begin + 1 && m_Catch) {
                m_Blocks[in] = "fkFrame.Caught" + std::to_string(m_Catch) + ".Rethrow();";
            } else if (token == "break" || token == "continue") {
                const auto target = token == "break" ? onBreak : onContinue;
                if (target < 0)
                    Error("loop control outside a coroutine loop");
                m_Blocks[in] = Jump(target);
            } else if (auto awaited = std::find(m_Tokens.begin() + begin, m_Tokens.begin() + end, "await");
                       awaited != m_Tokens.begin() + end) {
                const size_t start = awaited - m_Tokens.begin();
                size_t op = start + 1;
                if (At(op) == "new")
                    ++op;
                while (At(op + 1) == ".")
                    op += 2;
                const auto wait = At(op);
                const bool gameplay =
                    At(op + 1) == "(" && (wait == "WaitForNextFrame" || wait == "WaitForFixedUpdate" ||
                                          wait == "WaitUntil" || wait == "WaitForFrames" || wait == "WaitForSeconds");
                if (!gameplay) {
                    auto expression = Slice(start + 1, end);
                    const bool assign = start != begin;
                    std::string target, type;
                    if (assign) {
                        if (At(start - 1) != "=" || start < begin + 2)
                            Error("expected assignment before await");
                        if (start == begin + 2) {
                            target = std::string(At(begin));
                            if (!m_Bindings.contains(target))
                                Error("await result requires a typed coroutine local");
                            target = m_Bindings.at(target);
                            type = m_Locals.at(target);
                        } else {
                            type = Slice(begin, start - 2, false);
                            if (type == "var")
                                Error("use an explicit type for the awaited result");
                            target = BindLocal(std::string(At(start - 2)), type);
                        }
                    }
                    const auto task = "Awaited" + std::to_string(in);
                    if (assign) {
                        m_Locals.emplace(task, "System.Threading.Tasks.Task<" + type + ">");
                        const int check = Block("if (!fkFrame." + task + ".IsCompleted) return true; fkFrame." +
                                                target + "=fkFrame." + task + ".GetResult();" + Jump(out));
                        m_Blocks[in] = "fkFrame." + task + "=" + expression + ";" + Jump(check);
                    } else {
                        const int check = Block("if (!fkFrame.Observe.Value.Invoke()) return true;" + Jump(out));
                        m_Blocks[in] =
                            "fkFrame.Observe=Funke.FunkeApiTypes.ObserveTask(" + expression + ");" + Jump(check);
                    }
                    return;
                }
                const auto close = Close(op + 1, "(", ")");
                if (close + 1 != end || start != begin)
                    Error("invalid gameplay await");
                size_t arg = op + 2, split = close;
                for (size_t i = arg; i < close; i++) {
                    if (At(i) == "(")
                        i = Close(i, "(", ")");
                    else if (At(i) == ",") {
                        split = i;
                        break;
                    }
                }
                const bool phase = wait == "WaitForNextFrame" || wait == "WaitForFixedUpdate";
                auto tokenStart = phase ? arg : split < close ? split + 1 : close;
                const bool external = tokenStart < close;
                m_ExternalToken |= external;
                m_FrameWait |= wait=="WaitForFrames";
                m_TimeWait |= wait=="WaitForSeconds";
                m_FixedWait |= wait=="WaitForFixedUpdate";
                std::string setup = external
                    ? "fkFrame.OwnerWait=false;fkFrame.Token=" + Slice(tokenStart,close) + ";fkFrame.Token.ThrowIfCancellationRequested();"
                    : "fkFrame.OwnerWait=true;if(fkOwnerCanceled)throw System.OperationCanceledException(\"The operation was canceled.\");";
                const std::string clearWait = external ? "fkFrame.Token=default;" : "fkFrame.OwnerWait=false;";
                std::string mode;
                if (wait == "WaitForNextFrame")
                    mode = "";
                else if (wait == "WaitForFixedUpdate")
                    mode = "fkFrame.Fixed=true;";
                else if (wait == "WaitForFrames")
                    mode = "fkFrame.Frames=Mathf.Max(1," + Slice(arg, split) + ");";
                else if (wait == "WaitForSeconds")
                    mode = "fkFrame.Seconds=" + Slice(arg, split) + ";";
                else {
                    if (At(arg) == "(" && At(arg + 1) == ")" && At(arg + 2) == "=" && At(arg + 3) == ">")
                        arg += 4;
                    auto predicate = Slice(arg, split);
                    if (split == arg + 1 && arg == op + 2)
                        predicate += "()";
                    const int check = Block("if (!(" + predicate + ")) return true;" + clearWait + Jump(out));
                    m_Blocks[in] = setup + "fkFrame.Pc=" + std::to_string(check) + ";return true;";
                    return;
                }
                const int done = Block(clearWait + Jump(out));
                m_Blocks[in] = setup + mode + "fkFrame.Pc=" + std::to_string(done) + ";return true;";
            } else
                m_Blocks[in] = ExpressionStatement(begin, end) + Jump(out);
        }

      public:
        explicit CoroutineLowering(std::string_view source) : m_Source(source) {
            while (!source.empty()) {
                auto token = NextBoltToken(source);
                if (!token.empty())
                    m_Tokens.push_back(token);
            }
        }

        std::string Lower(size_t body, std::string className) {
            const auto classEnd =
                Close(std::lower_bound(m_Tokens.begin(), m_Tokens.end(), body - 1,
                                       [&](std::string_view t, size_t pos) {
                                           return static_cast<size_t>(t.data() - m_Source.data()) < pos;
                                       }) -
                          m_Tokens.begin(),
                      "{", "}");
            for (size_t k = 0; k < classEnd; ++k) {
                if (Offset(k) < body || At(k) != "async" || At(k + 1) != "Task")
                    continue;
                const size_t n = At(k + 2) == "<" ? Close(k + 2, "<", ">") + 1 : k + 2;
                if (Identifier(At(n)) && At(n + 1) == "(")
                    m_Methods.emplace_back(At(n));
            }
            std::string frames, pump, cancel, counts = "0";
            std::replace(className.begin(), className.end(), '.', '_');
            for (size_t i = 0; i < classEnd; ++i) {
                if (Offset(i) < body || At(i) != "async" || At(i + 1) != "Task")
                    continue;
                size_t method = i + 2;
                m_ReturnType = "void";
                if (At(method) == "<") {
                    const auto close = Close(method, "<", ">");
                    m_ReturnType = Slice(method + 1, close, false);
                    method = close + 1;
                }
                const auto taskType = "System.Threading.Tasks.Task<" + m_ReturnType + ">";
                if (!Identifier(At(method)) || At(method + 1) != "(")
                    continue;
                const auto name = std::string(At(method));
                const auto endParams = Close(method + 1, "(", ")");
                const auto args = Slice(method + 2, endParams, false);
                const auto frame = "FkCoroutine_" + className + "_" + name + "_" + std::to_string(i);
                const auto unique = name + "_" + std::to_string(i);
                const auto list = "fkTasks_" + unique;
                const auto first=list+"_first", active=list+"_active", extra=list+"_overflow";
                std::string initialize, forwarded;
                m_Locals.clear();
                m_Bindings.clear();
                for (size_t arg = method + 2; arg < endParams;) {
                    size_t end = arg;
                    while (end < endParams && At(end) != ",")
                        ++end;
                    if (end < arg + 2 || !Identifier(At(end - 1)))
                        Error("expected a typed coroutine parameter");
                    auto param = std::string(At(end - 1));
                    forwarded += "," + param;
                    const auto stored = BindLocal(param, Slice(arg, end - 1, false));
                    initialize += "fkFrame." + stored + "=" + param + ";";
                    arg = end + 1;
                }
                m_Pos = endParams + 1;
                m_Blocks.clear();
                m_BlockRegions.clear();
                m_Cleanups.clear();
                m_Region = 0;
                m_ExternalToken=m_FrameWait=m_TimeWait=m_FixedWait=false;
                m_Unwind = "FkUnwind_" + unique;
                Block();
                Block("return false;");
                Block("return false;");
                Statement(0, 1, -1, -1);
                frames += "\npublic class " + frame +
                          " { public: int Pc, Region, Frames; float Seconds; bool Fixed, ReportFault, OwnerWait; ulong Epoch;"
                          "Bolt.Runtime.ExceptionState Error; bool Destroying; System.Threading.CancellationToken "
                          "Token; System.Func<bool>? Observe; " +
                          taskType + " Completion;";
                for (size_t n = 1; n <= m_Cleanups.size(); n++)
                    frames += "int ExitPc" + std::to_string(n) + ",ExitRegion" + std::to_string(n) + ";";
                for (const auto& [key, type] : m_Locals)
                    frames += type + " " + key + ";";
                frames += "}\n";
                auto resume = "FkResume_" + unique;
                auto advance = "FkAdvance_" + unique;
                std::string code =
                    taskType + " " + name + "(" + args + ") {return FkStart_" + name + "(false" + forwarded + ");}" +
                    " private " + taskType + " FkStart_" + name + "(bool fkReport" + (args.empty()?"":","+args) +
                    ") {" + frame + " fkFrame;fkFrame.ReportFault=fkReport;" + initialize +
                    "fkFrame.Completion=" + taskType + ".CreatePending();" + "if (" + advance +
                    "(fkFrame,false)) {fkFrame.Epoch=Funke.FunkeApiTypes.CoroutineEpoch();if(!" + active + " && " + list +
                    ".Count==0) {" + first + "=fkFrame;" + active + "=true;}else {" + list +
                    ".Add(fkFrame);" + extra + "=true;}Funke.FunkeApiTypes.ScheduleCoroutine();}return fkFrame.Completion;}" +
                    " private " + frame + " " + first + ";private bool " + active +
                    "," + extra + ";private System.Collections.Generic.List<" + frame + "> " + list + ";" + " private void " +
                    m_Unwind + "(" + frame +
                    "& fkFrame,int region,int target) {if(fkFrame.Region==region){fkFrame.Pc=target;return;}";
                for (size_t n = 0; n < m_Cleanups.size(); n++) {
                    const auto& cleanup = m_Cleanups[n];
                    const auto suffix = std::to_string(n + 1);
                    if (cleanup.Entry < 0) {
                        code += "if(fkFrame.Region==" + suffix + "){fkFrame.Region=" + std::to_string(cleanup.Parent) +
                                ";if(target==2){" + cleanup.Handlers + "}" + m_Unwind +
                                "(fkFrame,region,target);return;}";
                        continue;
                    }
                    code += "if(fkFrame.Region==" + suffix + "){fkFrame.ExitRegion" + suffix +
                            "=region;fkFrame.ExitPc" + suffix +
                            "=target;fkFrame.Region=" + std::to_string(cleanup.Parent) +
                            ";fkFrame.Pc=" + std::to_string(cleanup.Entry) + ";return;}";
                }
                code += "fkFrame.Pc=target;} private bool "+advance+"("+frame+"& fkFrame,bool cancel){";
                if (m_Cleanups.empty()) {
                    code += "try {if(cancel)throw System.OperationCanceledException(\"Coroutine owner was destroyed.\");"
                        "if(fkFrame.OwnerWait){if(fkOwnerCanceled)throw System.OperationCanceledException(\"The operation was canceled.\");}"+
                        std::string(m_ExternalToken?"else fkFrame.Token.ThrowIfCancellationRequested();":"")+
                        "if("+resume+"(fkFrame))return true;}";
                } else {
                    code +=
                    "if(cancel)fkFrame.Destroying=true;try {while(true){try {"
                    "if(cancel){cancel=false;throw System.OperationCanceledException(\"Coroutine owner was "
                    "destroyed.\");}"
                    "if(fkFrame.OwnerWait){if(fkOwnerCanceled)throw System.OperationCanceledException(\"The operation was canceled.\");}" + std::string(m_ExternalToken?"else fkFrame.Token.ThrowIfCancellationRequested();":"") + "if(!" +
                    resume +
                    "(fkFrame))break;"
                    "if(fkFrame.Destroying)throw System.OperationCanceledException(\"Coroutine owner was "
                    "destroyed.\");return true;}"
                    "catch(System.Exception& error){fkFrame.Error.Capture(error);fkFrame.Frames=0;fkFrame.Seconds=0.0f;"
                    "fkFrame.Fixed=false;fkFrame.Token=default;fkFrame.OwnerWait=false;" +
                    m_Unwind +
                    "(fkFrame,0,2);}}"
                    "if(fkFrame.Pc==2)fkFrame.Error.Rethrow();}";
                }
                code +=
                    "catch(System.OperationCanceledException& canceled){fkFrame.Completion.SetCanceled(canceled);}"
                    "catch(System.Exception& error){fkFrame.Completion.SetException(error);if(fkFrame.ReportFault)Funke.Log.Error(error.Message);}"
                    "fkFrame.Completion.Complete();return false;}"
                    "private bool " +
                    resume + "(" + frame + "& fkFrame){while(true){";
                for (size_t block=0;block<m_Blocks.size();++block) {
                    code += "if(fkFrame.Pc=="+std::to_string(block)+"){";
                    size_t current=block;
                    std::vector<size_t> chain;
                    for (;;) {
                        chain.push_back(current);
                        const auto& part=m_Blocks[current];
                        const auto pos=part.rfind("fkFrame.Pc=");
                        if (!part.ends_with("; continue;") || pos==part.npos || chain.size()>=8) { code+=part; break; }
                        std::string_view next(part.data()+pos+11,part.size()-pos-11);
                        size_t target;
                        auto [after,err]=std::from_chars(next.data(),next.data()+next.size(),target);
                        if (err!=std::errc{} || std::string_view(after,next.data()+next.size()-after)!="; continue;" ||
                            target>=m_Blocks.size() || std::find(chain.begin(),chain.end(),target)!=chain.end()) { code+=part; break; }
                        code.append(part,0,part.size()-10);
                        current=target;
                    }
                    code+="}";
                }
                code += "return false;}return false;}";
                const auto tick="FkTick_"+unique;
                code += " private bool "+tick+"("+frame+"& fkFrame,bool fixedStep,float dt,ulong epoch){"
                    "if(fkFrame.Epoch==epoch)return true;bool canceled=" + std::string(m_ExternalToken?"fkFrame.OwnerWait?fkOwnerCanceled:fkFrame.Token.IsCancellationRequested":"fkFrame.OwnerWait && fkOwnerCanceled") + ";"
                    "if(!canceled && "+std::string(m_FixedWait?"fkFrame.Fixed!=fixedStep":"fixedStep")+")return true;"+
                    (m_FrameWait?"if(fkFrame.Frames>0)fkFrame.Frames--;":"")+
                    (m_TimeWait?"if(fkFrame.Seconds>0.0f)fkFrame.Seconds-=dt;":"")+
                    "if(canceled || (true"+(m_FrameWait?" && fkFrame.Frames<=0":"")+(m_TimeWait?" && !(fkFrame.Seconds>0.0f)":"")+
                    ")){"+(m_FixedWait?"fkFrame.Fixed=false;":"")+"if(!"+
                    advance+"(fkFrame,false))return false;}fkFrame.Epoch=epoch;return true;}";
                pump += "{int limit="+extra+"?"+list+".Count:0;if("+active+" && !"+tick+"("+first+",fixedStep,dt,epoch)){"+
                    active+"=false;"+first+"=default;}for(int i=0;i<limit;i++){"+frame+" fkFrame="+list+".ItemAt(i);if("+
                    tick+"(fkFrame,fixedStep,dt,epoch))"+list+"[i]=fkFrame;else{"+list+".RemoveAt(i);i--;limit--;"+extra+"="+list+".Count!=0;}}}";
                cancel += "{while("+list+".Count>0){int last="+list+".Count-1;"+frame+" fkFrame="+list+
                    ".ItemAt(last);"+list+".RemoveAt(last);"+advance+"(fkFrame,true);}"+extra+"=false;if("+active+"){"+active+
                    "=false;"+frame+" fkFrame="+first+";"+first+"=default;"+advance+"(fkFrame,true);}}";
                counts += "+(("+active+" || "+extra+")?1:0)";
                m_Edits.push_back({Offset(i), Offset(m_Pos - 1) + 1, std::move(code)});
                i = m_Pos - 1;
            }
            if (m_Methods.empty())
                return std::string(m_Source);
            for (size_t i = 0; i < classEnd; i++) {
                if (At(i) != "RunCoroutine" || At(i + 1) != "(")
                    continue;
                if (std::any_of(m_Edits.begin(), m_Edits.end(),
                                [&](const auto& e) { return Offset(i) >= e.Start && Offset(i) < e.End; }))
                    continue;
                const auto end = Close(i + 1, "(", ")");
                size_t arg = i + 2;
                if (At(arg) == "(" && At(arg + 1) == ")" && At(arg + 2) == "=" && At(arg + 3) == ">")
                    arg += 4;
                if (At(arg) == "this" && At(arg + 1) == ".")
                    arg += 2;
                if (std::find(m_Methods.begin(), m_Methods.end(), At(arg)) == m_Methods.end())
                    continue;
                std::string call = "FkStart_" + std::string(At(arg)) + "(true";
                if (arg + 1 < end) {
                    if (At(arg+1)!="(" || Close(arg+1,"(",")")!=end-1) continue;
                    if (arg+2<end-1) call += "," + Slice(arg+2,end-1,false);
                }
                m_Edits.push_back({Offset(i), Offset(end) + 1, call + ")"});
                i = end;
            }
            m_Edits.push_back({Offset(classEnd), Offset(classEnd),
                               " public override void FkCancel() {FkCancelOwner();" + cancel +
                                   "} public override bool FkPump(bool fixedStep,float dt,ulong epoch) {" + pump +
                                   "return (" + counts + ")>0;} "});
            std::sort(m_Edits.begin(), m_Edits.end(), [](const auto& a, const auto& b) { return a.Start > b.Start; });
            std::string result(m_Source);
            size_t previous = result.size();
            for (const auto& e : m_Edits) {
                if (e.End > previous)
                    Error("nested RunCoroutine inside an async body is not supported");
                result.replace(e.Start, e.End - e.Start, e.Text);
                previous = e.Start;
            }
            return result + frames;
        }
    };
} // namespace Funke::BoltScriptHost
