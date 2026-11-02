#pragma once
#include "Core/Export.hpp"
#include "Inspector/PropertyValue.hpp"
#include "Serialization/Json.hpp"

namespace Funke::PropertyJson {

	FUNKE_API bool IsSupported(PropertyType type);
	FUNKE_API bool Encode(const PropertyValue& value, Json::Value& out, std::string& error);
	FUNKE_API bool Decode(PropertyType type, const Json::Value& value, PropertyValue& out, std::string& error);

}
