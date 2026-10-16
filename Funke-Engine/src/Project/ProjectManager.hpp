#pragma once
#include "Project/FunkeProject.hpp"
#include "Core/Export.hpp"
#include <memory>

namespace Funke {

	class FUNKE_API ProjectManager {
	public:
		// assetsImmutable=true (standalone runtime) lets the registry trust the shipped manifest and
		// skip the directory walk. Set before the registry's first sync.
		static void SetCurrentProject(std::unique_ptr<FunkeProject> project, bool assetsImmutable = false);
		// Pushes the current project's engine-wide knobs (font baking) into their subsystems; also
		// for when the current project object is reloaded in place.
		static void ApplyCurrentProjectSettings();
		static FunkeProject* GetCurrentProject();
		static bool HasProject();

	private:
		static std::unique_ptr<FunkeProject> s_CurrentProject;
	};

} // namespace Funke
