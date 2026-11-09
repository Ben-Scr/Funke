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
		Physics2D,
		Physics3D,
	};

	inline constexpr EngineModule k_AllEngineModules[] = {
		EngineModule::TwoD, EngineModule::ThreeD, EngineModule::Physics2D, EngineModule::Physics3D };

	namespace EngineModules {
		inline constexpr std::string_view k_Id2D = "Funke.2D";
		inline constexpr std::string_view k_Id3D = "Funke.3D";
		inline constexpr std::string_view k_IdPhysics2D = "Funke.Physics2D";
		inline constexpr std::string_view k_IdPhysics3D = "Funke.Physics3D";

		enum class Status : uint8_t { Unknown, NotCompiled, Disabled, Enabled };

		constexpr bool IsCompiled(EngineModule module) {
			switch (module) {
				case EngineModule::TwoD: return FUNKE_WITH_2D != 0;
				case EngineModule::ThreeD: return FUNKE_WITH_3D != 0;
				case EngineModule::Physics2D: return FUNKE_WITH_PHYSICS2D != 0;
				case EngineModule::Physics3D: return FUNKE_WITH_PHYSICS3D != 0;
			}
			return false;
		}

		FUNKE_API std::string_view GetId(EngineModule module);
		// premake option suffix (--without-<option>) and runtime variant id part, as in EngineModules.json.
		FUNKE_API std::string_view GetOption(EngineModule module);
		// One line for the Package Manager and the CLI.
		FUNKE_API std::string_view GetDescription(EngineModule module);
		FUNKE_API std::optional<EngineModule> FromId(std::string_view id);
		FUNKE_API std::optional<EngineModule> GetSerializedPhysicsModule(std::string_view component);
		FUNKE_API std::optional<EngineModule> GetDependency(EngineModule module);
		FUNKE_API EngineModule GetDimension(EngineModule module);
		// Canonical dependency closure; unknown IDs survive for the caller's diagnostics.
		FUNKE_API std::vector<std::string> Resolve(const std::vector<std::string>& moduleIds);
		FUNKE_API std::vector<std::string> ExpandLegacySelection(const std::vector<std::string>& moduleIds);
		// Inspects a project selection without changing the running module state.
		FUNKE_API Status GetStatus(std::string_view id, const std::vector<std::string>& moduleIds);
		FUNKE_API std::string_view GetStatusName(Status status);
		FUNKE_API std::string GetStatusMessage(std::string_view id, Status status);

		FUNKE_API bool IsActive(EngineModule module);
		// Ids of the active modules, the set packages are resolved against at runtime.
		FUNKE_API std::vector<std::string> GetActiveIds();

		// Activates compiled IDs and their dependencies; project validation rejects unknown IDs first.
		FUNKE_API void Configure(const std::vector<std::string>& moduleIds);
		// No project (tests, headless tools): every compiled module is active.
		FUNKE_API void ActivateAllCompiled();

		// Ids of listed modules this build was compiled without (a project opened in a reduced runtime).
		FUNKE_API std::vector<std::string> GetMissingModules(const std::vector<std::string>& moduleIds);
	}

}
