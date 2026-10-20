#include "pch.hpp"
#include "Gui/GuiRenderer.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#if FUNKE_WITH_2D
#include "Components/Graphics/Camera2DComponent.hpp"
#endif
#include "Components/Graphics/ImageComponent.hpp"
#include "Gui/NineSlice.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/CircularSliderComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/UI/MaskComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/Log.hpp"
#include "Core/MouseButton.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/Instance44.hpp"
#include "Graphics/SpriteResources.hpp"
#include "Graphics/SpriteUVResolver.hpp"
#include "Graphics/CustomSpriteDrawer.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/Text/Font.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/Text/TextRenderer.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/UIDrawOrder.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/UILayoutSystem.hpp"

#include <webgpu/webgpu_cpp.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

// GuiRenderer — WebGPU UI renderer. Hierarchy walk + sort drives a single RenderPassEncoder with
// per-batch SetScissorRect. GPU state (instance VBO, UBO, bind-group cache) is keyed by `this`
// so the editor's two FBO GuiRenderers don't trample each other.

namespace Funke {

	namespace {
		bool Utf8DecodeAt(std::string_view s, int idx, uint32_t& outCp, int& outLen) {
			if (idx < 0 || idx >= static_cast<int>(s.size())) {
				outCp = 0; outLen = 0; return false;
			}
			const unsigned char b = static_cast<unsigned char>(s[idx]);
			if (b < 0x80) { outCp = b; outLen = 1; return true; }
			int len; uint32_t cp;
			if ((b & 0xE0) == 0xC0)      { len = 2; cp = b & 0x1F; }
			else if ((b & 0xF0) == 0xE0) { len = 3; cp = b & 0x0F; }
			else if ((b & 0xF8) == 0xF0) { len = 4; cp = b & 0x07; }
			else { outCp = b; outLen = 1; return true; }
			if (idx + len > static_cast<int>(s.size())) {
				outCp = b; outLen = 1; return true;
			}
			for (int i = 1; i < len; ++i) {
				cp = (cp << 6) | (static_cast<unsigned char>(s[idx + i]) & 0x3F);
			}
			outCp = cp; outLen = len; return true;
		}

		float MeasureUpToByteUI(const Font& font, std::string_view text, int targetByte,
			float letterSpacing)
		{
			if (targetByte <= 0) return 0.0f;
			const int n = static_cast<int>(text.size());
			if (targetByte > n) targetByte = n;
			float w = 0.0f;
			uint32_t prev = 0;
			int glyphCount = 0;
			int idx = 0;
			while (idx < targetByte) {
				uint32_t cp; int len;
				if (!Utf8DecodeAt(text, idx, cp, len)) break;
				if (idx + len > targetByte) break;
				const GlyphMetrics* g = font.GetGlyph(cp);
				if (g) {
					if (prev != 0) w += font.GetKerning(prev, cp);
					w += g->XAdvance;
					if (glyphCount > 0) w += letterSpacing;
					++glyphCount;
					prev = cp;
				}
				else {
					prev = 0;
				}
				idx += len;
			}
			return w;
		}

		struct InputFieldOverlayLayout {
			bool Valid = false;
			float OriginX = 0.0f;
			float Scale = 1.0f;
			float LetterSpacing = 0.0f;
			float LineSpacing = 0.0f;
			Font* FontPtr = nullptr;
			TextAlignment Align = TextAlignment::Left;
			TextVerticalAlignment VAlign = TextVerticalAlignment::Middle;
			Vec2 BL{};
			Vec2 TR{};
			float FontSize = 16.0f;
			TextWrapMode WrapMode = TextWrapMode::None;
			float WrapWidthPixels = 0.0f;
		};

		InputFieldOverlayLayout ResolveInputFieldOverlay(entt::registry& registry,
			const InputFieldComponent& field)
		{
			InputFieldOverlayLayout out{};
			if (field.TextEntity == entt::null || !registry.valid(field.TextEntity)) return out;
			if (!registry.all_of<RectTransform2DComponent, TextRendererComponent>(field.TextEntity)) return out;

			auto& rect = registry.get<RectTransform2DComponent>(field.TextEntity);
			auto& tc = registry.get<TextRendererComponent>(field.TextEntity);
			Font* font = TextRenderer::ResolveFontAtPixelSize(tc, tc.FontSize * std::max(0.01f, std::abs(rect.Scale.x))); // same bake as the text loop so GetLineHeight()/Scale match the render (fixes LineSpacing caret/selection drift)
			if (!font || !font->IsLoaded()) return out;

			const float uniformScale = rect.Scale.x;
			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();
			const float w = tr.x - bl.x;
			float originX;
			switch (tc.HAlign) {
			case TextAlignment::Center: originX = bl.x + w * 0.5f;         break;
			case TextAlignment::Right:  originX = tr.x - 4.0f * uniformScale; break;
			case TextAlignment::Left:
			default:                    originX = bl.x + 4.0f * uniformScale; break;
			}

			const float bakedSize = font->GetPixelSize() > 0.0f ? font->GetPixelSize() : tc.FontSize;
			out.Valid = true;
			out.OriginX = originX;
			out.Scale = (tc.FontSize / bakedSize) * uniformScale;
			out.LetterSpacing = tc.LetterSpacing;
			out.LineSpacing = tc.LineSpacing;
			out.FontPtr = font;
			out.Align = tc.HAlign;
			out.VAlign = tc.VAlign;
			out.BL = bl;
			out.TR = tr;
			out.FontSize = tc.FontSize * uniformScale;
			out.WrapMode = tc.WrapMode;
			out.WrapWidthPixels = 0.0f;
			if (tc.WrapMode != TextWrapMode::None && out.Scale > 0.0f) {
				const float padPixels = 8.0f * uniformScale;
				const float marginPixels = (tc.Margin.x + tc.Margin.z) * uniformScale;
				out.WrapWidthPixels = std::max(0.0f, ((tr.x - bl.x) - padPixels - marginPixels) / out.Scale);
			}
			return out;
		}

		// Visual line byte-ranges for the field's text, wrap-aware (matches the renderer).
		std::vector<std::pair<int, int>> InputVisualLines(const InputFieldOverlayLayout& layout, std::string_view text) {
			std::vector<std::pair<size_t, size_t>> raw;
			TextRenderer::ComputeVisualLines(*layout.FontPtr, text, layout.LetterSpacing,
				layout.WrapMode, layout.WrapWidthPixels, raw);
			std::vector<std::pair<int, int>> lines;
			lines.reserve(raw.size());
			for (const auto& p : raw) lines.emplace_back(static_cast<int>(p.first), static_cast<int>(p.second));
			if (lines.empty()) lines.emplace_back(0, 0);
			return lines;
		}

		// Visual line for caret byte b: the latest line whose start <= b. Char-wrap ranges
		// touch, so this puts the caret on the lower line; a '\n'/word-wrap gap keeps the
		// byte before the break on the upper line. (Line starts are monotonic.)
		int VisualLineOfByte(const std::vector<std::pair<int, int>>& lines, int b) {
			int result = 0;
			for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
				if (lines[i].first <= b) result = i;
				else break;
			}
			return result;
		}

		struct UIMaskClip {
			bool HasClip = false;
			Vec2 ClipMin{};
			Vec2 ClipMax{};
			bool HasTextureMask = false;
			TextureHandle MaskTextureHandle{};
			SpriteUVRect MaskUvRect{};
			Vec2 MaskRectMin{};
			Vec2 MaskRectMax{};
			Vec2 MaskPivot{};
			float MaskRotation = 0.0f;
		};

		void ApplyMaskClipToInstance(const UIMaskClip& clip, Instance44& inst)
		{
			inst.HasClip = clip.HasClip;
			inst.ClipMin = clip.ClipMin;
			inst.ClipMax = clip.ClipMax;
			inst.HasTextureMask = clip.HasTextureMask;
			inst.MaskTextureHandle = clip.MaskTextureHandle;
			inst.MaskUvRect = clip.MaskUvRect;
			inst.MaskRectMin = clip.MaskRectMin;
			inst.MaskRectMax = clip.MaskRectMax;
			inst.MaskPivot = clip.MaskPivot;
			inst.MaskRotation = clip.MaskRotation;
		}

		void ApplyMaskClipToText(const UIMaskClip& clip, TextDrawCmd& cmd)
		{
			cmd.HasClip = clip.HasClip;
			cmd.ClipMin = clip.ClipMin;
			cmd.ClipMax = clip.ClipMax;
			cmd.HasTextureMask = clip.HasTextureMask;
			cmd.MaskTextureHandle = clip.MaskTextureHandle;
			cmd.MaskUvRect = clip.MaskUvRect;
			cmd.MaskRectMin = clip.MaskRectMin;
			cmd.MaskRectMax = clip.MaskRectMax;
			cmd.MaskPivot = clip.MaskPivot;
			cmd.MaskRotation = clip.MaskRotation;
		}

		void ApplyImageSliceToInstance(Instance44& inst, const ImageComponent& image)
		{
			Texture2D* tex = TextureManager::GetTexture(image.TextureHandle);
			if (!tex) return;
			// Empty name resolves to the full texture; a named sprite to its slice rect.
			inst.UvRect = ResolveSpriteUVRect(image.TextureAssetId,
				image.SpriteName,
				static_cast<int>(tex->GetWidth()),
				static_cast<int>(tex->GetHeight()));
		}

		// Sprite size in texture pixels, undoing ResolveSpriteUVRect's half-texel inset.
		Vec2 SpritePixelSize(const Instance44& inst, const Texture2D& tex)
		{
			const Vec2 texSize{ static_cast<float>(tex.GetWidth()), static_cast<float>(tex.GetHeight()) };
			if (inst.UvRect.IsFullTexture) return texSize;
			return { (inst.UvRect.U1 - inst.UvRect.U0) * texSize.x + 1.0f, (inst.UvRect.V1 - inst.UvRect.V0) * texSize.y + 1.0f };
		}

		// Narrows `clip` by mask entity `current`; its texture mask wins because callers go top-down, nearest last.
		void AddMaskToClip(entt::registry& registry, EntityHandle current, UIMaskClip& result)
		{
			const auto& rect = registry.get<RectTransform2DComponent>(current);
			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();

			if (!result.HasClip) {
				result.ClipMin = bl;
				result.ClipMax = tr;
				result.HasClip = true;
			}
			else {
				result.ClipMin.x = std::max(result.ClipMin.x, bl.x);
				result.ClipMin.y = std::max(result.ClipMin.y, bl.y);
				result.ClipMax.x = std::min(result.ClipMax.x, tr.x);
				result.ClipMax.y = std::min(result.ClipMax.y, tr.y);
			}

			if (const auto* image = registry.try_get<ImageComponent>(current)) {
				Texture2D* tex = TextureManager::GetTexture(image->TextureHandle);
				if (tex && tex->IsValid()) {
					result.HasTextureMask = true;
					result.MaskTextureHandle = image->TextureHandle;
					result.MaskUvRect = ResolveSpriteUVRect(image->TextureAssetId,
						image->SpriteName,
						static_cast<int>(tex->GetWidth()),
						static_cast<int>(tex->GetHeight()));
					result.MaskRectMin = bl;
					result.MaskRectMax = tr;
					result.MaskPivot = rect.ResolvedPivot;
					result.MaskRotation = rect.Rotation;
				}
			}
		}

		// Per-pass scratch shared by both GuiRenderers; CollectAndDraw is not re-entrant.
		UIDrawOrder::OrderLookup s_OrderLookup;
		std::vector<UIMaskClip> s_ClipByOrder;

		// Clip of every entry in `order`, inherited from its nearest drawn rect ancestor (already resolved: the
		// order is a pre-order DFS) instead of walking every ancestor per entity.
		void BuildClipTable(entt::registry& registry, const std::vector<std::pair<EntityHandle, int>>& order)
		{
			s_ClipByOrder.resize(order.size());
			for (size_t i = 0; i < order.size(); ++i) {
				UIMaskClip clip{};
				EntityHandle current = order[i].first;
				for (int hop = 0; hop < 4096; ++hop) {
					const HierarchyComponent* h = registry.try_get<HierarchyComponent>(current);
					if (!h || h->Parent == entt::null || !registry.valid(h->Parent)) break;
					current = h->Parent;
					const uint32_t slot = s_OrderLookup.SlotOf(current);
					if (slot == UIDrawOrder::OrderLookup::k_None) continue;
					clip = s_ClipByOrder[slot];
					if (registry.all_of<MaskComponent>(current)) AddMaskToClip(registry, current, clip);
					break;
				}
				s_ClipByOrder[i] = clip;
			}
		}

		const UIMaskClip& ClipForEntity(EntityHandle entity)
		{
			static const UIMaskClip k_NoClip{};
			const uint32_t slot = s_OrderLookup.SlotOf(entity);
			return slot == UIDrawOrder::OrderLookup::k_None ? k_NoClip : s_ClipByOrder[slot];
		}

		bool ResolveUICanvasSize(int& outW, int& outH) {
			const Window::UIRegion uiRegion = Window::GetUIRegion();
			if (uiRegion.IsActive()) {
				outW = uiRegion.Width;
				outH = uiRegion.Height;
				return outW > 0 && outH > 0;
			}
			Viewport* vp = Window::GetMainViewport();
			if (!vp || vp->GetWidth() <= 0 || vp->GetHeight() <= 0) {
				return false;
			}
			outW = vp->GetWidth();
			outH = vp->GetHeight();
			return true;
		}

		// ── WebGPU per-GuiRenderer GPU state ────────────────────────────────
		// Keyed by `this` so the editor's two FBO GuiRenderers don't trample each other. Cleared on Shutdown.
		struct GuiRendererWebGPUState {
			wgpu::Buffer InstanceBuffer;
			uint32_t     InstanceBufferCapacity = 0;
			wgpu::Buffer UniformBuffer;
			// Stores Sampler+View so a mid-frame Texture2D::SetFilter (which rebuilds the GPU sampler) invalidates the cached bind group.
			struct BindGroupKey {
				uint64_t TexturePoolId = 0;
				uint64_t MaskPoolId = 0;

				bool operator==(const BindGroupKey& other) const {
					return TexturePoolId == other.TexturePoolId
						&& MaskPoolId == other.MaskPoolId;
				}
			};

			struct BindGroupKeyHash {
				size_t operator()(const BindGroupKey& key) const noexcept {
					const uint64_t mixed = key.TexturePoolId
						^ (key.MaskPoolId + 0x9e3779b97f4a7c15ull
							+ (key.TexturePoolId << 6)
							+ (key.TexturePoolId >> 2));
					return static_cast<size_t>(mixed);
				}
			};

			struct CachedBindGroup {
				wgpu::BindGroup   Group;
				wgpu::Sampler     Sampler;
				wgpu::TextureView View;
				wgpu::Sampler     MaskSampler;
				wgpu::TextureView MaskView;
			};
			std::unordered_map<BindGroupKey, CachedBindGroup, BindGroupKeyHash> BindGroupsThisFrame;
			uint64_t LastClearFrameCount = static_cast<uint64_t>(-1);
			std::vector<WebGPUSpriteResources::SpriteInstance> EncodedScratch;
			// Images with a UI material; per GuiRenderer, since its globals hold this renderer's view.
			std::unique_ptr<CustomSpriteDrawer> CustomImages;
			bool CustomImagesFailed = false;
		};
		std::unordered_map<const GuiRenderer*, GuiRendererWebGPUState> g_State;

		GuiRendererWebGPUState& GetState(const GuiRenderer* self) {
			return g_State[self];
		}

		bool EnsureUniformBuffer(wgpu::Device device, GuiRendererWebGPUState& state) {
			if (state.UniformBuffer) return true;
			wgpu::BufferDescriptor desc{};
			desc.size  = 64;
			desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
			desc.label = "guirenderer-viewproj-ubo";
			state.UniformBuffer = device.CreateBuffer(&desc);
			return static_cast<bool>(state.UniformBuffer);
		}

		bool EnsureInstanceBuffer(wgpu::Device device, GuiRendererWebGPUState& state,
			uint32_t neededInstances)
		{
			if (state.InstanceBufferCapacity >= neededInstances && state.InstanceBuffer) return true;
			uint32_t newCapacity = state.InstanceBufferCapacity > 0 ? state.InstanceBufferCapacity : 256;
			while (newCapacity < neededInstances) newCapacity *= 2;

			wgpu::BufferDescriptor desc{};
			desc.size  = static_cast<uint64_t>(newCapacity)
				* sizeof(WebGPUSpriteResources::SpriteInstance);
			desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
			desc.label = "guirenderer-instance-vbo";
			wgpu::Buffer buf = device.CreateBuffer(&desc);
			if (!buf) {
				FNK_CORE_ERROR_TAG("GuiRenderer",
					"Failed to create instance buffer (cap={})", newCapacity);
				return false;
			}
			state.InstanceBuffer         = std::move(buf);
			state.InstanceBufferCapacity = newCapacity;
			return true;
		}

		wgpu::BindGroup ResolveBindGroup(wgpu::Device device, GuiRendererWebGPUState& state,
			TextureHandle handle,
			TextureHandle maskHandle)
		{
			Texture2D* tex = TextureManager::GetTexture(handle);
			Texture2D* maskTex = TextureManager::GetTexture(maskHandle);
			if (!tex || !tex->IsValid() || !maskTex || !maskTex->IsValid()) return nullptr;
			const uint64_t poolId = tex->GetHandle();
			const uint64_t maskPoolId = maskTex->GetHandle();

			const auto lookup = WebGPUBackend::LookupTexture2D(poolId);
			const auto maskLookup = WebGPUBackend::LookupTexture2D(maskPoolId);
			if (!lookup.Valid || !lookup.View || !lookup.Sampler
				|| !maskLookup.Valid || !maskLookup.View || !maskLookup.Sampler) return nullptr;

			const GuiRendererWebGPUState::BindGroupKey key{ poolId, maskPoolId };
			auto cacheIt = state.BindGroupsThisFrame.find(key);
			if (cacheIt != state.BindGroupsThisFrame.end()) {
				const auto& cached = cacheIt->second;
				if (cached.Sampler.Get() == lookup.Sampler.Get()
					&& cached.View.Get() == lookup.View.Get()
					&& cached.MaskSampler.Get() == maskLookup.Sampler.Get()
					&& cached.MaskView.Get() == maskLookup.View.Get()) {
					return cached.Group;
				}
				state.BindGroupsThisFrame.erase(cacheIt);
			}

			wgpu::BindGroupEntry entries[5] = {};
			entries[0].binding = 0;
			entries[0].buffer  = state.UniformBuffer;
			entries[0].offset  = 0;
			entries[0].size    = 64;
			entries[1].binding     = 1;
			entries[1].textureView = lookup.View;
			entries[2].binding = 2;
			entries[2].sampler = lookup.Sampler;
			entries[3].binding     = 3;
			entries[3].textureView = maskLookup.View;
			entries[4].binding = 4;
			entries[4].sampler = maskLookup.Sampler;

			wgpu::BindGroupDescriptor desc{};
			desc.layout     = WebGPUSpriteResources::GetBindGroupLayout();
			desc.entryCount = 5;
			desc.entries    = entries;
			desc.label      = "guirenderer-ui-bindgroup";

			wgpu::BindGroup bg = device.CreateBindGroup(&desc);
			if (!bg) return nullptr;
			state.BindGroupsThisFrame.emplace(key,
				GuiRendererWebGPUState::CachedBindGroup{
					bg, lookup.Sampler, lookup.View, maskLookup.Sampler, maskLookup.View });
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
			// Y flip: NDC +Y up -> screen +Y down for top-left origin.
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
			// Clamp to target bounds so SetScissorRect doesn't trip Dawn's
			// validation (the spec requires x+w <= targetW, y+h <= targetH).
			if (ix >= targetW || iy >= targetH) return out;
			if (ix + iw > targetW) iw = targetW - ix;
			if (iy + ih > targetH) ih = targetH - iy;
			out.X = ix; out.Y = iy; out.W = iw; out.H = ih; out.Valid = true;
			return out;
		}

	}  // anonymous namespace

	void GuiRenderer::Initialize() {
		if (m_IsInitialized) return;

		WebGPUSpriteResources::Acquire();

		m_TextRenderer = std::make_unique<TextRenderer>();
		m_TextRenderer->Initialize();

		(void)GetState(this);  // ensure per-instance state entry exists

		m_IsInitialized = true;
	}

	void GuiRenderer::Shutdown() {
		if (!m_IsInitialized) return;

		g_State.erase(this);

		if (m_TextRenderer) {
			m_TextRenderer->Shutdown();
			m_TextRenderer.reset();
		}
		WebGPUSpriteResources::Release();
		m_IsInitialized = false;
	}

	void GuiRenderer::BeginFrame() {
		if (!m_IsInitialized || m_SkipBeginFrameRender) return;
		SceneManager::ForeachLoadedScene([&](const Scene& scene) { RenderScene(scene); });
	}
	void GuiRenderer::EndFrame() {
		// No-op — Present() flushes from Window::SwapBuffers.
	}

	float GuiRenderer::ComputeWorldUIPixelScale() {
		int canvasW = 0;
		int canvasH = 0;
		if (!ResolveUICanvasSize(canvasW, canvasH)) return 0.01f;
#if FUNKE_WITH_2D
		if (Camera2DComponent* cam = Camera2DComponent::GetMain()) {
			const float worldHeight = 2.0f * cam->GetOrthographicSize() * cam->GetZoom();
			if (worldHeight > 0.0f) return worldHeight / static_cast<float>(canvasH);
		}
#endif
		return 0.01f;
	}

	void GuiRenderer::RenderScene(const Scene& scene) {
		if (!m_IsInitialized) return;
		UpdateUILayout(const_cast<Scene&>(scene));

		int w = 0, h = 0;
		if (!ResolveUICanvasSize(w, h)) return;

		const float halfW = 0.5f * w;
		const float halfH = 0.5f * h;
		const glm::mat4 mvp = glm::ortho(-halfW, +halfW, -halfH, +halfH, -1.0f, 1.0f);
		CollectAndDraw(scene, mvp, halfW, halfH);
	}

	void GuiRenderer::RenderScene(const Scene& scene,
		const glm::mat4& worldVP, float pixelToWorldScale)
	{
		if (!m_IsInitialized) return;
		UpdateUILayout(const_cast<Scene&>(scene));

		int w = 0, h = 0;
		if (!ResolveUICanvasSize(w, h)) return;

		const glm::mat4 uiToWorld = glm::scale(glm::mat4(1.0f),
			glm::vec3(pixelToWorldScale, pixelToWorldScale, 1.0f));
		const glm::mat4 mvp = worldVP * uiToWorld;

		const float halfW = 0.5f * w;
		const float halfH = 0.5f * h;
		CollectAndDraw(scene, mvp, halfW, halfH);
	}

	void GuiRenderer::CollectAndDraw(const Scene& scene, const glm::mat4& mvp,
		float halfW, float halfH)
	{
		// MUST call here: UI text doesn't go through TextRenderer::RenderScene, so async bakes would never publish on UI-only scenes.
		FontManager::PollAsync();

		entt::registry& registry = const_cast<entt::registry&>(scene.GetRegistry());

		// ── 1. Build hierarchy draw order ────────────────────────────
		m_DrawOrder.clear();
		UIDrawOrder::Build(registry, m_DrawOrder);
		s_OrderLookup.Rebuild(m_DrawOrder);
		BuildClipTable(registry, m_DrawOrder);

		int counter = m_DrawOrder.empty()
			? 0
			: m_DrawOrder.back().second + UIDrawOrder::k_HierarchyStep;

		// ── 2. Image instances ───────────────────────────────────────
		m_InstancesScratch.clear();
		m_InstancesScratch.reserve(m_DrawOrder.size());

		// Replaces `whole` by 9-slice or tiled quads sharing its sort keys and clip; false = draw it as one quad.
		std::vector<NineSliceQuad> tiles;
		auto emitSlicedOrTiled = [&](const Instance44& whole, const ImageComponent& image,
			const RectTransform2DComponent& rect, Vec2 size) {
			const Texture2D* tex = TextureManager::GetTexture(image.TextureHandle);
			if (!tex) return false;
			const Vec2 borderScale = ResolveBorderScale(image.BorderScale,
				rect.ResolvedValid ? rect.Scale : rect.LocalScale);
			NineSliceBorder border{ image.BorderLeft, image.BorderBottom, image.BorderRight, image.BorderTop };
			if (border.Left == 0.0f && border.Bottom == 0.0f && border.Right == 0.0f && border.Top == 0.0f) {
				const SpriteBorder fromMeta = ResolveSpriteBorder(image.TextureAssetId, image.SpriteName);
				border = { static_cast<float>(fromMeta.Left), static_cast<float>(fromMeta.Bottom),
					static_cast<float>(fromMeta.Right), static_cast<float>(fromMeta.Top) };
			}
			const Vec2 spritePixels = SpritePixelSize(whole, *tex);
			int count = 0;
			const NineSliceQuad* quads = nullptr;
			std::array<NineSliceQuad, 9> nine;
			if (image.Type == ImageType::Tiled) {
				count = BuildTiled(size, spritePixels, border, borderScale, whole.UvRect, tiles);
				quads = tiles.data();
			}
			else {
				count = BuildNineSlice(size, spritePixels, border, borderScale, whole.UvRect, nine);
				quads = nine.data();
			}
			if (count == 0) return false;
			const float c = std::cos(whole.Rotation);
			const float s = std::sin(whole.Rotation);
			for (int i = 0; i < count; ++i) {
				Instance44 part = whole;
				part.Position = { whole.Position.x + c * quads[i].Center.x - s * quads[i].Center.y,
					whole.Position.y + s * quads[i].Center.x + c * quads[i].Center.y };
				part.Scale = quads[i].Size;
				part.UvRect = quads[i].Uv;
				m_InstancesScratch.push_back(part);
			}
			return true;
		};

		for (const auto& [entity, drawIndex] : m_DrawOrder) {
			if (!registry.all_of<RectTransform2DComponent, ImageComponent>(entity)) continue;
			if (const auto* mask = registry.try_get<MaskComponent>(entity)) {
				if (!mask->ShowMaskGraphic) continue;
			}

			const auto& rect = registry.get<RectTransform2DComponent>(entity);
			const auto& image = registry.get<ImageComponent>(entity);

			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();
			const Vec2 size{ tr.x - bl.x, tr.y - bl.y };
			Vec2 spritePos{ bl.x + size.x * 0.5f, bl.y + size.y * 0.5f };

			if (rect.Rotation != 0.0f && rect.ResolvedValid) {
				const Vec2 fromPivot{
					spritePos.x - rect.ResolvedPivot.x,
					spritePos.y - rect.ResolvedPivot.y
				};
				const float c = std::cos(rect.Rotation);
				const float s = std::sin(rect.Rotation);
				spritePos = {
					rect.ResolvedPivot.x + c * fromPivot.x - s * fromPivot.y,
					rect.ResolvedPivot.y + s * fromPivot.x + c * fromPivot.y
				};
			}

			Instance44 inst(
				spritePos,
				size,
				rect.Rotation,
				image.Color,
				image.TextureHandle,
				image.SortingOrder,
				image.SortingLayer,
				static_cast<std::uint32_t>(drawIndex));
			ApplyImageSliceToInstance(inst, image);
			inst.Material = image.Material;
			ApplyMaskClipToInstance(ClipForEntity(entity), inst);
			if (image.Type != ImageType::Simple && emitSlicedOrTiled(inst, image, rect, size)) continue;
			m_InstancesScratch.push_back(inst);
		}

		// ── 2b. Circular slider background + fill ───────────────────
		const TextureHandle defaultWhite  = TextureManager::GetDefaultTexture(DefaultTexture::Square);
		const TextureHandle defaultCircle = TextureManager::GetDefaultTexture(DefaultTexture::Circle);
		const TextureHandle bgTexture     = defaultCircle.IsValid() ? defaultCircle : defaultWhite;

		auto emitFillSlice = [&](const Vec2& centre, float parentRot,
			const Color& color, float startRad, float sweepRad,
			float outerRadius, int segments,
			std::uint32_t drawIndex, std::int16_t sortOrder, std::uint8_t sortLayer,
			const UIMaskClip& clip)
		{
			if (segments < 2) segments = 2;
			if (outerRadius <= 0.0f) return;
			if (std::abs(sweepRad) < 1e-4f) return;

			const float segAngle = sweepRad / static_cast<float>(segments);
			const float chord = 2.0f * outerRadius * std::sin(0.5f * std::abs(segAngle));
			const float quadWidth = chord * 1.02f;

			for (int i = 0; i < segments; ++i) {
				const float midAngle = startRad + (static_cast<float>(i) + 0.5f) * segAngle + parentRot;
				const float midRadius = outerRadius * 0.5f;
				const float dx = std::cos(midAngle) * midRadius;
				const float dy = std::sin(midAngle) * midRadius;
				Instance44 inst(
					Vec2{ centre.x + dx, centre.y + dy },
					Vec2{ quadWidth, outerRadius },
					0.0f,
					color,
					defaultWhite,
					sortOrder,
					sortLayer,
					drawIndex);
				inst.Rotation = midAngle + 1.5707963267948966f;
				ApplyMaskClipToInstance(clip, inst);
				m_InstancesScratch.push_back(inst);
			}
		};

		for (const auto& [entity, drawIndex] : m_DrawOrder) {
			if (!registry.all_of<RectTransform2DComponent, CircularSliderComponent>(entity)) continue;
			const auto& rect = registry.get<RectTransform2DComponent>(entity);
			const auto& cs   = registry.get<CircularSliderComponent>(entity);

			const Vec2 size = rect.GetSize();
			const float outerRadius = std::min(size.x, size.y) * 0.5f;
			if (outerRadius <= 0.0f) continue;

			const Vec2 centre = rect.GetCenter();
			const float parentRot = rect.Rotation;

			constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
			const float startRad = cs.StartAngleDegrees * kDeg2Rad;
			const float sweepRad = cs.SweepDegrees * kDeg2Rad
				* (cs.Clockwise ? -1.0f : 1.0f);

			const float range = cs.MaxValue - cs.MinValue;
			const float t = (range != 0.0f)
				? std::clamp((cs.Value - cs.MinValue) / range, 0.0f, 1.0f)
				: 0.0f;

			const UIMaskClip& clip = ClipForEntity(entity);

			{
				Instance44 bg(
					centre,
					Vec2{ outerRadius * 2.0f, outerRadius * 2.0f },
					parentRot,
					cs.BackgroundColor,
					bgTexture,
					cs.SortingOrder, cs.SortingLayer,
					static_cast<std::uint32_t>(drawIndex));
				ApplyMaskClipToInstance(clip, bg);
				m_InstancesScratch.push_back(bg);
			}

			if (t > 0.0f) {
				// Cap matches the inspector range (8..256). The fill is a fan of chord-wide
				// quads from the centre, so a low count leaves a blocky pinwheel at the apex;
				// honouring the configured RingSegments lets the user smooth it out (the old
				// hard cap of 32 ignored anything higher).
				const int totalSegments = std::max(8, std::min(cs.RingSegments, 256));
				const int fillSegments = std::max(1,
					static_cast<int>(std::round(static_cast<float>(totalSegments) * t)));
				emitFillSlice(centre, parentRot, cs.FillColor,
					startRad, sweepRad * t, outerRadius, fillSegments,
					static_cast<std::uint32_t>(drawIndex) + 1u,
					// Same sort key as the background; the +1 drawIndex above keeps the fill on top.
					cs.SortingOrder, cs.SortingLayer,
					clip);
			}
		}

		// ── 3. UI text instances (collect into m_TextScratch; submitted later in the merge walk) ──
		m_TextScratch.clear();
		m_TextScratch.reserve(m_DrawOrder.size());

		// Input-field text children scroll horizontally and clip to their rect so an
		// overflowing field keeps the caret visible instead of spilling text outside.
		std::unordered_map<EntityHandle, const InputFieldComponent*> inputFieldTextChildren;
		for (auto&& [ifEntity, ifField] : registry.view<InputFieldComponent>().each()) {
			if (ifField.TextEntity != entt::null) inputFieldTextChildren.emplace(ifField.TextEntity, &ifField);
		}

		for (const auto& [entity, drawIndex] : m_DrawOrder) {
			if (!registry.all_of<RectTransform2DComponent, TextRendererComponent>(entity)) continue;
			auto& text = registry.get<TextRendererComponent>(entity);
			if (text.Text.empty()) continue;

			const auto& rect = registry.get<RectTransform2DComponent>(entity);

			const float effectivePixelSize = text.FontSize * std::max(0.01f, std::abs(rect.Scale.x));
			Font* font = TextRenderer::ResolveFontAtPixelSize(text, effectivePixelSize);
			if (!font || !font->IsLoaded()) continue;
			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();
			const Vec2 size{ tr.x - bl.x, tr.y - bl.y };

			const float uniformScale = rect.Scale.x;
			const float bakedSize = font->GetPixelSize() > 0.0f ? font->GetPixelSize() : text.FontSize;
			const float drawScale = (text.FontSize / bakedSize) * uniformScale;

			const float marginLeftWorld   = text.Margin.x * uniformScale;
			const float marginTopWorld    = text.Margin.y * uniformScale;
			const float marginRightWorld  = text.Margin.z * uniformScale;
			const float marginBottomWorld = text.Margin.w * uniformScale;

			// Pick the baseline of the alignment-anchoring line within the rect;
			// EmitText then distributes the rest of a multi-line block from here.
			// Top: first line's ascent meets the top inset. Bottom: last line's
			// descender (GetDescent() is negative) rests on the bottom inset.
			const float ascentWorld  = font->GetAscent()  * drawScale;
			const float descentWorld = font->GetDescent() * drawScale;
			float baselineY;
			switch (text.VAlign) {
			case TextVerticalAlignment::Top:
				baselineY = tr.y - marginTopWorld - ascentWorld;
				break;
			case TextVerticalAlignment::Bottom:
				baselineY = bl.y + marginBottomWorld - descentWorld;
				break;
			case TextVerticalAlignment::Middle:
			default:
				baselineY = bl.y + size.y * 0.5f
					- text.FontSize * 0.35f * uniformScale
					- marginTopWorld;
				break;
			}

			float originX;
			switch (text.HAlign) {
			case TextAlignment::Center: originX = bl.x + size.x * 0.5f;                      break;
			case TextAlignment::Right:  originX = tr.x - 4.0f * uniformScale - marginRightWorld; break;
			case TextAlignment::Left:
			default:                    originX = bl.x + 4.0f * uniformScale + marginLeftWorld;  break;
			}

			TextDrawCmd cmd;
			cmd.FontPtr = font;
			cmd.Text = std::string_view(text.Text);
			cmd.X = originX;
			cmd.Y = baselineY;
			cmd.Scale = drawScale;
			cmd.LetterSpacing = text.LetterSpacing;
			cmd.LineSpacing = text.LineSpacing;
			cmd.Tint = text.Color;
			cmd.Align = text.HAlign;
			cmd.VAlign = text.VAlign;
			cmd.Wrap = text.WrapMode;
			if (text.WrapMode != TextWrapMode::None) {
				const float padPixels = 8.0f * uniformScale;
				const float marginPixels = marginLeftWorld + marginRightWorld;
				// EmitText accumulates raw `g->XAdvance` (baked-atlas pixel
				// units, unscaled) and compares against wrapWidthPixels — so
				// the available rect width has to be converted from UI
				// pixels back to baked-glyph pixels via /drawScale, not
				// /uniformScale. With the wrong divisor the wrap point
				// moves by (bakedSize / text.FontSize): a 16pt-on-32pt-bake
				// wrapped roughly twice as early as the rect actually
				// allowed, a 64pt-on-32pt-bake overflowed.
				cmd.WrapWidthPixels = drawScale > 0.0f
					? std::max(0.0f, (size.x - padPixels - marginPixels) / drawScale)
					: 0.0f;
			}
			cmd.SortingOrder = text.SortingOrder;
			cmd.SortingLayer = text.SortingLayer;
			cmd.DrawIndex = static_cast<std::uint32_t>(drawIndex);
			ApplyMaskClipToText(ClipForEntity(entity), cmd);
			cmd.Rotation = rect.Rotation;
			cmd.Pivot = rect.ResolvedPivot;
			cmd.SetEffects(text, uniformScale);
			auto ifIt = inputFieldTextChildren.find(entity);
			const InputFieldComponent* ifField = (ifIt != inputFieldTextChildren.end()) ? ifIt->second : nullptr;
			bool clipToField = false;
			if (ifField && !ifField->Multiline) {
				cmd.X -= ifField->ScrollOffsetX;
				clipToField = true;
			}
			else if (ifField && ifField->Multiline && ifField->VerticalScrollbarEntity != entt::null) {
				// Scrollable multiline: top-anchor line 0 at the top inset and shift the block
				// up by ScrollOffsetY; clip to the field so scrolled-out rows don't spill out.
				cmd.Y = (tr.y - marginTopWorld - ascentWorld) + ifField->ScrollOffsetY;
				cmd.VAlign = TextVerticalAlignment::Top;
				clipToField = true;
			}
			if (clipToField) {
				Vec2 fieldClipMin = bl;
				Vec2 fieldClipMax = tr;
				if (cmd.HasClip) {
					fieldClipMin.x = std::max(fieldClipMin.x, cmd.ClipMin.x);
					fieldClipMin.y = std::max(fieldClipMin.y, cmd.ClipMin.y);
					fieldClipMax.x = std::min(fieldClipMax.x, cmd.ClipMax.x);
					fieldClipMax.y = std::min(fieldClipMax.y, cmd.ClipMax.y);
				}
				cmd.HasClip = true;
				cmd.ClipMin = fieldClipMin;
				cmd.ClipMax = fieldClipMax;
			}
			m_TextScratch.push_back(cmd);
		}

		// ── 4. Dropdown popups ───────────────────────────────────────
		Application* app = Application::GetInstance();
		const Vec2 mousePosition = app ? app->GetInput().GetMousePosition() : Vec2{ 0, 0 };
		const Vec2 mouseUi{ mousePosition.x - halfW, halfH - mousePosition.y };
		const bool mouseHeld = app && app->GetInput().GetMouse(MouseButton::Left);

		auto resolveOptionColor = [](const DropdownComponent& dd, bool hovered, bool pressed, bool selected) -> Color {
			if (pressed && dd.OptionPressedColor.a > 0.f)  return dd.OptionPressedColor;
			if (hovered && dd.OptionHoverColor.a > 0.f)    return dd.OptionHoverColor;
			if (selected && dd.OptionSelectedColor.a > 0.f) return dd.OptionSelectedColor;
			if (dd.OptionNormalColor.a > 0.f)              return dd.OptionNormalColor;
			return dd.PopupBackgroundColor;
		};

		auto popupView = registry.view<RectTransform2DComponent, DropdownComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, rect, dd] : popupView.each()) {
			if (!dd.IsOpen || dd.Options.empty()) continue;

			const Vec2 bl = rect.GetBottomLeft();
			const Vec2 tr = rect.GetTopRight();
			const float width = tr.x - bl.x;
			const float topOfPopup = bl.y;

			Font* dropdownFont = nullptr;
			float fontPx = 16.0f;
			if (dd.LabelEntity != entt::null && registry.valid(dd.LabelEntity)
				&& registry.all_of<TextRendererComponent>(dd.LabelEntity))
			{
				auto& labelText = registry.get<TextRendererComponent>(dd.LabelEntity);
				dropdownFont = TextRenderer::ResolveFont(labelText);
				fontPx = labelText.FontSize;
			}
			else {
				FontHandle dh = FontManager::GetDefaultFont();
				dropdownFont = FontManager::GetFont(dh);
			}
			// Option-row quads emit regardless (textureless); labels only when the font is loaded.
			const float bakedSize = (dropdownFont && dropdownFont->GetPixelSize() > 0.0f)
				? dropdownFont->GetPixelSize() : fontPx;
			const float uniformScale = rect.Scale.x;
			const float textScale = (fontPx / bakedSize) * uniformScale;
			// Rows are authored in reference-res units like the button — scale with the canvas.
			// A raw OptionRowHeight was a fixed pixel height that only matched at viewport==reference
			// (looked right in the Game view, wrong in the Scene view + standalone).
			const float rowH = dd.OptionRowHeight * uniformScale;

			const int popupDraw = ++counter;
			for (int i = 0; i < static_cast<int>(dd.Options.size()); ++i) {
				const float rowTop = topOfPopup - rowH * static_cast<float>(i);
				const float rowBottom = rowTop - rowH;
				const Vec2 rowBL{ bl.x, rowBottom };
				const Vec2 rowSize{ width, rowH };

				const bool hovered = mouseUi.x >= rowBL.x && mouseUi.x <= rowBL.x + rowSize.x
					&& mouseUi.y >= rowBL.y && mouseUi.y <= rowBL.y + rowSize.y;
				const bool pressed = hovered && mouseHeld;
				const bool selected = (i == dd.SelectedIndex);

				const Color rowColor = resolveOptionColor(dd, hovered, pressed, selected);

				const Vec2 rowCenter{ rowBL.x + rowSize.x * 0.5f, rowBL.y + rowSize.y * 0.5f };
				m_InstancesScratch.emplace_back(
					rowCenter,
					rowSize,
					0.0f,
					rowColor,
					TextureHandle{},
					static_cast<short>((popupDraw + i) & 0x7fff),
					static_cast<std::uint8_t>(10),
					static_cast<std::uint32_t>(popupDraw + i));

				if (dropdownFont && dropdownFont->IsLoaded()) {
					TextDrawCmd cmd;
					cmd.FontPtr = dropdownFont;
					cmd.Text = std::string_view(dd.Options[i]);
					cmd.X = rowBL.x + 8.0f * uniformScale;
					cmd.Y = rowBL.y + rowSize.y * 0.5f - fontPx * 0.35f * uniformScale;
					cmd.Scale = textScale;
					cmd.LetterSpacing = 0.0f;
					cmd.Tint = dd.OptionTextColor;
					cmd.Align = TextAlignment::Left;
					cmd.SortingOrder = static_cast<int16_t>((popupDraw + i) & 0x7fff);
					cmd.SortingLayer = 11;
					cmd.DrawIndex = static_cast<std::uint32_t>(popupDraw + i);
					m_TextScratch.push_back(cmd);
				}
			}
			counter += static_cast<int>(dd.Options.size());
		}

		// ── 4.5 Input field overlays (selection + caret) ────────────
		const float elapsedSeconds = Time::GetElapsedTime();

		// At least one logical pixel: a 1 px caret is invisible on a ~400 dpi phone.
		const Window* caretWindow = Application::GetWindow();
		const float minCaretWidth = std::max(1.0f, caretWindow ? caretWindow->GetDisplayScale() : 1.0f);

		auto inputView = registry.view<RectTransform2DComponent, InputFieldComponent>(entt::exclude<DisabledTag>);
		for (auto&& [entity, fieldRect, field] : inputView.each()) {
			const bool hasSelection = field.SelectionAnchorBytePos != field.CaretBytePos;
			if (!field.IsFocused && !hasSelection) continue;

			InputFieldOverlayLayout layout = ResolveInputFieldOverlay(registry, field);
			if (!layout.Valid) continue;  // font failed to load

			const int* fieldIt = s_OrderLookup.DrawIndexOf(entity);
			const int* textIt = s_OrderLookup.DrawIndexOf(field.TextEntity);
			if (!fieldIt || !textIt) continue;

			const std::uint32_t fieldDI = static_cast<std::uint32_t>(*fieldIt);
			const std::uint32_t textDI = static_cast<std::uint32_t>(*textIt);

			// Overlays join the sorting bucket of what they sit on: with order 0 they vanished behind any
			// raised panel, e.g. a field in a popup. Selection sits over the field's image, the caret over the text.
			const auto* textSort = registry.try_get<TextRendererComponent>(field.TextEntity);
			const auto* fieldImageSort = registry.try_get<ImageComponent>(entity);
			const std::int16_t caretOrder = textSort ? textSort->SortingOrder : std::int16_t{ 0 };
			const std::uint8_t caretLayer = textSort ? textSort->SortingLayer : std::uint8_t{ 0 };
			const std::int16_t selectionOrder = fieldImageSort ? fieldImageSort->SortingOrder : caretOrder;
			const std::uint8_t selectionLayer = fieldImageSort ? fieldImageSort->SortingLayer : caretLayer;

			const UIMaskClip& fieldClip = ClipForEntity(entity);
			// Single-line fields also clip the caret/selection to the field's text rect so
			// scrolled-out glyphs and the highlight don't spill past the edges. Multiline
			// fields don't clip (they show all lines); inherited masks still apply.
			UIMaskClip fieldClipRect = fieldClip;
			if (!field.Multiline || field.VerticalScrollbarEntity != entt::null) {
				if (!fieldClipRect.HasClip) {
					fieldClipRect.HasClip = true;
					fieldClipRect.ClipMin = layout.BL;
					fieldClipRect.ClipMax = layout.TR;
				} else {
					fieldClipRect.ClipMin.x = std::max(fieldClipRect.ClipMin.x, layout.BL.x);
					fieldClipRect.ClipMin.y = std::max(fieldClipRect.ClipMin.y, layout.BL.y);
					fieldClipRect.ClipMax.x = std::min(fieldClipRect.ClipMax.x, layout.TR.x);
					fieldClipRect.ClipMax.y = std::min(fieldClipRect.ClipMax.y, layout.TR.y);
				}
			}

			// Caret/selection rows must follow the text's vertical alignment so they
			// land on the lines TextRenderer actually draws. Reproduce the text-loop
			// baseline math per alignment, then lift to the caret box's centre by the
			// same cap-height offset the Middle case bakes in. Margins are ignored to
			// match the rest of this overlay (input-field text uses zero margin).
			// Returns the caret centre Y of the first (top) line for a `lineCount` block.
			const float overlayLineHeight = (layout.FontPtr->GetLineHeight() + layout.LineSpacing) * layout.Scale;
			const float overlayCapOffset  = layout.FontSize * 0.35f;
			auto firstLineCaretCenterY = [&](int lineCount) -> float {
				const float ascentWorld  = layout.FontPtr->GetAscent()  * layout.Scale;
				const float descentWorld = layout.FontPtr->GetDescent() * layout.Scale; // negative (below baseline)
				const float blockSpan = static_cast<float>(std::max(0, lineCount - 1)) * overlayLineHeight;
				// Scrollable multiline: top-anchor + shift up by ScrollOffsetY (matches the text
				// render and ResolveInputTextLayout); per-VAlign anchoring otherwise.
				if (field.Multiline && field.VerticalScrollbarEntity != entt::null) {
					return layout.TR.y - ascentWorld + overlayCapOffset + field.ScrollOffsetY;
				}
				switch (layout.VAlign) {
				case TextVerticalAlignment::Top:
					return layout.TR.y - ascentWorld + overlayCapOffset;
				case TextVerticalAlignment::Bottom:
					return layout.BL.y - descentWorld + overlayCapOffset + blockSpan;
				case TextVerticalAlignment::Middle:
				default:
					return layout.BL.y + (layout.TR.y - layout.BL.y) * 0.5f + blockSpan * 0.5f;
				}
			};

			// ── Multiline overlay ───────────────────────────────────────
			// Visual lines come from TextRenderer's wrap (hard '\n' + soft word/char wrap),
			// so the caret/selection track the rows actually drawn. Each line steps down by
			// one line height from the alignment-anchored first line. Secret masking is
			// intentionally skipped — secret multiline is nonsensical.
			if (field.Multiline) {
				const float lineHeight = (layout.FontPtr->GetLineHeight() + layout.LineSpacing) * layout.Scale;
				const float verticalPad = 2.0f;
				const float halfHeight = layout.FontSize * 0.5f + verticalPad;

				std::vector<std::pair<int, int>> lines = InputVisualLines(layout, field.Text);

				// Anchor line 0 to match TextRenderer's per-alignment baseline; each
				// subsequent line steps down by one line height.
				const float firstCenterY = firstLineCaretCenterY(static_cast<int>(lines.size()));
				auto lineCenterY = [&](int lineIdx) {
					return firstCenterY - static_cast<float>(lineIdx) * lineHeight;
				};
				// Absolute X of a global byte offset that lies within `lineIdx`.
				auto byteToAbsX = [&](int lineIdx, int byte) {
					const auto [ls, le] = lines[lineIdx];
					std::string_view lineView{ field.Text.data() + ls, static_cast<size_t>(le - ls) };
					const float lineW = MeasureUpToByteUI(*layout.FontPtr, lineView,
						le - ls, layout.LetterSpacing) * layout.Scale;
					float shift = 0.0f;
					if (layout.Align == TextAlignment::Center) shift = -lineW * 0.5f;
					else if (layout.Align == TextAlignment::Right) shift = -lineW;
					const float w = MeasureUpToByteUI(*layout.FontPtr, lineView,
						std::clamp(byte - ls, 0, le - ls), layout.LetterSpacing) * layout.Scale;
					return layout.OriginX + shift + w - field.ScrollOffsetX;
				};

				if (hasSelection) {
					const int lo = std::min(field.SelectionAnchorBytePos, field.CaretBytePos);
					const int hi = std::max(field.SelectionAnchorBytePos, field.CaretBytePos);
					for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
						const auto [ls, le] = lines[li];
						const int a = std::max(lo, ls);
						const int b = std::min(hi, le);
						const bool spansNewline = hi > le; // selection continues onto the next line
						if (a > b) continue;
						if (a == b && !spansNewline) continue;
						const float xA = byteToAbsX(li, a);
						float xB = byteToAbsX(li, b);
						if (spansNewline) xB += std::max(2.0f, layout.FontSize * 0.25f);
						const float selW = std::max(0.0f, xB - xA);
						if (selW <= 0.0f) continue;
						const Vec2 selCenter{ xA + selW * 0.5f, lineCenterY(li) };
						const Vec2 selSize{ selW, halfHeight * 2.0f };
						Instance44 selInst(selCenter, selSize, 0.0f, field.SelectionColor,
							TextureHandle{}, selectionOrder, selectionLayer, fieldDI + 1u);
						ApplyMaskClipToInstance(fieldClipRect, selInst);
						m_InstancesScratch.push_back(selInst);
					}
				}

				const bool caretBlinkOnMl = (field.CaretBlinkRate <= 0.0f)
					? true
					: (std::fmod(elapsedSeconds * field.CaretBlinkRate, 1.0f) < 0.5f);
				if (field.IsFocused && caretBlinkOnMl && !field.IsReadOnly) {
					int caretLine = VisualLineOfByte(lines, std::clamp(field.CaretBytePos, 0, static_cast<int>(field.Text.size())));
					caretLine = std::clamp(caretLine, 0, static_cast<int>(lines.size()) - 1);
					const float caretX = byteToAbsX(caretLine, field.CaretBytePos);
					const float caretWidth = std::max(minCaretWidth, field.CaretWidth * fieldRect.Scale.x);
					const Vec2 caretCenter{ caretX + caretWidth * 0.5f, lineCenterY(caretLine) };
					const Vec2 caretSize{ caretWidth, halfHeight * 2.0f };
					Instance44 caretInst(caretCenter, caretSize, 0.0f, field.CaretColor,
						TextureHandle{}, caretOrder, caretLayer, textDI + 1u);
					ApplyMaskClipToInstance(fieldClipRect, caretInst);
					m_InstancesScratch.push_back(caretInst);
				}
				continue;
			}

			const auto& tc = registry.get<TextRendererComponent>(field.TextEntity);
			std::string secretMaskBuffer;
			if (field.IsSecret && !field.Text.empty()) {
				secretMaskBuffer.reserve(field.Text.size());
				int mIdx = 0;
				const int mN = static_cast<int>(field.Text.size());
				while (mIdx < mN) {
					std::uint32_t cp; int len;
					if (!Utf8DecodeAt(field.Text, mIdx, cp, len)) break;
					secretMaskBuffer.push_back('*');
					mIdx += len;
				}
			}
			const std::string& measureText = field.IsSecret ? secretMaskBuffer : field.Text;

			auto convertByte = [&](int byteInOriginal) -> int {
				if (!field.IsSecret) return byteInOriginal;
				int idx = 0;
				int count = 0;
				const int n = static_cast<int>(field.Text.size());
				if (byteInOriginal > n) byteInOriginal = n;
				while (idx < byteInOriginal) {
					std::uint32_t cp; int len;
					if (!Utf8DecodeAt(field.Text, idx, cp, len)) break;
					idx += len;
					++count;
				}
				return count;
			};

			const float verticalPad = 2.0f;
			const float halfHeight = layout.FontSize * 0.5f + verticalPad;
			const float centerY = firstLineCaretCenterY(1);

			const float fullLineW = MeasureUpToByteUI(*layout.FontPtr, measureText,
				static_cast<int>(measureText.size()), layout.LetterSpacing) * layout.Scale;
			float alignShift = 0.0f;
			if (layout.Align == TextAlignment::Center) alignShift = -fullLineW * 0.5f;
			else if (layout.Align == TextAlignment::Right) alignShift = -fullLineW;

			auto byteToAbsX = [&](int byte) {
				const float w = MeasureUpToByteUI(*layout.FontPtr, measureText,
					convertByte(byte), layout.LetterSpacing) * layout.Scale;
				return layout.OriginX + alignShift + w - field.ScrollOffsetX;
			};

			if (hasSelection) {
				const int lo = std::min(field.SelectionAnchorBytePos, field.CaretBytePos);
				const int hi = std::max(field.SelectionAnchorBytePos, field.CaretBytePos);
				const float xLo = byteToAbsX(lo);
				const float xHi = byteToAbsX(hi);
				const float selW = std::max(0.0f, xHi - xLo);
				if (selW > 0.0f) {
					const Vec2 selCenter{ xLo + selW * 0.5f, centerY };
					const Vec2 selSize{ selW, halfHeight * 2.0f };
					Instance44 selInst(
						selCenter, selSize, 0.0f,
						field.SelectionColor,
						TextureHandle{},
						selectionOrder, selectionLayer,
						fieldDI + 1u);
					ApplyMaskClipToInstance(fieldClipRect, selInst);
					m_InstancesScratch.push_back(selInst);
				}
			}

			const bool caretBlinkOn = (field.CaretBlinkRate <= 0.0f)
				? true
				: (std::fmod(elapsedSeconds * field.CaretBlinkRate, 1.0f) < 0.5f);
			if (field.IsFocused && caretBlinkOn && !field.IsReadOnly) {
				const float caretX = byteToAbsX(field.CaretBytePos);
				const float caretWidth = std::max(minCaretWidth, field.CaretWidth * fieldRect.Scale.x);
				const Vec2 caretCenter{ caretX + caretWidth * 0.5f, centerY };
				const Vec2 caretSize{ caretWidth, halfHeight * 2.0f };
				Instance44 caretInst(
					caretCenter, caretSize, 0.0f,
					field.CaretColor,
					TextureHandle{},
					caretOrder, caretLayer,
					textDI + 1u);
				ApplyMaskClipToInstance(fieldClipRect, caretInst);
				m_InstancesScratch.push_back(caretInst);
			}
			(void)tc;
		}

		// ── 5. Sort image and text instances by unified key ──────────
		std::sort(m_InstancesScratch.begin(), m_InstancesScratch.end(),
			[](const Instance44& a, const Instance44& b) {
				if (a.SortingLayer != b.SortingLayer)
					return a.SortingLayer < b.SortingLayer;
				if (a.SortingOrder != b.SortingOrder)
					return a.SortingOrder < b.SortingOrder;
				return a.DrawIndex < b.DrawIndex;
			});
		std::sort(m_TextScratch.begin(), m_TextScratch.end(),
			[](const TextDrawCmd& a, const TextDrawCmd& b) {
				if (a.SortingLayer != b.SortingLayer)
					return a.SortingLayer < b.SortingLayer;
				if (a.SortingOrder != b.SortingOrder)
					return a.SortingOrder < b.SortingOrder;
				return a.DrawIndex < b.DrawIndex;
			});

		if (m_InstancesScratch.empty() && m_TextScratch.empty()) {
			return;
		}

		// ── WebGPU submit setup ──────────────────────────────────────
		auto& state = GetState(this);
		// Clear per-frame, not per-call: the editor calls RenderScene multiple times per frame and per-call clearing forced CreateBindGroup for every texture on every call.
		const uint64_t currentFrame = app ? Time::GetFrameCount() : state.LastClearFrameCount;
		if (state.LastClearFrameCount != currentFrame) {
			state.BindGroupsThisFrame.clear();
			state.LastClearFrameCount = currentFrame;
		}

		auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!target.Valid) return;
		if (!WebGPUSpriteResources::IsReady()) return;

		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::Queue  queue  = WebGPUBackend::GetQueue();
		if (!device || !queue) return;

		const size_t n = m_InstancesScratch.size();
		wgpu::RenderPipeline pipeline;
		if (n > 0) {
			pipeline = WebGPUSpriteResources::GetSpritePipeline(
				target.ColorFormat, target.HasDepth);
			if (!pipeline) {
				FNK_CORE_WARN_TAG("GuiRenderer",
					"No pipeline for color-format {} hasDepth={} — image quads skipped",
					static_cast<int>(target.ColorFormat), target.HasDepth);
				// Don't return — text might still want to render below.
			} else {
				if (!EnsureUniformBuffer(device, state)) return;
				queue.WriteBuffer(state.UniformBuffer, 0, glm::value_ptr(mvp), 64);

				if (!EnsureInstanceBuffer(device, state, static_cast<uint32_t>(n))) return;
				state.EncodedScratch.resize(n);
				for (size_t k = 0; k < n; ++k) {
					WebGPUSpriteResources::EncodeInstance44(m_InstancesScratch[k], state.EncodedScratch[k]);
				}
				queue.WriteBuffer(state.InstanceBuffer, 0,
					state.EncodedScratch.data(),
					n * sizeof(WebGPUSpriteResources::SpriteInstance));
			}
		}

		const TextureHandle defaultTexture = TextureManager::GetDefaultTexture(DefaultTexture::Square);
		auto resolveHandle = [&](TextureHandle h) {
			return TextureManager::IsValid(h) ? h : defaultTexture;
		};
		auto resolveMaskHandle = [&](const Instance44& inst) {
			return inst.HasTextureMask && TextureManager::IsValid(inst.MaskTextureHandle)
				? inst.MaskTextureHandle
				: defaultTexture;
		};

		const bool anyCustom = std::any_of(m_InstancesScratch.begin(), m_InstancesScratch.end(),
			[](const Instance44& instance) { return instance.Material != 0; });
		if (anyCustom && !state.CustomImages && !state.CustomImagesFailed) {
			state.CustomImages = std::make_unique<CustomSpriteDrawer>();
			if (!state.CustomImages->Initialize("guirenderer-custom", false)) {
				FNK_CORE_WARN_TAG("GuiRenderer", "Custom shader resources unavailable; image materials draw with the standard shader");
				state.CustomImages.reset();
				state.CustomImagesFailed = true;
			}
		}
		const bool drawCustom = anyCustom && state.CustomImages;
		if (drawCustom) {
			state.CustomImages->WriteGlobals(CustomMaterialBindings::MakeGlobals(glm::mat4(1.0f), mvp, glm::vec3(0.0f),
				target.Width, target.Height));
		}

		auto submitImagePhase = [&](size_t start, size_t end) {
			if (!pipeline || start >= end) return;

			wgpu::CommandEncoder enc = WebGPUBackend::GetFrameEncoder();
			if (!enc) return;

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
			passDesc.label                  = "guirenderer-ui";
			passDesc.colorAttachmentCount   = 1;
			passDesc.colorAttachments       = &colorAtt;
			passDesc.depthStencilAttachment = target.HasDepth ? &depthAtt : nullptr;

			wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&passDesc);
			WebGPUBackend::ApplyCachedViewportToPass(pass);
			pass.SetPipeline(pipeline);
			wgpu::RenderPipeline boundPipeline = pipeline;
			pass.SetVertexBuffer(0, WebGPUSpriteResources::GetQuadVertexBuffer());
			pass.SetVertexBuffer(1, state.InstanceBuffer);
			pass.SetIndexBuffer(WebGPUSpriteResources::GetQuadIndexBuffer(),
				wgpu::IndexFormat::Uint16);
			pass.SetScissorRect(0, 0, target.Width, target.Height);

			bool scissorIsFull = true;
			size_t i = start;
			while (i < end) {
				const TextureHandle runHandle = resolveHandle(m_InstancesScratch[i].TextureHandle);
				const TextureHandle runMaskHandle = resolveMaskHandle(m_InstancesScratch[i]);
				const bool runHasClip = m_InstancesScratch[i].HasClip;
				const Vec2 runClipMin = m_InstancesScratch[i].ClipMin;
				const Vec2 runClipMax = m_InstancesScratch[i].ClipMax;
				const uint64_t runMaterial = drawCustom ? m_InstancesScratch[i].Material : 0;

				size_t runEnd = i + 1;
				while (runEnd < end) {
					if (drawCustom && m_InstancesScratch[runEnd].Material != runMaterial) break;
					const TextureHandle h = resolveHandle(m_InstancesScratch[runEnd].TextureHandle);
					if (!(h.index == runHandle.index && h.generation == runHandle.generation)) break;
					const TextureHandle maskH = resolveMaskHandle(m_InstancesScratch[runEnd]);
					if (!(maskH.index == runMaskHandle.index && maskH.generation == runMaskHandle.generation)) break;
					if (!ClipsEqual(runHasClip, runClipMin, runClipMax,
						m_InstancesScratch[runEnd].HasClip,
						m_InstancesScratch[runEnd].ClipMin,
						m_InstancesScratch[runEnd].ClipMax)) break;
					++runEnd;
				}
				const uint32_t count = static_cast<uint32_t>(runEnd - i);

				wgpu::BindGroup bg = ResolveBindGroup(device, state, runHandle, runMaskHandle);
				if (!bg) {
					i = runEnd;
					continue;
				}

				if (runHasClip) {
					const ScissorPx sc = ComputeScissor(mvp, runClipMin, runClipMax,
						target.Width, target.Height);
					if (sc.Valid) {
						pass.SetScissorRect(sc.X, sc.Y, sc.W, sc.H);
						scissorIsFull = false;
					} else {
						i = runEnd;
						continue;
					}
				} else if (!scissorIsFull) {
					pass.SetScissorRect(0, 0, target.Width, target.Height);
					scissorIsFull = true;
				}

				const CustomSpriteDrawer::Draw custom = runMaterial != 0
					? state.CustomImages->Resolve(runMaterial, ShaderTarget::UI, target.ColorFormat, target.HasDepth)
					: CustomSpriteDrawer::Draw{};
				wgpu::RenderPipeline runPipeline = custom.Pipeline ? custom.Pipeline : pipeline;
				if (runPipeline.Get() != boundPipeline.Get()) {
					pass.SetPipeline(runPipeline);
					boundPipeline = runPipeline;
				}
				if (custom.Pipeline) pass.SetBindGroup(1, custom.Group);
				pass.SetBindGroup(0, bg);
				pass.DrawIndexed(/*indexCount=*/6,
					/*instanceCount=*/count,
					/*firstIndex=*/0,
					/*baseVertex=*/0,
					/*firstInstance=*/static_cast<uint32_t>(i));

				i = runEnd;
			}

			pass.End();

			if (target.IsSwapChain) {
				WebGPUBackend::MarkSwapChainRendered();
			}
		};

		// ── Helper: submit one text phase covering [start, end) ──────
		auto submitTextPhase = [&](size_t start, size_t end) {
			if (!m_TextRenderer || !m_TextRenderer->IsInitialized() || start >= end) return;
			std::span<const TextDrawCmd> sub(m_TextScratch.data() + start, end - start);
			m_TextRenderer->RenderInstances(sub, mvp, /*viewId*/ 0xFFFFu, /*scissorCache*/ 0xFFFFu);
		};

		// ── Merge walk: interleave image / text phases ───────────────
		// Two-pass "all images then all text" broke hierarchy order; tandem walk preserves it. `<=` prefers images so text at equal DrawIndex renders behind.
		auto imgKey = [&](size_t k) {
			const auto& inst = m_InstancesScratch[k];
			return std::make_tuple(inst.SortingLayer, inst.SortingOrder, inst.DrawIndex);
		};
		auto txtKey = [&](size_t k) {
			const auto& cmd = m_TextScratch[k];
			return std::make_tuple(cmd.SortingLayer, cmd.SortingOrder, cmd.DrawIndex);
		};

		const size_t nTxt = m_TextScratch.size();
		size_t imgPos = 0;
		size_t txtPos = 0;

		while (imgPos < n || txtPos < nTxt) {
			bool drawImage;
			if (imgPos >= n)        drawImage = false;
			else if (txtPos >= nTxt) drawImage = true;
			else drawImage = imgKey(imgPos) <= txtKey(txtPos);

			if (drawImage) {
				size_t imgEnd = imgPos + 1;
				while (imgEnd < n
					&& (txtPos >= nTxt || imgKey(imgEnd) <= txtKey(txtPos)))
				{
					++imgEnd;
				}
				submitImagePhase(imgPos, imgEnd);
				imgPos = imgEnd;
			} else {
				size_t txtEnd = txtPos + 1;
				while (txtEnd < nTxt
					&& (imgPos >= n || txtKey(txtEnd) < imgKey(imgPos)))
				{
					++txtEnd;
				}
				// No flush between text phases: TextRenderer appends each phase into its own VBO range.
				submitTextPhase(txtPos, txtEnd);
				txtPos = txtEnd;
			}
		}

		// Submit this scene's UI as its own unit. BeginFrame/editor views call this once per
		// loaded scene, all writing the same instance buffer and TextRenderer VBO; Dawn
		// coalesces every WriteBuffer in a submit ahead of any pass, so without this flush the
		// next additive scene's uploads overwrite this scene's data before its passes execute.
		// Same fix as Renderer2D's per-scene sprite flush.
		if (n > 0 || nTxt > 0) {
			WebGPUBackend::FlushCommands();
		}
	}

}  // namespace Funke
