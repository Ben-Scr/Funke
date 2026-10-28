#include "pch.hpp"
#include "Graphics/Texture2D.hpp"

#include "Core/Log.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/ImageData.hpp"
#include "Graphics/TextureSampling.hpp"

#include <webgpu/webgpu_cpp.h>

#define STB_IMAGE_IMPLEMENTATION
#if defined(FNK_DEBUG)
// Debug builds compile stb_image at -O0; some compilers do not fold the
// SSE2 shift immediates early enough for the intrinsic wrappers.
#define STBI_NO_SIMD
#endif
#include <stb_image.h>

#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

// Texture2D WebGPU impl. m_Tex stores the raw WGPUTextureView pointer as uint64_t — this lets imgui_impl_wgpu receive a real Dawn handle directly from GetHandle() rather than a pool index it would crash dereferencing.

namespace Funke {

	namespace {
		// ── Pool ────────────────────────────────────────────────────────────
		struct GpuTexture {
			wgpu::Texture     Texture;
			wgpu::TextureView View;
			wgpu::Sampler     Sampler;
			wgpu::Sampler     SceneSampler;
			uint32_t          Width  = 0;
			uint32_t          Height = 0;
			uint32_t          MipLevelCount = 1;
			Filter            CachedFilter = Filter::Point;
			Wrap              CachedWrapU  = Wrap::Clamp;
			Wrap              CachedWrapV  = Wrap::Clamp;
		};
		std::unordered_map<uint64_t, GpuTexture> g_Textures;

		uint64_t RegisterTexture(GpuTexture&& tex) {
			const uint64_t key = reinterpret_cast<uint64_t>(tex.View.Get());
			if (key == 0) return 0;  // shouldn't happen; defensive
			g_Textures.emplace(key, std::move(tex));
			return key;
		}

		void FreeTextureSlot(uint64_t id) {
			if (id == 0) return;
			g_Textures.erase(id);
		}

		GpuTexture* TryLookup(uint64_t id) {
			if (id == 0) return nullptr;
			auto it = g_Textures.find(id);
			return (it == g_Textures.end()) ? nullptr : &it->second;
		}

		// ── Mip generation ──────────────────────────────────────────────────
		// Alpha-weighted 2x2 box filter: transparent texels don't darken the
		// colour of their opaque neighbours in lower levels (straight alpha).
		constexpr const char* k_MipDownsampleWGSL = R"WGSL(
@group(0) @binding(0) var t_src: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4<f32> {
	var pos = array<vec2<f32>, 3>(
		vec2<f32>(-1.0, -1.0),
		vec2<f32>( 3.0, -1.0),
		vec2<f32>(-1.0,  3.0),
	);
	return vec4<f32>(pos[idx], 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) frag: vec4<f32>) -> @location(0) vec4<f32> {
	let maxCoord = vec2<i32>(textureDimensions(t_src, 0)) - vec2<i32>(1, 1);
	let base = vec2<i32>(floor(frag.xy)) * 2;
	var weighted = vec3<f32>(0.0);
	var plain = vec3<f32>(0.0);
	var alpha = 0.0;
	for (var y = 0; y < 2; y++) {
		for (var x = 0; x < 2; x++) {
			let c = textureLoad(t_src, min(base + vec2<i32>(x, y), maxCoord), 0);
			weighted += c.rgb * c.a;
			plain += c.rgb;
			alpha += c.a;
		}
	}
	if (alpha <= 0.0) {
		return vec4<f32>(plain * 0.25, 0.0);
	}
	return vec4<f32>(weighted / alpha, alpha * 0.25);
}
)WGSL";

		struct MipGenCache {
			wgpu::Device       Device;
			wgpu::ShaderModule Module;
			std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline> Pipelines;
		};
		MipGenCache g_MipGen;

		bool IsMipRenderableFormat(wgpu::TextureFormat format) noexcept {
			switch (format) {
				case wgpu::TextureFormat::R8Unorm:
				case wgpu::TextureFormat::RG8Unorm:
				case wgpu::TextureFormat::RGBA8Unorm:
				case wgpu::TextureFormat::RGBA8UnormSrgb:
				case wgpu::TextureFormat::BGRA8Unorm:
				case wgpu::TextureFormat::BGRA8UnormSrgb:
				case wgpu::TextureFormat::RGBA16Float:
					return true;
				default:
					return false;
			}
		}

		// Null when the format can't be a render target or pipeline creation failed (cached either way).
		wgpu::RenderPipeline GetMipPipeline(wgpu::TextureFormat format) {
			if (!IsMipRenderableFormat(format)) return nullptr;
			wgpu::Device device = WebGPUBackend::GetDevice();
			if (!device) return nullptr;

			if (g_MipGen.Device.Get() != device.Get()) {
				g_MipGen = MipGenCache{};
				g_MipGen.Device = device;
			}

			if (auto it = g_MipGen.Pipelines.find(format); it != g_MipGen.Pipelines.end()) {
				return it->second;
			}

			if (!g_MipGen.Module) {
				wgpu::ShaderSourceWGSL src{};
				src.code = k_MipDownsampleWGSL;
				wgpu::ShaderModuleDescriptor moduleDesc{};
				moduleDesc.nextInChain = &src;
				moduleDesc.label = "texture2d-mipgen";
				g_MipGen.Module = device.CreateShaderModule(&moduleDesc);
				if (!g_MipGen.Module) {
					FNK_CORE_ERROR_TAG("Texture2D", "Mip downsample shader module creation failed");
					g_MipGen.Pipelines.emplace(format, nullptr);
					return nullptr;
				}
			}

			wgpu::VertexState vertexState{};
			vertexState.module     = g_MipGen.Module;
			vertexState.entryPoint = "vs_main";

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format    = format;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragmentState{};
			fragmentState.module      = g_MipGen.Module;
			fragmentState.entryPoint  = "fs_main";
			fragmentState.targetCount = 1;
			fragmentState.targets     = &colorTarget;

			wgpu::RenderPipelineDescriptor pipelineDesc{};
			pipelineDesc.label                  = "texture2d-mipgen-pipeline";
			pipelineDesc.layout                 = nullptr;  // auto layout; bind group built from GetBindGroupLayout(0)
			pipelineDesc.vertex                 = vertexState;
			pipelineDesc.fragment               = &fragmentState;
			pipelineDesc.primitive.topology     = wgpu::PrimitiveTopology::TriangleList;
			pipelineDesc.primitive.cullMode     = wgpu::CullMode::None;
			pipelineDesc.multisample.count      = 1;
			pipelineDesc.multisample.mask       = 0xFFFFFFFFu;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);
			if (!pipeline) {
				FNK_CORE_ERROR_TAG("Texture2D",
					"Mip downsample pipeline creation failed for format={}", static_cast<int>(format));
			}
			g_MipGen.Pipelines.emplace(format, pipeline);
			return pipeline;
		}

		// Levels 1..mipLevelCount-1 from level 0. Submitted on its own command buffer;
		// queue ordering places it after any preceding WriteTexture to level 0.
		bool GenerateMipChain(const wgpu::Texture& texture, wgpu::TextureFormat format, uint32_t mipLevelCount) {
			if (!texture || mipLevelCount <= 1) return true;
			wgpu::RenderPipeline pipeline = GetMipPipeline(format);
			if (!pipeline) return false;

			wgpu::Device device = WebGPUBackend::GetDevice();
			wgpu::Queue  queue  = WebGPUBackend::GetQueue();
			if (!device || !queue) return false;

			wgpu::BindGroupLayout layout = pipeline.GetBindGroupLayout(0);

			wgpu::CommandEncoderDescriptor encoderDesc{};
			encoderDesc.label = "texture2d-mipgen";
			wgpu::CommandEncoder encoder = device.CreateCommandEncoder(&encoderDesc);

			wgpu::TextureViewDescriptor levelDesc{};
			levelDesc.format          = format;
			levelDesc.dimension       = wgpu::TextureViewDimension::e2D;
			levelDesc.mipLevelCount   = 1;
			levelDesc.baseArrayLayer  = 0;
			levelDesc.arrayLayerCount = 1;
			levelDesc.aspect          = wgpu::TextureAspect::All;

			for (uint32_t level = 1; level < mipLevelCount; ++level) {
				levelDesc.baseMipLevel = level - 1;
				wgpu::TextureView srcView = texture.CreateView(&levelDesc);
				levelDesc.baseMipLevel = level;
				wgpu::TextureView dstView = texture.CreateView(&levelDesc);

				wgpu::BindGroupEntry entry{};
				entry.binding     = 0;
				entry.textureView = srcView;

				wgpu::BindGroupDescriptor bgDesc{};
				bgDesc.label      = "texture2d-mipgen-bg";
				bgDesc.layout     = layout;
				bgDesc.entryCount = 1;
				bgDesc.entries    = &entry;
				wgpu::BindGroup bindGroup = device.CreateBindGroup(&bgDesc);

				wgpu::RenderPassColorAttachment colorAtt{};
				colorAtt.view       = dstView;
				colorAtt.loadOp     = wgpu::LoadOp::Clear;
				colorAtt.storeOp    = wgpu::StoreOp::Store;
				colorAtt.clearValue = wgpu::Color{ 0.0, 0.0, 0.0, 0.0 };
				colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

				wgpu::RenderPassDescriptor passDesc{};
				passDesc.label                = "texture2d-mipgen-pass";
				passDesc.colorAttachmentCount = 1;
				passDesc.colorAttachments     = &colorAtt;

				wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
				pass.SetPipeline(pipeline);
				pass.SetBindGroup(0, bindGroup);
				pass.Draw(3, 1, 0, 0);
				pass.End();
			}

			wgpu::CommandBuffer commands = encoder.Finish();
			queue.Submit(1, &commands);
			return true;
		}

		// ── Enum mapping ────────────────────────────────────────────────────
		using WB = ::Funke::WebGPUBackend::FilterMode;
		using WW = ::Funke::WebGPUBackend::WrapMode;

		WB ToBackendFilter(Filter f) noexcept {
			switch (f) {
				case Filter::Point:       return WB::Point;
				case Filter::Bilinear:    return WB::Bilinear;
				case Filter::Trilinear:   return WB::Trilinear;
				case Filter::Anisotropic: return WB::Anisotropic;
				case Filter::Default:     return WB::Point;  // sentinel; normally resolved before sampler creation — safety net (no-meta default is Point)
			}
			return WB::Point;
		}
		WW ToBackendWrap(Wrap w) noexcept {
			switch (w) {
				case Wrap::Repeat: return WW::Repeat;
				case Wrap::Clamp:  return WW::Clamp;
				case Wrap::Mirror: return WW::Mirror;
				case Wrap::Border: return WW::Border;
			}
			return WW::Clamp;
		}

		wgpu::Sampler MakeSamplerFor(Filter f, Wrap u, Wrap v) {
			return WebGPUBackend::CreateSampler(
				ToBackendFilter(f), ToBackendWrap(u), ToBackendWrap(v));
		}

		TextureQualityLevel g_SceneTextureQuality = GraphicsSettings{}.TextureQuality;
		AnisotropicFilteringLevel g_SceneAnisotropy = GraphicsSettings{}.AnisotropicFiltering;

		// Shares the texture's own sampler when the settings leave it alone.
		void AssignSceneSampler(GpuTexture& slot) {
			const TextureSamplingQuality scene = ResolveTextureSampling(slot.CachedFilter, g_SceneTextureQuality, g_SceneAnisotropy);
			const uint16_t ownAnisotropy = slot.CachedFilter == Filter::Anisotropic ? 16 : 1;
			slot.SceneSampler = scene.LodMinClamp == 0.0f && scene.MaxAnisotropy == ownAnisotropy
				? slot.Sampler
				: WebGPUBackend::CreateSampler(ToBackendFilter(slot.CachedFilter), ToBackendWrap(slot.CachedWrapU),
					ToBackendWrap(slot.CachedWrapV), scene.LodMinClamp, scene.MaxAnisotropy);
		}

		void AssignSamplers(GpuTexture& slot) {
			slot.Sampler = MakeSamplerFor(slot.CachedFilter, slot.CachedWrapU, slot.CachedWrapV);
			AssignSceneSampler(slot);
		}
	}


	namespace WebGPUBackend {

		TextureLookup LookupTexture2D(uint64_t backendId) {
			GpuTexture* slot = TryLookup(backendId);
			if (!slot) return TextureLookup{};
			TextureLookup out;
			out.View   = slot->View;
			out.Sampler= slot->Sampler;
			out.SceneSampler = slot->SceneSampler;
			out.Width  = slot->Width;
			out.Height = slot->Height;
			out.Valid  = true;
			return out;
		}

		wgpu::Sampler CreateSampler(FilterMode filter, WrapMode wrapU, WrapMode wrapV) {
			return CreateSampler(filter, wrapU, wrapV, 0.0f, filter == FilterMode::Anisotropic ? 16 : 1);
		}

		wgpu::Sampler CreateSampler(FilterMode filter, WrapMode wrapU, WrapMode wrapV, float lodMinClamp, uint16_t maxAnisotropy) {
			wgpu::Device device = GetDevice();
			if (!device) return nullptr;

			auto toAddress = [](WrapMode w) {
				switch (w) {
					case WrapMode::Repeat: return wgpu::AddressMode::Repeat;
					case WrapMode::Clamp:  return wgpu::AddressMode::ClampToEdge;
					case WrapMode::Mirror: return wgpu::AddressMode::MirrorRepeat;
					case WrapMode::Border: return wgpu::AddressMode::ClampToEdge;  // WebGPU has no border-color mode
				}
				return wgpu::AddressMode::ClampToEdge;
			};

			wgpu::SamplerDescriptor desc{};
			desc.addressModeU = toAddress(wrapU);
			desc.addressModeV = toAddress(wrapV);
			desc.addressModeW = wgpu::AddressMode::ClampToEdge;

			switch (filter) {
				case FilterMode::Point:
					desc.magFilter    = wgpu::FilterMode::Nearest;
					desc.minFilter    = wgpu::FilterMode::Nearest;
					desc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
					desc.lodMaxClamp  = 0.0f;  // base level only: minified pixel art stays crisp
					desc.maxAnisotropy = 1;
					break;
				case FilterMode::Bilinear:
					desc.magFilter    = wgpu::FilterMode::Linear;
					desc.minFilter    = wgpu::FilterMode::Linear;
					desc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
					desc.lodMinClamp  = lodMinClamp;
					desc.maxAnisotropy = 1;
					break;
				case FilterMode::Trilinear:
				case FilterMode::Anisotropic:
					// maxAnisotropy > 1 requires linear filtering on all axes per the spec.
					desc.magFilter    = wgpu::FilterMode::Linear;
					desc.minFilter    = wgpu::FilterMode::Linear;
					desc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
					desc.lodMinClamp  = lodMinClamp;
					desc.maxAnisotropy = std::max<uint16_t>(maxAnisotropy, 1);
					break;
			}

			return device.CreateSampler(&desc);
		}

	}  // namespace WebGPUBackend

	// ── Texture2D ───────────────────────────────────────────────────────────

	void Texture2D::ReleaseMipGenResources() {
		g_MipGen = MipGenCache{};
	}

	Texture2D::Texture2D(const char* path,
		Filter filter, Wrap wrapU, Wrap wrapV,
		bool generateMipmaps, bool srgb, bool flipVertical)
		: m_Filter(filter), m_WrapU(wrapU), m_WrapV(wrapV), m_HasMips(generateMipmaps)
	{
		if (path != nullptr) {
			Load(path, generateMipmaps, srgb, flipVertical);
		}
	}

	Texture2D::~Texture2D() {
		Destroy();
	}

	Texture2D::Texture2D(Texture2D&& other) noexcept
		: m_Tex(other.m_Tex)
		, m_Width(other.m_Width)
		, m_Height(other.m_Height)
		, m_Channels(other.m_Channels)
		, m_Filter(other.m_Filter)
		, m_WrapU(other.m_WrapU)
		, m_WrapV(other.m_WrapV)
		, m_HasMips(other.m_HasMips)
		, m_FlippedY(other.m_FlippedY)
		, m_SourcePath(std::move(other.m_SourcePath))
	{
		other.m_Tex = 0;
		other.m_Width = 0;
		other.m_Height = 0;
		other.m_Channels = 0;
		other.m_SourcePath.clear();
	}

	Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
		if (this == &other) return *this;
		Destroy();
		m_Tex      = other.m_Tex;
		m_Width    = other.m_Width;
		m_Height   = other.m_Height;
		m_Channels = other.m_Channels;
		m_Filter   = other.m_Filter;
		m_WrapU    = other.m_WrapU;
		m_WrapV    = other.m_WrapV;
		m_HasMips  = other.m_HasMips;
		m_FlippedY = other.m_FlippedY;
		m_SourcePath = std::move(other.m_SourcePath);
		other.m_Tex = 0;
		other.m_Width = 0;
		other.m_Height = 0;
		other.m_Channels = 0;
		other.m_SourcePath.clear();
		return *this;
	}

	void Texture2D::Destroy() {
		if (m_Tex != 0) {
			FreeTextureSlot(m_Tex);
			m_Tex = 0;
		}
		m_Width = 0;
		m_Height = 0;
		m_Channels = 0;
		m_SourcePath.clear();
	}

	bool Texture2D::Load(const char* path, bool generateMipmaps, bool srgb, bool flipVertical) {
		Destroy();
		m_FlippedY = flipVertical;
		if (path == nullptr || *path == '\0') return false;

		if (!WebGPUBackend::IsInitialized()) {
			FNK_CORE_WARN_TAG("Texture2D",
				"Load called before WebGPU backend initialized: {}", path);
			return false;
		}

		auto image = DecodeFileToCpu(path, flipVertical);
		if (!image) {
			FNK_CORE_WARN_TAG("Texture2D", "Failed to load texture: {}", path);
			return false;
		}
		return LoadFromImage(*image, path, generateMipmaps, srgb, flipVertical);
	}

	bool Texture2D::LoadFromImage(const ImageData& image, const std::string& sourcePath,
		bool generateMipmaps, bool srgb, bool flipVertical)
	{
		Destroy();
		m_FlippedY = flipVertical;
		(void)srgb;
		const int w = image.Width;
		const int h = image.Height;
		if (w <= 0 || h <= 0 || !image.Pixels) return false;
		if (static_cast<size_t>(w) > std::numeric_limits<size_t>::max() / 4u / static_cast<size_t>(h)) return false;
		const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
		if (!image.OwnedPixels.empty() && image.OwnedPixels.size() < byteCount) return false;
		if (!WebGPUBackend::IsInitialized()) return false;

		// Over the device limit Dawn hands back an error texture, and every pass sampling it drops the frame.
		const uint32_t maxDimension = WebGPUBackend::GetMaxTextureDimension2D();
		if (static_cast<uint32_t>(w) > maxDimension || static_cast<uint32_t>(h) > maxDimension) {
			FNK_CORE_ERROR_TAG("Texture2D", "Texture {}x{} exceeds the GPU limit of {}px per side: {}",
				w, h, maxDimension, sourcePath);
			return false;
		}

		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::Queue  queue  = WebGPUBackend::GetQueue();

		constexpr wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm;
		uint32_t mipLevelCount = 1;
		if (generateMipmaps && GetMipPipeline(format)) {
			mipLevelCount = ComputeMipLevelCount(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
		}

		wgpu::TextureDescriptor texDesc{};
		texDesc.dimension       = wgpu::TextureDimension::e2D;
		texDesc.size            = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
		texDesc.format          = format;
		texDesc.usage           = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
		if (mipLevelCount > 1) {
			texDesc.usage |= wgpu::TextureUsage::RenderAttachment;
		}
		texDesc.mipLevelCount   = mipLevelCount;
		texDesc.sampleCount     = 1;
		texDesc.viewFormatCount = 0;

		wgpu::Texture texture = device.CreateTexture(&texDesc);
		if (!texture) {
			FNK_CORE_WARN_TAG("Texture2D", "wgpu::Device::CreateTexture failed: {}", sourcePath);
			return false;
		}

		wgpu::TexelCopyTextureInfo dst{};
		dst.texture  = texture;
		dst.mipLevel = 0;
		dst.origin   = { 0, 0, 0 };
		dst.aspect   = wgpu::TextureAspect::All;

		wgpu::TexelCopyBufferLayout layout{};
		layout.offset        = 0;
		layout.bytesPerRow   = static_cast<uint32_t>(w) * 4u;
		layout.rowsPerImage  = static_cast<uint32_t>(h);

		wgpu::Extent3D writeSize{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };

		queue.WriteTexture(&dst, image.Pixels, byteCount, &layout, &writeSize);

		if (!GenerateMipChain(texture, format, mipLevelCount)) {
			FNK_CORE_WARN_TAG("Texture2D", "Mip generation failed, sampling level 0 only: {}", sourcePath);
			mipLevelCount = 1;  // the view below hides the never-written levels
		}

		wgpu::TextureViewDescriptor viewDesc{};
		viewDesc.format          = format;
		viewDesc.dimension       = wgpu::TextureViewDimension::e2D;
		viewDesc.mipLevelCount   = mipLevelCount;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect          = wgpu::TextureAspect::All;
		wgpu::TextureView view = texture.CreateView(&viewDesc);

		GpuTexture slot;
		slot.Texture      = std::move(texture);
		slot.View         = std::move(view);
		slot.Width        = static_cast<uint32_t>(w);
		slot.Height       = static_cast<uint32_t>(h);
		slot.MipLevelCount = mipLevelCount;
		slot.CachedFilter = m_Filter;
		slot.CachedWrapU  = m_WrapU;
		slot.CachedWrapV  = m_WrapV;
		AssignSamplers(slot);

		m_Tex      = RegisterTexture(std::move(slot));
		m_Width    = w;
		m_Height   = h;
		m_Channels = 4;
		m_HasMips  = mipLevelCount > 1;
		m_SourcePath = sourcePath;
		return m_Tex != 0;
	}

	bool Texture2D::CreateFromPixels(int width, int height, const uint8_t* rgba,
		Filter filter, Wrap wrapU, Wrap wrapV)
	{
		Destroy();
		if (width <= 0 || height <= 0 || rgba == nullptr) return false;
		if (!WebGPUBackend::IsInitialized()) {
			FNK_CORE_WARN_TAG("Texture2D", "CreateFromPixels called before WebGPU backend initialized");
			return false;
		}

		const uint32_t maxDimension = WebGPUBackend::GetMaxTextureDimension2D();
		if (static_cast<uint32_t>(width) > maxDimension || static_cast<uint32_t>(height) > maxDimension) {
			FNK_CORE_ERROR_TAG("Texture2D", "CreateFromPixels: {}x{} exceeds the GPU limit of {}px per side",
				width, height, maxDimension);
			return false;
		}

		m_Filter = filter;
		m_WrapU  = wrapU;
		m_WrapV  = wrapV;
		m_FlippedY = false;

		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::Queue  queue  = WebGPUBackend::GetQueue();

		wgpu::TextureDescriptor texDesc{};
		texDesc.dimension       = wgpu::TextureDimension::e2D;
		texDesc.size            = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
		texDesc.format          = wgpu::TextureFormat::RGBA8Unorm;
		texDesc.usage           = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
		texDesc.mipLevelCount   = 1;
		texDesc.sampleCount     = 1;
		texDesc.viewFormatCount = 0;

		wgpu::Texture texture = device.CreateTexture(&texDesc);
		if (!texture) {
			FNK_CORE_WARN_TAG("Texture2D", "CreateFromPixels: device.CreateTexture failed");
			return false;
		}

		wgpu::TexelCopyTextureInfo dst{};
		dst.texture  = texture;
		dst.mipLevel = 0;
		dst.origin   = { 0, 0, 0 };
		dst.aspect   = wgpu::TextureAspect::All;

		wgpu::TexelCopyBufferLayout layout{};
		layout.offset       = 0;
		layout.bytesPerRow  = static_cast<uint32_t>(width) * 4u;
		layout.rowsPerImage = static_cast<uint32_t>(height);

		wgpu::Extent3D writeSize{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
		const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
		queue.WriteTexture(&dst, rgba, byteCount, &layout, &writeSize);

		wgpu::TextureViewDescriptor viewDesc{};
		viewDesc.format          = wgpu::TextureFormat::RGBA8Unorm;
		viewDesc.dimension       = wgpu::TextureViewDimension::e2D;
		viewDesc.mipLevelCount   = 1;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect          = wgpu::TextureAspect::All;
		wgpu::TextureView view = texture.CreateView(&viewDesc);

		GpuTexture slot;
		slot.Texture      = std::move(texture);
		slot.View         = std::move(view);
		slot.Width        = static_cast<uint32_t>(width);
		slot.Height       = static_cast<uint32_t>(height);
		slot.CachedFilter = m_Filter;
		slot.CachedWrapU  = m_WrapU;
		slot.CachedWrapV  = m_WrapV;
		AssignSamplers(slot);

		m_Tex      = RegisterTexture(std::move(slot));
		m_Width    = width;
		m_Height   = height;
		m_Channels = 4;
		m_HasMips  = false;
		m_SourcePath.clear();
		return m_Tex != 0;
	}

	bool Texture2D::UploadPixels(const uint8_t* rgba, size_t byteCount) {
		if (rgba == nullptr || !IsValid()) return false;
		const size_t expected = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height) * 4;
		if (byteCount != expected) {
			FNK_CORE_WARN_TAG("Texture2D",
				"UploadPixels byte count {} != expected {} ({}x{} RGBA8)",
				byteCount, expected, m_Width, m_Height);
			return false;
		}
		GpuTexture* slot = TryLookup(m_Tex);
		if (!slot || !slot->Texture) return false;

		wgpu::Queue queue = WebGPUBackend::GetQueue();

		wgpu::TexelCopyTextureInfo dst{};
		dst.texture  = slot->Texture;
		dst.mipLevel = 0;
		dst.origin   = { 0, 0, 0 };
		dst.aspect   = wgpu::TextureAspect::All;

		wgpu::TexelCopyBufferLayout layout{};
		layout.offset       = 0;
		layout.bytesPerRow  = static_cast<uint32_t>(m_Width) * 4u;
		layout.rowsPerImage = static_cast<uint32_t>(m_Height);

		wgpu::Extent3D writeSize{ static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height), 1 };
		queue.WriteTexture(&dst, rgba, byteCount, &layout, &writeSize);
		if (slot->MipLevelCount > 1) {
			GenerateMipChain(slot->Texture, slot->Texture.GetFormat(), slot->MipLevelCount);
		}
		return true;
	}

	void Texture2D::Submit(uint8_t /*unit*/) const {}

	// WebGPU samplers are immutable; SetFilter/SetWrapU/V replace the pool entry sampler rather than modifying it.
	void Texture2D::SetFilter(Filter filter) {
		SetSampler(filter, m_WrapU, m_WrapV);
	}
	void Texture2D::SetWrapU(Wrap u) {
		SetSampler(m_Filter, u, m_WrapV);
	}
	void Texture2D::SetWrapV(Wrap v) {
		SetSampler(m_Filter, m_WrapU, v);
	}
	void Texture2D::SetSampler(Filter filter, Wrap u, Wrap v) {
		m_Filter = filter;
		m_WrapU  = u;
		m_WrapV  = v;
		if (GpuTexture* slot = TryLookup(m_Tex)) {
			slot->CachedFilter = m_Filter;
			slot->CachedWrapU  = m_WrapU;
			slot->CachedWrapV  = m_WrapV;
			AssignSamplers(*slot);
		}
	}
	void Texture2D::ApplySamplerParams() const {}

	bool Texture2D::SetSceneSampling(TextureQualityLevel quality, AnisotropicFilteringLevel anisotropy) {
		if (quality == g_SceneTextureQuality && anisotropy == g_SceneAnisotropy) return false;
		g_SceneTextureQuality = quality;
		g_SceneAnisotropy = anisotropy;
		// Bind groups compare sampler handles, so the renderers rebuild theirs on the next draw.
		for (auto& [id, slot] : g_Textures) {
			AssignSceneSampler(slot);
		}
		return true;
	}

	TextureQualityLevel Texture2D::GetSceneTextureQuality() {
		return g_SceneTextureQuality;
	}

	AnisotropicFilteringLevel Texture2D::GetSceneAnisotropy() {
		return g_SceneAnisotropy;
	}

	std::unique_ptr<ImageData> Texture2D::GetImageData() const {
		if (m_SourcePath.empty()) return nullptr;
		return DecodeFileToCpu(m_SourcePath.c_str(), m_FlippedY);
	}

	std::unique_ptr<ImageData> Texture2D::DecodeFileToCpu(const char* path,
		bool flipVertical)
	{
		if (path == nullptr || *path == '\0') return nullptr;

		int w = 0, h = 0, n = 0;
		std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
			stbi_load(path, &w, &h, &n, 4), &stbi_image_free);
		if (!pixels || w <= 0 || h <= 0) return nullptr;
		if (static_cast<size_t>(w) > std::numeric_limits<size_t>::max() / 4u / static_cast<size_t>(h)) return nullptr;

		const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
		std::vector<unsigned char> owned(byteCount);
		std::memcpy(owned.data(), pixels.get(), byteCount);

		auto image = std::make_unique<ImageData>(w, h, std::move(owned));
		if (flipVertical) image->FlipVerticalRGBA();
		return image;
	}

}  // namespace Funke
