#include "pch.hpp"
#include "Graphics/PostProcessing/PostProcessor.hpp"

#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Core/Log.hpp"
#include "Core/Time.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/CustomShaderGpu.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Graphics/Material.hpp"

#include <array>
#include <cstring>
#include <unordered_map>


namespace Funke {

	namespace {
		// Mirrors Params in the "postcomposite" WGSL.
		struct CompositeUniforms {
			std::array<float, 4> Flags0;  // bloom, color grading, lens distortion, chromatic aberration
			std::array<float, 4> Flags1;  // vignette, grain, time, aspect
			std::array<float, 4> Bloom;
			std::array<float, 4> ColorFilter;
			std::array<float, 4> ExposureContrastSaturation;
			std::array<float, 4> TemperatureTint;
			std::array<float, 4> Lens;
			std::array<float, 4> Chromatic;
			std::array<float, 4> VignetteColor;
			std::array<float, 4> VignetteCenterSmooth;
			std::array<float, 4> VignetteRound;
			std::array<float, 4> Grain;
		};
		static_assert(sizeof(CompositeUniforms) == 12 * 16);
	}

	struct PostProcessor::Impl {
		// Blit pass (2-binding bgl: texture + sampler).
		wgpu::BindGroupLayout BindGroupLayout;
		wgpu::PipelineLayout  PipelineLayout;
		std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> BlitPipelines;
		wgpu::BindGroupLayout PointBindGroupLayout;
		wgpu::PipelineLayout  PointPipelineLayout;
		std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> PointBlitPipelines;

		wgpu::BindGroupLayout EffectBindGroupLayout;
		wgpu::PipelineLayout  EffectPipelineLayout;

		// Bloom blur H/V use SEPARATE uniform buffers: Dawn's queue.WriteBuffer collapses successive writes
		// to the same buffer within one submission — sharing would make both passes read the vertical direction.
		wgpu::Buffer          BloomThresholdUniformBuffer;      // 16 bytes
		wgpu::Buffer          BloomBlurUniformBufferH;          // 16 bytes
		wgpu::Buffer          BloomBlurUniformBufferV;          // 16 bytes
		std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> BloomThresholdPipelines;
		std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> BloomBlurPipelines;
		Framebuffer           BloomTempA;
		Framebuffer           BloomTempB;

		// The fused per-pixel pass; one buffer per composite step in a frame (a Gaussian Blur splits the
		// list into at most two), for the same WriteBuffer-collapsing reason as the blur buffers.
		std::array<wgpu::Buffer, 2> CompositeUniformBuffers;
		std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> CompositePipelines;
		wgpu::BindGroupLayout CompositeBgl;
		wgpu::PipelineLayout  CompositePipelineLayout;

		wgpu::Buffer          PixelatedUniformBuffer;           // 16 bytes
		std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> PixelatedPipelines;

		wgpu::Buffer          GaussianBlurUniformBufferH;       // 16 bytes
		wgpu::Buffer          GaussianBlurUniformBufferV;       // 16 bytes
		std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> GaussianBlurPipelines;
		Framebuffer           GaussianBlurTemp;                 // half-res RGBA16F

		Framebuffer           PingPongA;
		Framebuffer           PingPongB;

		// By (format, quality); one fragment entry point per quality level, blit layout.
		std::unordered_map<uint32_t, wgpu::RenderPipeline> FxaaPipelines;

		// User PostProcess shaders: group 0 = the blit layout (source + sampler), group 1 = the material's.
		CustomMaterialBindings CustomBindings;
		// By program id and target format; failed creations stay cached as null.
		std::unordered_map<uint64_t, wgpu::RenderPipeline> CustomPipelines;
		std::unordered_map<uint32_t, wgpu::PipelineLayout> CustomPipelineLayouts;

		// Shared.
		wgpu::Sampler         LinearSampler;
		wgpu::Sampler         PointSampler;
		bool                  Initialized = false;
	};

	PostProcessor::PostProcessor() : m_Impl(std::make_unique<Impl>()) {}

	PostProcessor::~PostProcessor() {
		Shutdown();
	}

	bool PostProcessor::IsInitialized() const {
		return m_Impl && m_Impl->Initialized;
	}

	bool PostProcessor::Initialize() {
		if (!m_Impl) m_Impl = std::make_unique<Impl>();
		if (m_Impl->Initialized) return true;

		if (!WebGPUBackend::IsInitialized()) {
			FNK_CORE_ERROR_TAG("PostProcessor",
				"Initialize called before WebGPU backend is up");
			return false;
		}

		wgpu::Device device = WebGPUBackend::GetDevice();
		if (!device) {
			FNK_CORE_ERROR_TAG("PostProcessor", "WebGPU device handle null");
			return false;
		}

		m_BlitShader = std::make_unique<Shader>("postblit", "postblit");
		if (!m_BlitShader->IsValid()) {
			FNK_CORE_ERROR_TAG("PostProcessor",
				"Failed to compile post-blit shader (built-in lookup miss)");
			m_BlitShader.reset();
			return false;
		}

		wgpu::BindGroupLayoutEntry bglEntries[2]{};
		bglEntries[0].binding = 0;
		bglEntries[0].visibility = wgpu::ShaderStage::Fragment;
		bglEntries[0].texture.sampleType = wgpu::TextureSampleType::Float;
		bglEntries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
		bglEntries[0].texture.multisampled = false;

		bglEntries[1].binding = 1;
		bglEntries[1].visibility = wgpu::ShaderStage::Fragment;
		bglEntries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

		wgpu::BindGroupLayoutDescriptor bglDesc{};
		bglDesc.label = "postprocess-blit-bgl";
		bglDesc.entryCount = 2;
		bglDesc.entries = bglEntries;
		m_Impl->BindGroupLayout = device.CreateBindGroupLayout(&bglDesc);
		if (!m_Impl->BindGroupLayout) {
			FNK_CORE_ERROR_TAG("PostProcessor", "CreateBindGroupLayout failed");
			return false;
		}

		wgpu::PipelineLayoutDescriptor plDesc{};
		plDesc.label = "postprocess-blit-pipeline-layout";
		plDesc.bindGroupLayoutCount = 1;
		plDesc.bindGroupLayouts = &m_Impl->BindGroupLayout;
		m_Impl->PipelineLayout = device.CreatePipelineLayout(&plDesc);
		if (!m_Impl->PipelineLayout) {
			FNK_CORE_ERROR_TAG("PostProcessor", "CreatePipelineLayout failed");
			return false;
		}
		bglEntries[1].sampler.type = wgpu::SamplerBindingType::NonFiltering;
		bglDesc.label = "postprocess-point-blit-bgl";
		m_Impl->PointBindGroupLayout = device.CreateBindGroupLayout(&bglDesc);
		if (!m_Impl->PointBindGroupLayout) {
			FNK_CORE_ERROR_TAG("PostProcessor", "Create point bind group layout failed");
			return false;
		}
		plDesc.label = "postprocess-point-blit-pipeline-layout";
		plDesc.bindGroupLayouts = &m_Impl->PointBindGroupLayout;
		m_Impl->PointPipelineLayout = device.CreatePipelineLayout(&plDesc);
		if (!m_Impl->PointPipelineLayout) {
			FNK_CORE_ERROR_TAG("PostProcessor", "Create point pipeline layout failed");
			return false;
		}

		wgpu::SamplerDescriptor samplerDesc{};
		samplerDesc.label = "postprocess-linear-sampler";
		samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
		samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
		samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
		samplerDesc.magFilter = wgpu::FilterMode::Linear;
		samplerDesc.minFilter = wgpu::FilterMode::Linear;
		samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
		samplerDesc.lodMinClamp = 0.0f;
		samplerDesc.lodMaxClamp = 1.0f;
		samplerDesc.maxAnisotropy = 1;
		m_Impl->LinearSampler = device.CreateSampler(&samplerDesc);
		if (!m_Impl->LinearSampler) {
			FNK_CORE_ERROR_TAG("PostProcessor", "CreateSampler failed");
			return false;
		}
		samplerDesc.label = "postprocess-point-sampler";
		samplerDesc.magFilter = wgpu::FilterMode::Nearest;
		samplerDesc.minFilter = wgpu::FilterMode::Nearest;
		m_Impl->PointSampler = device.CreateSampler(&samplerDesc);
		if (!m_Impl->PointSampler) {
			FNK_CORE_ERROR_TAG("PostProcessor", "Create point sampler failed");
			return false;
		}

		wgpu::BindGroupLayoutEntry effectBglEntries[3]{};
		effectBglEntries[0].binding = 0;
		effectBglEntries[0].visibility = wgpu::ShaderStage::Fragment;
		effectBglEntries[0].texture.sampleType = wgpu::TextureSampleType::Float;
		effectBglEntries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
		effectBglEntries[0].texture.multisampled = false;

		effectBglEntries[1].binding = 1;
		effectBglEntries[1].visibility = wgpu::ShaderStage::Fragment;
		effectBglEntries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

		effectBglEntries[2].binding = 2;
		effectBglEntries[2].visibility = wgpu::ShaderStage::Fragment;
		effectBglEntries[2].buffer.type = wgpu::BufferBindingType::Uniform;
		effectBglEntries[2].buffer.hasDynamicOffset = false;
		effectBglEntries[2].buffer.minBindingSize = 0;

		wgpu::BindGroupLayoutDescriptor effectBglDesc{};
		effectBglDesc.label = "postprocess-effect-bgl";
		effectBglDesc.entryCount = 3;
		effectBglDesc.entries = effectBglEntries;
		m_Impl->EffectBindGroupLayout = device.CreateBindGroupLayout(&effectBglDesc);
		if (!m_Impl->EffectBindGroupLayout) {
			FNK_CORE_ERROR_TAG("PostProcessor", "CreateBindGroupLayout (effect) failed");
			return false;
		}

		wgpu::PipelineLayoutDescriptor effectPlDesc{};
		effectPlDesc.label = "postprocess-effect-pipeline-layout";
		effectPlDesc.bindGroupLayoutCount = 1;
		effectPlDesc.bindGroupLayouts = &m_Impl->EffectBindGroupLayout;
		m_Impl->EffectPipelineLayout = device.CreatePipelineLayout(&effectPlDesc);
		if (!m_Impl->EffectPipelineLayout) {
			FNK_CORE_ERROR_TAG("PostProcessor", "CreatePipelineLayout (effect) failed");
			return false;
		}

		auto makeUniformBuffer = [&](const char* label, uint64_t size) -> wgpu::Buffer {
			wgpu::BufferDescriptor d{};
			d.label = label;
			d.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
			d.size = size;
			d.mappedAtCreation = false;
			return device.CreateBuffer(&d);
		};

		m_Impl->BloomThresholdUniformBuffer  = makeUniformBuffer("postprocess-bloom-threshold-uniforms", 16);
		m_Impl->BloomBlurUniformBufferH      = makeUniformBuffer("postprocess-bloom-blur-h-uniforms", 16);
		m_Impl->BloomBlurUniformBufferV      = makeUniformBuffer("postprocess-bloom-blur-v-uniforms", 16);
		m_Impl->CompositeUniformBuffers[0]   = makeUniformBuffer("postprocess-composite-uniforms-0", sizeof(CompositeUniforms));
		m_Impl->CompositeUniformBuffers[1]   = makeUniformBuffer("postprocess-composite-uniforms-1", sizeof(CompositeUniforms));
		m_Impl->PixelatedUniformBuffer       = makeUniformBuffer("postprocess-pixelated-uniforms", 16);
		m_Impl->GaussianBlurUniformBufferH   = makeUniformBuffer("postprocess-gaussian-blur-h-uniforms", 16);
		m_Impl->GaussianBlurUniformBufferV   = makeUniformBuffer("postprocess-gaussian-blur-v-uniforms", 16);

		if (!m_Impl->BloomThresholdUniformBuffer
			|| !m_Impl->BloomBlurUniformBufferH
			|| !m_Impl->BloomBlurUniformBufferV
			|| !m_Impl->CompositeUniformBuffers[0]
			|| !m_Impl->CompositeUniformBuffers[1]
			|| !m_Impl->PixelatedUniformBuffer
			|| !m_Impl->GaussianBlurUniformBufferH
			|| !m_Impl->GaussianBlurUniformBufferV)
		{
			FNK_CORE_ERROR_TAG("PostProcessor", "CreateBuffer (per-effect uniform) failed");
			return false;
		}

		// The composite pass uses 4 bindings (scene + sampler + bloom + uniform) unlike all other effects.
		wgpu::BindGroupLayoutEntry compositeBglEntries[4]{};
		compositeBglEntries[0].binding = 0;
		compositeBglEntries[0].visibility = wgpu::ShaderStage::Fragment;
		compositeBglEntries[0].texture.sampleType = wgpu::TextureSampleType::Float;
		compositeBglEntries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;

		compositeBglEntries[1].binding = 1;
		compositeBglEntries[1].visibility = wgpu::ShaderStage::Fragment;
		compositeBglEntries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

		compositeBglEntries[2].binding = 2;
		compositeBglEntries[2].visibility = wgpu::ShaderStage::Fragment;
		compositeBglEntries[2].texture.sampleType = wgpu::TextureSampleType::Float;
		compositeBglEntries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;

		compositeBglEntries[3].binding = 3;
		compositeBglEntries[3].visibility = wgpu::ShaderStage::Fragment;
		compositeBglEntries[3].buffer.type = wgpu::BufferBindingType::Uniform;
		compositeBglEntries[3].buffer.hasDynamicOffset = false;
		compositeBglEntries[3].buffer.minBindingSize = 0;

		wgpu::BindGroupLayoutDescriptor compositeBglDesc{};
		compositeBglDesc.label = "postprocess-composite-bgl";
		compositeBglDesc.entryCount = 4;
		compositeBglDesc.entries = compositeBglEntries;
		m_Impl->CompositeBgl = device.CreateBindGroupLayout(&compositeBglDesc);
		if (!m_Impl->CompositeBgl) {
			FNK_CORE_ERROR_TAG("PostProcessor", "CreateBindGroupLayout (composite) failed");
			return false;
		}

		wgpu::PipelineLayoutDescriptor compositePlDesc{};
		compositePlDesc.label = "postprocess-composite-pipeline-layout";
		compositePlDesc.bindGroupLayoutCount = 1;
		compositePlDesc.bindGroupLayouts = &m_Impl->CompositeBgl;
		m_Impl->CompositePipelineLayout = device.CreatePipelineLayout(&compositePlDesc);
		if (!m_Impl->CompositePipelineLayout) {
			FNK_CORE_ERROR_TAG("PostProcessor", "CreatePipelineLayout (composite) failed");
			return false;
		}

		auto loadShader = [](const char* name) -> std::unique_ptr<Shader> {
			auto s = std::make_unique<Shader>(name, name);
			if (!s->IsValid()) {
				FNK_CORE_ERROR_TAG("PostProcessor",
					"Failed to compile post-effect shader '{}' (built-in lookup miss)",
					name);
				return nullptr;
			}
			return s;
		};

		m_BloomThresholdShader  = loadShader("postbloomthreshold");
		m_BloomBlurShader       = loadShader("postbloomblur");
		m_CompositeShader       = loadShader("postcomposite");
		m_PixelatedShader       = loadShader("postpixelated");

		if (!m_BloomThresholdShader || !m_BloomBlurShader || !m_CompositeShader || !m_PixelatedShader)
		{
			m_BloomThresholdShader.reset();
			m_BloomBlurShader.reset();
			m_CompositeShader.reset();
			m_PixelatedShader.reset();
			return false;
		}
		m_FxaaShader = loadShader("postfxaa");

		// Screen-space effects: their textures (LUTs, noise) keep full detail at every texture quality.
		if (!m_Impl->CustomBindings.Initialize("postprocess-custom", false)) {
			FNK_CORE_WARN_TAG("PostProcessor", "Custom effect resources unavailable; custom post effects are skipped");
		}

		m_Impl->Initialized = true;
		return true;
	}

	void PostProcessor::Shutdown() {
		if (!m_Impl) return;

		m_Impl->CustomPipelines.clear();
		m_Impl->CustomPipelineLayouts.clear();
		m_Impl->CustomBindings.Shutdown();

		m_Impl->BloomThresholdPipelines.clear();
		m_Impl->BloomBlurPipelines.clear();
		m_Impl->CompositePipelines.clear();
		m_Impl->PixelatedPipelines.clear();
		m_Impl->GaussianBlurPipelines.clear();
		m_Impl->FxaaPipelines.clear();

		m_Impl->BloomThresholdUniformBuffer = nullptr;
		m_Impl->BloomBlurUniformBufferH = nullptr;
		m_Impl->BloomBlurUniformBufferV = nullptr;
		m_Impl->CompositeUniformBuffers = {};
		m_Impl->PixelatedUniformBuffer = nullptr;
		m_Impl->GaussianBlurUniformBufferH = nullptr;
		m_Impl->GaussianBlurUniformBufferV = nullptr;

		m_Impl->CompositePipelineLayout = nullptr;
		m_Impl->CompositeBgl = nullptr;
		m_Impl->BloomTempA.Destroy();
		m_Impl->BloomTempB.Destroy();
		m_Impl->GaussianBlurTemp.Destroy();

		m_Impl->PingPongA.Destroy();
		m_Impl->PingPongB.Destroy();
		m_Impl->EffectPipelineLayout = nullptr;
		m_Impl->EffectBindGroupLayout = nullptr;
		m_Impl->BlitPipelines.clear();
		m_Impl->PointBlitPipelines.clear();
		m_Impl->LinearSampler = nullptr;
		m_Impl->PointSampler = nullptr;
		m_Impl->PointPipelineLayout = nullptr;
		m_Impl->PointBindGroupLayout = nullptr;
		m_Impl->PipelineLayout = nullptr;
		m_Impl->BindGroupLayout = nullptr;
		m_Impl->Initialized = false;

		m_FxaaShader.reset();
		m_PixelatedShader.reset();
		m_CompositeShader.reset();
		m_BloomBlurShader.reset();
		m_BloomThresholdShader.reset();
		m_BlitShader.reset();
	}

	namespace {
		wgpu::RenderPipeline GetOrCreateBlitPipeline(
			PostProcessor::Impl& impl,
			const Shader& blitShader,
			wgpu::TextureFormat dstFormat,
			bool pointFiltered)
		{
			auto& pipelines = pointFiltered ? impl.PointBlitPipelines : impl.BlitPipelines;
			auto it = pipelines.find(dstFormat);
			if (it != pipelines.end()) return it->second;

			wgpu::Device device = WebGPUBackend::GetDevice();
			if (!device) return nullptr;

			WebGPUBackend::ShaderLookup look = WebGPUBackend::LookupShader(blitShader.GetHandle());
			if (!look.Valid) {
				FNK_CORE_ERROR_TAG("PostProcessor",
					"Blit shader module not resolvable (handle={})", blitShader.GetHandle());
				return nullptr;
			}

			wgpu::VertexState vertexState{};
			vertexState.module = look.Module;
			vertexState.entryPoint = "vs_main";
			vertexState.bufferCount = 0;
			vertexState.buffers = nullptr;

			wgpu::BlendState blend{};
			blend.color.srcFactor = wgpu::BlendFactor::One;
			blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.color.operation = wgpu::BlendOperation::Add;
			blend.alpha.srcFactor = wgpu::BlendFactor::One;
			blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.alpha.operation = wgpu::BlendOperation::Add;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = dstFormat;
			colorTarget.blend = &blend;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragmentState{};
			fragmentState.module = look.Module;
			fragmentState.entryPoint = "fs_main";
			fragmentState.targetCount = 1;
			fragmentState.targets = &colorTarget;

			wgpu::RenderPipelineDescriptor pipelineDesc{};
			pipelineDesc.label = "postprocess-blit-pipeline";
			pipelineDesc.layout = pointFiltered ? impl.PointPipelineLayout : impl.PipelineLayout;
			pipelineDesc.vertex = vertexState;
			pipelineDesc.fragment = &fragmentState;
			pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
			pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
			pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
			pipelineDesc.depthStencil = nullptr;  // no depth/stencil on blit
			pipelineDesc.multisample.count = 1;
			pipelineDesc.multisample.mask = 0xFFFFFFFFu;
			pipelineDesc.multisample.alphaToCoverageEnabled = false;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);
			if (!pipeline) {
				FNK_CORE_ERROR_TAG("PostProcessor",
					"CreateRenderPipeline failed for dstFormat={}", static_cast<int>(dstFormat));
				return nullptr;
			}

			pipelines.emplace(dstFormat, pipeline);
			return pipeline;
		}

		wgpu::RenderPipeline GetOrCreateFxaaPipeline(
			PostProcessor::Impl& impl,
			const Shader& fxaaShader,
			wgpu::TextureFormat dstFormat,
			AntiAliasingQualityLevel quality)
		{
			const uint32_t key = static_cast<uint32_t>(dstFormat) | (static_cast<uint32_t>(quality) << 24);
			if (auto it = impl.FxaaPipelines.find(key); it != impl.FxaaPipelines.end()) return it->second;

			wgpu::Device device = WebGPUBackend::GetDevice();
			WebGPUBackend::ShaderLookup look = WebGPUBackend::LookupShader(fxaaShader.GetHandle());
			if (!device || !look.Valid) return nullptr;

			// Premultiplied over, like Blit: an intermediate is cleared first, so there it is a plain copy.
			wgpu::BlendState blend{};
			blend.color.srcFactor = wgpu::BlendFactor::One;
			blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.color.operation = wgpu::BlendOperation::Add;
			blend.alpha = blend.color;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = dstFormat;
			colorTarget.blend = &blend;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragmentState{};
			fragmentState.module = look.Module;
			fragmentState.entryPoint = quality == AntiAliasingQualityLevel::Low ? "fs_low"
				: quality == AntiAliasingQualityLevel::Medium ? "fs_medium" : "fs_high";
			fragmentState.targetCount = 1;
			fragmentState.targets = &colorTarget;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "postprocess-fxaa-pipeline";
			desc.layout = impl.PipelineLayout;
			desc.vertex.module = look.Module;
			desc.vertex.entryPoint = "vs_main";
			desc.fragment = &fragmentState;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.multisample.count = 1;
			desc.multisample.mask = 0xFFFFFFFFu;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (!pipeline) {
				FNK_CORE_ERROR_TAG("PostProcessor", "FXAA CreateRenderPipeline failed for dstFormat={}", static_cast<int>(dstFormat));
			}
			impl.FxaaPipelines.emplace(key, pipeline);
			return pipeline;
		}

		wgpu::RenderPipeline BuildEffectPipeline(
			PostProcessor::Impl& impl,
			const Shader& shader,
			wgpu::TextureFormat dstFormat,
			const char* label)
		{
			wgpu::Device device = WebGPUBackend::GetDevice();
			if (!device) return nullptr;

			WebGPUBackend::ShaderLookup look = WebGPUBackend::LookupShader(shader.GetHandle());
			if (!look.Valid) {
				FNK_CORE_ERROR_TAG("PostProcessor",
					"{} shader module not resolvable (handle={})", label, shader.GetHandle());
				return nullptr;
			}

			wgpu::VertexState vertexState{};
			vertexState.module = look.Module;
			vertexState.entryPoint = "vs_main";
			vertexState.bufferCount = 0;
			vertexState.buffers = nullptr;

			wgpu::BlendState blend{};
			blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
			blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.color.operation = wgpu::BlendOperation::Add;
			blend.alpha.srcFactor = wgpu::BlendFactor::One;
			blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.alpha.operation = wgpu::BlendOperation::Add;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = dstFormat;
			colorTarget.blend = &blend;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragmentState{};
			fragmentState.module = look.Module;
			fragmentState.entryPoint = "fs_main";
			fragmentState.targetCount = 1;
			fragmentState.targets = &colorTarget;

			wgpu::RenderPipelineDescriptor pipelineDesc{};
			pipelineDesc.label = label;
			pipelineDesc.layout = impl.EffectPipelineLayout;
			pipelineDesc.vertex = vertexState;
			pipelineDesc.fragment = &fragmentState;
			pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
			pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
			pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
			pipelineDesc.depthStencil = nullptr;
			pipelineDesc.multisample.count = 1;
			pipelineDesc.multisample.mask = 0xFFFFFFFFu;
			pipelineDesc.multisample.alphaToCoverageEnabled = false;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);
			if (!pipeline) {
				FNK_CORE_ERROR_TAG("PostProcessor",
					"{} CreateRenderPipeline failed for dstFormat={}",
					label, static_cast<int>(dstFormat));
			}
			return pipeline;
		}

		wgpu::RenderPipeline GetOrCreateEffectPipeline(
			PostProcessor::Impl& impl,
			const Shader& shader,
			std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline>& cache,
			wgpu::TextureFormat dstFormat,
			const char* label)
		{
			auto it = cache.find(dstFormat);
			if (it != cache.end()) return it->second;
			wgpu::RenderPipeline pipeline = BuildEffectPipeline(impl, shader, dstFormat, label);
			if (pipeline) cache.emplace(dstFormat, pipeline);
			return pipeline;
		}

		void RunEffectPass(
			PostProcessor::Impl& impl,
			wgpu::RenderPipeline pipeline,
			wgpu::TextureView srcView,
			wgpu::TextureView dstView,
			wgpu::Buffer uniformBuffer,
			const void* uniformData,
			size_t uniformSize,
			const char* passLabel,
			bool clearTarget)
		{
			wgpu::Device device = WebGPUBackend::GetDevice();
			wgpu::Queue  queue  = WebGPUBackend::GetQueue();
			if (!device || !queue || !pipeline || !srcView || !dstView) return;

			queue.WriteBuffer(uniformBuffer, 0, uniformData, uniformSize);

			wgpu::BindGroupEntry entries[3]{};
			entries[0].binding = 0;
			entries[0].textureView = srcView;
			entries[1].binding = 1;
			entries[1].sampler = impl.LinearSampler;
			entries[2].binding = 2;
			entries[2].buffer  = uniformBuffer;
			entries[2].offset  = 0;
			entries[2].size    = uniformSize;

			wgpu::BindGroupDescriptor bgDesc{};
			bgDesc.label = passLabel;
			bgDesc.layout = impl.EffectBindGroupLayout;
			bgDesc.entryCount = 3;
			bgDesc.entries = entries;
			wgpu::BindGroup bg = device.CreateBindGroup(&bgDesc);
			if (!bg) {
				FNK_CORE_WARN_TAG("PostProcessor", "{} CreateBindGroup failed", passLabel);
				return;
			}

			wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
			if (!encoder) return;

			wgpu::RenderPassColorAttachment colorAtt{};
			colorAtt.view       = dstView;
			colorAtt.loadOp     = clearTarget ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
			colorAtt.storeOp    = wgpu::StoreOp::Store;
			colorAtt.clearValue = wgpu::Color{ 0.0, 0.0, 0.0, 0.0 };
			colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

			wgpu::RenderPassDescriptor passDesc{};
			passDesc.label = passLabel;
			passDesc.colorAttachmentCount = 1;
			passDesc.colorAttachments = &colorAtt;
			passDesc.depthStencilAttachment = nullptr;

			wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
			pass.SetPipeline(pipeline);
			pass.SetBindGroup(0, bg);
			pass.Draw(/*vertexCount=*/3, /*instanceCount=*/1, /*firstVertex=*/0, /*firstInstance=*/0);
			pass.End();
		}

		wgpu::RenderPipeline GetCustomEffectPipeline(PostProcessor::Impl& impl, const CustomShaderProgram& program, wgpu::TextureFormat dstFormat) {
			const uint64_t key = (program.Id << 16) ^ static_cast<uint64_t>(dstFormat);
			if (auto it = impl.CustomPipelines.find(key); it != impl.CustomPipelines.end()) return it->second;
			// Recompiles leave old ids behind; rebuilding the few live ones is cheap.
			if (impl.CustomPipelines.size() > 64) impl.CustomPipelines.clear();

			wgpu::BindGroupLayout customLayout = impl.CustomBindings.GetLayout(program.Description);
			wgpu::PipelineLayout& layout = impl.CustomPipelineLayouts[program.Description.TextureCount];
			if (!layout && customLayout) {
				const wgpu::BindGroupLayout layouts[2] = { impl.BindGroupLayout, customLayout };
				wgpu::PipelineLayoutDescriptor layoutDesc{};
				layoutDesc.bindGroupLayoutCount = 2;
				layoutDesc.bindGroupLayouts = layouts;
				layoutDesc.label = "postprocess-custom-layout";
				layout = WebGPUBackend::GetDevice().CreatePipelineLayout(&layoutDesc);
			}

			// Same blend as the built-in effects, so a translucent result composites like theirs.
			const wgpu::BlendState blend = CustomShaderGpu::MakeBlendState(ShaderBlend::Alpha);
			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = dstFormat;
			colorTarget.blend = &blend;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragment{};
			fragment.module = program.Module;
			fragment.entryPoint = "fs_main";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "postprocess-custom-pipeline";
			desc.layout = layout;
			desc.vertex.module = program.Module;
			desc.vertex.entryPoint = "vs_main";
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.multisample.count = 1;
			desc.multisample.mask = 0xFFFFFFFFu;

			wgpu::RenderPipeline pipeline = layout ? CustomShaderGpu::CreatePipeline(desc, program) : nullptr;
			impl.CustomPipelines.emplace(key, pipeline);
			return pipeline;
		}

		void RunCustomEffectPass(PostProcessor::Impl& impl, uint64_t materialGuid, const Material* material,
			const CustomShaderProgram* program, wgpu::TextureView srcView, wgpu::TextureView dstView, wgpu::TextureFormat dstFormat,
			bool clearTarget)
		{
			wgpu::Device device = WebGPUBackend::GetDevice();
			if (!device || !srcView || !dstView) return;
			wgpu::RenderPipeline pipeline;
			wgpu::BindGroup materialGroup;
			for (int attempt = 0; attempt < 2 && program; ++attempt) {
				pipeline = GetCustomEffectPipeline(impl, *program, dstFormat);
				materialGroup = pipeline ? impl.CustomBindings.GetGroup(materialGuid, material, *program) : nullptr;
				if (pipeline && materialGroup) break;
				program = program->IsFallback ? nullptr : CustomShaderGpu::GetFallback(ShaderTarget::PostProcess);
			}
			if (!pipeline || !materialGroup) return;

			wgpu::BindGroupEntry entries[2]{};
			entries[0].binding = 0;
			entries[0].textureView = srcView;
			entries[1].binding = 1;
			entries[1].sampler = impl.LinearSampler;
			wgpu::BindGroupDescriptor bgDesc{};
			bgDesc.label = "postprocess-custom-source";
			bgDesc.layout = impl.BindGroupLayout;
			bgDesc.entryCount = 2;
			bgDesc.entries = entries;
			wgpu::BindGroup sourceGroup = device.CreateBindGroup(&bgDesc);
			wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
			if (!sourceGroup || !encoder) return;

			wgpu::RenderPassColorAttachment colorAtt{};
			colorAtt.view       = dstView;
			colorAtt.loadOp     = clearTarget ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
			colorAtt.storeOp    = wgpu::StoreOp::Store;
			colorAtt.clearValue = wgpu::Color{ 0.0, 0.0, 0.0, 0.0 };
			colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

			wgpu::RenderPassDescriptor passDesc{};
			passDesc.label = "postprocess-custom";
			passDesc.colorAttachmentCount = 1;
			passDesc.colorAttachments = &colorAtt;

			wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
			pass.SetPipeline(pipeline);
			pass.SetBindGroup(0, sourceGroup);
			pass.SetBindGroup(1, materialGroup);
			pass.Draw(3, 1, 0, 0);
			pass.End();
		}

		wgpu::TextureView ViewOf(const Framebuffer& fb) {
			WebGPUBackend::FramebufferLookup look =
				WebGPUBackend::LookupFramebufferByFboId(fb.GetBackendId());
			return look.Valid ? look.ColorView : nullptr;
		}

		// Clear a target to transparent via an empty load-clear render pass. Effect passes blend, so a
		// destination reused across frames must start at zero or a transparent background smears.
		void ClearViewTransparent(wgpu::TextureView view) {
			if (!view) return;
			wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
			if (!encoder) return;

			wgpu::RenderPassColorAttachment colorAtt{};
			colorAtt.view       = view;
			colorAtt.loadOp     = wgpu::LoadOp::Clear;
			colorAtt.storeOp    = wgpu::StoreOp::Store;
			colorAtt.clearValue = wgpu::Color{ 0.0, 0.0, 0.0, 0.0 };
			colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

			wgpu::RenderPassDescriptor passDesc{};
			passDesc.label                = "postprocess-pingpong-clear";
			passDesc.colorAttachmentCount = 1;
			passDesc.colorAttachments     = &colorAtt;

			wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
			pass.End();
		}

		bool EnsurePingPongFbos(PostProcessor::Impl& impl, uint32_t w, uint32_t h) {
			const int iw = static_cast<int>(w);
			const int ih = static_cast<int>(h);
			if (!impl.PingPongA.Recreate(iw, ih, TextureFormat::RGBA16F)) return false;
			if (!impl.PingPongB.Recreate(iw, ih, TextureFormat::RGBA16F)) return false;
			return true;
		}

		wgpu::RenderPipeline BuildCompositePipeline(
			PostProcessor::Impl& impl,
			const Shader& shader,
			wgpu::TextureFormat dstFormat)
		{
			wgpu::Device device = WebGPUBackend::GetDevice();
			if (!device) return nullptr;

			WebGPUBackend::ShaderLookup look = WebGPUBackend::LookupShader(shader.GetHandle());
			if (!look.Valid) {
				FNK_CORE_ERROR_TAG("PostProcessor",
					"Composite shader module not resolvable");
				return nullptr;
			}

			wgpu::VertexState vertexState{};
			vertexState.module = look.Module;
			vertexState.entryPoint = "vs_main";
			vertexState.bufferCount = 0;

			wgpu::BlendState blend{};
			blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
			blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.color.operation = wgpu::BlendOperation::Add;
			blend.alpha.srcFactor = wgpu::BlendFactor::One;
			blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.alpha.operation = wgpu::BlendOperation::Add;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = dstFormat;
			colorTarget.blend = &blend;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragmentState{};
			fragmentState.module = look.Module;
			fragmentState.entryPoint = "fs_main";
			fragmentState.targetCount = 1;
			fragmentState.targets = &colorTarget;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "postprocess-composite-pipeline";
			desc.layout = impl.CompositePipelineLayout;
			desc.vertex = vertexState;
			desc.fragment = &fragmentState;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.frontFace = wgpu::FrontFace::CCW;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.multisample.count = 1;
			desc.multisample.mask = 0xFFFFFFFFu;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (!pipeline) {
				FNK_CORE_ERROR_TAG("PostProcessor",
					"CreateRenderPipeline (composite) failed for dstFormat={}",
					static_cast<int>(dstFormat));
			}
			return pipeline;
		}

		wgpu::RenderPipeline GetOrCreateCompositePipeline(
			PostProcessor::Impl& impl,
			const Shader& shader,
			wgpu::TextureFormat dstFormat)
		{
			auto it = impl.CompositePipelines.find(dstFormat);
			if (it != impl.CompositePipelines.end()) return it->second;
			wgpu::RenderPipeline p = BuildCompositePipeline(impl, shader, dstFormat);
			if (p) impl.CompositePipelines.emplace(dstFormat, p);
			return p;
		}

		void RunCompositePass(
			PostProcessor::Impl& impl,
			wgpu::RenderPipeline pipeline,
			wgpu::TextureView sceneView,
			wgpu::TextureView bloomView,
			wgpu::TextureView dstView,
			wgpu::Buffer uniformBuffer,
			const void* uniformData,
			size_t uniformSize,
			bool clearTarget)
		{
			wgpu::Device device = WebGPUBackend::GetDevice();
			wgpu::Queue  queue  = WebGPUBackend::GetQueue();
			if (!device || !queue || !pipeline
				|| !sceneView || !bloomView || !dstView) return;

			queue.WriteBuffer(uniformBuffer, 0, uniformData, uniformSize);

			wgpu::BindGroupEntry entries[4]{};
			entries[0].binding = 0;
			entries[0].textureView = sceneView;
			entries[1].binding = 1;
			entries[1].sampler = impl.LinearSampler;
			entries[2].binding = 2;
			entries[2].textureView = bloomView;
			entries[3].binding = 3;
			entries[3].buffer = uniformBuffer;
			entries[3].offset = 0;
			entries[3].size = uniformSize;

			wgpu::BindGroupDescriptor bgDesc{};
			bgDesc.label = "postprocess-composite-bg";
			bgDesc.layout = impl.CompositeBgl;
			bgDesc.entryCount = 4;
			bgDesc.entries = entries;
			wgpu::BindGroup bg = device.CreateBindGroup(&bgDesc);
			if (!bg) {
				FNK_CORE_WARN_TAG("PostProcessor",
					"Composite CreateBindGroup failed");
				return;
			}

			wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
			if (!encoder) return;

			wgpu::RenderPassColorAttachment colorAtt{};
			colorAtt.view = dstView;
			colorAtt.loadOp = clearTarget ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
			colorAtt.storeOp = wgpu::StoreOp::Store;
			colorAtt.clearValue = wgpu::Color{ 0.0, 0.0, 0.0, 0.0 };
			colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

			wgpu::RenderPassDescriptor passDesc{};
			passDesc.label = "postprocess-composite";
			passDesc.colorAttachmentCount = 1;
			passDesc.colorAttachments = &colorAtt;

			wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
			pass.SetPipeline(pipeline);
			pass.SetBindGroup(0, bg);
			pass.Draw(3, 1, 0, 0);
			pass.End();
		}

		// Threshold + blur into BloomTempA; returns its view, or null when bloom can't run this frame.
		wgpu::TextureView RunBloomPrepass(
			PostProcessor::Impl& impl,
			const Shader& thresholdShader,
			const Shader& blurShader,
			wgpu::TextureView srcView,
			uint32_t dstWidth, uint32_t dstHeight,
			const PostProcessing2DComponent::BloomSettings& b,
			int maximumTaps)
		{
			// Half resolution: the threshold pass's bilinear fetch doubles as a 2x2 downsample, and the
			// blur touches a quarter of the pixels. The composite samples by UV, so it's resolution-agnostic.
			const int fullW = std::max(1, static_cast<int>(dstWidth));
			const int fullH = std::max(1, static_cast<int>(dstHeight));
			const int bloomW = std::max(1, (fullW + 1) / 2);
			const int bloomH = std::max(1, (fullH + 1) / 2);
			if (!impl.BloomTempA.Recreate(bloomW, bloomH, TextureFormat::RGBA16F)) return nullptr;
			if (!impl.BloomTempB.Recreate(bloomW, bloomH, TextureFormat::RGBA16F)) return nullptr;

			WebGPUBackend::FramebufferLookup tempALook =
				WebGPUBackend::LookupFramebufferByFboId(impl.BloomTempA.GetBackendId());
			WebGPUBackend::FramebufferLookup tempBLook =
				WebGPUBackend::LookupFramebufferByFboId(impl.BloomTempB.GetBackendId());
			if (!tempALook.Valid || !tempBLook.Valid) return nullptr;

			const wgpu::TextureFormat tempFormat = wgpu::TextureFormat::RGBA16Float;

			const int tapsClamped = std::max(3, std::min(maximumTaps, b.Taps));
			// The requested kernel is in full-res texels. Keep its on-screen radius but cover it with at
			// most 25 taps per direction by widening the step; past that the extra taps cost far more than
			// they add to an already soft, half-res glow.
			constexpr int k_MaxBloomHalfKernel = 12;
			const float requestedHalfKernel = static_cast<float>(std::max(1, (tapsClamped - 1) / 2));
			const float halfKernel = std::min(requestedHalfKernel, static_cast<float>(k_MaxBloomHalfKernel));
			const float stepScale = requestedHalfKernel / halfKernel;

			// ── Pass 1: Threshold extract (srcView -> BloomTempA) ──
			{
				wgpu::RenderPipeline thresholdPipeline = GetOrCreateEffectPipeline(
					impl, thresholdShader, impl.BloomThresholdPipelines, tempFormat,
					"postprocess-bloom-threshold-pipeline");
				if (!thresholdPipeline) return nullptr;

				float u[4]{};
				u[0] = b.Threshold;
				RunEffectPass(impl, thresholdPipeline, srcView, tempALook.ColorView,
					impl.BloomThresholdUniformBuffer, u, sizeof(u),
					"postprocess-bloom-threshold", true);
			}

			// ── Pass 2: Horizontal blur (BloomTempA -> BloomTempB) ──
			wgpu::RenderPipeline blurPipeline = GetOrCreateEffectPipeline(
				impl, blurShader, impl.BloomBlurPipelines, tempFormat,
				"postprocess-bloom-blur-pipeline");
			if (!blurPipeline) return nullptr;

			const float scatterMul = 1.0f + 10.0f * b.Scatter;   // [1.0 .. 11.0]
			const float texelW = scatterMul * stepScale / static_cast<float>(fullW);
			const float texelH = scatterMul * stepScale / static_cast<float>(fullH);

			{
				float u[4]{};
				u[0] = 1.0f; u[1] = 0.0f; u[2] = texelW; u[3] = halfKernel;
				RunEffectPass(impl, blurPipeline,
					tempALook.ColorView, tempBLook.ColorView,
					impl.BloomBlurUniformBufferH, u, sizeof(u),
					"postprocess-bloom-blur-h", true);
			}

			// ── Pass 3: Vertical blur (BloomTempB -> BloomTempA) ──
			{
				float u[4]{};
				u[0] = 0.0f; u[1] = 1.0f; u[2] = texelH; u[3] = halfKernel;
				RunEffectPass(impl, blurPipeline,
					tempBLook.ColorView, tempALook.ColorView,
					impl.BloomBlurUniformBufferV, u, sizeof(u),
					"postprocess-bloom-blur-v", true);
			}

			return tempALook.ColorView;
		}

		void RunGaussianBlurPass(
			PostProcessor::Impl& impl,
			const Shader& blurShader,
			wgpu::TextureView srcView,
			wgpu::TextureView dstView,
			wgpu::TextureFormat dstFormat,
			uint32_t dstWidth, uint32_t dstHeight,
			const PostProcessing2DComponent::GaussianBlurSettings& g,
			int maximumTaps,
			bool clearTarget)
		{
			const int fullW = std::max(1, static_cast<int>(dstWidth));
			const int fullH = std::max(1, static_cast<int>(dstHeight));
			// The horizontal pass writes half-res: its bilinear fetches already average 2x2, and the
			// vertical pass reads it back by UV.
			if (!impl.GaussianBlurTemp.Recreate(std::max(1, (fullW + 1) / 2), std::max(1, (fullH + 1) / 2),
				TextureFormat::RGBA16F)) return;

			WebGPUBackend::FramebufferLookup tempLook =
				WebGPUBackend::LookupFramebufferByFboId(impl.GaussianBlurTemp.GetBackendId());
			if (!tempLook.Valid || !tempLook.ColorView) return;

			wgpu::RenderPipeline pipelineToTemp = GetOrCreateEffectPipeline(
				impl, blurShader, impl.GaussianBlurPipelines,
				wgpu::TextureFormat::RGBA16Float,
				"postprocess-gaussianblur-pipeline-temp");
			wgpu::RenderPipeline pipelineToDst = GetOrCreateEffectPipeline(
				impl, blurShader, impl.GaussianBlurPipelines, dstFormat,
				"postprocess-gaussianblur-pipeline-dst");
			if (!pipelineToTemp || !pipelineToDst) return;

			// Same radius, at most 33 taps per direction: the step widens instead, still far below sigma.
			constexpr int k_MaxBlurHalfKernel = 16;
			const int tapsClamped = std::max(3, std::min(maximumTaps, g.Taps));
			const float requestedHalfKernel = static_cast<float>(std::max(1, (tapsClamped - 1) / 2));
			const float halfKernel = std::min(requestedHalfKernel, static_cast<float>(k_MaxBlurHalfKernel));
			const float stepScale = requestedHalfKernel / halfKernel;

			const float radiusMul = (0.5f + 6.0f * g.Radius) * stepScale;
			const float texelW = radiusMul / static_cast<float>(fullW);
			const float texelH = radiusMul / static_cast<float>(fullH);

			// ── Pass 1: Horizontal blur (srcView -> GaussianBlurTemp) ──
			{
				float u[4]{};
				u[0] = 1.0f; u[1] = 0.0f; u[2] = texelW; u[3] = halfKernel;
				RunEffectPass(impl, pipelineToTemp, srcView, tempLook.ColorView,
					impl.GaussianBlurUniformBufferH, u, sizeof(u),
					"postprocess-gaussianblur-h", true);
			}

			// ── Pass 2: Vertical blur (GaussianBlurTemp -> dstView) ──
			{
				float u[4]{};
				u[0] = 0.0f; u[1] = 1.0f; u[2] = texelH; u[3] = halfKernel;
				RunEffectPass(impl, pipelineToDst, tempLook.ColorView, dstView,
					impl.GaussianBlurUniformBufferV, u, sizeof(u),
					"postprocess-gaussianblur-v", clearTarget);
			}
		}

		void RunPixelatedPass(PostProcessor::Impl& impl, const Shader& shader,
			wgpu::TextureView srcView, wgpu::TextureView dstView,
			wgpu::TextureFormat dstFormat,
			uint32_t dstWidth, uint32_t dstHeight,
			const PostProcessing2DComponent::PixelatedSettings& p,
			bool clearTarget)
		{
			wgpu::RenderPipeline pipeline = GetOrCreateEffectPipeline(
				impl, shader, impl.PixelatedPipelines, dstFormat,
				"postprocess-pixelated-pipeline");
			if (!pipeline) return;

			float u[4]{};
			u[0] = p.BlockSize;
			u[1] = static_cast<float>(dstWidth);
			u[2] = static_cast<float>(dstHeight);
			// 0 disables palette quantisation in the shader; >= 2 sets the
			// per-channel level count.
			u[3] = p.QuantizeColor
				? static_cast<float>(p.PaletteSteps < 2 ? 2 : p.PaletteSteps)
				: 0.0f;
			RunEffectPass(impl, pipeline, srcView, dstView,
				impl.PixelatedUniformBuffer, u, sizeof(u), "postprocess-pixelated", clearTarget);
		}

		// Which per-pixel effects one composite pass applies.
		struct CompositeEffects {
			bool Bloom = false;
			bool ColorGrading = false;
			bool LensDistortion = false;
			bool Chromatic = false;
			bool Vignette = false;
			bool Grain = false;
		};

		void RunCompositeStep(PostProcessor::Impl& impl,
			const Shader& thresholdShader, const Shader& blurShader, const Shader& compositeShader,
			wgpu::TextureView srcView, wgpu::TextureView dstView, wgpu::TextureFormat dstFormat,
			uint32_t dstWidth, uint32_t dstHeight,
			const PostProcessing2DComponent& settings, const CompositeEffects& effects,
			int maximumTaps, float timeSeconds, wgpu::Buffer uniformBuffer, bool clearTarget)
		{
			wgpu::RenderPipeline pipeline = GetOrCreateCompositePipeline(impl, compositeShader, dstFormat);
			if (!pipeline) return;

			wgpu::TextureView bloomView = effects.Bloom
				? RunBloomPrepass(impl, thresholdShader, blurShader, srcView, dstWidth, dstHeight, settings.Bloom, maximumTaps)
				: nullptr;

			const auto& bloom = settings.Bloom;
			const auto& cg = settings.ColorGrading;
			const auto& lens = settings.LensDistortion;
			const auto& vignette = settings.Vignette;
			const auto& grain = settings.Grain;
			auto flag = [](bool on) { return on ? 1.0f : 0.0f; };

			CompositeUniforms u{};
			u.Flags0 = { flag(bloomView != nullptr), flag(effects.ColorGrading), flag(effects.LensDistortion), flag(effects.Chromatic) };
			u.Flags1 = { flag(effects.Vignette), flag(effects.Grain), timeSeconds,
				dstHeight > 0 ? static_cast<float>(dstWidth) / static_cast<float>(dstHeight) : 1.0f };
			u.Bloom = { bloom.Tint.r * bloom.Tint.a, bloom.Tint.g * bloom.Tint.a, bloom.Tint.b * bloom.Tint.a, bloom.Intensity };
			u.ColorFilter = { cg.ColorFilter.r, cg.ColorFilter.g, cg.ColorFilter.b, cg.ColorFilter.a };
			u.ExposureContrastSaturation = { cg.Exposure, cg.Contrast, cg.Saturation, 0.0f };
			u.TemperatureTint = { cg.Temperature, cg.Tint, 0.0f, 0.0f };
			u.Lens = { lens.Intensity, lens.Scale, lens.Center.x, lens.Center.y };
			u.Chromatic = { settings.ChromaticAberration.Intensity, 0.0f, 0.0f, 0.0f };
			u.VignetteColor = { vignette.Color.r, vignette.Color.g, vignette.Color.b, vignette.Color.a };
			u.VignetteCenterSmooth = { vignette.Center.x, vignette.Center.y, vignette.Intensity, vignette.Smoothness };
			u.VignetteRound = { vignette.Roundness, 0.0f, 0.0f, 0.0f };
			u.Grain = { grain.Intensity, grain.Size, flag(grain.Colored), 0.0f };

			RunCompositePass(impl, pipeline, srcView, bloomView ? bloomView : srcView, dstView,
				uniformBuffer, &u, sizeof(u), clearTarget);
		}
	}

	void PostProcessor::Blit(const Framebuffer& src,
		wgpu::TextureView dstView,
		wgpu::TextureFormat dstFormat,
		uint32_t dstWidth,
		uint32_t dstHeight,
		UpscaleFilterMode filter)
	{
		if (!IsInitialized()) {
			FNK_CORE_WARN_TAG("PostProcessor", "Blit called before Initialize()");
			return;
		}
		if (!src.IsValid()) {
			FNK_CORE_WARN_TAG("PostProcessor", "Blit src framebuffer is invalid");
			return;
		}
		if (!dstView) {
			FNK_CORE_WARN_TAG("PostProcessor", "Blit dst view is null");
			return;
		}
		if (dstWidth == 0 || dstHeight == 0) return;

		wgpu::Device device = WebGPUBackend::GetDevice();
		if (!device) return;

		WebGPUBackend::FramebufferLookup srcLook =
			WebGPUBackend::LookupFramebufferByFboId(src.GetBackendId());
		if (!srcLook.Valid || !srcLook.ColorView) {
			FNK_CORE_WARN_TAG("PostProcessor",
				"Blit src color view lookup failed (fboId={})", src.GetBackendId());
			return;
		}

		const bool pointFiltered = filter == UpscaleFilterMode::Point;
		wgpu::RenderPipeline pipeline = GetOrCreateBlitPipeline(
			*m_Impl, *m_BlitShader, dstFormat, pointFiltered);
		if (!pipeline) return;

		wgpu::BindGroupEntry bgEntries[2]{};
		bgEntries[0].binding = 0;
		bgEntries[0].textureView = srcLook.ColorView;
		bgEntries[1].binding = 1;
		bgEntries[1].sampler = filter == UpscaleFilterMode::Point
			? m_Impl->PointSampler
			: m_Impl->LinearSampler;

		wgpu::BindGroupDescriptor bgDesc{};
		bgDesc.label = "postprocess-blit-bg";
		bgDesc.layout = pointFiltered ? m_Impl->PointBindGroupLayout : m_Impl->BindGroupLayout;
		bgDesc.entryCount = 2;
		bgDesc.entries = bgEntries;
		wgpu::BindGroup bindGroup = device.CreateBindGroup(&bgDesc);
		if (!bindGroup) {
			FNK_CORE_WARN_TAG("PostProcessor", "CreateBindGroup failed");
			return;
		}

		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!encoder) return;

		wgpu::RenderPassColorAttachment colorAtt{};
		colorAtt.view       = dstView;
		colorAtt.loadOp     = wgpu::LoadOp::Load;
		colorAtt.storeOp    = wgpu::StoreOp::Store;
		colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "postprocess-blit";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &colorAtt;
		passDesc.depthStencilAttachment = nullptr;

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		// MUST apply viewport before draw: aspect-lock caller sets a sub-rect so the blit
		// respects the letterbox region painted by the swap-chain clear.
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, bindGroup);
		pass.Draw(/*vertexCount=*/3, /*instanceCount=*/1, /*firstVertex=*/0, /*firstInstance=*/0);
		pass.End();
	}

	bool PostProcessor::RunAntiAliasing(const Framebuffer& src,
		wgpu::TextureView dstView,
		wgpu::TextureFormat dstFormat,
		AntiAliasingQualityLevel quality,
		bool finalTarget)
	{
		if (!IsInitialized() || !m_FxaaShader || !src.IsValid() || !dstView) return false;
		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		const wgpu::TextureView srcView = ViewOf(src);
		if (!device || !encoder || !srcView) return false;

		wgpu::RenderPipeline pipeline = GetOrCreateFxaaPipeline(*m_Impl, *m_FxaaShader, dstFormat, quality);
		if (!pipeline) return false;

		// Bilinear: the edge walk reads between two texels to average across the edge.
		wgpu::BindGroupEntry entries[2]{};
		entries[0].binding = 0;
		entries[0].textureView = srcView;
		entries[1].binding = 1;
		entries[1].sampler = m_Impl->LinearSampler;
		wgpu::BindGroupDescriptor bgDesc{};
		bgDesc.label = "postprocess-fxaa-bg";
		bgDesc.layout = m_Impl->BindGroupLayout;
		bgDesc.entryCount = 2;
		bgDesc.entries = entries;
		wgpu::BindGroup bindGroup = device.CreateBindGroup(&bgDesc);
		if (!bindGroup) return false;

		wgpu::RenderPassColorAttachment colorAtt{};
		colorAtt.view       = dstView;
		colorAtt.loadOp     = finalTarget ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
		colorAtt.storeOp    = wgpu::StoreOp::Store;
		colorAtt.clearValue = wgpu::Color{ 0.0, 0.0, 0.0, 0.0 };
		colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "postprocess-fxaa";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &colorAtt;

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		if (finalTarget) WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, bindGroup);
		pass.Draw(3, 1, 0, 0);
		pass.End();
		return true;
	}

	void PostProcessor::Run(const Framebuffer& src,
		wgpu::TextureView dstView,
		wgpu::TextureFormat dstFormat,
		uint32_t dstWidth,
		uint32_t dstHeight,
		const PostProcessing2DComponent* settings,
		PostProcessingQualityLevel quality,
		bool clearDestination)
	{
		if (!IsInitialized()) {
			FNK_CORE_WARN_TAG("PostProcessor", "Run called before Initialize()");
			return;
		}
		if (!src.IsValid() || !dstView || dstWidth == 0 || dstHeight == 0) {
			return;
		}

		// No component on the camera → passthrough.
		if (!settings) {
			Blit(src, dstView, dstFormat, dstWidth, dstHeight);
			return;
		}
		if (clearDestination) ClearViewTransparent(dstView);

		// Effect order: Bloom (needs HDR signal) → ColorGrading → GaussianBlur → Lens → CA → Vignette → Grain → Pixelated → custom slots.
		// Neighbouring per-pixel effects (everything but the blur, Pixelated and custom effects) run as one
		// composite pass, so a typical Bloom + Color Grading + Vignette stack is a single full-screen pass.
		struct Step {
			enum class Kind : uint8_t { Composite, GaussianBlur, Pixelated, Custom };
			Kind Type = Kind::Composite;
			CompositeEffects Effects{};
			size_t CustomSlot = 0;
		};
		std::array<Step, 8> steps{};
		size_t stepCount = 0;
		auto composite = [&]() -> CompositeEffects& {
			if (stepCount == 0 || steps[stepCount - 1].Type != Step::Kind::Composite) {
				steps[stepCount++] = Step{ Step::Kind::Composite };
			}
			return steps[stepCount - 1].Effects;
		};
		if (settings->Bloom.Enabled)               composite().Bloom = true;
		if (settings->ColorGrading.Enabled)        composite().ColorGrading = true;
		if (settings->GaussianBlur.Enabled)        steps[stepCount++] = Step{ Step::Kind::GaussianBlur };
		if (settings->LensDistortion.Enabled)      composite().LensDistortion = true;
		if (settings->ChromaticAberration.Enabled) composite().Chromatic = true;
		if (settings->Vignette.Enabled)            composite().Vignette = true;
		if (settings->Grain.Enabled)               composite().Grain = true;
		if (settings->Pixelated.Enabled)           steps[stepCount++] = Step{ Step::Kind::Pixelated };

		struct CustomEffect {
			uint64_t MaterialGuid = 0;
			const Material* Material = nullptr;
			const CustomShaderProgram* Program = nullptr;
		};
		std::array<CustomEffect, 4> customEffects{};
		static_assert(std::tuple_size_v<decltype(settings->CustomEffects)> == 4);
		if (m_Impl->CustomBindings.IsInitialized()) {
			for (size_t slot = 0; slot < settings->CustomEffects.size(); ++slot) {
				const uint64_t guid = settings->CustomEffects[slot];
				const Material* material = guid != 0 ? MaterialLibrary::Get(guid) : nullptr;
				if (!material || material->Shader == 0) continue;
				const CustomShaderProgram* program = CustomShaderGpu::GetProgram(material->Shader, ShaderTarget::PostProcess);
				if (!program) continue;
				customEffects[slot] = { guid, material, program };
				Step step{ Step::Kind::Custom };
				step.CustomSlot = slot;
				steps[stepCount++] = step;
			}
			if (std::any_of(customEffects.begin(), customEffects.end(), [](const CustomEffect& e) { return e.Program != nullptr; })) {
				m_Impl->CustomBindings.WriteGlobals(CustomMaterialBindings::MakeGlobals(glm::mat4(1.0f), glm::mat4(1.0f), glm::vec3(0.0f), dstWidth, dstHeight));
			}
		}

		if (stepCount == 0) {
			Blit(src, dstView, dstFormat, dstWidth, dstHeight);
			return;
		}

		if (stepCount > 1 && !EnsurePingPongFbos(*m_Impl, dstWidth, dstHeight)) {
			FNK_CORE_WARN_TAG("PostProcessor",
				"EnsurePingPongFbos failed at {}x{} — falling back to Blit",
				dstWidth, dstHeight);
			Blit(src, dstView, dstFormat, dstWidth, dstHeight);
			return;
		}

		const float currentTime = Time::GetRealtimeSinceStartup();
		const int maximumTaps = quality == PostProcessingQualityLevel::Low
			? 11
			: quality == PostProcessingQualityLevel::Medium ? 33 : 500;

		const wgpu::TextureFormat ppFormat = wgpu::TextureFormat::RGBA16Float;

		wgpu::TextureView curSrcView = ViewOf(src);
		if (!curSrcView) {
			FNK_CORE_WARN_TAG("PostProcessor", "Source FBO has no color view");
			return;
		}

		int pp = 0;
		size_t compositeIndex = 0;
		for (size_t i = 0; i < stepCount; ++i) {
			const bool isLast = (i + 1 == stepCount);
			wgpu::TextureView passDstView = dstView;
			wgpu::TextureFormat passDstFormat = dstFormat;
			if (!isLast) {
				passDstView = ViewOf(pp == 0 ? m_Impl->PingPongA : m_Impl->PingPongB);
				passDstFormat = ppFormat;
				if (!passDstView) {
					FNK_CORE_WARN_TAG("PostProcessor",
						"Ping-pong FBO {} has no color view at pass {}", pp, i);
					return;
				}
			}
			// Intermediates start transparent each frame (cleared by the pass itself); the destination keeps
			// what the caller drew or cleared.
			const bool clearTarget = !isLast;

			const Step& step = steps[i];
			switch (step.Type) {
				case Step::Kind::Composite:
					RunCompositeStep(*m_Impl, *m_BloomThresholdShader, *m_BloomBlurShader, *m_CompositeShader,
						curSrcView, passDstView, passDstFormat, dstWidth, dstHeight, *settings, step.Effects,
						maximumTaps, currentTime, m_Impl->CompositeUniformBuffers[std::min<size_t>(compositeIndex++, 1)],
						clearTarget);
					break;
				case Step::Kind::GaussianBlur:
					RunGaussianBlurPass(*m_Impl, *m_BloomBlurShader,
						curSrcView, passDstView, passDstFormat, dstWidth, dstHeight, settings->GaussianBlur, maximumTaps,
						clearTarget);
					break;
				case Step::Kind::Pixelated:
					RunPixelatedPass(*m_Impl, *m_PixelatedShader,
						curSrcView, passDstView, passDstFormat, dstWidth, dstHeight, settings->Pixelated, clearTarget);
					break;
				case Step::Kind::Custom: {
					const CustomEffect& effect = customEffects[step.CustomSlot];
					RunCustomEffectPass(*m_Impl, effect.MaterialGuid, effect.Material, effect.Program,
						curSrcView, passDstView, passDstFormat, clearTarget);
					break;
				}
			}

			if (!isLast) {
				curSrcView = passDstView;
				pp = 1 - pp;
			}
		}
	}

} // namespace Funke
