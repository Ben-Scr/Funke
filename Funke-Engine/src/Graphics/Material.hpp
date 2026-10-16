#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"
#include "Graphics/CustomShader.hpp"
#include "Serialization/Json.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Funke {

	// Serialized as its integer value; append only.
	enum class MaterialSurface : uint8_t {
		Opaque = 0,
		// Alpha blended with what is behind (BaseColor alpha * texture alpha * renderer Color alpha); drawn after
		// opaque meshes, sorted back to front by object, without writing depth.
		Transparent = 1
	};
	inline constexpr int k_MaterialSurfaceCount = 2;

	// Value of a custom shader property. Texture properties keep the texture asset GUID (0 = the shader's default).
	struct FUNKE_API MaterialPropertyValue {
		ShaderPropertyType Type = ShaderPropertyType::Float;
		glm::vec4 Value{ 0.0f };
		uint64_t Texture = 0;

		static MaterialPropertyValue FromDefault(const ShaderProperty& property);
		bool operator==(const MaterialPropertyValue&) const = default;
	};

	// Surface description of a .material asset (JSON, GUID via its .meta). Colors are sRGB as authored.
	struct FUNKE_API Material {
		Color BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Texture asset GUID multiplied into BaseColor; 0 = none.
		uint64_t BaseColorTexture = 0;
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		Color EmissiveColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		float EmissiveIntensity = 1.0f;
		glm::vec2 Tiling{ 1.0f, 1.0f };
		glm::vec2 Offset{ 0.0f, 0.0f };
		// Unlit materials ignore lights, exposure and tone mapping and show BaseColor (+ emissive) as is.
		bool Lit = true;
		// Takes part in the camera's distance fog; off for things that are the backdrop themselves (a sky dome).
		bool Fog = true;
		MaterialSurface Surface = MaterialSurface::Opaque;
		// User .wgsl shader asset GUID; 0 = the standard PBR shader (and the fields above).
		uint64_t Shader = 0;
		// Values for the shader's @property declarations by name. Properties the shader no longer declares are kept, so
		// switching shaders back and forth loses nothing; missing ones use the shader's default.
		std::map<std::string, MaterialPropertyValue> Properties;

		bool operator==(const Material&) const = default;
	};

	// Property value the shader sees: the material's own when its type matches the declaration, else the default.
	FUNKE_API MaterialPropertyValue ResolveMaterialProperty(const Material& material, const ShaderProperty& property);
	// Fills the property uniform block (description.UniformSize bytes) in the layout of the shader's MaterialProperties.
	FUNKE_API void PackMaterialProperties(const Material& material, const ShaderDescription& description, std::vector<uint8_t>& outBytes);

	inline constexpr int k_MaterialFormatVersion = 1;
	inline constexpr const char* k_MaterialExtension = ".material";

	// Missing keys keep the Material defaults, so older or hand-written files load.
	FUNKE_API Json::Value MaterialToJson(const Material& material);
	FUNKE_API Material MaterialFromJson(const Json::Value& value);

	// Shared live .material instances by asset GUID (main thread). Edits through Save/Set are seen by every
	// MeshRenderer that references the GUID on the next frame.
	class FUNKE_API MaterialLibrary {
	public:
		MaterialLibrary() = delete;

		// nullptr for 0, unknown GUIDs and unreadable files (retried at most once per second).
		static const Material* Get(uint64_t guid);

		static bool LoadFromFile(const std::string& path, Material& outMaterial);
		static bool SaveToFile(const std::string& path, const Material& material);

		// Writes the asset's file and updates the live instance.
		static bool Save(uint64_t guid, const Material& material);
		// Changes the live instance only (runtime scripting); the file is untouched.
		static void Set(uint64_t guid, const Material& material);

		static void Invalidate(uint64_t guid);
		// Re-reads cached materials whose file changed outside the engine; returns how many were reloaded.
		static size_t ReloadChangedFromDisk();
		static void Clear();
	};

}
