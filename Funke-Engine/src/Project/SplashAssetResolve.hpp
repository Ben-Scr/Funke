#pragma once

#include "Core/Export.hpp"

#include <string>

namespace Funke {
	struct FunkeProject;

	// Centralises the three stored-path forms (project-relative, "funke:<sub>", absolute) for splash/icon assets.
	namespace SplashAssetResolve {
		FUNKE_API std::string Resolve(const std::string& stored, const FunkeProject* project);
		FUNKE_API std::string NormalizeForStorage(const std::string& pickedAbsolute, const FunkeProject* project);
		FUNKE_API std::string DefaultLogoPath();
		// Returns "Funke <version> · <platform> · <profile>"; each binary compiles its own values.
		FUNKE_API std::string DefaultSubtitleLine();
	}
}
