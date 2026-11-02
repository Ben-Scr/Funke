#include "pch.hpp"
#include "Inspector/PropertyJson.hpp"

#include <charconv>
#include <cmath>
#include <limits>

namespace Funke::PropertyJson {

	namespace {
		int Channels(PropertyType type) {
			switch (type) {
			case PropertyType::Vec2: case PropertyType::IntVec2: return 2;
			case PropertyType::Vec3: case PropertyType::IntVec3: return 3;
			case PropertyType::Vec4: case PropertyType::IntVec4: case PropertyType::Color: return 4;
			default: return 0;
			}
		}

		bool IntegerVector(PropertyType type) {
			return type >= PropertyType::IntVec2 && type <= PropertyType::IntVec4;
		}

		template<typename T>
		bool Decimal(const Json::Value& value, T& out) {
			if (!value.IsString()) return false;
			const std::string text = value.AsStringOr();
			if (text.empty()) return false;
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
			return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::to_string(out) == text;
		}

		bool Number(const Json::Value& value, double min, double max, bool integer, double& out) {
			if (!value.IsNumber()) return false;
			out = value.AsDoubleOr(0.0);
			return std::isfinite(out) && out >= min && out <= max && (!integer || std::floor(out) == out);
		}
	}

	bool IsSupported(PropertyType type) {
		return type != PropertyType::None && type != PropertyType::StringList && type != PropertyType::List && type != PropertyType::Graph;
	}

	bool Encode(const PropertyValue& value, Json::Value& out, std::string& error) {
		error.clear();
		out = Json::Value();
		if (!IsSupported(value.Type)) { error = "unsupported_field_type"; return false; }
		if (const int channels = Channels(value.Type); channels > 0) {
			out = Json::Value::MakeArray();
			for (int i = 0; i < channels; ++i) {
				if (IntegerVector(value.Type)) out.Append(value.IntVec[i]);
				else {
					if (!std::isfinite(value.FloatVec[i])) { error = "non_finite_value"; return false; }
					out.Append(static_cast<double>(value.FloatVec[i]));
				}
			}
			return true;
		}
		switch (value.Type) {
		case PropertyType::Bool: out = Json::Value(value.BoolValue); break;
		case PropertyType::Int8: case PropertyType::Int16: case PropertyType::Int32: out = Json::Value(value.IntValue); break;
		case PropertyType::UInt8: case PropertyType::UInt16: case PropertyType::UInt32: out = Json::Value(value.UIntValue); break;
		case PropertyType::Int64: case PropertyType::Enum: case PropertyType::FlagEnum: out = Json::Value(std::to_string(value.IntValue)); break;
		case PropertyType::UInt64: out = Json::Value(std::to_string(value.UIntValue)); break;
		case PropertyType::Float: case PropertyType::Double:
			if (!std::isfinite(value.FloatValue)) { error = "non_finite_value"; return false; }
			out = Json::Value(value.FloatValue); break;
		case PropertyType::String:
			if (value.StringValue.size() > 65536) { error = "value_limit_exceeded"; return false; }
			out = Json::Value(value.StringValue); break;
		default:
			out = Json::Value::MakeObject();
			out.AddMember("id", std::to_string(value.UIntValue));
			out.AddMember("kind", value.Type == PropertyType::EntityRef && value.StringValue == "prefab"
				? std::string("prefab") : std::string(ToString(value.Type)));
			if (value.Type == PropertyType::TextureRef) out.AddMember("slice", value.StringValue);
			if (value.Type == PropertyType::ComponentRef) out.AddMember("component", value.StringValue);
			break;
		}
		return true;
	}

	bool Decode(PropertyType type, const Json::Value& value, PropertyValue& out, std::string& error) {
		out = PropertyValue{};
		out.Type = type;
		error = "invalid_value";
		if (!IsSupported(type)) { error = "unsupported_field_type"; return false; }
		if (const int channels = Channels(type); channels > 0) {
			if (!value.IsArray() || value.GetArray().size() != static_cast<size_t>(channels)) return false;
			for (int i = 0; i < channels; ++i) {
				double number;
				const bool integer = IntegerVector(type);
				if (!Number(value.GetArray()[i], integer ? INT32_MIN : -std::numeric_limits<float>::max(),
					integer ? INT32_MAX : std::numeric_limits<float>::max(), integer, number)) return false;
				if (integer) out.IntVec[i] = static_cast<int32_t>(number);
				else out.FloatVec[i] = static_cast<float>(number);
			}
		}
		else if (type >= PropertyType::Int8 && type <= PropertyType::Int32) {
			const int bits = type == PropertyType::Int8 ? 8 : type == PropertyType::Int16 ? 16 : 32;
			double number;
			if (!Number(value, -std::ldexp(1.0, bits - 1), std::ldexp(1.0, bits - 1) - 1.0, true, number)) return false;
			out.IntValue = static_cast<int64_t>(number);
		}
		else if (type >= PropertyType::UInt8 && type <= PropertyType::UInt32) {
			const int bits = type == PropertyType::UInt8 ? 8 : type == PropertyType::UInt16 ? 16 : 32;
			double number;
			if (!Number(value, 0, std::ldexp(1.0, bits) - 1.0, true, number)) return false;
			out.UIntValue = static_cast<uint64_t>(number);
		}
		else switch (type) {
		case PropertyType::Bool:
			if (!value.IsBool()) return false;
			out.BoolValue = value.AsBoolOr(false); break;
		case PropertyType::Int64: case PropertyType::Enum: case PropertyType::FlagEnum:
			if (!Decimal(value, out.IntValue)) return false;
			break;
		case PropertyType::UInt64:
			if (!Decimal(value, out.UIntValue)) return false;
			break;
		case PropertyType::Float: case PropertyType::Double: {
			const double max = type == PropertyType::Float ? std::numeric_limits<float>::max() : std::numeric_limits<double>::max();
			if (!Number(value, -max, max, false, out.FloatValue)) return false;
			if (type == PropertyType::Float) out.FloatValue = static_cast<float>(out.FloatValue);
			break;
		}
		case PropertyType::String:
			if (!value.IsString() || value.AsStringOr().size() > 65536) return false;
			out.StringValue = value.AsStringOr(); break;
		default: {
			const auto* id = value.FindMember("id");
			const auto* kind = value.FindMember("kind");
			if (!value.IsObject() || !id || !kind || !kind->IsString() || !Decimal(*id, out.UIntValue)) return false;
			const std::string kindName = kind->AsStringOr();
			if (type == PropertyType::EntityRef && kindName == "prefab") out.StringValue = "prefab";
			else if (kindName != ToString(type)) return false;
			if (type == PropertyType::ComponentRef || type == PropertyType::TextureRef) {
				const auto* text = value.FindMember(type == PropertyType::ComponentRef ? "component" : "slice");
				if (!text || !text->IsString() || text->AsStringOr().size() > 4096) return false;
				out.StringValue = text->AsStringOr();
			}
			break;
		}
		}
		error.clear();
		return true;
	}
}
