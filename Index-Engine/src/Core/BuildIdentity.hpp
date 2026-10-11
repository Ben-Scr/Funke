#pragma once

// Per-build identity (commit + dirty fingerprint). Kept out of Version.hpp on
// purpose: the fingerprint changes on every working-tree edit, and Version.hpp
// sits in pch.hpp, so including this there turns every build into a full
// rebuild. Include it only from the few .cpp files that report the build ID.

#include "Core/Version.hpp"

#if __has_include("BuildIdentity.generated.hpp")
#include "BuildIdentity.generated.hpp"
#else
#define IDX_BUILD_ID "0.0.0-dev+unknown"
#define IDX_BUILD_COMMIT "unknown"
#define IDX_BUILD_DIRTY 1
#endif

#define IDX_VERSION_LONG "Index " IDX_BUILD_ID " (" IDX_BUILD_PLATFORM_NAME " " IDX_BUILD_CONFIG_NAME ")"
