#pragma once

#include "Inspector/PropertyRegistration.hpp"
#include "Utils/CharConv.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

namespace Funke {

	class NativeScript;

	inline constexpr uint32_t k_NativeScriptAbiVersion = 3;

	// Strings and callbacks remain owned by the script DLL. Get copies into a caller-owned buffer.
	struct NativeScriptField {
		const char* Name = nullptr;
		const char* DisplayName = nullptr;
		const char* Tooltip = nullptr;
		PropertyType Type = PropertyType::None;
		bool ReadOnly = false;
		bool HasClamp = false;
		double ClampMin = 0.0;
		double ClampMax = 0.0;
		uint32_t (*Get)(const NativeScript*, char*, uint32_t) noexcept = nullptr;
		bool (*Set)(NativeScript*, const char*, uint32_t) noexcept = nullptr;
		const char* (*GetEnumOption)(uint32_t, int64_t*) noexcept = nullptr;

		constexpr NativeScriptField WithTooltip(const char* text) const {
			auto result = *this;
			result.Tooltip = text;
			return result;
		}
		constexpr NativeScriptField WithClamp(double min, double max) const {
			auto result = *this;
			result.HasClamp = true;
			result.ClampMin = min;
			result.ClampMax = max;
			return result;
		}
		constexpr NativeScriptField AsReadOnly() const {
			auto result = *this;
			result.ReadOnly = true;
			return result;
		}
	};

	namespace NativeFields {
		namespace detail {
			template<typename T> struct MemberTraits;
			template<typename TClass, typename TValue>
			struct MemberTraits<TValue TClass::*> {
				using Class = TClass;
				using Value = TValue;
			};

			template<typename T>
			bool ParseNumber(std::string_view text, T& value) {
				if (text.empty()) return false;
				const char* begin = text.data();
				const char* end = begin + text.size();
				std::from_chars_result result;
				if constexpr (std::is_floating_point_v<T>) {
					result = CharConv::ParseFloat(begin, end, value);
					if (!std::isfinite(value)) return false;
				} else {
					result = std::from_chars(begin, end, value);
				}
				return result.ec == std::errc{} && result.ptr == end;
			}

			template<typename T>
			bool Parse(std::string_view text, T& value) {
				if constexpr (std::is_same_v<T, std::string>) {
					value.assign(text);
					return true;
				} else if constexpr (std::is_same_v<T, bool>) {
					if (text == "true" || text == "1") { value = true; return true; }
					if (text == "false" || text == "0") { value = false; return true; }
					return false;
				} else if constexpr (std::is_enum_v<T>) {
					std::underlying_type_t<T> parsed{};
					if (!ParseNumber(text, parsed)) return false;
					value = static_cast<T>(parsed);
					return true;
				} else if constexpr (std::is_arithmetic_v<T>) {
					return ParseNumber(text, value);
				} else {
					constexpr auto type = Properties::detail::DeducePropertyType<T>();
					constexpr bool integer = type == PropertyType::IntVec2 || type == PropertyType::IntVec3 || type == PropertyType::IntVec4;
					constexpr uint32_t count = type == PropertyType::Vec2 || type == PropertyType::IntVec2 ? 2
						: type == PropertyType::Vec3 || type == PropertyType::IntVec3 ? 3 : 4;
					PropertyValue parsed;
					parsed.Type = type;
					for (uint32_t i = 0; i < count; ++i) {
						const size_t comma = text.find(',');
						if ((i + 1 < count) != (comma != std::string_view::npos)) return false;
						const auto part = text.substr(0, comma);
						if constexpr (integer) {
							if (!ParseNumber(part, parsed.IntVec[i])) return false;
						} else {
							if (!ParseNumber(part, parsed.FloatVec[i])) return false;
						}
						if (comma != std::string_view::npos) text.remove_prefix(comma + 1);
					}
					Properties::detail::Unbox(parsed, value);
					return true;
				}
			}
		}

		template<auto Member>
		constexpr NativeScriptField Make(const char* name, const char* displayName = nullptr) {
			using TClass = typename detail::MemberTraits<decltype(Member)>::Class;
			using TMember = typename detail::MemberTraits<decltype(Member)>::Value;
			using TValue = std::remove_cv_t<TMember>;
			constexpr PropertyType type = Properties::detail::DeducePropertyType<TValue>();
			static_assert(type != PropertyType::None, "Native script fields require a scalar, string, vector, color or enum.");
			NativeScriptField field;
			field.Name = name;
			field.DisplayName = displayName ? displayName : name;
			field.Type = type;
			field.ReadOnly = std::is_const_v<TMember>;
			field.Get = [](const NativeScript* script, char* buffer, uint32_t capacity) noexcept -> uint32_t {
				try {
					if (!script) return 0;
					const auto value = Properties::detail::Box<TValue>(static_cast<const TClass*>(script)->*Member).ToString();
					if (value.size() >= std::numeric_limits<uint32_t>::max()) return 0;
					const auto required = static_cast<uint32_t>(value.size() + 1);
					if (buffer && capacity >= required) std::memcpy(buffer, value.c_str(), required);
					return required;
				} catch (...) { return 0; }
			};
			if constexpr (!std::is_const_v<TMember>) {
				field.Set = [](NativeScript* script, const char* text, uint32_t length) noexcept -> bool {
					try {
						if (!script || !text) return false;
						TValue parsed{};
						if (!detail::Parse<TValue>(std::string_view(text, length), parsed)) return false;
						static_cast<TClass*>(script)->*Member = std::move(parsed);
						return true;
					} catch (...) { return false; }
				};
			}
			if constexpr (std::is_enum_v<TValue>) {
				field.GetEnumOption = [](uint32_t index, int64_t* value) noexcept -> const char* {
					constexpr auto entries = magic_enum::enum_entries<TValue>();
					if (index >= entries.size() || !value) return nullptr;
					*value = static_cast<int64_t>(entries[index].first);
					return entries[index].second.data();
				};
			}
			return field;
		}
	}
}

#define FNK_NATIVE_FIELDS(...) \
	const Funke::NativeScriptField* GetFields(uint32_t* count) const noexcept override { \
		static constexpr Funke::NativeScriptField fields[] = { __VA_ARGS__ }; \
		if (count) *count = static_cast<uint32_t>(sizeof(fields) / sizeof(fields[0])); \
		return fields; \
	}
