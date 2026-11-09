#pragma once

#include "Inspector/PropertyType.hpp"
#include "Serialization/Json.hpp"
#include "Utils/CharConv.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>

namespace Funke::ScriptCollectionValue {

	inline bool IsScalar(PropertyType type) {
		return (type >= PropertyType::Bool && type <= PropertyType::String)
			|| type == PropertyType::Enum || type == PropertyType::FlagEnum;
	}

	inline bool ValidScalar(const Json::Value& value, PropertyType type) {
		if (type == PropertyType::String) return value.IsNull() || value.IsString();
		if (!value.IsString()) return false;
		const std::string text = value.AsStringOr();
		if (text.empty()) return false;
		if (type == PropertyType::Bool)
			return text == "true" || text == "false" || text == "True" || text == "False" || text == "1" || text == "0";
		const char* first = text.data();
		const char* last = first + text.size();
		if (type == PropertyType::Float || type == PropertyType::Double) {
			double number = 0;
			const auto parsed = CharConv::ParseFloat(first, last, number);
			return parsed.ec == std::errc{} && parsed.ptr == last;
		}
		if (type >= PropertyType::UInt8 && type <= PropertyType::UInt64) {
			uint64_t number = 0;
			const auto parsed = std::from_chars(first, last, number);
			const uint64_t max = type == PropertyType::UInt8 ? UINT8_MAX : type == PropertyType::UInt16 ? UINT16_MAX
				: type == PropertyType::UInt32 ? UINT32_MAX : UINT64_MAX;
			return parsed.ec == std::errc{} && parsed.ptr == last && number <= max;
		}
		int64_t number = 0;
		const auto parsed = std::from_chars(first, last, number);
		const int64_t min = type == PropertyType::Int8 ? INT8_MIN : type == PropertyType::Int16 ? INT16_MIN
			: type == PropertyType::Int32 ? INT32_MIN : INT64_MIN;
		const int64_t max = type == PropertyType::Int8 ? INT8_MAX : type == PropertyType::Int16 ? INT16_MAX
			: type == PropertyType::Int32 ? INT32_MAX : INT64_MAX;
		return parsed.ec == std::errc{} && parsed.ptr == last && number >= min && number <= max;
	}

	inline bool Decode(PropertyType type, const std::string& text, PropertyType leaf, Json::Value& root, std::string& error) {
		auto fail = [&](const char* message) { error = message; return false; };
		if (!Json::TryParse(text, root, &error) || !root.IsObject()) return fail("Invalid collection data; the stored value is unchanged.");
		const auto* version = root.FindMember("version");
		const auto* kind = root.FindMember("kind");
		const auto* leafTag = root.FindMember("leaf");
		const auto* items = root.FindMember("items");
		if (!version || version->GetNumberKind() == Json::Value::NumberKind::Double || version->AsIntOr(0) != 1
			|| !kind || kind->AsStringOr() != ToString(type)
			|| !leafTag || leafTag->AsStringOr() != ToString(leaf) || !IsScalar(leaf) || !items)
			return fail("Unsupported collection schema; the stored value is unchanged.");
		if (type == PropertyType::Dictionary) {
			const auto* comparer = root.FindMember("comparer");
			if (!comparer || comparer->AsStringOr() != "ordinal")
				return fail("Only the default or StringComparer.Ordinal dictionary comparer is supported.");
		}
		if (items->IsNull()) { error.clear(); return true; }
		if (!items->IsArray()) return fail("Expected collection items or null.");
		std::unordered_set<std::string> keys;
		for (const auto& row : items->GetArray()) {
			if (type == PropertyType::Dictionary) {
				if (!row.IsArray() || row.GetArray().size() != 2 || !row.GetArray()[0].IsString())
					return fail("Dictionary keys must be non-null strings.");
				if (!keys.insert(row.GetArray()[0].AsStringOr()).second) return fail("Duplicate dictionary key; the stored value is unchanged.");
				if (!ValidScalar(row.GetArray()[1], leaf)) return fail("Invalid dictionary value; the stored value is unchanged.");
			}
			else {
				if (row.IsNull()) continue;
				if (!row.IsArray()) return fail("Expected an inner list or null.");
				for (const auto& item : row.GetArray())
					if (!ValidScalar(item, leaf)) return fail("Invalid list value; the stored value is unchanged.");
			}
		}
		error.clear();
		return true;
	}

	inline std::string Encode(Json::Value root) {
		const auto* kind = root.FindMember("kind");
		auto* items = root.FindMember("items");
		if (kind && kind->AsStringOr() == "dictionary" && items && items->IsArray()) {
			auto& entries = items->GetArray();
			std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
				return a.GetArray()[0].AsStringOr() < b.GetArray()[0].AsStringOr();
			});
		}
		return Json::Stringify(root);
	}
}
