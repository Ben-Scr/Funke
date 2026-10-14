#pragma once

#include <charconv>
#include <locale>
#include <sstream>
#include <string_view>
#include <system_error>

namespace Index::CharConv {

	// Locale-independent floating-point parsing. std::from_chars is the fast path; libc++ before 20
	// (Android NDK, older Apple SDKs) has no floating-point overload, so it falls back to a stream
	// imbued with the classic "C" locale (strtod would honor LC_NUMERIC and misread "3.14" in German).
	template <typename T>
	inline std::from_chars_result ParseFloat(const char* first, const char* last, T& value) {
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 200000
		std::istringstream stream(std::string(first, last));
		stream.imbue(std::locale::classic());
		T parsed{};
		stream >> parsed;
		if (stream.fail()) return { first, std::errc::invalid_argument };
		const std::streamoff consumed = stream.eof() ? (last - first) : static_cast<std::streamoff>(stream.tellg());
		value = parsed;
		return { first + consumed, std::errc{} };
#else
		return std::from_chars(first, last, value);
#endif
	}

}
