#pragma once

// Per-build identity (commit + dirty fingerprint). Kept out of Version.hpp on
// purpose: the fingerprint changes on every working-tree edit, and Version.hpp
// sits in pch.hpp, so including this there turns every build into a full
// rebuild. Include it only from the few .cpp files that report the build ID.

#include "Core/Version.hpp"

#if __has_include("BuildIdentity.generated.hpp")
#include "BuildIdentity.generated.hpp"
#else
#define FNK_BUILD_ID "0.0.0-dev+unknown"
#define FNK_BUILD_COMMIT "unknown"
#define FNK_BUILD_DIRTY 1
#endif

#define FNK_VERSION_LONG "Funke " FNK_BUILD_ID " (" FNK_BUILD_PLATFORM_NAME " " FNK_BUILD_CONFIG_NAME ")"
