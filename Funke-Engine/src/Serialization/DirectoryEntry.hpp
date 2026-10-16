#pragma once

#include <string>

namespace Funke {

	struct DirectoryEntry {
		std::string Path;
		std::string Name;
		bool IsDirectory = false;
	};

} // namespace Funke
