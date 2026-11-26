#pragma once

#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace Funke::GameBuilder::WindowsBoltAot {
    enum class CallShape { Words, Pointer, Submit };
    struct HostImport { std::string_view Name; CallShape Shape; };
    inline constexpr HostImport k_RawImports[] = {
#define FUNKE_BOLT_RAW(kind, name, target) {#name, CallShape::kind},
#include "Scripting/Bolt/RawBindings.inc"
#undef FUNKE_BOLT_RAW
    };

    inline std::string Tool(const std::filesystem::path& llvm, const char* name) {
        const auto sibling = llvm.parent_path() / name;
        if (std::filesystem::is_regular_file(sibling)) return Path::ToUtf8(sibling);
        if (const char* programs = std::getenv("ProgramFiles")) {
            const auto installed = Path::FromUtf8(programs) / "LLVM/bin" / name;
            if (std::filesystem::is_regular_file(installed)) return Path::ToUtf8(installed);
        }
        return name;
    }

    inline std::string ModuleBridge(const std::set<std::string>& imports) {
        std::string code = R"C(
#include <intrin.h>
typedef unsigned long long U64;
typedef struct { int failed; unsigned flags; volatile long long steps; } Context;
__declspec(dllimport) Context* br_create(U64 steps, unsigned depth);
__declspec(dllimport) int br_finish(Context*, int);
extern void bolt_module_initialize(Context*);
extern int bolt_entry(Context*);
extern void bolt_module_finalize(Context*);
__declspec(dllexport) void FunkeBoltCloseV1(Context* ctx) {
    bolt_module_finalize(ctx);
    br_finish(ctx, 0);
}
__declspec(dllexport) int FunkeBoltRunV1(Context** session, int persistent, int* status) {
    Context* ctx = *session;
    if (!ctx) {
        ctx = br_create(1000000, 128);
        if (!ctx) return 0;
        if (persistent) *session = ctx;
        bolt_module_initialize(ctx);
    } else {
        _InterlockedExchange64(&ctx->steps, 1000000);
    }
    if (!ctx->failed) *status = bolt_entry(ctx);
    int success = !ctx->failed;
    if (!persistent) {
        bolt_module_finalize(ctx);
        success = !ctx->failed;
        br_finish(ctx, 0);
    }
    return success;
}
)C";
        std::string bind = "__declspec(dllexport) const char* FunkeBoltBindV1(void* (*resolve)(const char*)) {\n";
        for (const auto& name : imports) {
            const auto raw = std::find_if(std::begin(k_RawImports), std::end(k_RawImports), [&](const auto& item) { return item.Name == name; });
            const auto shape = raw == std::end(k_RawImports) ? CallShape::Words : raw->Shape;
            const bool submit = shape == CallShape::Submit;
            const std::string result = shape == CallShape::Pointer ? "void*" : "U64";
            const std::string args = submit ? "U64 a, void* b, void* c" : "U64 a, U64 b";
            code += "static " + result + " (*slot_" + name + ")(" + args + ");\n";
            code += result + " " + name + "(" + args + ") { return slot_" + name + (submit ? "(a,b,c); }\n" : "(a,b); }\n");
            bind += "if (!(slot_" + name + " = resolve(\"" + name + "\"))) return \"" + name + "\";\n";
        }
        return code + bind + "return 0;\n}\n";
    }

    inline bool Build(const std::filesystem::path& stage, const std::filesystem::path& output,
                      const std::filesystem::path& llvm, std::string& error) {
        try {
            auto read = [](const std::filesystem::path& path) {
                Json::Value value;
                std::string diagnostic;
                if (!Json::TryParse(File::ReadAllText(Path::ToUtf8(path)), value, &diagnostic))
                    throw std::runtime_error(Path::ToUtf8(path) + ": " + diagnostic);
                return value;
            };
            auto write = [](const std::filesystem::path& path, const std::string& body) {
                if (!File::WriteAllText(Path::ToUtf8(path), body))
                    throw std::runtime_error("Cannot write " + Path::ToUtf8(path));
            };
            const auto compiler = read(stage / "compiler.json").AsStringOr();
            const auto bindings = read(stage / "bindings.json");
            std::set<std::string> hosts;
            for (const auto& raw : k_RawImports) hosts.emplace(raw.Name);
            for (const auto& row : bindings.GetArray()) hosts.insert(row.AsStringOr());
            const auto destination = output / "BoltModules";
            std::filesystem::create_directories(destination);
            auto modules = Json::Value::MakeObject();
            auto layouts = Json::Value::MakeObject();
            for (const auto& item : std::filesystem::directory_iterator(stage)) {
                if (!item.is_directory()) continue;
                const auto project = item.path();
                const auto guid = Path::ToUtf8(project.filename());
                const auto meta = read(project / "module.json");
                const auto asset = meta.FindMember("asset")->AsStringOr();
                auto run = [&](const std::vector<std::string>& command) {
                    auto result = Process::Run(command, project);
                    if (!result.Succeeded()) throw std::runtime_error(asset + " (" + command[0] + "): " + result.Output);
                    return result.Output;
                };
                run({compiler, "compile", ".", "--backend", "llvm", "--llvm", Path::ToUtf8(llvm),
                    "--release", "--object-only", "-o", "module.obj"});
                std::istringstream symbols(run({Tool(llvm, "llvm-nm.exe"), "--undefined-only", "module.obj"}));
                std::set<std::string> imports;
                std::set<std::string> runtime{"br_create", "br_finish"};
                std::string type, name;
                while (symbols >> type >> name) {
                    if (hosts.contains(name)) imports.insert(name);
                    if (name.starts_with("br_")) runtime.insert(name);
                }
                std::string definition = "LIBRARY BoltRuntime.dll\nEXPORTS\n";
                for (const auto& symbol : runtime) definition += symbol + "\n";
                write(project / "runtime.def", definition);
                run({Tool(llvm, "llvm-lib.exe"), "/def:runtime.def", "/machine:x64", "/out:runtime.lib"});
                write(project / "bridge.c", ModuleBridge(imports));
                run({Tool(llvm, "clang-cl.exe"), "--target=x86_64-pc-windows-msvc", "/nologo", "/O2", "/MD",
                    "/LD", "bridge.c", "module.obj", "runtime.lib", "/link", "/out:module.dll", "kernel32.lib", "bcrypt.lib"});
                std::filesystem::copy_file(project / "module.dll", destination / (guid + ".dll"));
                modules.AddMember(guid, *meta.FindMember("fields"));
                if (!std::filesystem::exists(project / "layouts.json")) continue;
                const auto catalog = read(project / "layouts.json");
                for (const auto& layout : catalog.GetArray()) {
                    if (layout.FindMember("managed")->AsBoolOr(false)) continue;
                    const auto key = layout.FindMember("name")->AsStringOr();
                    if (const auto* previous = layouts.FindMember(key)) {
                        if (*previous != layout) throw std::runtime_error("Conflicting Bolt component layouts: " + key);
                    } else layouts.AddMember(key, layout);
                }
            }
            auto catalog = Json::Value::MakeObject();
            catalog.AddMember("modules", std::move(modules));
            catalog.AddMember("layouts", std::move(layouts));
            write(destination / "catalog.json", Json::Stringify(catalog));
            std::filesystem::copy_file(Path::FromUtf8(compiler).parent_path() / "BoltRuntime.dll", output / "BoltRuntime.dll");
            std::filesystem::remove_all(stage);
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }
}
