#pragma once
#include "Core/Export.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Index {

	// What a user .wgsl shader renders; selects the engine template its functions are inserted into.
	enum class ShaderTarget : uint8_t {
		Mesh3D = 0,
		Sprite2D = 1,
		UI = 2,
		PostProcess = 3,
		// Camera3D background: fn surface gets the view direction and returns the sky color.
		Skybox = 4
	};

	enum class ShaderBlend : uint8_t {
		Opaque = 0,
		Alpha = 1,
		Additive = 2
	};

	enum class ShaderCull : uint8_t {
		Back = 0,
		Front = 1,
		None = 2
	};

	enum class ShaderPropertyType : uint8_t {
		Float = 0,
		Vec2 = 1,
		Vec3 = 2,
		Vec4 = 3,
		// vec4 edited with a color picker; sRGB as authored, like every other color in the engine.
		Color = 4,
		Texture = 5
	};

	// What a texture property samples while the material assigns no texture.
	enum class ShaderDefaultTexture : uint8_t {
		White = 0,
		Black = 1,
		Gray = 2,
		// (0.5, 0.5, 1): a flat tangent-space normal.
		Normal = 3
	};

	struct INDEX_API ShaderProperty {
		std::string Name;
		ShaderPropertyType Type = ShaderPropertyType::Float;
		glm::vec4 Default{ 0.0f };
		ShaderDefaultTexture DefaultTexture = ShaderDefaultTexture::White;
		bool HasRange = false;
		float Min = 0.0f;
		float Max = 0.0f;
		// Byte offset in the property uniform block (non-texture properties).
		uint32_t Offset = 0;
		// Index among the texture properties (texture properties only).
		uint32_t TextureSlot = 0;
		int Line = 0;

		int GetComponentCount() const;
	};

	struct INDEX_API ShaderDiagnostic {
		int Line = 0;
		std::string Message;
		bool IsError = true;
	};

	struct INDEX_API ShaderDescription {
		std::string Name;
		ShaderTarget Target = ShaderTarget::Mesh3D;
		ShaderBlend Blend = ShaderBlend::Opaque;
		ShaderCull Cull = ShaderCull::Back;
		bool DepthWrite = true;
		std::vector<ShaderProperty> Properties;
		// Size of the property uniform block; at least 16 even without properties.
		uint32_t UniformSize = 16;
		uint32_t TextureCount = 0;
		bool HasVertexFunction = false;
		// `// @displacement`: the farthest fn vertex moves a vertex, in local units; widens editor picking.
		float Displacement = 0.0f;

		const ShaderProperty* FindProperty(std::string_view name) const;
	};

	struct INDEX_API ShaderParseResult {
		ShaderDescription Description;
		std::vector<ShaderDiagnostic> Diagnostics;

		bool HasErrors() const;
	};

	// Reads the `// @name`, `// @target`, `// @blend`, `// @cull`, `// @depthWrite`, `// @displacement` and `// @property` directives and
	// checks the user functions. Pure text work: WGSL errors are only found when the composed module is compiled.
	INDEX_API ShaderParseResult ParseCustomShader(std::string_view source);

	struct INDEX_API ComposedShader {
		std::string Wgsl;
		// Line 1 of the user source is line UserLineOffset + 1 of Wgsl.
		int UserLineOffset = 0;
		int UserLineCount = 0;

		// User source line for a line of Wgsl; 0 when it lies in the generated template.
		int ToUserLine(int composedLine) const;
	};

	// Template prelude + the user source verbatim + entry points. Group 0 belongs to the target's renderer,
	// group 1 holds globals (binding 0), the property block (binding 1) and texture/sampler pairs (2 + 2 * slot).
	INDEX_API ComposedShader ComposeCustomShader(const ShaderDescription& description, std::string_view userSource);

	// Magenta stand-in used while a shader has never compiled.
	INDEX_API std::string_view GetFallbackShaderSource(ShaderTarget target);

	struct ShaderTemplate {
		const char* MenuName;
		const char* FileName;   // without extension
		const char* Source;
	};

	// Ready-made Skybox shaders the editor offers under Create: gradient, 6-sided (texture or color per cube face) and panoramic.
	INDEX_API const std::vector<ShaderTemplate>& GetSkyboxShaderTemplates();

	INDEX_API const char* ShaderTargetToString(ShaderTarget target);
	INDEX_API const char* ShaderPropertyTypeToString(ShaderPropertyType type);

	inline constexpr uint32_t k_CustomShaderGroup = 1;
	inline constexpr uint32_t k_CustomShaderFirstTextureBinding = 2;
	inline constexpr uint32_t k_MaxShaderTextureProperties = 6;

	// Matches `struct Globals` of the template.
	struct ShaderGlobals {
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::mat4 ViewProjection{ 1.0f };
		glm::mat4 InverseViewProjection{ 1.0f };
		glm::vec4 CameraPosition{ 0.0f };
		float Time = 0.0f;
		float DeltaTime = 0.0f;
		float Frame = 0.0f;
		float Padding = 0.0f;
		glm::vec4 ScreenSize{ 1.0f, 1.0f, 1.0f, 1.0f };
	};
	static_assert(sizeof(ShaderGlobals) == 304);

}
