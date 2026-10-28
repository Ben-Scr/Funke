#include "pch.hpp"
#include "Graphics/Text/TextRenderer.hpp"

#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#endif
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Log.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/Shader.hpp"
#include "Graphics/Text/Font.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/Text/TextEncoding.hpp"
#include "Graphics/Text/TextLayout.hpp"
#include "Graphics/QuadVisibility.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Path.hpp"

#include <webgpu/webgpu_cpp.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <utility>


namespace Funke {

	namespace {
		constexpr size_t k_VerticesPerGlyph     = 4;
		constexpr size_t k_IndicesPerGlyph = 6;
		constexpr size_t k_InitialVertexCapacity = 1024;

		// CPU helpers.

		bool DecodeUtf8(std::string_view s, size_t idx, uint32_t& outCp, int& outLen) {
			return TextEncoding::DecodeUtf8(s, idx, outCp, outLen);
		}

		float MeasureLineWidth(const Font& font, std::string_view line, float letterSpacing) {
			float width = 0.0f;
			uint32_t prev = 0;
			int glyphCount = 0;
			size_t i = 0;
			while (i < line.size()) {
				uint32_t cp = 0;
				int len = 0;
				if (!DecodeUtf8(line, i, cp, len)) break;
				i += static_cast<size_t>(len);

				const GlyphMetrics* g = font.GetGlyph(cp);
				if (!g) { prev = 0; continue; }
				if (prev != 0) width += font.GetKerning(prev, cp);
				width += g->XAdvance;
				if (glyphCount > 0) width += letterSpacing;
				++glyphCount;
				prev = cp;
			}
			return width;
		}

		// ── Process-shared global state (ref-counted) ───────────────────────
		struct GlobalTextState {
			int                    RefCount = 0;
			std::unique_ptr<Shader> ShaderObj;
			wgpu::ShaderModule     Module;
			wgpu::BindGroupLayout  BindGroupLayout;
			wgpu::PipelineLayout   PipelineLayout;
			wgpu::Sampler          AtlasSampler;
			// Pipeline cache keyed by (colorFormat << 1) | hasDepth, matching
			// the SpriteResources scheme.
			std::unordered_map<uint32_t, wgpu::RenderPipeline> PipelineCache;
		};

		GlobalTextState& Globals() {
			static GlobalTextState s;
			return s;
		}

		uint32_t MakePipelineKey(wgpu::TextureFormat fmt, bool hasDepth) {
			return (static_cast<uint32_t>(fmt) << 1) | (hasDepth ? 1u : 0u);
		}

		wgpu::BindGroupLayout BuildBindGroupLayout(wgpu::Device device) {
			wgpu::BindGroupLayoutEntry entries[5] = {};

			entries[0].binding    = 0;
			entries[0].visibility = wgpu::ShaderStage::Vertex;
			entries[0].buffer.type           = wgpu::BufferBindingType::Uniform;
			entries[0].buffer.hasDynamicOffset = false;
			entries[0].buffer.minBindingSize = 64;

			entries[1].binding    = 1;
			entries[1].visibility = wgpu::ShaderStage::Fragment;
			entries[1].texture.sampleType    = wgpu::TextureSampleType::Float;
			entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
			entries[1].texture.multisampled  = false;

			entries[2].binding    = 2;
			entries[2].visibility = wgpu::ShaderStage::Fragment;
			entries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

			entries[3].binding    = 3;
			entries[3].visibility = wgpu::ShaderStage::Fragment;
			entries[3].texture.sampleType    = wgpu::TextureSampleType::Float;
			entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;
			entries[3].texture.multisampled  = false;

			entries[4].binding    = 4;
			entries[4].visibility = wgpu::ShaderStage::Fragment;
			entries[4].sampler.type = wgpu::SamplerBindingType::Filtering;

			wgpu::BindGroupLayoutDescriptor desc{};
			desc.entryCount = 5;
			desc.entries    = entries;
			desc.label      = "text-bindgroup-layout";
			return device.CreateBindGroupLayout(&desc);
		}

		wgpu::PipelineLayout BuildPipelineLayout(wgpu::Device device, wgpu::BindGroupLayout bgl) {
			wgpu::BindGroupLayout bgls[1] = { bgl };
			wgpu::PipelineLayoutDescriptor desc{};
			desc.bindGroupLayoutCount = 1;
			desc.bindGroupLayouts     = bgls;
			desc.label                = "text-pipeline-layout";
			return device.CreatePipelineLayout(&desc);
		}

		wgpu::Sampler BuildAtlasSampler(wgpu::Device device) {
			wgpu::SamplerDescriptor desc{};
			desc.addressModeU  = wgpu::AddressMode::ClampToEdge;
			desc.addressModeV  = wgpu::AddressMode::ClampToEdge;
			desc.addressModeW  = wgpu::AddressMode::ClampToEdge;
			desc.magFilter     = wgpu::FilterMode::Linear;
			desc.minFilter     = wgpu::FilterMode::Linear;
			desc.mipmapFilter  = wgpu::MipmapFilterMode::Nearest;
			desc.maxAnisotropy = 1;
			desc.label         = "text-atlas-sampler";
			return device.CreateSampler(&desc);
		}

		wgpu::RenderPipeline BuildTextPipeline(wgpu::Device device,
			wgpu::ShaderModule module, wgpu::PipelineLayout layout,
			wgpu::TextureFormat colorFormat, bool hasDepth)
		{
			// Vertex layout: 1 buffer × 3 attributes (per-vertex).
			wgpu::VertexAttribute attrs[6] = {};
			attrs[0].format         = wgpu::VertexFormat::Float32x2;
			attrs[0].offset         = offsetof(TextVertex, X);
			attrs[0].shaderLocation = 0;
			attrs[1].format         = wgpu::VertexFormat::Float32x2;
			attrs[1].offset         = offsetof(TextVertex, U);
			attrs[1].shaderLocation = 1;
			attrs[2].format         = wgpu::VertexFormat::Float32x4;
			attrs[2].offset         = offsetof(TextVertex, R);
			attrs[2].shaderLocation = 2;
			attrs[3].format         = wgpu::VertexFormat::Float32x4;
			attrs[3].offset         = offsetof(TextVertex, MaskRect);
			attrs[3].shaderLocation = 3;
			attrs[4].format         = wgpu::VertexFormat::Float32x4;
			attrs[4].offset         = offsetof(TextVertex, MaskRot);
			attrs[4].shaderLocation = 4;
			attrs[5].format         = wgpu::VertexFormat::Float32x4;
			attrs[5].offset         = offsetof(TextVertex, MaskUv);
			attrs[5].shaderLocation = 5;

			wgpu::VertexBufferLayout vbl{};
			vbl.arrayStride    = sizeof(TextVertex);
			vbl.stepMode       = wgpu::VertexStepMode::Vertex;
			vbl.attributeCount = 6;
			vbl.attributes     = attrs;

			wgpu::BlendState blend{};
			blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
			blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.color.operation = wgpu::BlendOperation::Add;
			blend.alpha.srcFactor = wgpu::BlendFactor::One;
			blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.alpha.operation = wgpu::BlendOperation::Add;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format    = colorFormat;
			colorTarget.blend     = &blend;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragState{};
			fragState.module      = module;
			fragState.entryPoint  = "fs_main";
			fragState.targetCount = 1;
			fragState.targets     = &colorTarget;

			wgpu::PrimitiveState prim{};
			prim.topology         = wgpu::PrimitiveTopology::TriangleList;
			prim.stripIndexFormat = wgpu::IndexFormat::Undefined;
			prim.frontFace        = wgpu::FrontFace::CCW;
			prim.cullMode         = wgpu::CullMode::None;

			wgpu::DepthStencilState depthState{};
			if (hasDepth) {
				depthState.format            = wgpu::TextureFormat::Depth24PlusStencil8;
				depthState.depthWriteEnabled = false;
				depthState.depthCompare      = wgpu::CompareFunction::Always;
			}

			wgpu::RenderPipelineDescriptor desc{};
			desc.label  = "text-pipeline";
			desc.layout = layout;

			desc.vertex.module      = module;
			desc.vertex.entryPoint  = "vs_main";
			desc.vertex.bufferCount = 1;
			desc.vertex.buffers     = &vbl;

			desc.fragment     = &fragState;
			desc.primitive    = prim;
			desc.depthStencil = hasDepth ? &depthState : nullptr;

			desc.multisample.count = 1;
			desc.multisample.mask  = 0xFFFFFFFF;

			return device.CreateRenderPipeline(&desc);
		}

		bool EnsureGlobalState() {
			GlobalTextState& g = Globals();
			if (g.RefCount++ > 0) {
				return static_cast<bool>(g.Module);
			}
			if (!WebGPUBackend::IsInitialized()) {
				--g.RefCount;
				return false;
			}
			wgpu::Device device = WebGPUBackend::GetDevice();
			if (!device) { --g.RefCount; return false; }

			// Shader's built-in registry maps "text" -> the embedded WGSL.
			// Pass vs/fs paths whose stem resolves to that name.
			g.ShaderObj = std::make_unique<Shader>(
				std::string("FunkeAssets/Shaders/TextShader.vs"),
				std::string("FunkeAssets/Shaders/TextShader.fs"));
			if (!g.ShaderObj || !g.ShaderObj->IsValid()) {
				FNK_CORE_ERROR_TAG("TextRenderer",
					"Text shader failed to load — text disabled");
				g.ShaderObj.reset();
				--g.RefCount;
				return false;
			}
			const auto lookup = WebGPUBackend::LookupShader(g.ShaderObj->GetHandle());
			if (!lookup.Valid) {
				g.ShaderObj.reset();
				--g.RefCount;
				return false;
			}
			g.Module = lookup.Module;

			g.BindGroupLayout = BuildBindGroupLayout(device);
			g.PipelineLayout  = BuildPipelineLayout(device, g.BindGroupLayout);
			g.AtlasSampler    = BuildAtlasSampler(device);
			if (!g.BindGroupLayout || !g.PipelineLayout || !g.AtlasSampler) {
				FNK_CORE_ERROR_TAG("TextRenderer",
					"Failed to build text bind-group / pipeline layout / sampler");
				g.ShaderObj.reset();
				g.Module          = nullptr;
				g.BindGroupLayout = nullptr;
				g.PipelineLayout  = nullptr;
				g.AtlasSampler    = nullptr;
				--g.RefCount;
				return false;
			}
			return true;
		}

		void ReleaseGlobalState() {
			GlobalTextState& g = Globals();
			if (g.RefCount == 0) return;
			if (--g.RefCount > 0) return;
			g.PipelineCache.clear();
			g.AtlasSampler    = nullptr;
			g.PipelineLayout  = nullptr;
			g.BindGroupLayout = nullptr;
			g.Module          = nullptr;
			g.ShaderObj.reset();
		}

		wgpu::RenderPipeline GetOrBuildPipeline(wgpu::TextureFormat colorFormat, bool hasDepth) {
			GlobalTextState& g = Globals();
			if (!g.Module || !g.PipelineLayout) return nullptr;

			const uint32_t key = MakePipelineKey(colorFormat, hasDepth);
			auto it = g.PipelineCache.find(key);
			if (it != g.PipelineCache.end()) return it->second;

			wgpu::Device device = WebGPUBackend::GetDevice();
			if (!device) return nullptr;
			wgpu::RenderPipeline pipeline = BuildTextPipeline(
				device, g.Module, g.PipelineLayout, colorFormat, hasDepth);
			if (!pipeline) return nullptr;
			g.PipelineCache.emplace(key, pipeline);
			return pipeline;
		}

		// ── Per-TextRenderer instance state ─────────────────────────────────
		struct PerInstance {
			struct BindGroupKey {
				unsigned AtlasId = 0;
				uint64_t MaskPoolId = 0;

				bool operator==(const BindGroupKey& other) const {
					return AtlasId == other.AtlasId
						&& MaskPoolId == other.MaskPoolId;
				}
			};

			struct BindGroupKeyHash {
				size_t operator()(const BindGroupKey& key) const noexcept {
					const uint64_t mixed = static_cast<uint64_t>(key.AtlasId)
						^ (key.MaskPoolId + 0x9e3779b97f4a7c15ull
							+ (static_cast<uint64_t>(key.AtlasId) << 6)
							+ (static_cast<uint64_t>(key.AtlasId) >> 2));
					return static_cast<size_t>(mixed);
				}
			};

			// Views/sampler are kept so a recycled atlas id or mask pool slot is detected and rebuilt.
			struct CachedBindGroup {
				wgpu::BindGroup     Group;
				wgpu::TextureView   AtlasView;
				wgpu::TextureView   MaskView;
				wgpu::Sampler       MaskSampler;
			};

			TextLayout::Cache Layouts;
			wgpu::Buffer IndexBuffer;
			uint32_t IndexGlyphCapacity = 0;
			wgpu::Buffer VertexBuffer;
			uint32_t     VertexBufferCapacityBytes = 0;
			wgpu::Buffer UniformBuffer;
			std::unordered_map<BindGroupKey, CachedBindGroup, BindGroupKeyHash> BindGroups;

			// Several RenderInstances calls share one submit (GuiRenderer's text phases); each appends its
			// vertices after the previous call's instead of overwriting them, so no flush is needed between.
			uint64_t  PendingSubmitSerial = ~0ull;
			uint32_t  VertexWriteOffset = 0;
			bool      HasPendingMvp = false;
			glm::mat4 PendingMvp{ 1.0f };
		};
		std::unordered_map<const TextRenderer*, PerInstance> g_PerInstance;

		PerInstance& GetPerInstance(const TextRenderer* self) {
			return g_PerInstance[self];
		}

		bool EnsureVertexBuffer(wgpu::Device device, PerInstance& inst, uint32_t neededBytes) {
			if (inst.VertexBufferCapacityBytes >= neededBytes && inst.VertexBuffer) return true;
			// Over maxBufferSize Dawn returns an error buffer (never null) that would drop every text draw.
			const uint64_t maxBytes = std::min<uint64_t>(WebGPUBackend::GetMaxBufferSize(), UINT32_MAX);
			if (neededBytes > maxBytes) {
				FNK_CORE_ERROR_TAG("TextRenderer", "Text vertex data ({} bytes) exceeds the GPU buffer limit; skipping text",
					neededBytes);
				return false;
			}
			uint64_t cap = inst.VertexBufferCapacityBytes > 0
				? inst.VertexBufferCapacityBytes
				: static_cast<uint64_t>(k_InitialVertexCapacity * sizeof(TextVertex));
			while (cap < neededBytes) cap *= 2;
			cap = std::min(cap, maxBytes);

			wgpu::BufferDescriptor desc{};
			desc.size  = cap;
			desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
			desc.label = "text-vertex-buffer";
			wgpu::Buffer buf = device.CreateBuffer(&desc);
			if (!buf) return false;
			inst.VertexBuffer              = std::move(buf);
			inst.VertexBufferCapacityBytes = static_cast<uint32_t>(cap);
			return true;
		}

		bool EnsureIndexBuffer(wgpu::Device device, wgpu::Queue queue, PerInstance& inst, size_t glyphCount) {
			if (inst.IndexBuffer && glyphCount <= inst.IndexGlyphCapacity) return true;
			const uint64_t maxGlyphs = std::min<uint64_t>(WebGPUBackend::GetMaxBufferSize() / (k_IndicesPerGlyph * sizeof(uint32_t)),
				INT32_MAX / k_VerticesPerGlyph);
			if (glyphCount > maxGlyphs) return false;
			uint64_t capacity = std::max<uint64_t>(inst.IndexGlyphCapacity, 256);
			while (capacity < glyphCount) capacity *= 2;
			capacity = std::min(capacity, maxGlyphs);
			std::vector<uint32_t> indices(static_cast<size_t>(capacity) * k_IndicesPerGlyph);
			for (uint32_t glyph = 0; glyph < capacity; ++glyph) {
				const uint32_t base = glyph * static_cast<uint32_t>(k_VerticesPerGlyph);
				const size_t i = static_cast<size_t>(glyph) * k_IndicesPerGlyph;
				indices[i] = base;
				indices[i + 1] = base + 1;
				indices[i + 2] = base + 2;
				indices[i + 3] = base;
				indices[i + 4] = base + 2;
				indices[i + 5] = base + 3;
			}
			wgpu::BufferDescriptor descriptor{};
			descriptor.size = indices.size() * sizeof(uint32_t);
			descriptor.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
			descriptor.label = "text-quad-indices";
			wgpu::Buffer buffer = device.CreateBuffer(&descriptor);
			if (!buffer) return false;
			queue.WriteBuffer(buffer, 0, indices.data(), descriptor.size);
			inst.IndexBuffer = std::move(buffer);
			inst.IndexGlyphCapacity = static_cast<uint32_t>(capacity);
			return true;
		}

		bool EnsureUniformBuffer(wgpu::Device device, PerInstance& inst) {
			if (inst.UniformBuffer) return true;
			wgpu::BufferDescriptor desc{};
			desc.size  = 64;
			desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
			desc.label = "text-viewproj-ubo";
			inst.UniformBuffer = device.CreateBuffer(&desc);
			return static_cast<bool>(inst.UniformBuffer);
		}

		wgpu::BindGroup ResolveAtlasBindGroup(wgpu::Device device, PerInstance& inst,
			unsigned atlasId,
			TextureHandle maskHandle)
		{
			Texture2D* maskTex = TextureManager::GetTexture(maskHandle);
			if (!maskTex || !maskTex->IsValid()) return nullptr;
			const uint64_t maskPoolId = maskTex->GetHandle();
			const PerInstance::BindGroupKey key{ atlasId, maskPoolId };

			wgpu::TextureView view = WebGPUBackend::LookupFontAtlas(atlasId);
			if (!view) return nullptr;
			const auto maskLookup = WebGPUBackend::LookupTexture2D(maskPoolId);
			if (!maskLookup.Valid || !maskLookup.View || !maskLookup.Sampler) return nullptr;

			auto it = inst.BindGroups.find(key);
			if (it != inst.BindGroups.end()
				&& it->second.AtlasView.Get() == view.Get()
				&& it->second.MaskView.Get() == maskLookup.View.Get()
				&& it->second.MaskSampler.Get() == maskLookup.Sampler.Get())
			{
				return it->second.Group;
			}

			GlobalTextState& g = Globals();
			wgpu::BindGroupEntry entries[5] = {};
			entries[0].binding = 0;
			entries[0].buffer  = inst.UniformBuffer;
			entries[0].offset  = 0;
			entries[0].size    = 64;
			entries[1].binding     = 1;
			entries[1].textureView = view;
			entries[2].binding = 2;
			entries[2].sampler = g.AtlasSampler;
			entries[3].binding     = 3;
			entries[3].textureView = maskLookup.View;
			entries[4].binding = 4;
			entries[4].sampler = maskLookup.Sampler;

			wgpu::BindGroupDescriptor desc{};
			desc.layout     = g.BindGroupLayout;
			desc.entryCount = 5;
			desc.entries    = entries;
			desc.label      = "text-atlas-bindgroup";
			wgpu::BindGroup bg = device.CreateBindGroup(&desc);
			if (!bg) return nullptr;
			// Entries pin their views; bound the cache so destroyed fonts/masks don't stay alive forever.
			constexpr size_t k_MaxCachedBindGroups = 128;
			if (it == inst.BindGroups.end() && inst.BindGroups.size() >= k_MaxCachedBindGroups) {
				inst.BindGroups.clear();
			}
			inst.BindGroups[key] = PerInstance::CachedBindGroup{ bg, view, maskLookup.View, maskLookup.Sampler };
			return bg;
		}

		bool ClipsEqual(bool aHas, const Vec2& aMin, const Vec2& aMax,
			bool bHas, const Vec2& bMin, const Vec2& bMax)
		{
			if (aHas != bHas) return false;
			if (!aHas) return true;
			return aMin.x == bMin.x && aMin.y == bMin.y
				&& aMax.x == bMax.x && aMax.y == bMax.y;
		}

		struct ScissorPx { uint32_t X, Y, W, H; bool Valid; };
		ScissorPx ComputeScissor(const glm::mat4& mvp,
			const Vec2& clipMin, const Vec2& clipMax,
			uint32_t targetW, uint32_t targetH)
		{
			ScissorPx out{};
			const glm::vec4 corners[4] = {
				mvp * glm::vec4(clipMin.x, clipMin.y, 0.0f, 1.0f),
				mvp * glm::vec4(clipMax.x, clipMin.y, 0.0f, 1.0f),
				mvp * glm::vec4(clipMin.x, clipMax.y, 0.0f, 1.0f),
				mvp * glm::vec4(clipMax.x, clipMax.y, 0.0f, 1.0f),
			};
			const float invW0 = corners[0].w != 0.0f ? 1.0f / corners[0].w : 1.0f;
			float ndcMinX = corners[0].x * invW0;
			float ndcMaxX = ndcMinX;
			float ndcMinY = corners[0].y * invW0;
			float ndcMaxY = ndcMinY;
			for (int i = 1; i < 4; ++i) {
				const float invW = corners[i].w != 0.0f ? 1.0f / corners[i].w : 1.0f;
				const float x = corners[i].x * invW;
				const float y = corners[i].y * invW;
				ndcMinX = std::min(ndcMinX, x);
				ndcMaxX = std::max(ndcMaxX, x);
				ndcMinY = std::min(ndcMinY, y);
				ndcMaxY = std::max(ndcMaxY, y);
			}
			ndcMinX = std::clamp(ndcMinX, -1.0f, 1.0f);
			ndcMaxX = std::clamp(ndcMaxX, -1.0f, 1.0f);
			ndcMinY = std::clamp(ndcMinY, -1.0f, 1.0f);
			ndcMaxY = std::clamp(ndcMaxY, -1.0f, 1.0f);

			// NDC maps onto the pass viewport, not the whole target: letterboxed output offsets and scales it.
			uint32_t vpX = 0, vpY = 0, vpW = targetW, vpH = targetH;
			WebGPUBackend::GetCachedViewport(vpX, vpY, vpW, vpH);
			const float fX = static_cast<float>(vpX);
			const float fY = static_cast<float>(vpY);
			const float fW = static_cast<float>(vpW);
			const float fH = static_cast<float>(vpH);
			const float xMin = fX + (ndcMinX * 0.5f + 0.5f) * fW;
			const float xMax = fX + (ndcMaxX * 0.5f + 0.5f) * fW;
			const float yMinTop = fY + (1.0f - (ndcMaxY * 0.5f + 0.5f)) * fH;
			const float yMaxTop = fY + (1.0f - (ndcMinY * 0.5f + 0.5f)) * fH;
			const float sx = std::floor(std::min(xMin, xMax));
			const float sy = std::floor(std::min(yMinTop, yMaxTop));
			const float sw = std::ceil(std::abs(xMax - xMin));
			const float sh = std::ceil(std::abs(yMaxTop - yMinTop));
			if (sw <= 0.0f || sh <= 0.0f) return out;

			const uint32_t ix = static_cast<uint32_t>(std::max(0.0f, sx));
			const uint32_t iy = static_cast<uint32_t>(std::max(0.0f, sy));
			uint32_t iw = static_cast<uint32_t>(std::max(0.0f, sw));
			uint32_t ih = static_cast<uint32_t>(std::max(0.0f, sh));
			if (ix >= targetW || iy >= targetH) return out;
			if (ix + iw > targetW) iw = targetW - ix;
			if (iy + ih > targetH) ih = targetH - iy;
			out.X = ix; out.Y = iy; out.W = iw; out.H = ih; out.Valid = true;
			return out;
		}
	}  // anonymous namespace

	// ── Static font resolution (backend-neutral logic) ──────────────────────

	Vec2 TextRenderer::MeasureNaturalSize(Font& font, std::string_view text, float letterSpacing,
		float lineSpacing) {
		if (text.empty()) return Vec2{ 0.0f, font.GetLineHeight() };

		float maxLineWidth = 0.0f;
		int lineCount = 0;
		size_t lineStart = 0;
		const size_t textSize = text.size();
		while (lineStart <= textSize) {
			size_t lineEnd = text.find('\n', lineStart);
			if (lineEnd == std::string_view::npos) lineEnd = textSize;
			std::string_view line(text.data() + lineStart, lineEnd - lineStart);
			const float w = MeasureLineWidth(font, line, letterSpacing);
			if (w > maxLineWidth) maxLineWidth = w;
			++lineCount;
			if (lineEnd == textSize) break;
			lineStart = lineEnd + 1;
		}
		const float gaps = static_cast<float>(lineCount > 1 ? lineCount - 1 : 0);
		return Vec2{ maxLineWidth,
			font.GetLineHeight() * static_cast<float>(lineCount) + lineSpacing * gaps };
	}

	Font* TextRenderer::ResolveFont(TextRendererComponent& text) {
		return ResolveFontAtPixelSize(text, text.FontSize);
	}

	Font* TextRenderer::ResolveFontAtPixelSize(TextRendererComponent& text, float pixelSize) {
		constexpr float k_MaxBakedPixelSize = 192.0f;
		const float requested = pixelSize > 0.0f ? pixelSize : text.FontSize;
		const float bakeRequest = std::min(requested, k_MaxBakedPixelSize);

		uint64_t uuid = static_cast<uint64_t>(text.FontAssetId);
		if (uuid == 0 || uuid == k_DefaultFontAssetId) {
			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				if (project->DefaultFontAssetId != 0) {
					uuid = project->DefaultFontAssetId;
				}
			}
		}

		// Bucket via FontManager so the cached-font check matches exactly what
		// LoadFontByUUIDAsync will key the slot under (incl. the user's bake-step setting).
		// Also require the requested asset: otherwise a default-font fallback cached while a custom
		// font was still baking would pass the size check and stick forever.
		if (FontManager::IsValid(text.ResolvedFont)) {
			if (Font* font = FontManager::GetFont(text.ResolvedFont)) {
				const bool sameAsset = uuid == 0 || FontManager::GetFontAssetUUID(text.ResolvedFont) == uuid;
				if (sameAsset && FontManager::QuantizeBakeSize(font->GetPixelSize())
					== FontManager::QuantizeBakeSize(bakeRequest)) {
					return font;
				}
			}
		}

		if (uuid != 0) {
			text.ResolvedFont = FontManager::LoadFontByUUIDAsync(uuid, bakeRequest);
			if (Font* f = FontManager::GetFont(text.ResolvedFont)) return f;
			// Still baking: draw with the default this frame but keep the pending handle cached.
			if (FontManager::IsValid(text.ResolvedFont)) {
				if (Font* fallback = FontManager::GetFont(FontManager::GetDefaultFont())) return fallback;
			}
		}

		text.ResolvedFont = FontManager::GetDefaultFont();
		Font* fallback = FontManager::GetFont(text.ResolvedFont);
		if (!fallback) {
			static bool s_LoggedMissingFont = false;
			if (!s_LoggedMissingFont) {
				s_LoggedMissingFont = true;
				FNK_CORE_WARN_TAG("TextRenderer",
					"No font available — assign one in the inspector or ensure FunkeAssets/Fonts/IBMPlexSansCondensed-Regular.ttf is shipped next to the executable.");
			}
		}
		return fallback;
	}

	// ── Lifecycle ───────────────────────────────────────────────────────────

	TextRenderer::TextRenderer() = default;
	TextRenderer::~TextRenderer() {
		Shutdown();
	}

	void TextRenderer::Initialize() {
		if (m_IsInitialized) return;
		if (!EnsureGlobalState()) return;
		m_Vertices.reserve(k_InitialVertexCapacity);
		m_Runs.reserve(16);
		(void)GetPerInstance(this);
		m_IsInitialized = true;
		FNK_CORE_INFO_TAG("TextRenderer", "Text renderer initialized (WebGPU)");
	}

	void TextRenderer::Shutdown() {
		if (!m_IsInitialized) return;
		m_Vertices.clear();
		m_Vertices.shrink_to_fit();
		m_Runs.clear();
		m_Runs.shrink_to_fit();
		g_PerInstance.erase(this);
		ReleaseGlobalState();
		m_IsInitialized = false;
	}

	void TextRenderer::EnsureGpuCapacity(size_t /*requiredBytes*/) {
		// Per-frame vertex buffer growth is handled inside RenderInstances —
		// this hook is the OpenGL holdover and is empty under WebGPU.
	}

	// ── Glyph emission (CPU side) ────────────────────────────────────────────

	// Shared with EmitText (which renders the same rows) and the input-field caret/
	// selection/hit-test code, so wrapped glyph rows and caret geometry never disagree.
	void TextRenderer::ComputeVisualLines(Font& font, std::string_view text, float letterSpacing,
		TextWrapMode wrapMode, float wrapWidthPixels, std::vector<std::pair<size_t, size_t>>& out)
	{
		TextLayout::VisualLines(font, text, letterSpacing, wrapMode, wrapWidthPixels, out);
	}

	void TextRenderer::EmitText(const TextLayout::Layout& layout, const TextDrawCmd& command,
		float offsetX, float offsetY, const Color& color,
		const glm::mat4& mvp, const QuadVisibility::Clip& clip)
	{
		if (layout.Glyphs.empty()) return;
		const float rotationC = std::cos(command.Rotation);
		const float rotationS = std::sin(command.Rotation);
		auto transform = [&](float x, float y) -> Vec2 {
			const float px = command.X + offsetX + x * command.Scale;
			const float py = command.Y + offsetY + y * command.Scale;
			if (command.Rotation == 0.0f) return { px, py };
			const float dx = px - command.Pivot.x;
			const float dy = py - command.Pivot.y;
			return { command.Pivot.x + rotationC * dx - rotationS * dy,
				command.Pivot.y + rotationS * dx + rotationC * dy };
		};
		const std::array<Vec2, 4> bounds{ transform(layout.Min.x, layout.Min.y),
			transform(layout.Max.x, layout.Min.y), transform(layout.Max.x, layout.Max.y),
			transform(layout.Min.x, layout.Max.y) };
		const auto visibility = QuadVisibility::Classify(bounds, mvp, clip);
		if (visibility == QuadVisibility::Visibility::Outside) return;

		TextVertex prototype;
		prototype.R = color.r;
		prototype.G = color.g;
		prototype.B = color.b;
		prototype.A = color.a;
		prototype.MaskRot[2] = 1.0f;
		prototype.MaskUv[0] = command.MaskUvRect.U0;
		prototype.MaskUv[1] = command.MaskUvRect.V0;
		prototype.MaskUv[2] = command.MaskUvRect.U1;
		prototype.MaskUv[3] = command.MaskUvRect.V1;
		if (command.HasTextureMask) {
			const float width = command.MaskRectMax.x - command.MaskRectMin.x;
			const float height = command.MaskRectMax.y - command.MaskRectMin.y;
			if (width > 0.0f && height > 0.0f) {
				prototype.MaskRect[0] = command.MaskRectMin.x;
				prototype.MaskRect[1] = command.MaskRectMin.y;
				prototype.MaskRect[2] = 1.0f / width;
				prototype.MaskRect[3] = 1.0f / height;
				prototype.MaskRot[0] = command.MaskPivot.x;
				prototype.MaskRot[1] = command.MaskPivot.y;
				prototype.MaskRot[2] = std::cos(-command.MaskRotation);
				prototype.MaskRot[3] = std::sin(-command.MaskRotation);
			}
		}
		for (const TextLayout::Glyph& glyph : layout.Glyphs) {
			const std::array<Vec2, 4> corners{ transform(glyph.X0, glyph.Y1),
				transform(glyph.X1, glyph.Y1), transform(glyph.X1, glyph.Y0),
				transform(glyph.X0, glyph.Y0) };
			if (visibility != QuadVisibility::Visibility::Inside && !QuadVisibility::IsVisible(corners, mvp, clip)) continue;
			if (m_Runs.empty() || m_Runs.back().Key.AtlasTexture != glyph.Atlas) {
				GlyphRun run;
				run.Key.AtlasTexture = glyph.Atlas;
				run.VertexStart = m_Vertices.size();
				m_Runs.push_back(run);
			}
			m_Runs.back().VertexCount += k_VerticesPerGlyph;
			const float us[]{ glyph.U0, glyph.U1, glyph.U1, glyph.U0 };
			const float vs[]{ glyph.V1, glyph.V1, glyph.V0, glyph.V0 };
			for (size_t i = 0; i < corners.size(); ++i) {
				TextVertex vertex = prototype;
				vertex.X = corners[i].x;
				vertex.Y = corners[i].y;
				vertex.U = us[i];
				vertex.V = vs[i];
				m_Vertices.push_back(vertex);
			}
		}
	}

	void TextDrawCmd::SetEffects(const TextRendererComponent& text, float unitsPerTextPixel)
	{
		OutlineWidth = std::max(text.OutlineWidth, 0.0f) * unitsPerTextPixel;
		OutlineColor = text.OutlineColor;
		ShadowOffset = text.ShadowOffset * unitsPerTextPixel;
		ShadowColor = text.ShadowColor;
	}

	void TextRenderer::RenderInstances(std::span<const TextDrawCmd> commands, const glm::mat4& mvp,
		unsigned short /*viewId*/, unsigned short /*scissorCache*/)
	{
		m_LastFrameGlyphCount = 0;
		m_LastFrameDrawCalls = 0;
		if (!m_IsInitialized || commands.empty()) return;

		GlobalTextState& g = Globals();
		if (!g.Module || !g.PipelineLayout) return;

		auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!target.Valid) return;

		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::Queue  queue  = WebGPUBackend::GetQueue();
		if (!device || !queue) return;

		wgpu::RenderPipeline pipeline = GetOrBuildPipeline(target.ColorFormat, target.HasDepth);
		if (!pipeline) return;

		PerInstance& inst = GetPerInstance(this);

		if (!EnsureUniformBuffer(device, inst)) return;
		// One shared view-projection per submit: a different mvp before the pending passes ran must flush first.
		if (inst.HasPendingMvp && inst.PendingSubmitSerial == WebGPUBackend::GetFrameSubmitSerial()
			&& inst.PendingMvp != mvp)
		{
			WebGPUBackend::FlushCommands();
		}
		if (inst.PendingSubmitSerial != WebGPUBackend::GetFrameSubmitSerial()) {
			// Retired font pages must not remain resident through cached bind groups.
			std::erase_if(inst.BindGroups, [](const auto& entry) {
				return !WebGPUBackend::LookupFontAtlas(entry.first.AtlasId);
			});
			inst.PendingSubmitSerial = WebGPUBackend::GetFrameSubmitSerial();
			inst.VertexWriteOffset = 0;
			inst.HasPendingMvp = false;
		}
		if (!inst.HasPendingMvp || inst.PendingMvp != mvp) {
			queue.WriteBuffer(inst.UniformBuffer, 0, glm::value_ptr(mvp), 64);
			inst.PendingMvp = mvp;
			inst.HasPendingMvp = true;
		}

		m_Order.clear();
		m_Order.reserve(commands.size());
		for (size_t i = 0; i < commands.size(); ++i) {
			if (commands[i].FontPtr && !commands[i].Text.empty()) {
				m_Order.push_back(i);
			}
		}
		std::sort(m_Order.begin(), m_Order.end(), [&](size_t a, size_t b) {
			const auto& ca = commands[a];
			const auto& cb = commands[b];
			if (ca.SortingLayer != cb.SortingLayer) return ca.SortingLayer < cb.SortingLayer;
			if (ca.SortingOrder != cb.SortingOrder) return ca.SortingOrder < cb.SortingOrder;
			// DrawIndex is the documented paint-order tiebreaker (hierarchy/submit order)
			// and must outrank FontPtr — FontPtr ordering is only an atlas-batching
			// optimization. Without this, overlapping equal-order UI text (e.g. a panel
			// title and a button label sharing a font) gets non-deterministic z-order.
			if (ca.DrawIndex != cb.DrawIndex) return ca.DrawIndex < cb.DrawIndex;
			return ca.FontPtr < cb.FontPtr;
		});

		m_Vertices.clear();
		m_Runs.clear();

		struct Run {
			unsigned AtlasId = 0;
			TextureHandle MaskTextureHandle{};
			int      SortingLayer = 0;
			int16_t  SortingOrder = 0;
			bool     HasClip = false;
			Vec2     ClipMin{};
			Vec2     ClipMax{};
			size_t   VertexStart = 0;
			size_t   VertexCount = 0;
		};
		std::vector<Run> runs;
		runs.reserve(16);
		const TextureHandle defaultMaskTexture = TextureManager::GetDefaultTexture(DefaultTexture::Square);
		auto resolveMaskHandle = [&](const TextDrawCmd& cmd) {
			return cmd.HasTextureMask && TextureManager::IsValid(cmd.MaskTextureHandle)
				? cmd.MaskTextureHandle
				: defaultMaskTexture;
		};

		for (size_t commandIndex : m_Order) {
			const TextDrawCmd& cmd = commands[commandIndex];
			m_Runs.clear();
			QuadVisibility::Clip clip;
			if (cmd.HasClip) {
				const ScissorPx sc = ComputeScissor(mvp, cmd.ClipMin, cmd.ClipMax, target.Width, target.Height);
				if (!sc.Valid) continue;
				uint32_t vx = 0, vy = 0, vw = target.Width, vh = target.Height;
				WebGPUBackend::GetCachedViewport(vx, vy, vw, vh);
				clip = QuadVisibility::FromScissor(sc.X, sc.Y, sc.W, sc.H, vx, vy, vw, vh);
			}
			const TextLayout::Settings settings{ cmd.LetterSpacing, cmd.LineSpacing, cmd.WrapWidthPixels,
				cmd.Wrap, cmd.Align, cmd.VAlign };
			const auto& layout = inst.Layouts.Get(cmd.FontPtr->GetAtlasTexture(), cmd.Text, settings,
				[&](TextLayout::Layout& result) { TextLayout::Build(*cmd.FontPtr, cmd.Text, settings, result, m_WrapScratch); });
			auto emit = [&](float dx, float dy, const Color& color) {
				EmitText(layout, cmd, dx, dy, color, mvp, clip);
			};
			// Effects first so the text paints over them; they fade with the text.
			if (cmd.ShadowColor.a > 0.0f && (cmd.ShadowOffset.x != 0.0f || cmd.ShadowOffset.y != 0.0f)) {
				Color shadow = cmd.ShadowColor;
				shadow.a *= cmd.Tint.a;
				emit(cmd.ShadowOffset.x, cmd.ShadowOffset.y, shadow);
			}
			if (cmd.OutlineWidth > 0.0f && cmd.OutlineColor.a > 0.0f) {
				Color outline = cmd.OutlineColor;
				outline.a *= cmd.Tint.a;
				constexpr float k_Diagonal = 0.70710678f;
				static constexpr float k_Ring[8][2] = {
					{ 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f },
					{ k_Diagonal, k_Diagonal }, { -k_Diagonal, k_Diagonal },
					{ k_Diagonal, -k_Diagonal }, { -k_Diagonal, -k_Diagonal },
				};
				for (const auto& direction : k_Ring) {
					emit(direction[0] * cmd.OutlineWidth, direction[1] * cmd.OutlineWidth, outline);
				}
			}
			emit(0.0f, 0.0f, cmd.Tint);
			for (const GlyphRun& glyphRun : m_Runs) {
				Run run;
				run.AtlasId = glyphRun.Key.AtlasTexture;
				run.MaskTextureHandle = resolveMaskHandle(cmd);
				run.SortingLayer = cmd.SortingLayer;
				run.SortingOrder = cmd.SortingOrder;
				run.HasClip = cmd.HasClip;
				run.ClipMin = cmd.ClipMin;
				run.ClipMax = cmd.ClipMax;
				run.VertexStart = glyphRun.VertexStart;
				run.VertexCount = glyphRun.VertexCount;
				if (!runs.empty()) {
					auto& previous = runs.back();
					if (previous.AtlasId == run.AtlasId && previous.MaskTextureHandle == run.MaskTextureHandle
						&& previous.SortingLayer == run.SortingLayer && previous.SortingOrder == run.SortingOrder
						&& ClipsEqual(previous.HasClip, previous.ClipMin, previous.ClipMax, run.HasClip, run.ClipMin, run.ClipMax)) {
						previous.VertexCount += run.VertexCount;
						continue;
					}
				}
				runs.push_back(run);
			}
		}

		if (m_Vertices.empty() || runs.empty()) return;

		const size_t byteCount = m_Vertices.size() * sizeof(TextVertex);
		if (byteCount > UINT32_MAX - inst.VertexWriteOffset) return;
		const uint32_t totalBytes = static_cast<uint32_t>(byteCount);
		if (!EnsureIndexBuffer(device, queue, inst, m_Vertices.size() / k_VerticesPerGlyph)) return;
		const WGPUBuffer previousBuffer = inst.VertexBuffer.Get();
		if (!EnsureVertexBuffer(device, inst, inst.VertexWriteOffset + totalBytes)) return;
		// A regrown buffer is a fresh object; passes already recorded keep the old one alive.
		if (inst.VertexBuffer.Get() != previousBuffer) inst.VertexWriteOffset = 0;
		const uint32_t vertexOffset = inst.VertexWriteOffset;
		queue.WriteBuffer(inst.VertexBuffer, vertexOffset, m_Vertices.data(), totalBytes);
		inst.VertexWriteOffset += totalBytes;

		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!encoder) return;

		wgpu::RenderPassColorAttachment colorAtt{};
		colorAtt.view       = target.ColorView;
		colorAtt.loadOp     = wgpu::LoadOp::Load;
		colorAtt.storeOp    = wgpu::StoreOp::Store;
		colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

		wgpu::RenderPassDepthStencilAttachment depthAtt{};
		if (target.HasDepth) {
			depthAtt.view              = target.DepthView;
			depthAtt.depthLoadOp       = wgpu::LoadOp::Load;
			// Ignored for Load, but browsers reject the non-finite default.
			depthAtt.depthClearValue   = 1.0f;
			depthAtt.depthStoreOp      = wgpu::StoreOp::Store;
			depthAtt.stencilLoadOp     = wgpu::LoadOp::Load;
			depthAtt.stencilStoreOp    = wgpu::StoreOp::Store;
		}

		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label                  = "text-pass";
		passDesc.colorAttachmentCount   = 1;
		passDesc.colorAttachments       = &colorAtt;
		passDesc.depthStencilAttachment = target.HasDepth ? &depthAtt : nullptr;

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetPipeline(pipeline);
		pass.SetVertexBuffer(0, inst.VertexBuffer, vertexOffset, totalBytes);
		pass.SetIndexBuffer(inst.IndexBuffer, wgpu::IndexFormat::Uint32);
		pass.SetScissorRect(0, 0, target.Width, target.Height);

		bool scissorIsFull = true;
		for (const Run& r : runs) {
			wgpu::BindGroup bg = ResolveAtlasBindGroup(device, inst,
				r.AtlasId, r.MaskTextureHandle);
			if (!bg) continue;  // atlas missing (Font destroyed mid-frame)

			if (r.HasClip) {
				const ScissorPx sc = ComputeScissor(mvp, r.ClipMin, r.ClipMax,
					target.Width, target.Height);
				if (!sc.Valid) continue;  // fully outside target — skip
				pass.SetScissorRect(sc.X, sc.Y, sc.W, sc.H);
				scissorIsFull = false;
			} else if (!scissorIsFull) {
				pass.SetScissorRect(0, 0, target.Width, target.Height);
				scissorIsFull = true;
			}

			pass.SetBindGroup(0, bg);
			pass.DrawIndexed(static_cast<uint32_t>(r.VertexCount / k_VerticesPerGlyph * k_IndicesPerGlyph),
				1, 0, static_cast<int32_t>(r.VertexStart), 0);
			++m_LastFrameDrawCalls;
		}

		pass.End();

		if (target.IsSwapChain) {
			WebGPUBackend::MarkSwapChainRendered();
		}

		m_LastFrameGlyphCount = m_Vertices.size() / k_VerticesPerGlyph;
	}

#if FUNKE_WITH_2D
	// World-space text sits on Transform2D entities and is drawn by Renderer2D (Funke.2D).
	void TextRenderer::RenderScene(Scene& scene, const glm::mat4& vp, const AABB& viewportAABB) {
		m_LastFrameGlyphCount = 0;
		m_LastFrameDrawCalls = 0;
		if (!m_IsInitialized) return;

		// Publish any async font bakes that finished since last frame, so a
		// newly-rasterized atlas can be sampled by this frame's text rather
		// than waiting one more frame to flip from the default fallback.
		FontManager::PollAsync();

		m_PendingDrawCmds.clear();

		entt::registry& registry = scene.GetRegistry();
		auto view = registry.view<TextRendererComponent, Transform2DComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, text, tr] : view.each()) {
			if (text.Text.empty()) continue;

			Font* font = ResolveFont(text);
			if (!font || !font->IsLoaded()) continue;

			const AABB approx = AABB::FromTransform(tr);
			if (!AABB::Intersects(viewportAABB, approx)) {
				const float radius = text.FontSize * static_cast<float>(text.Text.size()) * 0.5f;
				AABB textBounds{
					{ tr.Position.x - radius, tr.Position.y - radius },
					{ tr.Position.x + radius, tr.Position.y + radius }
				};
				if (!AABB::Intersects(viewportAABB, textBounds)) continue;
			}

			const float bakedSize = font->GetPixelSize() > 0.0f ? font->GetPixelSize() : text.FontSize;
			const float drawScale = (text.FontSize / bakedSize)
				* tr.Scale.x
				/ k_TextPixelsPerWorldUnit;

			TextDrawCmd cmd;
			cmd.FontPtr = font;
			cmd.Text = std::string_view(text.Text);
			cmd.X = tr.Position.x + text.Margin.x * drawScale;
			cmd.Y = tr.Position.y - text.Margin.y * drawScale;
			cmd.Scale = drawScale;
			cmd.LetterSpacing = text.LetterSpacing;
			cmd.LineSpacing = text.LineSpacing;
			cmd.Tint = text.Color;
			cmd.Align = text.HAlign;
			cmd.VAlign = text.VAlign;
			cmd.Wrap = text.WrapMode;
			cmd.WrapWidthPixels = 0.0f;
			cmd.SortingOrder = text.SortingOrder;
			cmd.SortingLayer = text.SortingLayer;
			cmd.Rotation = tr.Rotation;
			cmd.Pivot = tr.Position;
			cmd.SetEffects(text, tr.Scale.x / k_TextPixelsPerWorldUnit);
			m_PendingDrawCmds.push_back(cmd);
		}

		RenderInstances(m_PendingDrawCmds, vp);
	}
#endif

}  // namespace Funke
