#pragma once

#include "Core/Export.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Index {

	// Optional engine feature modules (Index-Engine/EngineModules.json). A module is compiled when the
	// build has INDEX_WITH_<module>=1 and active when it is compiled and the project lists it in
	// project.json `modules`. Only active modules register their systems, renderers and script bindings.
	enum class EngineModule : uint8_t {
		TwoD,
		ThreeD,
	};

	inline constexpr EngineModule k_AllEngineModules[] = { EngineModule::TwoD, EngineModule::ThreeD };

	namespace EngineModules {
		inline constexpr std::string_view k_Id2D = "Index.2D";
		inline constexpr std::string_view k_Id3D = "Index.3D";

		constexpr bool IsCompiled(EngineModule module) {
			switch (module) {
				case EngineModule::TwoD: return INDEX_WITH_2D != 0;
				case EngineModule::ThreeD: return INDEX_WITH_3D != 0;
			}
			return false;
		}

		INDEX_API std::string_view GetId(EngineModule module);
		// premake option suffix (--without-<option>) and runtime variant id part, as in EngineModules.json.
		INDEX_API std::string_view GetOption(EngineModule module);
		// One line for the Package Manager and the CLI.
		INDEX_API std::string_view GetDescription(EngineModule module);
		INDEX_API std::optional<EngineModule> FromId(std::string_view id);

		INDEX_API bool IsActive(EngineModule module);
		// Ids of the active modules, the set packages are resolved against at runtime.
		INDEX_API std::vector<std::string> GetActiveIds();

		// Activates exactly the listed module ids that are compiled in; unknown ids are ignored.
		INDEX_API void Configure(const std::vector<std::string>& moduleIds);
		// No project (tests, headless tools): every compiled module is active.
		INDEX_API void ActivateAllCompiled();

		// Ids of listed modules this build was compiled without (a project opened in a reduced runtime).
		INDEX_API std::vector<std::string> GetMissingModules(const std::vector<std::string>& moduleIds);
	}

}
