#include "pch.hpp"
#include "Graphics/CustomSpriteDrawer.hpp"

#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/SpriteResources.hpp"

namespace Funke {

	bool CustomSpriteDrawer::Initialize(const char* label) {
		m_Label = label ? label : "custom-sprite";
		return m_Bindings.Initialize(m_Label.c_str());
	}

	void CustomSpriteDrawer::Shutdown() {
		m_Pipelines.clear();
		m_PipelineLayouts.clear();
		m_Bindings.Shutdown();
	}

	wgpu::RenderPipeline CustomSpriteDrawer::GetPipeline(const CustomShaderProgram& program, wgpu::TextureFormat colorFormat, bool hasDepth) {
		const uint64_t key = (program.Id << 17) ^ (static_cast<uint64_t>(colorFormat) << 1) ^ (hasDepth ? 1u : 0u);
		if (auto it = m_Pipelines.find(key); it != m_Pipelines.end()) return it->second;
		// Recompiles leave old ids behind; rebuilding the few live ones is cheap.
		if (m_Pipelines.size() > 128) m_Pipelines.clear();

		const ShaderDescription& description = program.Description;
		wgpu::BindGroupLayout customLayout = m_Bindings.GetLayout(description);
		wgpu::PipelineLayout& layout = m_PipelineLayouts[description.TextureCount];
		if (!layout && customLayout) {
			const wgpu::BindGroupLayout layouts[2] = { WebGPUSpriteResources::GetBindGroupLayout(), customLayout };
			const std::string layoutLabel = m_Label + "-layout";
			wgpu::PipelineLayoutDescriptor layoutDesc{};
			layoutDesc.bindGroupLayoutCount = 2;
			layoutDesc.bindGroupLayouts = layouts;
			layoutDesc.label = layoutLabel.c_str();
			layout = WebGPUBackend::GetDevice().CreatePipelineLayout(&layoutDesc);
		}

		// Same streams as the "sprite" pipeline: a 12-byte quad vertex and the SpriteInstance (7 vec4).
		wgpu::VertexAttribute vertexAttribute{};
		vertexAttribute.format = wgpu::VertexFormat::Float32x3;
		vertexAttribute.offset = 0;
		vertexAttribute.shaderLocation = 0;
		wgpu::VertexAttribute instanceAttributes[7] = {};
		for (uint32_t i = 0; i < 7; ++i) {
			instanceAttributes[i].format = wgpu::VertexFormat::Float32x4;
			instanceAttributes[i].offset = static_cast<uint64_t>(i) * 16u;
			instanceAttributes[i].shaderLocation = 1u + i;
		}
		wgpu::VertexBufferLayout buffers[2] = {};
		buffers[0].arrayStride = sizeof(float) * 3;
		buffers[0].stepMode = wgpu::VertexStepMode::Vertex;
		buffers[0].attributeCount = 1;
		buffers[0].attributes = &vertexAttribute;
		buffers[1].arrayStride = sizeof(WebGPUSpriteResources::SpriteInstance);
		buffers[1].stepMode = wgpu::VertexStepMode::Instance;
		buffers[1].attributeCount = 7;
		buffers[1].attributes = instanceAttributes;

		const wgpu::BlendState blend = CustomShaderGpu::MakeBlendState(description.Blend);
		wgpu::ColorTargetState colorTarget{};
		colorTarget.format = colorFormat;
		colorTarget.writeMask = wgpu::ColorWriteMask::All;
		if (description.Blend != ShaderBlend::Opaque) colorTarget.blend = &blend;

		wgpu::FragmentState fragment{};
		fragment.module = program.Module;
		fragment.entryPoint = "fs_main";
		fragment.targetCount = 1;
		fragment.targets = &colorTarget;

		wgpu::DepthStencilState depth{};
		depth.format = wgpu::TextureFormat::Depth24PlusStencil8;
		depth.depthWriteEnabled = false;
		depth.depthCompare = wgpu::CompareFunction::Always;

		const std::string pipelineLabel = m_Label + "-pipeline";
		wgpu::RenderPipelineDescriptor desc{};
		desc.label = pipelineLabel.c_str();
		desc.layout = layout;
		desc.vertex.module = program.Module;
		desc.vertex.entryPoint = "vs_main";
		desc.vertex.bufferCount = 2;
		desc.vertex.buffers = buffers;
		desc.fragment = &fragment;
		desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		desc.primitive.frontFace = wgpu::FrontFace::CCW;
		desc.primitive.cullMode = CustomShaderGpu::ToCullMode(description.Cull);
		desc.depthStencil = hasDepth ? &depth : nullptr;
		desc.multisample.count = 1;
		desc.multisample.mask = 0xFFFFFFFF;

		wgpu::RenderPipeline pipeline = layout ? CustomShaderGpu::CreatePipeline(desc, program) : nullptr;
		m_Pipelines.emplace(key, pipeline);
		return pipeline;
	}

	CustomSpriteDrawer::Draw CustomSpriteDrawer::Resolve(uint64_t materialGuid, ShaderTarget target,
		wgpu::TextureFormat colorFormat, bool hasDepth)
	{
		if (materialGuid == 0 || !IsInitialized()) return {};
		const Material* material = MaterialLibrary::Get(materialGuid);
		if (!material || material->Shader == 0) return {};
		const CustomShaderProgram* program = CustomShaderGpu::GetProgram(material->Shader, target);
		for (int attempt = 0; attempt < 2 && program; ++attempt) {
			Draw draw;
			draw.Pipeline = GetPipeline(*program, colorFormat, hasDepth);
			draw.Group = draw.Pipeline ? m_Bindings.GetGroup(materialGuid, material, *program) : nullptr;
			if (draw.Pipeline && draw.Group) return draw;
			program = program->IsFallback ? nullptr : CustomShaderGpu::GetFallback(target);
		}
		return {};
	}

}
