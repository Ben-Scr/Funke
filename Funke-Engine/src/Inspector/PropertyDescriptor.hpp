#pragma once

#include "Inspector/PropertyMetadata.hpp"
#include "Inspector/PropertyType.hpp"
#include "Inspector/PropertyValue.hpp"
#include "Scene/Entity.hpp"
#include "Utils/InlineFunction.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Funke {

	// One inspectable field; Get/Set are per-entity lambdas — drawer never sees the native type.
	// When VariantBranches is non-empty this acts as a discriminator; mixed-selection variants render a "mixed variant" hint instead of any branch.
	struct PropertyDescriptor {
		struct Branch {
			int64_t TagValue = 0;
			std::vector<PropertyDescriptor> Properties;
		};

		std::string Name;          // Stable serialised name (matches the C# field name).
		std::string DisplayName;   // Pretty label shown in the inspector. Defaults to Name.
		PropertyType Type = PropertyType::None;
		PropertyMetadata Metadata;

		// InlineFunction, not std::function: every registered field instantiates its own lambda pair,
		// and std::function's per-type vtable + RTTI added ~0.35 MB to the engine DLL.
		InlineFunction<PropertyValue(const Entity&)> Get;
		InlineFunction<void(Entity&, const PropertyValue&)> Set;

		// Optional. When non-empty, this descriptor renders its own widget
		// AND the matching branch's nested descriptors below it.
		std::vector<Branch> VariantBranches;
	};

} // namespace Funke
