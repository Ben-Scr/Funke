#pragma once

#include "Core/Base.hpp"
#include "Core/Exceptions.hpp"
#include "Core/Log.hpp"

#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/fmt/fmt.h>

#ifdef FNK_PLATFORM_WINDOWS
#define FNK_DEBUG_BREAK __debugbreak()
#else
#include <csignal>
#define FNK_DEBUG_BREAK raise(SIGTRAP)
#endif

namespace Funke {

	FUNKE_API void ReportAssertionFailure(
		const char* kind,
		const char* expression,
		std::string_view message,
		std::source_location location = std::source_location::current());

	inline std::string BuildAssertMessage(const std::string_view message) {
		return std::string(message);
	}

	inline std::string BuildAssertMessage(const FunkeErrorCode code, const std::string_view message) {
		return fmt::format("[{}] {}", ErrorCodeToString(code), message);
	}

	template <typename T>
	inline std::string BuildAssertMessage(T&& message) {
		return std::string(std::forward<T>(message));
	}

} // namespace Funke

#ifdef FNK_DEBUG
#define FNK_ENABLE_ASSERTS
#endif

#define FNK_ENABLE_VERIFY

#define FNK_ASSERT(cond, ...) \
	do { \
		if (!(cond)) { \
			::Funke::ReportAssertionFailure("ASSERT", #cond, ::Funke::BuildAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} \
	} while (0)

#define FNK_CORE_ASSERT(cond, ...) \
	do { \
		if (!(cond)) { \
			::Funke::ReportAssertionFailure("CORE_ASSERT", #cond, ::Funke::BuildAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} \
	} while (0)

#ifdef FNK_ENABLE_VERIFY
#define FNK_VERIFY(cond, ...) \
	do { \
		if (!(cond)) { \
			::Funke::ReportAssertionFailure("VERIFY", #cond, ::Funke::BuildAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} \
	} while (0)

#define FNK_CORE_VERIFY(cond, ...) \
	do { \
		if (!(cond)) { \
			::Funke::ReportAssertionFailure("CORE_VERIFY", #cond, ::Funke::BuildAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} \
	} while (0)
#else
#define FNK_VERIFY(cond, ...) ((void)(cond))
#define FNK_CORE_VERIFY(cond, ...) ((void)(cond))
#endif