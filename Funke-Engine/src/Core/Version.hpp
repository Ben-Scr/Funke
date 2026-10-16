#pragma once

#if __has_include("BuildMetadata.generated.hpp")
#include "BuildMetadata.generated.hpp"
#else
#define FNK_VERSION "0.0.0-dev"
#define FNK_VERSION_MAJOR 0
#define FNK_VERSION_MINOR 0
#define FNK_VERSION_PATCH 0
#define FNK_VERSION_RESOURCE 0,0,0,0
#endif

// FNK_BUILD_ID / FNK_BUILD_COMMIT / FNK_BUILD_DIRTY / FNK_VERSION_LONG live in
// Core/BuildIdentity.hpp, which must stay out of pch.hpp.

//
// Build Configuration
//
#if defined(FNK_DEBUG)
#define FNK_BUILD_CONFIG_NAME "Debug"
#elif defined(FNK_RELEASE)
#define FNK_BUILD_CONFIG_NAME "Release"
#elif defined(FNK_DIST)
#define FNK_BUILD_CONFIG_NAME "Dist"
#else
#error Undefined configuration?
#endif

//
// Build Platform
//
#include "Core/PlatformDetection.hpp"
#define FNK_BUILD_PLATFORM_NAME FNK_PLATFORM_NAME " " FNK_ARCH_NAME

#include <cstdint>
#include <string_view>

namespace Funke::Version {

	inline bool IsAtLeast(std::string_view actual, std::string_view required) {
		if (actual.empty() || required.empty()) return false;

		auto core = [](std::string_view value) {
			const std::size_t suffix = value.find_first_of("-+");
			return value.substr(0, suffix);
		};
		auto prerelease = [](std::string_view value) {
			const std::size_t plus = value.find('+');
			const std::size_t dash = value.find('-');
			if (dash == std::string_view::npos || (plus != std::string_view::npos && dash > plus)) {
				return std::string_view{};
			}
			return value.substr(dash + 1, plus == std::string_view::npos
				? std::string_view::npos
				: plus - dash - 1);
		};
		auto nextIdentifier = [](std::string_view& value) {
			const std::size_t separator = value.find('.');
			const std::string_view identifier = value.substr(0, separator);
			value = separator == std::string_view::npos ? std::string_view{} : value.substr(separator + 1);
			return identifier;
		};
		auto numeric = [](std::string_view value) {
			if (value.empty()) return false;
			for (char character : value) {
				if (character < '0' || character > '9') return false;
			}
			return true;
		};
		auto trimZeros = [](std::string_view value) {
			while (value.size() > 1 && value.front() == '0') value.remove_prefix(1);
			return value;
		};

		std::string_view actualCore = core(actual);
		std::string_view requiredCore = core(required);
		while (!actualCore.empty() || !requiredCore.empty()) {
			const bool hasActualSegment = !actualCore.empty();
			const bool hasRequiredSegment = !requiredCore.empty();
			std::string_view a = hasActualSegment ? nextIdentifier(actualCore) : std::string_view("0");
			std::string_view r = hasRequiredSegment ? nextIdentifier(requiredCore) : std::string_view("0");
			if ((hasActualSegment && !numeric(a)) || (hasRequiredSegment && !numeric(r))) return false;
			a = trimZeros(a);
			r = trimZeros(r);
			if (a.size() != r.size()) return a.size() > r.size();
			if (a < r) return false;
			if (a > r) return true;
		}

		std::string_view actualPre = prerelease(actual);
		std::string_view requiredPre = prerelease(required);
		if (actualPre.empty() || requiredPre.empty()) {
			return actualPre.empty();
		}

		while (!actualPre.empty() || !requiredPre.empty()) {
			if (actualPre.empty()) return false;
			if (requiredPre.empty()) return true;
			std::string_view a = nextIdentifier(actualPre);
			std::string_view r = nextIdentifier(requiredPre);
			const bool aNumeric = numeric(a);
			const bool rNumeric = numeric(r);
			if (aNumeric != rNumeric) return !aNumeric;
			if (aNumeric) {
				a = trimZeros(a);
				r = trimZeros(r);
				if (a.size() != r.size()) return a.size() > r.size();
			}
			if (a != r) return a > r;
		}
		return true;
	}

} // namespace Funke::Version
