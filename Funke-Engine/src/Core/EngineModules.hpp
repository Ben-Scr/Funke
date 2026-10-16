#pragma once

#include "Core/Export.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	// Optional engine feature modules (Funke-Engine/EngineModules.json). A module is compiled when the
	// build has FUNKE_WITH_<module>=1 and active when it is compiled and the project lists it in
	// project.json `modules`. Only active modules register their systems, renderers and script bindings.
	enum class EngineModule : uint8_t {
		TwoD,
		ThreeD,
	};

	inline constexpr EngineModule k_AllEngineModules[] = { EngineModule::TwoD, EngineModule::ThreeD };

	namespace EngineModules {
		inline constexpr std::string_view k_Id2D = "Funke.2D";
		inline constexpr std::string_view k_Id3D = "Funke.3D";

		constexpr bool IsCompiled(EngineModule module) {
			switch (module) {
				case EngineModule::TwoD: return FUNKE_WITH_2D != 0;
				case EngineModule::ThreeD: return FUNKE_WITH_3D != 0;
			}
			return false;
		}

		FUNKE_API std::string_view GetId(EngineModule module);
		// premake option suffix (--without-<option>) and runtime variant id part, as in EngineModules.json.
		FUNKE_API std::string_view GetOption(EngineModule module);
		// One line for the Package Manager and the CLI.
		FUNKE_API std::string_view GetDescription(EngineModule module);
		FUNKE_API std::optional<EngineModule> FromId(std::string_view id);

		FUNKE_API bool IsActive(EngineModule module);
		// Ids of the active modules, the set packages are resolved against at runtime.
		FUNKE_API std::vector<std::string> GetActiveIds();

		// Activates exactly the listed module ids that are compiled in; unknown ids are ignored.
		FUNKE_API void Configure(const std::vector<std::string>& moduleIds);
		// No project (tests, headless tools): every compiled module is active.
		FUNKE_API void ActivateAllCompiled();

		// Ids of listed modules this build was compiled without (a project opened in a reduced runtime).
		FUNKE_API std::vector<std::string> GetMissingModules(const std::vector<std::string>& moduleIds);
	}

}
