#pragma once
#include "Inspector/PropertyDescriptor.hpp"

namespace Funke::PropertyValidation {
	FUNKE_API bool ValidateValue(const Entity* entity, const PropertyDescriptor& descriptor,
		const PropertyValue& value, std::string& error);
	FUNKE_API bool Validate(const Entity* entity, const PropertyDescriptor& descriptor,
		const PropertyValue& value, std::string& error);
}
