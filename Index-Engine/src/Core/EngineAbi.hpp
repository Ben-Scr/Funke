#pragma once

// IDX_ENGINE_ABI: hash of the engine headers this binary is compiled against (scripts/generate_build_metadata.py).
// Changes with every engine header edit, so include it only from an executable's entry TU and Engine.cpp,
// never from a header or the PCH.

#include "Core/Engine.hpp"

#include <cstdio>
#include <string>
#include <string_view>

#if __has_include("EngineAbi.generated.hpp")
#include "EngineAbi.generated.hpp"
#else
#define IDX_ENGINE_ABI ""
#endif

namespace Index {

	// Empty when the executable matches the loaded Index-Engine.dll (or either side has no key).
	inline std::string DescribeEngineAbiMismatch(std::string_view executableName) {
		const std::string_view compiled = IDX_ENGINE_ABI;
		const std::string_view loaded = Engine::GetAbiKey();
		if (compiled.empty() || loaded.empty() || compiled == loaded) return {};
		return std::string(executableName) + " was compiled against other engine headers (" + std::string(compiled)
			+ ") than the loaded Index-Engine.dll (" + std::string(loaded) + "). Rebuild it together with the engine "
			"(scripts\\dev\\Build.ps1 Engine,<App>) or reinstall; until then it may crash or misbehave.";
	}

	inline void ReportEngineAbiMismatch(std::string_view executableName) {
		const std::string message = DescribeEngineAbiMismatch(executableName);
		if (message.empty()) return;
		std::fprintf(stderr, "Index: %s\n", message.c_str());
	}

}
