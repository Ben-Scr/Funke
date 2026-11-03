#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace Funke::Platform {

	struct DialogText {
		std::string Preview;
		bool Truncated = false;

		static DialogText Prepare(std::string_view message) {
			DialogText result;
			size_t offset = 0;
			int line = 0;
			int column = 0;
			while (offset < message.size()) {
				const unsigned char first = static_cast<unsigned char>(message[offset]);
				if (first == '\r') { ++offset; continue; }
				if (first == '\n' || column == 80) {
					if (++line == 8) break;
					result.Preview += '\n';
					column = 0;
					if (first == '\n') { ++offset; continue; }
				}
				size_t length = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
				length = std::min(length, message.size() - offset);
				if (first < 0x20) result.Preview += ' ';
				else result.Preview.append(message.substr(offset, length));
				offset += length;
				++column;
			}
			result.Truncated = offset < message.size();
			if (result.Truncated) result.Preview += "\n\nFull details are available with 'Show details'.";
			return result;
		}
	};

}
