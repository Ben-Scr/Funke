#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Funke::TextEncoding {

	inline constexpr uint32_t k_ReplacementCodepoint = 0xFFFD;

	constexpr bool IsScalar(uint32_t codepoint) {
		return codepoint <= 0x10FFFF && (codepoint < 0xD800 || codepoint > 0xDFFF);
	}

	inline bool DecodeUtf8(std::string_view text, size_t index, uint32_t& codepoint, int& length) {
		codepoint = 0;
		length = 0;
		if (index >= text.size()) return false;
		const auto first = static_cast<unsigned char>(text[index]);
		length = 1;
		if (first < 0x80) {
			codepoint = first;
			return true;
		}
		codepoint = k_ReplacementCodepoint;
		const int count = first >= 0xC2 && first <= 0xDF ? 2
			: first >= 0xE0 && first <= 0xEF ? 3
			: first >= 0xF0 && first <= 0xF4 ? 4 : 0;
		if (count == 0 || text.size() - index < static_cast<size_t>(count)) return true;
		uint32_t value = first & (0x7F >> count);
		for (int i = 1; i < count; ++i) {
			const auto next = static_cast<unsigned char>(text[index + i]);
			if ((next & 0xC0) != 0x80) return true;
			value = (value << 6) | (next & 0x3F);
		}
		const uint32_t minimum = count == 2 ? 0x80 : count == 3 ? 0x800 : 0x10000;
		if (value < minimum || !IsScalar(value)) return true;
		codepoint = value;
		length = count;
		return true;
	}

	inline size_t PreviousByte(std::string_view text, size_t index) {
		if (index > text.size()) index = text.size();
		if (index == 0) return 0;
		const size_t first = index > 4 ? index - 4 : 0;
		for (size_t candidate = first; candidate < index; ++candidate) {
			uint32_t codepoint;
			int length;
			if (DecodeUtf8(text, candidate, codepoint, length)
				&& candidate + static_cast<size_t>(length) == index) return candidate;
		}
		return index - 1;
	}
}
