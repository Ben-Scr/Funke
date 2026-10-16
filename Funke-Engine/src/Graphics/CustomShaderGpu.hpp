#pragma once
#include "Core/Export.hpp"
#include "Graphics/CustomShader.hpp"
#include "Graphics/TextureHandle.hpp"

#include <webgpu/webgpu_cpp.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Funke {
	struct Material;

	// A compiled user shader (or a target's magenta fallback). Pipelines are keyed by Id: it changes on every recompile.
	struct CustomShaderProgram {
		uint64_t Guid = 0;
		uint64_t Id = 0;
		ShaderDescription Description;
		wgpu::ShaderModule Module;
		bool IsFallback = false;
	};

	namespace CustomShaderGpu {
		// Last compiled version of the shader for that target; the target's fallback when it never compiled or was
		// written for another target; nullptr without a GPU device.
		FUNKE_API const CustomShaderProgram* GetProgram(uint64_t shaderGuid, ShaderTarget target);
		FUNKE_API const CustomShaderProgram* GetFallback(ShaderTarget target);

		// Creates the pipeline inside a validation error scope so a failure is reported once (against the shader's
		// file) instead of poisoning every frame; nullptr on failure.
		FUNKE_API wgpu::RenderPipeline CreatePipeline(const wgpu::RenderPipelineDescriptor& descriptor, const CustomShaderProgram& program);

		FUNKE_API wgpu::BlendState MakeBlendState(ShaderBlend blend);
		FUNKE_API wgpu::CullMode ToCullMode(ShaderCull cull);
	}

	// Group 1 of custom shader pipelines for one renderer: the globals buffer it writes per pass and a bind group with
	// the property block and textures per (material, program).
	class FUNKE_API CustomMaterialBindings {
	public:
		CustomMaterialBindings() = default;
		~CustomMaterialBindings();
		CustomMaterialBindings(const CustomMaterialBindings&) = delete;
		CustomMaterialBindings& operator=(const CustomMaterialBindings&) = delete;

		bool Initialize(const char* label);
		void Shutdown();
		bool IsInitialized() const { return static_cast<bool>(m_GlobalsBuffer); }

		// Fills time, deltaTime and frame; the caller sets the matrices and screen size.
		static ShaderGlobals MakeGlobals(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
			uint32_t targetWidth, uint32_t targetHeight);
		// Dawn applies every WriteBuffer of a submit before its passes: flush between passes that need different globals.
		void WriteGlobals(const ShaderGlobals& globals);

		wgpu::BindGroupLayout GetLayout(const ShaderDescription& description);
		// material may be null (defaults only). Writes the property block when its bytes changed.
		wgpu::BindGroup GetGroup(uint64_t materialGuid, const Material* material, const CustomShaderProgram& program);

	private:
		struct GroupEntry {
			wgpu::BindGroup Group;
			wgpu::Buffer Uniforms;
			std::vector<uint8_t> Bytes;
			std::vector<WGPUTextureView> Views;
			std::chrono::steady_clock::time_point LastUsed{};
		};
		struct TextureEntry {
			TextureHandle Handle;
			std::chrono::steady_clock::time_point LastAttempt{};
		};

		TextureHandle ResolveTexture(uint64_t guid);
		TextureHandle GetDefaultTexture(ShaderDefaultTexture type);

		wgpu::Buffer m_GlobalsBuffer;
		std::unordered_map<uint32_t, wgpu::BindGroupLayout> m_Layouts;
		std::unordered_map<uint64_t, GroupEntry> m_Groups;
		std::unordered_map<uint64_t, TextureEntry> m_Textures;
		uint64_t m_DefaultTextures[4] = {};
		std::vector<uint8_t> m_Scratch;
		uint32_t m_ReferenceToken = 0;
		uint32_t m_DestroyToken = 0;
		std::string m_Label;
	};

}
