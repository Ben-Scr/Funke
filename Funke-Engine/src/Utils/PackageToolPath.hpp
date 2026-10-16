#pragma once

#include "Core/Export.hpp"

#include <filesystem>
#include <string>

namespace Funke::PackageTool {

	// Filename of the Funke-PackageTool executable for the current platform
	// ("Funke-PackageTool.exe" on Windows, "Funke-PackageTool" elsewhere).
	FUNKE_API std::string ExecutableName();

	FUNKE_API std::filesystem::path ResolveExecutable();

} // namespace Funke::PackageTool
