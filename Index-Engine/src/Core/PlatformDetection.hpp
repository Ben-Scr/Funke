#pragma once

// premake predefines IDX_PLATFORM_WINDOWS / IDX_PLATFORM_LINUX; other toolchains
// (Xcode, Android NDK, Emscripten) rely on the compiler's own target macros.
#if !defined(IDX_PLATFORM_WINDOWS) && !defined(IDX_PLATFORM_LINUX) && !defined(IDX_PLATFORM_MACOS) \
	&& !defined(IDX_PLATFORM_ANDROID) && !defined(IDX_PLATFORM_IOS) && !defined(IDX_PLATFORM_WEB)
	#if defined(_WIN32)
		#define IDX_PLATFORM_WINDOWS
	#elif defined(__EMSCRIPTEN__)
		#define IDX_PLATFORM_WEB
	#elif defined(__ANDROID__)
		#define IDX_PLATFORM_ANDROID
	#elif defined(__APPLE__)
		#include <TargetConditionals.h>
		#if TARGET_OS_IPHONE
			#define IDX_PLATFORM_IOS
		#else
			#define IDX_PLATFORM_MACOS
		#endif
	#elif defined(__linux__)
		#define IDX_PLATFORM_LINUX
	#else
		#error Unknown platform.
	#endif
#endif

#if defined(IDX_PLATFORM_ANDROID) || defined(IDX_PLATFORM_IOS)
	#define IDX_PLATFORM_MOBILE 1
#else
	#define IDX_PLATFORM_MOBILE 0
#endif

#if defined(IDX_PLATFORM_WINDOWS) || defined(IDX_PLATFORM_LINUX) || defined(IDX_PLATFORM_MACOS)
	#define IDX_PLATFORM_DESKTOP 1
#else
	#define IDX_PLATFORM_DESKTOP 0
#endif

#if !defined(IDX_PLATFORM_WINDOWS)
	#define IDX_PLATFORM_POSIX 1
#else
	#define IDX_PLATFORM_POSIX 0
#endif

// Processes, shared-library hot reload and file watching only exist on desktop hosts.
#define IDX_PLATFORM_HAS_PROCESSES IDX_PLATFORM_DESKTOP
#if defined(IDX_PLATFORM_IOS) || defined(IDX_PLATFORM_WEB)
	#define IDX_PLATFORM_HAS_DYNAMIC_LIBRARIES 0
#else
	#define IDX_PLATFORM_HAS_DYNAMIC_LIBRARIES 1
#endif
// Web builds without pthreads cannot start std::thread; background work runs inline there.
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
	#define IDX_PLATFORM_HAS_THREADS 0
#else
	#define IDX_PLATFORM_HAS_THREADS 1
#endif

#if defined(IDX_PLATFORM_WINDOWS)
	#define IDX_PLATFORM_NAME "Windows"
#elif defined(IDX_PLATFORM_MACOS)
	#define IDX_PLATFORM_NAME "macOS"
#elif defined(IDX_PLATFORM_LINUX)
	#define IDX_PLATFORM_NAME "Linux"
#elif defined(IDX_PLATFORM_ANDROID)
	#define IDX_PLATFORM_NAME "Android"
#elif defined(IDX_PLATFORM_IOS)
	#define IDX_PLATFORM_NAME "iOS"
#elif defined(IDX_PLATFORM_WEB)
	#define IDX_PLATFORM_NAME "Web"
#endif

#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
	#define IDX_ARCH_NAME "arm64"
#elif defined(_M_X64) || defined(__x86_64__)
	#define IDX_ARCH_NAME "x64"
#elif defined(__wasm32__)
	#define IDX_ARCH_NAME "wasm32"
#elif defined(__arm__)
	#define IDX_ARCH_NAME "arm"
#else
	#define IDX_ARCH_NAME "x86"
#endif
