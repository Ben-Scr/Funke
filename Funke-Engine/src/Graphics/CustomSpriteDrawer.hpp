#pragma once
#include "Core/Export.hpp"
#include "Graphics/CustomShaderGpu.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Funke {

	// Quads drawn through a material's Sprite2D or UI shader: the standard sprite vertex streams and group 0, the
	// material's group 1 on top. One per renderer, because the globals buffer is written once per pass.
	class FUNKE_API CustomSpriteDrawer {
	public:
		struct Draw {
			wgpu::RenderPipeline Pipeline;
			wgpu::BindGroup Group;
		};

		// sceneSamplers: see CustomMaterialBindings::Initialize.
		bool Initialize(const char* label, bool sceneSamplers = true);
		void Shutdown();
		bool IsInitialized() const { return m_Bindings.IsInitialized(); }

		void WriteGlobals(const ShaderGlobals& globals) { m_Bindings.WriteGlobals(globals); }
		// Empty Pipeline = draw with the standard sprite pipeline (no material or shader; a shader that never
		// compiles falls back to the magenta program first).
		Draw Resolve(uint64_t materialGuid, ShaderTarget target, wgpu::TextureFormat colorFormat, bool hasDepth);

	private:
		wgpu::RenderPipeline GetPipeline(const CustomShaderProgram& program, wgpu::TextureFormat colorFormat, bool hasDepth);

		CustomMaterialBindings m_Bindings;
		// By program id, color format and depth; failed creations stay cached as null.
		std::unordered_map<uint64_t, wgpu::RenderPipeline> m_Pipelines;
		std::unordered_map<uint32_t, wgpu::PipelineLayout> m_PipelineLayouts;
		std::string m_Label;
	};

}
