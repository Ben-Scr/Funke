#include "pch.hpp"

#include "Core/Exceptions.hpp"

#include <utility>
#include <spdlog/fmt/fmt.h>

namespace Funke {

	const char* ErrorCodeToString(const FunkeErrorCode code) {
		switch (code) {
		case FunkeErrorCode::InvalidArgument: return "InvalidArgument";
		case FunkeErrorCode::NotInitialized: return "NotInitialized";
		case FunkeErrorCode::AlreadyInitialized: return "AlreadyInitialized";
		case FunkeErrorCode::FileNotFound: return "FileNotFound";
		case FunkeErrorCode::InvalidHandle: return "InvalidHandle";
		case FunkeErrorCode::OutOfRange: return "OutOfRange";
		case FunkeErrorCode::OutOfBounds: return "OutOfBounds";
		case FunkeErrorCode::Overflow: return "Overflow";
		case FunkeErrorCode::NullReference: return "NullReference";
		case FunkeErrorCode::LoadFailed: return "LoadFailed";
		case FunkeErrorCode::InvalidValue: return "InvalidValue";
		case FunkeErrorCode::Undefined: return "Undefined";
		default: return "Undefined";
		}
	}

	FunkeException::FunkeException(const FunkeErrorCode code, std::string message, const std::source_location location)
		: std::runtime_error(BuildWhat(code, message, location)),
		m_Code(code),
		m_Message(std::move(message)),
		m_Location(location) {
	}

	std::string FunkeException::BuildWhat(const FunkeErrorCode code, const std::string_view message, const std::source_location& location) {
		return fmt::format("FunkeException[{}] {} @ {}:{} ({})",
			ErrorCodeToString(code),
			message,
			location.file_name(),
			location.line(),
			location.function_name());
	}

	[[noreturn]] void ThrowError(const FunkeErrorCode code, const std::string_view message, const std::source_location location) {
		throw FunkeException(code, std::string(message), location);
	}

	[[noreturn]] void RethrowWithContext(const FunkeErrorCode code, std::string message, const std::source_location location) {
		try {
			throw;
		}
		catch (const std::exception& ex) {
			message += std::string(" | caused by: ") + ex.what();
			throw FunkeException(code, std::move(message), location);
		}
		catch (...) {
			message += " | caused by: <non-std exception>";
			throw FunkeException(code, std::move(message), location);
		}
	}

} // namespace Funke