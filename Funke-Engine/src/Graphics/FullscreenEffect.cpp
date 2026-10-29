#include "pch.hpp"
#include "Graphics/FullscreenEffect.hpp"

#include "Core/Log.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/Framebuffer.hpp"
#include <cstring>
#include <string>

namespace Funke {

	struct FullscreenEffect::Impl {
		wgpu::BindGroupLayout Layout;
		wgpu::RenderPipeline Pipeline;
		wgpu::Buffer Uniforms;
		wgpu::Sampler Sampler;
		Framebuffer Output;
		uint32_t UniformBytes = 0;
	};

	FullscreenEffect::FullscreenEffect() = default;
	FullscreenEffect::~FullscreenEffect() = default;

	bool FullscreenEffect::IsInitialized() const {
		return m_Impl && m_Impl->Pipeline;
	}

	void FullscreenEffect::Shutdown() {
		m_Impl.reset();
	}

	bool FullscreenEffect::Initialize(std::string_view wgsl, uint32_t uniformBytes) {
		if (IsInitialized()) return true;
		if (wgsl.empty() || uniformBytes == 0 || uniformBytes % 16 != 0 || uniformBytes > 16384) return false;
		const auto device = WebGPUBackend::GetDevice();
		if (!device) return false;
		auto impl = std::make_unique<Impl>();
		const std::string source(wgsl);
		wgpu::ShaderSourceWGSL shaderSource{};
		shaderSource.code = source.c_str();
		wgpu::ShaderModuleDescriptor shaderDesc{};
		shaderDesc.nextInChain = &shaderSource;
		shaderDesc.label = "package-fullscreen-effect";
		const auto shader = device.CreateShaderModule(&shaderDesc);
		std::string error;
		if (!WebGPUBackend::ShaderModuleCompiled(shader, &error)) {
			FNK_CORE_ERROR_TAG("FullscreenEffect", "{}", error);
			return false;
		}
		wgpu::BindGroupLayoutEntry entries[3]{};
		entries[0].binding = 0;
		entries[0].visibility = wgpu::ShaderStage::Fragment;
		entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
		entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
		entries[1].binding = 1;
		entries[1].visibility = wgpu::ShaderStage::Fragment;
		entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
		entries[2].binding = 2;
		entries[2].visibility = wgpu::ShaderStage::Fragment;
		entries[2].buffer.type = wgpu::BufferBindingType::Uniform;
		entries[2].buffer.minBindingSize = uniformBytes;
		wgpu::BindGroupLayoutDescriptor layoutDesc{};
		layoutDesc.entryCount = 3;
		layoutDesc.entries = entries;
		impl->Layout = device.CreateBindGroupLayout(&layoutDesc);
		wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
		pipelineLayoutDesc.bindGroupLayoutCount = 1;
		pipelineLayoutDesc.bindGroupLayouts = &impl->Layout;
		const auto pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDesc);
		wgpu::ColorTargetState color{};
		color.format = wgpu::TextureFormat::RGBA16Float;
		color.writeMask = wgpu::ColorWriteMask::All;
		wgpu::FragmentState fragment{};
		fragment.module = shader;
		fragment.entryPoint = "fs_main";
		fragment.targetCount = 1;
		fragment.targets = &color;
		wgpu::RenderPipelineDescriptor pipelineDesc{};
		pipelineDesc.layout = pipelineLayout;
		pipelineDesc.vertex.module = shader;
		pipelineDesc.vertex.entryPoint = "vs_main";
		pipelineDesc.fragment = &fragment;
		pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		pipelineDesc.multisample.count = 1;
#if !defined(FNK_PLATFORM_WEB)
		device.PushErrorScope(wgpu::ErrorFilter::Validation);
#endif
		impl->Pipeline = device.CreateRenderPipeline(&pipelineDesc);
#if !defined(FNK_PLATFORM_WEB)
		const auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
			[&error](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView message) {
				if (status == wgpu::PopErrorScopeStatus::Success && type != wgpu::ErrorType::NoError && message.data)
					error.assign(message.data, message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length);
			});
		wgpu::FutureWaitInfo wait{ future };
		WebGPUBackend::GetInstance().WaitAny(1, &wait, UINT64_MAX);
#endif
		if (!impl->Pipeline || !error.empty()) {
			FNK_CORE_ERROR_TAG("FullscreenEffect", "Invalid pipeline: {}", error);
			return false;
		}
		wgpu::BufferDescriptor bufferDesc{};
		bufferDesc.size = uniformBytes;
		bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		impl->Uniforms = device.CreateBuffer(&bufferDesc);
		wgpu::SamplerDescriptor samplerDesc{};
		samplerDesc.magFilter = wgpu::FilterMode::Nearest;
		samplerDesc.minFilter = wgpu::FilterMode::Nearest;
		samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
		impl->Sampler = device.CreateSampler(&samplerDesc);
		impl->UniformBytes = uniformBytes;
		m_Impl = std::move(impl);
		return true;
	}

	const Framebuffer* FullscreenEffect::Render(const Framebuffer& source, const void* uniforms, size_t uniformBytes) {
		if (!IsInitialized() || !uniforms || uniformBytes != m_Impl->UniformBytes || !source.IsValid()
			|| source.GetSampleCount() != 1 || &source == &m_Impl->Output) return nullptr;
		if (!m_Impl->Output.Recreate(source.GetWidth(), source.GetHeight(), TextureFormat::RGBA16F)) return nullptr;
		const auto input = WebGPUBackend::LookupFramebufferByFboId(source.GetBackendId());
		const auto output = WebGPUBackend::LookupFramebufferByFboId(m_Impl->Output.GetBackendId());
		if (!input.Valid || !output.Valid) return nullptr;
		// Shared pass buffers can be reused by Editor View and Game View in the same frame.
		WebGPUBackend::FlushCommands();
		const auto device = WebGPUBackend::GetDevice();
		WebGPUBackend::GetQueue().WriteBuffer(m_Impl->Uniforms, 0, uniforms, uniformBytes);
		wgpu::BindGroupEntry entries[3]{};
		entries[0].binding = 0;
		entries[0].textureView = input.ColorView;
		entries[1].binding = 1;
		entries[1].sampler = m_Impl->Sampler;
		entries[2].binding = 2;
		entries[2].buffer = m_Impl->Uniforms;
		entries[2].size = uniformBytes;
		wgpu::BindGroupDescriptor bindDesc{};
		bindDesc.layout = m_Impl->Layout;
		bindDesc.entryCount = 3;
		bindDesc.entries = entries;
		const auto bindings = device.CreateBindGroup(&bindDesc);
		wgpu::RenderPassColorAttachment color{};
		color.view = output.ColorView;
		color.loadOp = wgpu::LoadOp::Clear;
		color.storeOp = wgpu::StoreOp::Store;
		color.depthSlice = wgpu::kDepthSliceUndefined;
		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "package-fullscreen-effect";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &color;
		auto pass = WebGPUBackend::GetFrameEncoder().BeginRenderPass(&passDesc);
		pass.SetPipeline(m_Impl->Pipeline);
		pass.SetBindGroup(0, bindings);
		pass.Draw(3);
		pass.End();
		WebGPUBackend::FlushCommands();
		return &m_Impl->Output;
	}
}
