#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Assets/AssetKind.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Serialization/SceneSerializerShared.hpp"

#include <string>

// SkinnedMeshRenderer. Its own slice, registered after Animation so no older typeIdU32 shifts.
namespace Funke::BuiltInComponentRegistrationInternal {
	namespace {
		using Json::Value;
		using SceneSerializerShared::GetBoolMember;
		using SceneSerializerShared::GetFloatMember;
		using SceneSerializerShared::GetStringMember;
		using SceneSerializerShared::GetUInt64Member;
		using SMR = SkinnedMeshRendererComponent;

		PropertyDescriptor MakeAssetRef(const char* name, const char* displayName, AssetKind kind, const char* tooltip,
			uint64_t SMR::* field, void (*invalidate)(uint64_t)) {
			PropertyDescriptor desc;
			desc.Name = name;
			desc.DisplayName = displayName;
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = kind;
			desc.Metadata.Tooltip = tooltip;
			desc.Get = [field](const Entity& e) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = e.GetComponent<SMR>().*field;
				return v;
			};
			desc.Set = [field, invalidate](Entity& e, const PropertyValue& v) {
				// Re-read on assignment so a file replaced moments ago is picked up.
				invalidate(v.UIntValue);
				e.GetComponent<SMR>().*field = v.UIntValue;
			};
			return desc;
		}

		Value Serialize(Entity e) {
			const SMR& c = e.GetComponent<SMR>();
			Value v = Value::MakeObject();
			if (c.Model != 0) v.AddMember("model", Value(std::to_string(c.Model)));
			if (c.Material != 0) v.AddMember("material", Value(std::to_string(c.Material)));
			v.AddMember("color", UIColorToJson(c.Color));
			v.AddMember("lit", Value(c.Lit));
			v.AddMember("castShadows", Value(c.CastShadows));
			v.AddMember("clip", Value(c.Clip));
			v.AddMember("speed", Value(c.Speed));
			v.AddMember("loop", Value(c.Loop));
			v.AddMember("playOnAwake", Value(c.PlayOnAwake));
			v.AddMember("previewInEditMode", Value(c.PreviewInEditMode));
			return v;
		}

		void Deserialize(Entity e, const Value& v) {
			SMR& c = e.GetComponent<SMR>();
			const SMR d{};
			c.Model = GetUInt64Member(v, "model", 0);
			c.Material = GetUInt64Member(v, "material", 0);
			const Value* color = v.FindMember("color");
			c.Color = color ? UIColorFromJson(*color, d.Color) : d.Color;
			c.Lit = GetBoolMember(v, "lit", d.Lit);
			c.CastShadows = GetBoolMember(v, "castShadows", d.CastShadows);
			c.Clip = GetStringMember(v, "clip", d.Clip);
			c.Speed = GetFloatMember(v, "speed", d.Speed);
			c.Loop = GetBoolMember(v, "loop", d.Loop);
			c.PlayOnAwake = GetBoolMember(v, "playOnAwake", d.PlayOnAwake);
			c.PreviewInEditMode = GetBoolMember(v, "previewInEditMode", d.PreviewInEditMode);
		}
	}

	void RegisterSkinnedMesh3D(ComponentRegistry& registry) {
		ComponentInfo info{ "Skinned Mesh Renderer", "3D", ComponentCategory::Component };
		info.serializedName = "SkinnedMeshRenderer";
		info.properties = {
			MakeAssetRef("Model", "Model", AssetKind::Mesh,
				"An animated model (.gltf/.glb with a skin or node animation). A model without one draws static.",
				&SMR::Model, &MeshLibrary::Invalidate),
			MakeAssetRef("Material", "Material", AssetKind::Material,
				"A .material asset. None uses a white default material tinted by Color.", &SMR::Material, &MaterialLibrary::Invalidate),
			Properties::Make("Color", "Color", &SMR::Color, Properties::Meta::Tooltip("Tint multiplied into the material's base color.")),
			Properties::Make("Lit", "Lit", &SMR::Lit,
				Properties::Meta::EnabledIf([](const Entity& e) { return e.GetComponent<SMR>().Material == 0; })
					.WithTooltip("Only used without a material; a material's own Lit flag wins.")),
			Properties::Make("CastShadows", "Cast Shadows", &SMR::CastShadows),

			Properties::Make("Clip", "Clip", &SMR::Clip,
				Properties::Meta::Header("Animation").WithTooltip("Name of the model's clip to play; empty plays its first clip.")),
			Properties::Make("Speed", "Speed", &SMR::Speed,
				Properties::Meta::DragSpeed(0.01f).WithTooltip("Playback rate; negative plays backwards.")),
			Properties::Make("Loop", "Loop", &SMR::Loop),
			Properties::Make("PlayOnAwake", "Play On Awake", &SMR::PlayOnAwake),
			Properties::Make("PreviewInEditMode", "Preview In Edit Mode", &SMR::PreviewInEditMode,
				Properties::Meta::Tooltip("Plays the clip in the editor outside Play mode.")),
		};
		info.serialize = Serialize;
		info.deserialize = Deserialize;
		registry.Register<SMR>(info);
	}
}
