#include <pch.hpp>
#include "Scripting/StructFieldBytes.hpp"
#include "Serialization/Json.hpp"

#include <charconv>
#include <cstring>
#include <limits>
#include <vector>

namespace Funke {

	namespace {
		enum class Scalar { None, F32, F64, I8, U8, I16, U16, I32, U32, I64, U64, Bool };

		struct Layout {
			Scalar Kind = Scalar::None;
			int Count = 1;
		};

		uint32_t ScalarSize(Scalar kind) {
			switch (kind) {
				case Scalar::I8: case Scalar::U8: case Scalar::Bool: return 1;
				case Scalar::I16: case Scalar::U16: return 2;
				case Scalar::F32: case Scalar::I32: case Scalar::U32: return 4;
				case Scalar::F64: case Scalar::I64: case Scalar::U64: return 8;
				default: return 0;
			}
		}

		Layout ResolveLayout(const StructFieldSlot& slot) {
			const std::string_view t = slot.Type;
			if (t == "float") return { Scalar::F32 };
			if (t == "double") return { Scalar::F64 };
			if (t == "int") return { Scalar::I32 };
			if (t == "uint") return { Scalar::U32 };
			if (t == "short") return { Scalar::I16 };
			if (t == "ushort") return { Scalar::U16 };
			if (t == "byte") return { Scalar::U8 };
			if (t == "sbyte") return { Scalar::I8 };
			if (t == "long") return { Scalar::I64 };
			if (t == "ulong") return { Scalar::U64 };
			if (t == "bool") return { Scalar::Bool };
			if (t == "vector2") return { Scalar::F32, 2 };
			if (t == "vector3") return { Scalar::F32, 3 };
			if (t == "vector4" || t == "color") return { Scalar::F32, 4 };
			if (t == "vector2Int") return { Scalar::I32, 2 };
			if (t == "vector3Int") return { Scalar::I32, 3 };
			if (t == "vector4Int") return { Scalar::I32, 4 };
			if (t == "enum" || t == "flagenum") {
				// The wire carries the underlying integer; byte-sized enums are almost always `: byte`.
				switch (slot.Size) {
					case 1: return { Scalar::U8 };
					case 2: return { Scalar::I16 };
					case 4: return { Scalar::I32 };
					case 8: return { Scalar::I64 };
					default: return {};
				}
			}
			return {};
		}

		bool Fits(const Layout& layout, const StructFieldSlot& slot, std::size_t byteCount) {
			const uint32_t size = ScalarSize(layout.Kind) * static_cast<uint32_t>(layout.Count);
			return size != 0 && size == slot.Size && static_cast<std::size_t>(slot.Offset) + size <= byteCount;
		}

		template <typename T>
		T Load(const uint8_t* p) {
			T value;
			std::memcpy(&value, p, sizeof(T));
			return value;
		}

		template <typename T>
		void Store(uint8_t* p, T value) {
			std::memcpy(p, &value, sizeof(T));
		}

		template <typename T>
		void AppendNumber(std::string& out, T value) {
			char buffer[64];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			out.append(buffer, result.ptr);
		}

		void AppendScalar(std::string& out, Scalar kind, const uint8_t* p) {
			switch (kind) {
				case Scalar::F32: AppendNumber(out, Load<float>(p)); break;
				case Scalar::F64: AppendNumber(out, Load<double>(p)); break;
				case Scalar::I8: AppendNumber(out, static_cast<int>(Load<int8_t>(p))); break;
				case Scalar::U8: AppendNumber(out, static_cast<unsigned>(Load<uint8_t>(p))); break;
				case Scalar::I16: AppendNumber(out, Load<int16_t>(p)); break;
				case Scalar::U16: AppendNumber(out, Load<uint16_t>(p)); break;
				case Scalar::I32: AppendNumber(out, Load<int32_t>(p)); break;
				case Scalar::U32: AppendNumber(out, Load<uint32_t>(p)); break;
				case Scalar::I64: AppendNumber(out, Load<int64_t>(p)); break;
				case Scalar::U64: AppendNumber(out, Load<uint64_t>(p)); break;
				case Scalar::Bool: out += Load<uint8_t>(p) != 0 ? "true" : "false"; break;
				default: break;
			}
		}

		template <typename T>
		bool Parse(std::string_view text, T& out) {
			const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
			return result.ec == std::errc{} && result.ptr == text.data() + text.size();
		}

		template <typename T>
		bool ParseInto(std::string_view text, uint8_t* p) {
			T value{};
			if (!Parse(text, value)) return false;
			Store(p, value);
			return true;
		}

		std::string_view Trim(std::string_view s) {
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
			while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
			return s;
		}

		bool WriteScalar(Scalar kind, std::string_view text, uint8_t* p) {
			text = Trim(text);
			switch (kind) {
				case Scalar::F32: return ParseInto<float>(text, p);
				case Scalar::F64: return ParseInto<double>(text, p);
				case Scalar::I8: return ParseInto<int8_t>(text, p);
				case Scalar::U8: return ParseInto<uint8_t>(text, p);
				case Scalar::I16: return ParseInto<int16_t>(text, p);
				case Scalar::U16: return ParseInto<uint16_t>(text, p);
				case Scalar::I32: return ParseInto<int32_t>(text, p);
				case Scalar::U32: return ParseInto<uint32_t>(text, p);
				case Scalar::I64: return ParseInto<int64_t>(text, p);
				case Scalar::U64: return ParseInto<uint64_t>(text, p);
				case Scalar::Bool:
					if (text == "true" || text == "True" || text == "1") { Store<uint8_t>(p, 1); return true; }
					if (text == "false" || text == "False" || text == "0") { Store<uint8_t>(p, 0); return true; }
					return false;
				default: return false;
			}
		}
	}

	bool WriteStructFieldByName(uint8_t* bytes, std::size_t byteCount, std::string_view currentFieldDefs,
		std::string_view fieldName, std::string_view expectedType, std::string_view value) {
		Json::Value fields;
		if (!Json::TryParse(currentFieldDefs, fields) || !fields.IsArray()) return false;
		for (const auto& field : fields.GetArray()) {
			const auto* name = field.FindMember("name");
			if (!name || name->AsStringOr() != fieldName) continue;
			const auto* type = field.FindMember("type");
			const auto* offset = field.FindMember("offset");
			const auto* size = field.FindMember("size");
			const auto* readOnly = field.FindMember("readOnly");
			if (!type || type->AsStringOr() != expectedType || !offset || !size
				|| (readOnly && readOnly->AsBoolOr(false))) return false;
			const uint64_t currentOffset = offset->AsUInt64Or(std::numeric_limits<uint64_t>::max());
			const uint64_t currentSize = size->AsUInt64Or(0);
			if (currentOffset > std::numeric_limits<uint32_t>::max() || currentSize > std::numeric_limits<uint32_t>::max()) return false;
			return WriteStructField(bytes, byteCount,
				{ expectedType, static_cast<uint32_t>(currentOffset), static_cast<uint32_t>(currentSize) }, value);
		}
		return false;
	}

	std::optional<std::string> ReadStructField(const uint8_t* bytes, std::size_t byteCount, const StructFieldSlot& slot) {
		const Layout layout = ResolveLayout(slot);
		if (!bytes || !Fits(layout, slot, byteCount)) return std::nullopt;
		std::string out;
		const uint32_t stride = ScalarSize(layout.Kind);
		for (int i = 0; i < layout.Count; ++i) {
			if (i > 0) out += ',';
			AppendScalar(out, layout.Kind, bytes + slot.Offset + static_cast<std::size_t>(i) * stride);
		}
		return out;
	}

	bool WriteStructField(uint8_t* bytes, std::size_t byteCount, const StructFieldSlot& slot, std::string_view value) {
		const Layout layout = ResolveLayout(slot);
		if (!bytes || !Fits(layout, slot, byteCount)) return false;

		std::vector<std::string_view> parts;
		std::size_t start = 0;
		while (true) {
			const std::size_t comma = value.find(',', start);
			parts.push_back(value.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
			if (comma == std::string_view::npos) break;
			start = comma + 1;
		}
		if (static_cast<int>(parts.size()) != layout.Count) return false;

		// Parse into a scratch copy first so a bad component leaves the field untouched.
		const uint32_t stride = ScalarSize(layout.Kind);
		std::vector<uint8_t> scratch(bytes + slot.Offset, bytes + slot.Offset + slot.Size);
		for (int i = 0; i < layout.Count; ++i) {
			if (!WriteScalar(layout.Kind, parts[static_cast<std::size_t>(i)], scratch.data() + static_cast<std::size_t>(i) * stride)) return false;
		}
		std::memcpy(bytes + slot.Offset, scratch.data(), scratch.size());
		return true;
	}

}
