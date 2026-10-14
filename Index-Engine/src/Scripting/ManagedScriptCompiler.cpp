#include "pch.hpp"
#include "Scripting/ManagedScriptCompiler.hpp"

#include "Scripting/ScriptEngine.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace Index {

	namespace {
		using CompileFn = char* (*)(const char* requestJson);
		using FreeFn = void (*)(char* result);

		std::atomic<CompileFn> s_Compile{ nullptr };
		std::atomic<FreeFn> s_Free{ nullptr };
		bool s_LoadAttempted = false;

		ManagedScriptCompiler::Result MakeFallback(std::string message) {
			ManagedScriptCompiler::Result result;
			result.Message = std::move(message);
			return result;
		}
	}

	bool ManagedScriptCompiler::EnsureLoaded()
	{
		if (s_Compile.load(std::memory_order_acquire)) return true;
		if (s_LoadAttempted) return false;
		s_LoadAttempted = true;

		// Escape hatch while the in-process path is new: INDEX_SCRIPT_COMPILER=dotnet restores plain dotnet build.
		if (const char* forced = std::getenv("INDEX_SCRIPT_COMPILER"); forced && std::string_view(forced) == "dotnet") {
			IDX_CORE_INFO_TAG("ScriptCompiler", "INDEX_SCRIPT_COMPILER=dotnet: hot reload uses dotnet build.");
			return false;
		}

		// A private copy, so rebuilding Index-ScriptCompiler never fights the running editor's file lock.
		const std::filesystem::path assembly = Path::FromUtf8(Path::ExecutableDir()) / "ScriptCompiler" / "Index-ScriptCompiler.dll";
		std::error_code ec;
		if (!std::filesystem::is_regular_file(assembly, ec)) {
			IDX_CORE_INFO_TAG("ScriptCompiler", "{} not deployed; hot reload uses dotnet build.", Path::ToUtf8(assembly));
			return false;
		}

		constexpr std::string_view k_Type = "Index.ScriptCompiler.CompilerHost";
		auto compile = reinterpret_cast<CompileFn>(ScriptEngine::GetUnmanagedEntryPoint(assembly, "Index-ScriptCompiler", k_Type, "Compile"));
		auto release = reinterpret_cast<FreeFn>(ScriptEngine::GetUnmanagedEntryPoint(assembly, "Index-ScriptCompiler", k_Type, "FreeResult"));
		if (!compile || !release) {
			IDX_CORE_WARN_TAG("ScriptCompiler", "Could not bind Index-ScriptCompiler entry points; hot reload uses dotnet build.");
			return false;
		}
		s_Free.store(release, std::memory_order_release);
		s_Compile.store(compile, std::memory_order_release);
		return true;
	}

	ManagedScriptCompiler::Result ManagedScriptCompiler::Compile(const Request& request)
	{
		const CompileFn compile = s_Compile.load(std::memory_order_acquire);
		const FreeFn release = s_Free.load(std::memory_order_acquire);
		if (!compile || !release) {
			return MakeFallback("in-process compiler not loaded");
		}

		Json::Value properties = Json::Value::MakeArray();
		for (const std::string& property : request.MSBuildProperties) {
			properties.Append(property);
		}
		Json::Value payload = Json::Value::MakeObject();
		payload.AddMember("project", request.ProjectPath);
		payload.AddMember("configuration", request.Configuration);
		payload.AddMember("properties", std::move(properties));
		payload.AddMember("outputPath", request.OutputPath);
		payload.AddMember("dotnet", "dotnet");
		const std::string requestJson = Json::Stringify(payload);

		char* responseJson = compile(requestJson.c_str());
		if (!responseJson) {
			return MakeFallback("in-process compiler returned nothing");
		}
		std::string error;
		Json::Value response;
		const bool parsed = Json::TryParse(responseJson, response, &error);
		release(responseJson);
		if (!parsed || !response.IsObject()) {
			return MakeFallback("unreadable compiler response: " + error);
		}

		Result result;
		const auto member = [&](std::string_view key) -> const Json::Value* { return response.FindMember(key); };
		const std::string status = member("status") ? member("status")->AsStringOr() : std::string();
		result.Status = status == "compiled" ? Status::Compiled : status == "failed" ? Status::Failed : Status::Fallback;
		if (const auto* v = member("message")) result.Message = v->AsStringOr();
		if (const auto* v = member("diagnostics")) result.Diagnostics = v->AsStringOr();
		if (const auto* v = member("warnings")) result.Warnings = v->AsIntOr(0);
		if (const auto* v = member("elapsedMs")) result.ElapsedMs = v->AsDoubleOr(0.0);
		if (const auto* v = member("evaluated")) result.Evaluated = v->AsBoolOr(false);
		return result;
	}

}
