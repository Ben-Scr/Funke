#pragma once

#include "Core/Export.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace Funke::Hash {

	// Portable SHA-256 on every platform. Returns false (with outError) only when the file can't be read.
	FUNKE_API bool Sha256OfFile(const std::filesystem::path& filePath,
		std::string& outHexLower,
		std::string& outError);

	// Lowercase hex SHA-256 of an in-memory buffer.
	FUNKE_API std::string Sha256OfBytes(const void* data, size_t size);

	// Returns `s` with ASCII letters lowered. Used to normalize user-supplied
	// expected-hash strings before equality compare.
	FUNKE_API std::string ToLowerHex(std::string_view s);

} // namespace Funke::Hash
