#include "pch.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Scripting/ScriptBindingGuard.hpp"

#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/Material.hpp"

#include <string>

// Shader properties of .material assets for C# (Material.SetFloat & co.). Core rather than Index.3D: sprites and post
// effects use materials too. Changes go to the shared live instance; files are not written.
namespace Index {
	namespace {
		const ShaderProperty* FindDeclared(const Material& material, const char* name) {
			const ShaderDescription* description = material.Shader != 0 ? CustomShaderLibrary::GetDescription(material.Shader) : nullptr;
			return description ? description->FindProperty(name) : nullptr;
		}

		bool IsFourWide(ShaderPropertyType type) {
			return type == ShaderPropertyType::Vec4 || type == ShaderPropertyType::Color;
		}

		uint64_t Index_Material_GetShader(uint64_t guid) {
			const Material* material = MaterialLibrary::Get(guid);
			return material ? material->Shader : 0;
		}

		void Index_Material_SetShader(uint64_t guid, uint64_t shaderGuid) {
			const Material* current = MaterialLibrary::Get(guid);
			if (!current) return;
			Material m = *current;
			m.Shader = shaderGuid;
			MaterialLibrary::Set(guid, m);
		}

		// Returns the property type + 1, or 0 when neither the shader declares it nor the material stores it.
		int Index_Material_GetProperty(uint64_t guid, const char* name, float* outValues, uint64_t* outTexture) {
			const Material* material = MaterialLibrary::Get(guid);
			if (!material || !name) return 0;
			MaterialPropertyValue value;
			if (const ShaderProperty* declared = FindDeclared(*material, name)) {
				value = ResolveMaterialProperty(*material, *declared);
			}
			else if (auto it = material->Properties.find(name); it != material->Properties.end()) {
				value = it->second;
			}
			else {
				return 0;
			}
			if (outValues) {
				for (int i = 0; i < 4; ++i) outValues[i] = value.Value[i];
			}
			if (outTexture) *outTexture = value.Texture;
			return static_cast<int>(value.Type) + 1;
		}

		// Returns 0 when the shader declares the property with a type the value cannot feed.
		int Index_Material_SetProperty(uint64_t guid, const char* name, int type, const float* values, uint64_t texture) {
			const Material* current = MaterialLibrary::Get(guid);
			if (!current || !name || type < 0 || type > static_cast<int>(ShaderPropertyType::Texture)) return 0;
			MaterialPropertyValue value;
			value.Type = static_cast<ShaderPropertyType>(type);
			if (values) value.Value = glm::vec4(values[0], values[1], values[2], values[3]);
			value.Texture = texture;
			if (const ShaderProperty* declared = FindDeclared(*current, name)) {
				const bool texturesMatch = (declared->Type == ShaderPropertyType::Texture) == (value.Type == ShaderPropertyType::Texture);
				// Any number feeds a float or vector; four-wide values feed colors.
				const bool compatible = texturesMatch && (declared->Type != ShaderPropertyType::Color || IsFourWide(value.Type));
				if (!compatible) return 0;
				value.Type = declared->Type;
				if (declared->HasRange) value.Value = glm::clamp(value.Value, glm::vec4(declared->Min), glm::vec4(declared->Max));
			}
			Material m = *current;
			m.Properties[name] = value;
			MaterialLibrary::Set(guid, m);
			return 1;
		}
	}

	void PopulateMaterialBindings(NativeBindings& b) {
		b.Material_GetShader = IDX_GUARDED_BINDING(Index_Material_GetShader);
		b.Material_SetShader = IDX_GUARDED_BINDING(Index_Material_SetShader);
		b.Material_GetProperty = IDX_GUARDED_BINDING(Index_Material_GetProperty);
		b.Material_SetProperty = IDX_GUARDED_BINDING(Index_Material_SetProperty);
	}

}
