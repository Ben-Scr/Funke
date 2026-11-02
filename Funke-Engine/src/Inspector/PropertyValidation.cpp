#include "pch.hpp"
#include "Inspector/PropertyValidation.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Inspector/PropertyJson.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"

#include <algorithm>

namespace Funke::PropertyValidation {
	bool ValidateValue(const Entity* entity, const PropertyDescriptor& descriptor, const PropertyValue& value, std::string& error) {
		error.clear();
		auto fail = [&](const char* code) { error = code; return false; };
		if (descriptor.Metadata.ReadOnly || !descriptor.Set) return fail("read_only_property");
		if (descriptor.Type != value.Type) return fail("invalid_type");
		Json::Value encoded;
		PropertyValue decoded;
		if (!PropertyJson::Encode(value, encoded, error) || !PropertyJson::Decode(value.Type, encoded, decoded, error)) return false;
		if (descriptor.Metadata.EnabledIfFn) {
			if (!entity) return fail("unsupported_condition");
			if (!descriptor.Metadata.EnabledIfFn(*entity)) return fail("condition_not_met");
		}
		if (descriptor.Metadata.HasClamp || descriptor.Metadata.HasDragClamp) {
			const auto check = [&](double number) {
				return (!descriptor.Metadata.HasClamp || (number >= descriptor.Metadata.ClampMin && number <= descriptor.Metadata.ClampMax))
					&& (!descriptor.Metadata.HasDragClamp || (number >= descriptor.Metadata.DragClampMin && number <= descriptor.Metadata.DragClampMax));
			};
			if (value.Type >= PropertyType::Int8 && value.Type <= PropertyType::Int64) { if (!check(static_cast<double>(value.IntValue))) return fail("out_of_range"); }
			else if (value.Type >= PropertyType::UInt8 && value.Type <= PropertyType::UInt64) { if (!check(static_cast<double>(value.UIntValue))) return fail("out_of_range"); }
			else if (value.Type == PropertyType::Float || value.Type == PropertyType::Double) { if (!check(value.FloatValue)) return fail("out_of_range"); }
			else if (encoded.IsArray()) for (const auto& channel : encoded.GetArray()) if (!check(channel.AsDoubleOr(0))) return fail("out_of_range");
		}
		if (descriptor.Metadata.Enum) {
			if (value.Type == PropertyType::Enum && std::none_of(descriptor.Metadata.Enum->Options.begin(), descriptor.Metadata.Enum->Options.end(),
				[&](const auto& option) { return option.Value == value.IntValue; })) return fail("invalid_enum");
			if (value.Type == PropertyType::FlagEnum) {
				uint64_t mask = 0;
				for (const auto& option : descriptor.Metadata.Enum->Options) mask |= static_cast<uint64_t>(option.Value);
				if ((static_cast<uint64_t>(value.IntValue) & ~mask) != 0) return fail("invalid_enum");
			}
		}
		return true;
	}

	bool Validate(const Entity* entity, const PropertyDescriptor& descriptor, const PropertyValue& value, std::string& error) {
		if (!ValidateValue(entity, descriptor, value, error)) return false;
		auto fail = [&](const char* code) { error = code; return false; };
		if ((value.Type == PropertyType::EntityRef && value.StringValue != "prefab") || value.Type == PropertyType::ComponentRef) {
			if (value.UIntValue == 0) return true;
			EntityHandle target = entt::null;
			if (!entity || !entity->GetScene() || !entity->GetScene()->TryResolveEntityRef(value.UIntValue, target)) return fail("invalid_reference");
			if (value.Type == PropertyType::ComponentRef) {
				const ComponentInfo* info = nullptr;
				SceneManager::GetComponentRegistryOrBuiltIn().ForEachComponentInfo([&](auto, const ComponentInfo& candidate) {
					if (candidate.serializedName == value.StringValue || candidate.displayName == value.StringValue) info = &candidate;
				});
				if (!info || !info->has || !info->has(const_cast<Scene*>(entity->GetScene())->GetEntity(target))) return fail("invalid_reference");
				if (!descriptor.Metadata.ComponentTypeName.empty() && descriptor.Metadata.ComponentTypeName != info->displayName
					&& descriptor.Metadata.ComponentTypeName != info->serializedName) return fail("invalid_reference_type");
			}
		}
		else if (value.Type >= PropertyType::TextureRef && value.Type <= PropertyType::PrefabRef) {
			if (value.UIntValue == 0) return true;
			if (!AssetRegistry::Exists(value.UIntValue)) return fail("invalid_reference");
			AssetKind expected = descriptor.Metadata.AssetKindFilter;
			switch (value.Type) {
			case PropertyType::TextureRef: expected = AssetKind::Texture; break;
			case PropertyType::AudioRef: expected = AssetKind::Audio; break;
			case PropertyType::FontRef: expected = AssetKind::Font; break;
			case PropertyType::SceneRef: expected = AssetKind::Scene; break;
			case PropertyType::EntityRef: case PropertyType::PrefabRef: expected = AssetKind::Prefab; break;
			default: break;
			}
			if (expected != AssetKind::Unknown && AssetRegistry::GetKind(value.UIntValue) != expected) return fail("invalid_reference_type");
			if (value.Type == PropertyType::TextureRef && descriptor.Metadata.SuppressTextureSlices && !value.StringValue.empty()) return fail("unsupported_texture_slice");
		}
		return true;
	}
}
