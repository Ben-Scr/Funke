#pragma once

#include <algorithm>
#include <sstream>
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
			std::vector<std::string> ChangedPaths;
			bool RescanSources = true;
		};

		struct Result {
			Status Status = Status::Fallback;
			std::string Message;
			std::string Diagnostics;
			int Warnings = 0;
			double ElapsedMs = 0.0;
			bool Evaluated = false;
			bool FullBuild = false;
		};

		static std::string LimitOutput(const std::string& output) {
			std::istringstream stream(output);
			std::vector<std::string> lines;
			for (std::string line; std::getline(stream, line);) if (!line.empty()) lines.push_back(std::move(line));
			if (lines.size() <= 200 || (lines.size() == 201 && lines.back().find("omitted") != std::string::npos)) return output;
			auto priority = [](const std::string& line) {
				return line.find("error ") != std::string::npos ? 0 : line.find("warning ") != std::string::npos ? 1 : 2;
			};
			std::stable_sort(lines.begin(), lines.end(), [&](const auto& a, const auto& b) { return priority(a) < priority(b); });
			std::string limited;
			for (size_t i = 0; i < 200; ++i) limited += lines[i] + "\n";
			return limited + std::to_string(lines.size() - 200) + " additional build output lines omitted.";
		}

		// Main thread, after ScriptEngine::LoadCoreAssembly. False when the compiler isn't deployed next to the editor.
		static bool EnsureLoaded();
		// Thread-safe; calls are serialized on the managed side.
		static Result Compile(const Request& request);
	};

}
