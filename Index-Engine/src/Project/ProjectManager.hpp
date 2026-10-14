#pragma once
#include "Project/IndexProject.hpp"
#include "Core/Export.hpp"
#include <memory>

namespace Index {

	class INDEX_API ProjectManager {
	public:
		// assetsImmutable=true (standalone runtime) lets the registry trust the shipped manifest and
		// skip the directory walk. Set before the registry's first sync.
		static void SetCurrentProject(std::unique_ptr<IndexProject> project, bool assetsImmutable = false);
		// Pushes the current project's engine-wide knobs (font baking) into their subsystems; also
		// for when the current project object is reloaded in place.
		static void ApplyCurrentProjectSettings();
		static IndexProject* GetCurrentProject();
		static bool HasProject();

	private:
		static std::unique_ptr<IndexProject> s_CurrentProject;
	};

} // namespace Index
