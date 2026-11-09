#include "pch.hpp"

#if __has_include(<sanitizer/asan_interface.h>)
// Establish C linkage before the amalgamation redeclares the ASan functions.
#include <sanitizer/asan_interface.h>
#endif

#define ZSTDLIB_VISIBILITY
#if defined(__ANDROID__)
// The PCH can define _GNU_SOURCE, but the NDK has no qsort_r; use zstd's fallback.
#pragma push_macro("_GNU_SOURCE")
#undef _GNU_SOURCE
#endif
#include "../../../vendor/zstd/zstd.c"
#if defined(__ANDROID__)
#pragma pop_macro("_GNU_SOURCE")
#endif
