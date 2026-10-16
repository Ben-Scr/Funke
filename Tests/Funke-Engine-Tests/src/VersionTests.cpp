#include <doctest/doctest.h>

#include "Core/BuildIdentity.hpp"

using Funke::Version::IsAtLeast;

TEST_CASE("Semantic version comparison keeps prereleases below the final release") {
	CHECK(IsAtLeast("2026.1.0", "2026.1.0"));
	CHECK(IsAtLeast("2026.1.0", "2026.1.0-rc.1"));
	CHECK_FALSE(IsAtLeast("2026.1.0-rc.1", "2026.1.0"));
}

TEST_CASE("Semantic version comparison orders prerelease identifiers") {
	CHECK(IsAtLeast("2026.1.0-rc.10", "2026.1.0-rc.2"));
	CHECK_FALSE(IsAtLeast("2026.1.0-rc.2", "2026.1.0-rc.10"));
	CHECK(IsAtLeast("2026.1.0-rc.beta", "2026.1.0-rc.10"));
	CHECK(IsAtLeast("2026.1.0-rc.1.1", "2026.1.0-rc.1"));
	CHECK_FALSE(IsAtLeast("2026.1.0-rc.1", "2026.1.0-rc.1.1"));
}

TEST_CASE("Semantic version comparison evaluates core versions before prereleases") {
	CHECK(IsAtLeast("2026.2.0-alpha", "2026.1.9"));
	CHECK_FALSE(IsAtLeast("2026.1.9", "2026.2.0-alpha"));
	CHECK(IsAtLeast("2026.1.0+build.2", "2026.1.0+build.1"));
	CHECK(IsAtLeast("2026.1.0+build-with-hyphen", "2026.1.0"));
}

TEST_CASE("Semantic version comparison handles untrusted numeric lengths without overflow") {
	CHECK(IsAtLeast("999999999999999999999999999999.0.0", "2026.1.0"));
	CHECK_FALSE(IsAtLeast("2026.1.0", "999999999999999999999999999999.0.0"));
	CHECK_FALSE(IsAtLeast("2026.1.0", "not-a-version"));
}

TEST_CASE("Build identity macros are always available") {
	CHECK_FALSE(std::string_view(FNK_BUILD_ID).empty());
	CHECK_FALSE(std::string_view(FNK_BUILD_COMMIT).empty());
	CHECK(std::string_view(FNK_VERSION_LONG).find(FNK_BUILD_ID) != std::string_view::npos);
}
