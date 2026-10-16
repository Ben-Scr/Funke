#pragma once

#include <string>
#include <vector>

namespace Funke {

	// Editor hot reload through Funke-ScriptCompiler: Roslyn inside the running CoreCLR, fed the exact csc
	// arguments MSBuild would use. Anything it can't reproduce comes back as Fallback so the caller runs dotnet build.
	class ManagedScriptCompiler {
	public:
		enum class Status { Compiled, Failed, Fallback };

		struct Request {
			std::string ProjectPath;
			std::string Configuration;
			// Verbatim dotnet build switches (-p:DefineConstants=..., -p:FunkeCodegenEnabled=false).
			std::vector<std::string> MSBuildProperties;
			std::string OutputPath;
		};

		struct Result {
			Status Status = Status::Fallback;
			std::string Message;
			std::string Diagnostics;
			int Warnings = 0;
			double ElapsedMs = 0.0;
			bool Evaluated = false;
		};

		// Main thread, after ScriptEngine::LoadCoreAssembly. False when the compiler isn't deployed next to the editor.
		static bool EnsureLoaded();
		// Thread-safe; calls are serialized on the managed side.
		static Result Compile(const Request& request);
	};

}
