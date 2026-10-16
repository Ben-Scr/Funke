#include <pch.hpp>
#include "Inspector/MaterialShaderInspector.hpp"

#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/Material.hpp"
#include "Gui/EditorUI.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Scene/Entity.hpp"

#include <imgui.h>

#include <cctype>
#include <span>
#include <string>
#include <vector>

namespace Index::MaterialShaderInspector {
	namespace {
		// "waveHeight" -> "Wave Height"
		std::string DisplayName(const std::string& name) {
			std::string result;
			for (size_t i = 0; i < name.size(); ++i) {
				const char c = name[i];
				if (c == '_') {
					result += ' ';
					continue;
				}
				if (i > 0 && std::isupper(static_cast<unsigned char>(c)) && std::islower(static_cast<unsigned char>(name[i - 1]))) result += ' ';
				result += i == 0 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
			}
			return result;
		}

		Material Current(uint64_t guid) {
			const Material* material = MaterialLibrary::Get(guid);
			return material ? *material : Material{};
		}

		template <typename Change>
		void Edit(uint64_t guid, Change change) {
			const Material* material = MaterialLibrary::Get(guid);
			if (!material) return;
			Material updated = *material;
			change(updated);
			MaterialLibrary::Save(guid, updated);
		}

		PropertyDescriptor MakeShaderRef(uint64_t guid) {
			PropertyDescriptor desc;
			desc.Name = "Shader";
			desc.DisplayName = "Shader";
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = AssetKind::Shader;
			desc.Metadata.Tooltip = "A .wgsl user shader. None uses the standard PBR shader and the fields below.";
			desc.Get = [guid](const Entity&) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = Current(guid).Shader;
				return v;
			};
			desc.Set = [guid](Entity&, const PropertyValue& v) {
				Edit(guid, [&v](Material& m) { m.Shader = v.UIntValue; });
			};
			return desc;
		}

		template <typename T>
		PropertyDescriptor MakeValueField(uint64_t guid, const ShaderProperty& property, PropertyMetadata metadata) {
			const std::string name = property.Name;
			const ShaderProperty declaration = property;
			return Properties::MakeWith<T>(name, DisplayName(name),
				[guid, declaration](const Entity&) {
					const glm::vec4 v = ResolveMaterialProperty(Current(guid), declaration).Value;
					if constexpr (std::is_same_v<T, float>) return v.x;
					else if constexpr (std::is_same_v<T, glm::vec2>) return glm::vec2(v);
					else if constexpr (std::is_same_v<T, glm::vec3>) return glm::vec3(v);
					else if constexpr (std::is_same_v<T, glm::vec4>) return v;
					else return Color(v.r, v.g, v.b, v.a);
				},
				[guid, declaration](Entity&, T value) {
					MaterialPropertyValue stored;
					stored.Type = declaration.Type;
					if constexpr (std::is_same_v<T, float>) stored.Value = glm::vec4(value, 0.0f, 0.0f, 0.0f);
					else if constexpr (std::is_same_v<T, glm::vec2>) stored.Value = glm::vec4(value, 0.0f, 0.0f);
					else if constexpr (std::is_same_v<T, glm::vec3>) stored.Value = glm::vec4(value, 0.0f);
					else if constexpr (std::is_same_v<T, glm::vec4>) stored.Value = value;
					else stored.Value = glm::vec4(value.r, value.g, value.b, value.a);
					Edit(guid, [&](Material& m) { m.Properties[declaration.Name] = stored; });
				},
				std::move(metadata));
		}

		PropertyDescriptor MakeField(uint64_t guid, const ShaderProperty& property) {
			PropertyMetadata metadata;
			if (property.HasRange) metadata = Properties::Meta::Clamp(property.Min, property.Max, (property.Max - property.Min) / 200.0f);
			else metadata = Properties::Meta::DragSpeed(0.01f);
			switch (property.Type) {
				case ShaderPropertyType::Float: return MakeValueField<float>(guid, property, std::move(metadata));
				case ShaderPropertyType::Vec2: return MakeValueField<glm::vec2>(guid, property, std::move(metadata));
				case ShaderPropertyType::Vec3: return MakeValueField<glm::vec3>(guid, property, std::move(metadata));
				case ShaderPropertyType::Vec4: return MakeValueField<glm::vec4>(guid, property, std::move(metadata));
				case ShaderPropertyType::Color: return MakeValueField<Color>(guid, property, PropertyMetadata{});
				case ShaderPropertyType::Texture: break;
			}
			const ShaderProperty declaration = property;
			return Properties::MakeTextureRef(property.Name, DisplayName(property.Name),
				[guid, declaration](const Entity&) { return ResolveMaterialProperty(Current(guid), declaration).Texture; },
				[guid, declaration](Entity&, uint64_t texture) {
					MaterialPropertyValue stored;
					stored.Type = ShaderPropertyType::Texture;
					stored.Texture = texture;
					Edit(guid, [&](Material& m) { m.Properties[declaration.Name] = stored; });
				},
				Properties::Meta::Tooltip("None samples the shader's default texture."));
		}

		void DrawDiagnostics(uint64_t shaderGuid) {
			const std::vector<ShaderDiagnostic> diagnostics = CustomShaderLibrary::GetDiagnostics(shaderGuid);
			if (diagnostics.empty()) return;
			for (const ShaderDiagnostic& diagnostic : diagnostics) {
				const std::string text = diagnostic.Line > 0
					? "Line " + std::to_string(diagnostic.Line) + ": " + diagnostic.Message
					: diagnostic.Message;
				EditorUI::StatusText(diagnostic.IsError ? EditorUI::Tone::Error : EditorUI::Tone::Warning, text.c_str());
			}
		}
	}

	bool Draw(uint64_t materialGuid) {
		const Entity placeholder = Entity::Null;
		const std::span<const Entity> entitySpan(&placeholder, 1);
		const std::string keyPrefix = "material:" + std::to_string(materialGuid) + ".";

		const PropertyDescriptor shaderField = MakeShaderRef(materialGuid);
		PropertyDrawer::Draw(entitySpan, shaderField, keyPrefix + shaderField.Name);
		const uint64_t shaderGuid = Current(materialGuid).Shader;
		if (shaderGuid == 0) return false;

		const ShaderDescription* description = CustomShaderLibrary::GetDescription(shaderGuid);
		if (!description) {
			DrawDiagnostics(shaderGuid);
			EditorUI::StatusText(EditorUI::Tone::Warning, "The shader has not parsed yet; fix its errors to edit its properties.");
			return true;
		}

		const std::string summary = std::string(ShaderTargetToString(description->Target)) + " shader"
			+ (description->Name.empty() ? std::string{} : " \"" + description->Name + "\"");
		ImGui::TextDisabled("%s", summary.c_str());
		DrawDiagnostics(shaderGuid);

		if (description->Properties.empty()) {
			ImGui::TextDisabled("No // @property declarations.");
			return true;
		}
		for (const ShaderProperty& property : description->Properties) {
			const PropertyDescriptor field = MakeField(materialGuid, property);
			PropertyDrawer::Draw(entitySpan, field, keyPrefix + "prop." + field.Name);
		}
		if (ImGui::SmallButton("Reset to Shader Defaults")) {
			Edit(materialGuid, [description](Material& m) {
				for (const ShaderProperty& property : description->Properties) m.Properties.erase(property.Name);
			});
		}
		return true;
	}

}
