#pragma once
#include "Core/FunkeErrorCode.hpp"
#include "Core/Export.hpp"

#include <exception>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Core/Log.hpp"

namespace Funke {
	FUNKE_API const char* ErrorCodeToString(FunkeErrorCode code);

	class FUNKE_API FunkeException : public std::runtime_error {
	public:
		FunkeException(FunkeErrorCode code, std::string message, std::source_location location = std::source_location::current());

		FunkeErrorCode Code() const noexcept { return m_Code; }
		const std::string& Message() const noexcept { return m_Message; }
		const std::source_location& Location() const noexcept { return m_Location; }

	private:
		static std::string BuildWhat(FunkeErrorCode code, std::string_view message, const std::source_location& location);

		FunkeErrorCode m_Code;
		std::string m_Message;
		std::source_location m_Location;
	};

	using FunkeError = FunkeException;

	[[noreturn]] FUNKE_API void ThrowError(FunkeErrorCode code, std::string_view message, std::source_location location = std::source_location::current());
	[[noreturn]] FUNKE_API void RethrowWithContext(FunkeErrorCode code, std::string message, std::source_location location = std::source_location::current());

} // namespace Funke

#define FNK_THROW(code, msg) ::Funke::ThrowError((code), (msg), std::source_location::current())

#define FNK_LOG_ERROR(code, msg) \
	do { \
		FNK_CORE_ERROR("[{}] {}", ::Funke::ErrorCodeToString(code), (msg)); \
	} while (0)

#define FUNKE_LOG_ERROR_IF(cond, code, msg) \
	do { \
		if (cond) { \
			FNK_LOG_ERROR((code), (msg)); \
		} \
	} while (0)

#define FUNKE_TRY_CATCH_LOG(stmt) \
	do { \
		try { \
			stmt; \
		} catch (const std::exception& ex) { \
			FNK_CORE_ERROR("Exception: {}", ex.what()); \
		} catch (...) { \
			FNK_CORE_ERROR("Unknown exception"); \
		} \
	} while (0)
