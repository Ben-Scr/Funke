#pragma once

#include <chrono>
#include <functional>
#include <string>

namespace Funke {

	struct FunkeProject;

	namespace ExternalEditorDetail {
		// Native scripts retain their intended solution while CMake is still generating it.
		std::string ResolveProjectContext(const FunkeProject* project, const std::string& filePath);

		enum class ProjectContextStatus { Ready, TimedOut, Cancelled };
		ProjectContextStatus WaitForProjectContext(const std::string& solution, const std::function<bool()>& isCancelled,
			std::chrono::milliseconds timeout = std::chrono::seconds(30));
	}
}
