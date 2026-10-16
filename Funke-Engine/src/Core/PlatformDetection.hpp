#pragma once

// premake predefines FNK_PLATFORM_WINDOWS / FNK_PLATFORM_LINUX; other toolchains
// (Xcode, Android NDK, Emscripten) rely on the compiler's own target macros.
#if !defined(FNK_PLATFORM_WINDOWS) && !defined(FNK_PLATFORM_LINUX) && !defined(FNK_PLATFORM_MACOS) \
	&& !defined(FNK_PLATFORM_ANDROID) && !defined(FNK_PLATFORM_IOS) && !defined(FNK_PLATFORM_WEB)
	#if defined(_WIN32)
		#define FNK_PLATFORM_WINDOWS
	#elif defined(__EMSCRIPTEN__)
		#define FNK_PLATFORM_WEB
	#elif defined(__ANDROID__)
		#define FNK_PLATFORM_ANDROID
	#elif defined(__APPLE__)
		#include <TargetConditionals.h>
		#if TARGET_OS_IPHONE
			#define FNK_PLATFORM_IOS
		#else
			#define FNK_PLATFORM_MACOS
		#endif
	#elif defined(__linux__)
		#define FNK_PLATFORM_LINUX
	#else
		#error Unknown platform.
	#endif
#endif

#if defined(FNK_PLATFORM_ANDROID) || defined(FNK_PLATFORM_IOS)
	#define FNK_PLATFORM_MOBILE 1
#else
	#define FNK_PLATFORM_MOBILE 0
#endif

#if defined(FNK_PLATFORM_WINDOWS) || defined(FNK_PLATFORM_LINUX) || defined(FNK_PLATFORM_MACOS)
	#define FNK_PLATFORM_DESKTOP 1
#else
	#define FNK_PLATFORM_DESKTOP 0
#endif

#if !defined(FNK_PLATFORM_WINDOWS)
	#define FNK_PLATFORM_POSIX 1
#else
	#define FNK_PLATFORM_POSIX 0
#endif

// Processes, shared-library hot reload and file watching only exist on desktop hosts.
#define FNK_PLATFORM_HAS_PROCESSES FNK_PLATFORM_DESKTOP
#if defined(FNK_PLATFORM_IOS) || defined(FNK_PLATFORM_WEB)
	#define FNK_PLATFORM_HAS_DYNAMIC_LIBRARIES 0
#else
	#define FNK_PLATFORM_HAS_DYNAMIC_LIBRARIES 1
#endif
// Web builds without pthreads cannot start std::thread; background work runs inline there.
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
	#define FNK_PLATFORM_HAS_THREADS 0
#else
	#define FNK_PLATFORM_HAS_THREADS 1
#endif

#if defined(FNK_PLATFORM_WINDOWS)
	#define FNK_PLATFORM_NAME "Windows"
#elif defined(FNK_PLATFORM_MACOS)
	#define FNK_PLATFORM_NAME "macOS"
#elif defined(FNK_PLATFORM_LINUX)
	#define FNK_PLATFORM_NAME "Linux"
#elif defined(FNK_PLATFORM_ANDROID)
	#define FNK_PLATFORM_NAME "Android"
#elif defined(FNK_PLATFORM_IOS)
	#define FNK_PLATFORM_NAME "iOS"
#elif defined(FNK_PLATFORM_WEB)
	#define FNK_PLATFORM_NAME "Web"
#endif

#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
	#define FNK_ARCH_NAME "arm64"
#elif defined(_M_X64) || defined(__x86_64__)
	#define FNK_ARCH_NAME "x64"
#elif defined(__wasm32__)
	#define FNK_ARCH_NAME "wasm32"
#elif defined(__arm__)
	#define FNK_ARCH_NAME "arm"
#else
	#define FNK_ARCH_NAME "x86"
#endif
