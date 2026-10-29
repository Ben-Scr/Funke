#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Funke {

	// One field of a C# IComponent struct inside its raw component bytes, as described by the managed field defs
	// (wire type tag such as "float"/"vector2"/"enum", plus offset and size). Values use the same text format as
	// the script-field wire ("1.5", "true", "1,2" for vectors, the integer for enums).
	struct StructFieldSlot {
		std::string_view Type;
		uint32_t Offset = 0;
		uint32_t Size = 0;
	};

	// Empty when the type is not a plain value or the slot does not fit in the bytes.
	std::optional<std::string> ReadStructField(const uint8_t* bytes, std::size_t byteCount, const StructFieldSlot& slot);
	bool WriteStructField(uint8_t* bytes, std::size_t byteCount, const StructFieldSlot& slot, std::string_view value);
	bool WriteStructFieldByName(uint8_t* bytes, std::size_t byteCount, std::string_view currentFieldDefs,
		std::string_view fieldName, std::string_view expectedType, std::string_view value);

}
