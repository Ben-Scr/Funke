#include "pch.hpp"
#include "Graphics/Text/Font.hpp"

#include "Core/Log.hpp"
#include "Core/PlatformDetection.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/Text/FontGlyphRaster.hpp"
#include "Graphics/Text/TextEncoding.hpp"
#include "Serialization/File.hpp"

#include <webgpu/webgpu_cpp.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <unordered_map>
#include <utility>


namespace Funke {

	namespace {
		struct CodepointRange { uint32_t First; uint32_t Last; };
		constexpr CodepointRange k_BakedRanges[] = {
			{ 32,  126 },  // ASCII printable
			{ 160, 255 },  // Latin-1 supplement
		};
		constexpr int k_CodepointRangeCount =
			static_cast<int>(sizeof(k_BakedRanges) / sizeof(k_BakedRanges[0]));

		constexpr int ComputeBakedCodepointCount() {
			int total = 0;
			for (const auto& r : k_BakedRanges) total += int(r.Last - r.First + 1);
			return total;
		}
		constexpr int k_CodepointCount = ComputeBakedCodepointCount();

		constexpr bool IsBakedCodepoint(uint32_t cp) {
			for (const auto& r : k_BakedRanges)
				if (cp >= r.First && cp <= r.Last) return true;
			return false;
		}
		static_assert(k_BakedRanges[k_CodepointRangeCount - 1].Last < Font::k_GlyphTableSize,
			"Baked ranges must fit the direct glyph table");

		constexpr uint32_t k_KernFirst = 32;
		constexpr uint32_t k_KernLast  = 126;
		constexpr uint32_t k_KernSpan  = k_KernLast - k_KernFirst + 1;

		constexpr int k_AtlasInitialSide = 256;
		constexpr int k_AtlasMaxSide     = 4096;
		constexpr size_t k_MaxUnicodeCachedGlyphs = 65536;

		// ── Pool ────────────────────────────────────────────────────────────
		struct GpuFontAtlas {
			wgpu::Texture     Texture;
			wgpu::TextureView View;
		};
		std::unordered_map<unsigned, GpuFontAtlas> g_Atlases;
		unsigned g_NextAtlasId = 1;

		unsigned RegisterAtlas(GpuFontAtlas&& atlas) {
			unsigned id = g_NextAtlasId++;
			if (id == 0) id = g_NextAtlasId++;
			g_Atlases.emplace(id, std::move(atlas));
			return id;
		}
		void DeregisterAtlas(unsigned id) {
			if (id == 0) return;
			g_Atlases.erase(id);
		}
		GpuFontAtlas* TryLookupAtlas(unsigned id) {
			if (id == 0) return nullptr;
			auto it = g_Atlases.find(id);
			return (it == g_Atlases.end()) ? nullptr : &it->second;
		}

		struct CpuBakeResult {
			std::vector<uint8_t> Bitmap;
			std::vector<stbtt_packedchar> Packed;
			std::vector<uint8_t> StbFontInfoStorage;  // raw bytes, copied back into Font
			std::shared_ptr<const std::vector<uint8_t>> TtfBuffer;  // shared with Font; read-only on the worker
			std::string Filepath;
			int   AtlasSide = 0;
			float PixelSize = 0.0f;
			float Ascent = 0.0f;
			float Descent = 0.0f;
			float LineHeight = 0.0f;
			float StbScale = 0.0f;
			std::vector<int16_t> AsciiKerning;
			std::vector<int> GlyphIndices;
			bool  HasKerning = false;
			bool  Ok = false;  // worker sets to true on successful pack
		};

		// Pairs are bounded by k_KernSpan² GPOS lookups, cheap next to rasterizing the atlas.
		void BakeKerning(const stbtt_fontinfo* font, CpuBakeResult& out) {
			out.GlyphIndices.assign(Font::k_GlyphTableSize, 0);
			for (uint32_t cp = 0; cp < Font::k_GlyphTableSize; ++cp)
				out.GlyphIndices[cp] = stbtt_FindGlyphIndex(font, int(cp));

			out.HasKerning = font->kern != 0 || font->gpos != 0;
			out.AsciiKerning.clear();
			if (!out.HasKerning) return;

			out.AsciiKerning.assign(size_t(k_KernSpan) * k_KernSpan, 0);
			bool any = false;
			for (uint32_t a = 0; a < k_KernSpan; ++a) {
				const int ga = out.GlyphIndices[k_KernFirst + a];
				for (uint32_t b = 0; b < k_KernSpan; ++b) {
					const int kern = stbtt_GetGlyphKernAdvance(font, ga, out.GlyphIndices[k_KernFirst + b]);
					out.AsciiKerning[size_t(a) * k_KernSpan + b] = static_cast<int16_t>(kern);
					any |= kern != 0;
				}
			}
			if (!any) out.AsciiKerning.clear();
		}

		// CPU rasterization shared between the synchronous BakeAtlas path and
		// the async worker. Touches no engine state — pure stbtt work against
		// the provided TTF buffer. Fills `out` on success.
		bool RasterizeAtlasCpu(const std::vector<uint8_t>& ttfBuffer,
			float pixelSize, CpuBakeResult& out)
		{
			if (ttfBuffer.empty() || pixelSize <= 0.0f) return false;

			out.StbFontInfoStorage.assign(sizeof(stbtt_fontinfo), 0);
			stbtt_fontinfo* font = reinterpret_cast<stbtt_fontinfo*>(out.StbFontInfoStorage.data());
			if (!stbtt_InitFont(font, ttfBuffer.data(),
				stbtt_GetFontOffsetForIndex(ttfBuffer.data(), 0)))
			{
				return false;
			}

			const float scale = stbtt_ScaleForPixelHeight(font, pixelSize);
			int ascent = 0, descent = 0, lineGap = 0;
			stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);
			out.StbScale   = scale;
			out.PixelSize  = pixelSize;
			out.Ascent     = ascent * scale;
			out.Descent    = descent * scale;
			out.LineHeight = (ascent - descent + lineGap) * scale;
			BakeKerning(font, out);

			out.Packed.assign(k_CodepointCount, stbtt_packedchar{});

			// Two-tier bake: 2× oversample at sizes ≤ 48 px for sharper text,
			// 1× otherwise. Same ladder as the synchronous path.
			constexpr float k_OversampleSkipThresholdPx = 48.0f;
			const std::initializer_list<int> oversampleTiers = (pixelSize > k_OversampleSkipThresholdPx)
				? std::initializer_list<int>{ 1 }
				: std::initializer_list<int>{ 2, 1 };

			for (int oversample : oversampleTiers) {
				int side = k_AtlasInitialSide;
				while (side <= k_AtlasMaxSide) {
					out.Bitmap.assign(static_cast<size_t>(side) * static_cast<size_t>(side), 0);
					stbtt_pack_context pctx{};
					if (!stbtt_PackBegin(&pctx, out.Bitmap.data(), side, side, 0, 1, nullptr)) break;
					stbtt_PackSetOversampling(&pctx, oversample, oversample);

					stbtt_pack_range packRanges[k_CodepointRangeCount]{};
					int cumulative = 0;
					for (int r = 0; r < k_CodepointRangeCount; ++r) {
						const auto& cr = k_BakedRanges[r];
						const int count = int(cr.Last - cr.First + 1);
						packRanges[r].font_size = pixelSize;
						packRanges[r].first_unicode_codepoint_in_range = int(cr.First);
						packRanges[r].array_of_unicode_codepoints = nullptr;
						packRanges[r].num_chars = count;
						packRanges[r].chardata_for_range = out.Packed.data() + cumulative;
						cumulative += count;
					}
					const int packResult = stbtt_PackFontRanges(&pctx, ttfBuffer.data(), 0,
						packRanges, k_CodepointRangeCount);
					stbtt_PackEnd(&pctx);

					if (packResult) {
						out.AtlasSide = side;
						out.Ok = true;
						return true;
					}
					side *= 2;
				}
			}
			return false;
		}
	}

	struct FontAsyncBakeState {
		std::thread                       Worker;
		std::shared_ptr<CpuBakeResult>    Result;
		std::atomic<bool>                 Ready{ false };
	};

	struct FontUnicodeCache {
		struct Page {
			unsigned Atlas = 0;
			FontRaster::AtlasShelf Shelf;
		};
		std::unordered_map<uint32_t, GlyphMetrics> Glyphs;
		std::vector<Page> Pages;
		size_t Bytes = 0;
		bool AtCapacity = false;

		~FontUnicodeCache() {
			for (const Page& page : Pages) DeregisterAtlas(page.Atlas);
		}
	};

	bool FontRaster::Rasterize(std::span<const uint8_t> bytes, float pixelSize, uint32_t codepoint, Glyph& out) {
		out = {};
		if (bytes.size() < 12 || !std::isfinite(pixelSize) || pixelSize <= 0.0f || pixelSize > k_AtlasMaxSide) return false;
		const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
		stbtt_fontinfo font{};
		if (offset < 0 || static_cast<size_t>(offset) >= bytes.size() || !stbtt_InitFont(&font, bytes.data(), offset)) return false;
		if (!TextEncoding::IsScalar(codepoint)) codepoint = TextEncoding::k_ReplacementCodepoint;
		int glyph = stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint));
		if (glyph == 0) {
			codepoint = TextEncoding::k_ReplacementCodepoint;
			glyph = stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint));
			if (glyph == 0) {
				codepoint = '?';
				glyph = stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint));
			}
		}
		out.Codepoint = codepoint;
		const float scale = stbtt_ScaleForPixelHeight(&font, pixelSize);
		int advance = 0;
		stbtt_GetGlyphHMetrics(&font, glyph, &advance, nullptr);
		out.Metrics.XAdvance = advance * scale;
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		stbtt_GetGlyphBitmapBox(&font, glyph, scale, scale, &x0, &y0, &x1, &y1);
		out.Width = x1 - x0;
		out.Height = y1 - y0;
		if (out.Width < 0 || out.Height < 0 || out.Width > k_AtlasMaxSide - 2 || out.Height > k_AtlasMaxSide - 2) return false;
		out.Metrics.Width = static_cast<float>(out.Width);
		out.Metrics.Height = static_cast<float>(out.Height);
		out.Metrics.XOffset = static_cast<float>(x0);
		out.Metrics.YOffset = static_cast<float>(y0);
		if (out.Width != 0 && out.Height != 0) {
			out.Bitmap.resize(static_cast<size_t>(out.Width) * out.Height);
			stbtt_MakeGlyphBitmap(&font, out.Bitmap.data(), out.Width, out.Height, out.Width, scale, scale, glyph);
		}
		return true;
	}

	namespace WebGPUBackend {
		wgpu::TextureView LookupFontAtlas(unsigned atlasId) {
			GpuFontAtlas* slot = TryLookupAtlas(atlasId);
			return slot ? slot->View : wgpu::TextureView{};
		}
	}

	Font::Font() = default;

	Font::~Font() {
		Cleanup();
	}

	Font::Font(Font&& other) noexcept
		: m_TtfBuffer(std::move(other.m_TtfBuffer))
		, m_Glyphs(std::move(other.m_Glyphs))
		, m_AsciiKerning(std::move(other.m_AsciiKerning))
		, m_GlyphIndices(std::move(other.m_GlyphIndices))
		, m_HasKerning(other.m_HasKerning)
		, m_AtlasTexture(other.m_AtlasTexture)
		, m_AtlasWidth(other.m_AtlasWidth)
		, m_AtlasHeight(other.m_AtlasHeight)
		, m_PixelSize(other.m_PixelSize)
		, m_Ascent(other.m_Ascent)
		, m_Descent(other.m_Descent)
		, m_LineHeight(other.m_LineHeight)
		, m_StbScale(other.m_StbScale)
		, m_StbFontInfoStorage(std::move(other.m_StbFontInfoStorage))
		, m_Filepath(std::move(other.m_Filepath))
		, m_AsyncBake(std::move(other.m_AsyncBake))
		, m_UnicodeCache(std::move(other.m_UnicodeCache))
	{
		other.m_AtlasTexture = 0;
		other.m_AtlasWidth = 0;
		other.m_AtlasHeight = 0;
		other.m_PixelSize = 0.0f;
		other.m_Ascent = 0.0f;
		other.m_Descent = 0.0f;
		other.m_LineHeight = 0.0f;
		other.m_StbScale = 0.0f;
		other.m_HasKerning = false;
	}

	Font& Font::operator=(Font&& other) noexcept {
		if (this == &other) return *this;
		Cleanup();
		m_TtfBuffer = std::move(other.m_TtfBuffer);
		m_Glyphs = std::move(other.m_Glyphs);
		m_AsciiKerning = std::move(other.m_AsciiKerning);
		m_GlyphIndices = std::move(other.m_GlyphIndices);
		m_HasKerning = other.m_HasKerning;
		other.m_HasKerning = false;
		m_AtlasTexture = other.m_AtlasTexture;
		m_AtlasWidth = other.m_AtlasWidth;
		m_AtlasHeight = other.m_AtlasHeight;
		m_PixelSize = other.m_PixelSize;
		m_Ascent = other.m_Ascent;
		m_Descent = other.m_Descent;
		m_LineHeight = other.m_LineHeight;
		m_StbScale = other.m_StbScale;
		m_StbFontInfoStorage = std::move(other.m_StbFontInfoStorage);
		m_Filepath = std::move(other.m_Filepath);
		m_AsyncBake = std::move(other.m_AsyncBake);
		m_UnicodeCache = std::move(other.m_UnicodeCache);
		other.m_AtlasTexture = 0;
		other.m_AtlasWidth = 0;
		other.m_AtlasHeight = 0;
		other.m_PixelSize = 0.0f;
		other.m_Ascent = 0.0f;
		other.m_Descent = 0.0f;
		other.m_LineHeight = 0.0f;
		other.m_StbScale = 0.0f;
		return *this;
	}

	bool Font::LoadFromFile(const std::string& path, float pixelSize) {
		if (path.empty() || pixelSize <= 0.0f) return false;
		Cleanup();
		if (!File::Exists(path)) {
			FNK_CORE_ERROR_TAG("Font", "TTF not found: {}", path);
			return false;
		}
		std::vector<uint8_t> bytes = File::ReadAllBytes(path);
		if (bytes.empty()) {
			FNK_CORE_ERROR_TAG("Font", "TTF empty / unreadable: {}", path);
			return false;
		}
		m_TtfBuffer = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
		m_Filepath = path;
		return BakeAtlas(pixelSize);
	}

	bool Font::LoadFromBuffer(const std::string& sourcePath,
		std::shared_ptr<const std::vector<uint8_t>> ttfBuffer, float pixelSize)
	{
		if (!ttfBuffer || ttfBuffer->empty() || pixelSize <= 0.0f) return false;
		Cleanup();
		m_TtfBuffer = std::move(ttfBuffer);
		m_Filepath = sourcePath;
		return BakeAtlas(pixelSize);
	}

	namespace {
		bool PublishAtlasGpu(const CpuBakeResult& result,
			unsigned& outAtlasTexture, int& outAtlasSide)
		{
			if (!result.Ok) return false;

			wgpu::Device device = WebGPUBackend::GetDevice();
			wgpu::Queue  queue  = WebGPUBackend::GetQueue();
			if (!device || !queue) {
				FNK_CORE_ERROR_TAG("Font", "Device / queue unavailable while publishing atlas: {}",
					result.Filepath);
				return false;
			}

			// Upload as R8Unorm (single-channel coverage). Power-of-two atlas
			// sides ≥ 256 already satisfy WebGPU's COPY_BYTES_PER_ROW_ALIGNMENT
			// (256-byte) requirement, so bytesPerRow = AtlasSide is valid as-is.
			wgpu::TextureDescriptor texDesc{};
			texDesc.dimension       = wgpu::TextureDimension::e2D;
			texDesc.size            = { static_cast<uint32_t>(result.AtlasSide),
			                            static_cast<uint32_t>(result.AtlasSide), 1 };
			texDesc.format          = wgpu::TextureFormat::R8Unorm;
			texDesc.usage           = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
			texDesc.mipLevelCount   = 1;
			texDesc.sampleCount     = 1;
			texDesc.viewFormatCount = 0;
			texDesc.label           = "font-atlas";

			wgpu::Texture texture = device.CreateTexture(&texDesc);
			if (!texture) {
				FNK_CORE_ERROR_TAG("Font", "CreateTexture failed for atlas: {}", result.Filepath);
				return false;
			}

			// Dawn renamed these structs mid-2025: ImageCopyTexture ->
			// TexelCopyTextureInfo, TextureDataLayout -> TexelCopyBufferLayout.
			wgpu::TexelCopyTextureInfo dst{};
			dst.texture  = texture;
			dst.mipLevel = 0;
			dst.origin   = { 0, 0, 0 };
			dst.aspect   = wgpu::TextureAspect::All;

			wgpu::TexelCopyBufferLayout layout{};
			layout.offset       = 0;
			layout.bytesPerRow  = static_cast<uint32_t>(result.AtlasSide);
			layout.rowsPerImage = static_cast<uint32_t>(result.AtlasSide);

			wgpu::Extent3D writeSize{ static_cast<uint32_t>(result.AtlasSide),
			                          static_cast<uint32_t>(result.AtlasSide), 1 };
			const size_t byteCount = static_cast<size_t>(result.AtlasSide)
				* static_cast<size_t>(result.AtlasSide);

			queue.WriteTexture(&dst, result.Bitmap.data(), byteCount, &layout, &writeSize);

			wgpu::TextureViewDescriptor viewDesc{};
			viewDesc.format          = wgpu::TextureFormat::R8Unorm;
			viewDesc.dimension       = wgpu::TextureViewDimension::e2D;
			viewDesc.mipLevelCount   = 1;
			viewDesc.arrayLayerCount = 1;
			viewDesc.aspect          = wgpu::TextureAspect::All;
			wgpu::TextureView view   = texture.CreateView(&viewDesc);
			if (!view) {
				FNK_CORE_ERROR_TAG("Font", "Atlas view creation failed: {}", result.Filepath);
				return false;
			}

			GpuFontAtlas atlas;
			atlas.Texture = std::move(texture);
			atlas.View    = std::move(view);
			outAtlasTexture = RegisterAtlas(std::move(atlas));
			outAtlasSide    = result.AtlasSide;
			return true;
		}

		// Translate stb's packed[] table into a Font's glyph table. Shared
		// between the sync and async publish paths.
		void PopulateGlyphTable(const std::vector<stbtt_packedchar>& packed,
			int atlasSide, unsigned atlasTexture, std::vector<GlyphMetrics>& outGlyphs)
		{
			outGlyphs.assign(Font::k_GlyphTableSize, GlyphMetrics{});
			int packedIndex = 0;
			for (int r = 0; r < k_CodepointRangeCount; ++r) {
				const auto& cr = k_BakedRanges[r];
				const int count = int(cr.Last - cr.First + 1);
				for (int i = 0; i < count; ++i) {
					const stbtt_packedchar& pc = packed[packedIndex++];
					GlyphMetrics m;
					m.U0 = float(pc.x0) / float(atlasSide);
					m.V0 = float(pc.y0) / float(atlasSide);
					m.U1 = float(pc.x1) / float(atlasSide);
					m.V1 = float(pc.y1) / float(atlasSide);
					m.Width    = pc.xoff2 - pc.xoff;
					m.Height   = pc.yoff2 - pc.yoff;
					m.XOffset  = pc.xoff;
					m.YOffset  = pc.yoff;
					m.XAdvance = pc.xadvance;
					m.AtlasTexture = atlasTexture;
					outGlyphs[cr.First + uint32_t(i)] = m;
				}
			}
		}
	}

	bool Font::BakeAtlas(float pixelSize) {
		if (!m_TtfBuffer || m_TtfBuffer->empty() || pixelSize <= 0.0f) return false;
		if (!WebGPUBackend::IsInitialized()) {
			FNK_CORE_ERROR_TAG("Font", "BakeAtlas called before WebGPU backend initialized: {}",
				m_Filepath);
			m_TtfBuffer.reset();
			return false;
		}

		CpuBakeResult result;
		result.Filepath = m_Filepath;
		if (!RasterizeAtlasCpu(*m_TtfBuffer, pixelSize, result)) {
			FNK_CORE_ERROR_TAG("Font", "Atlas pack failed (max {}px): {}", k_AtlasMaxSide, m_Filepath);
			m_StbFontInfoStorage.clear();
			m_TtfBuffer.reset();
			return false;
		}

		unsigned atlasTexture = 0;
		int      atlasSide    = 0;
		if (!PublishAtlasGpu(result, atlasTexture, atlasSide)) {
			m_StbFontInfoStorage.clear();
			m_TtfBuffer.reset();
			return false;
		}

		m_AtlasWidth         = atlasSide;
		m_AtlasHeight        = atlasSide;
		m_PixelSize          = result.PixelSize;
		m_Ascent             = result.Ascent;
		m_Descent            = result.Descent;
		m_LineHeight         = result.LineHeight;
		m_StbScale           = result.StbScale;
		m_StbFontInfoStorage = std::move(result.StbFontInfoStorage);
		PopulateGlyphTable(result.Packed, atlasSide, atlasTexture, m_Glyphs);
		m_AsciiKerning       = std::move(result.AsciiKerning);
		m_GlyphIndices       = std::move(result.GlyphIndices);
		m_HasKerning         = result.HasKerning;
		m_AtlasTexture       = atlasTexture;  // last: makes IsLoaded() flip true
		return true;
	}

	bool Font::BeginAsyncBake(const std::string& sourcePath,
		std::shared_ptr<const std::vector<uint8_t>> ttfBuffer, float pixelSize)
	{
		if (!ttfBuffer || ttfBuffer->empty() || pixelSize <= 0.0f) return false;
		if (!WebGPUBackend::IsInitialized()) {
			FNK_CORE_ERROR_TAG("Font", "BeginAsyncBake called before WebGPU backend initialized: {}",
				sourcePath);
			return false;
		}
		Cleanup();
		m_Filepath = sourcePath;

		m_AsyncBake = std::make_unique<FontAsyncBakeState>();
		m_AsyncBake->Result = std::make_shared<CpuBakeResult>();
		m_AsyncBake->Result->Filepath = sourcePath;
		m_AsyncBake->Result->TtfBuffer = std::move(ttfBuffer);

		// Capture shared state by value into the worker so Font teardown
		// doesn't dangle the worker's writes. The TtfBuffer is shared (read-only
		// on the worker), so concurrent bakes of other sizes of the same font are safe.
		std::shared_ptr<CpuBakeResult> result = m_AsyncBake->Result;
		std::atomic<bool>* ready              = &m_AsyncBake->Ready;
#if FNK_PLATFORM_HAS_THREADS
		m_AsyncBake->Worker = std::thread([result, ready, pixelSize]() {
			RasterizeAtlasCpu(*result->TtfBuffer, pixelSize, *result);
			ready->store(true, std::memory_order_release);
		});
#else
		RasterizeAtlasCpu(*result->TtfBuffer, pixelSize, *result);
		ready->store(true, std::memory_order_release);
#endif
		return true;
	}

	bool Font::PollAsyncBake() {
		if (!m_AsyncBake) return false;
		if (!m_AsyncBake->Ready.load(std::memory_order_acquire)) return false;

		// Worker is done; reclaim it before touching its result.
		if (m_AsyncBake->Worker.joinable()) m_AsyncBake->Worker.join();
		auto state  = std::move(m_AsyncBake);
		auto result = state->Result;

		if (!result->Ok) {
			FNK_CORE_ERROR_TAG("Font", "Async atlas pack failed (max {}px): {}",
				k_AtlasMaxSide, result->Filepath);
			return true;  // bake is "done" — failed, but no longer pending
		}

		unsigned atlasTexture = 0;
		int      atlasSide    = 0;
		if (!PublishAtlasGpu(*result, atlasTexture, atlasSide)) {
			return true;
		}

		m_TtfBuffer          = std::move(result->TtfBuffer);
		m_AtlasWidth         = atlasSide;
		m_AtlasHeight        = atlasSide;
		m_PixelSize          = result->PixelSize;
		m_Ascent             = result->Ascent;
		m_Descent            = result->Descent;
		m_LineHeight         = result->LineHeight;
		m_StbScale           = result->StbScale;
		m_StbFontInfoStorage = std::move(result->StbFontInfoStorage);
		PopulateGlyphTable(result->Packed, atlasSide, atlasTexture, m_Glyphs);
		m_AsciiKerning       = std::move(result->AsciiKerning);
		m_GlyphIndices       = std::move(result->GlyphIndices);
		m_HasKerning         = result->HasKerning;
		m_AtlasTexture       = atlasTexture;  // last: flips IsLoaded() true
		return true;
	}

	bool Font::IsBakingAsync() const {
		return m_AsyncBake != nullptr;
	}

	const GlyphMetrics* Font::GetGlyph(uint32_t codepoint) const {
		if (m_StbFontInfoStorage.empty() || codepoint < 32 || (codepoint >= 127 && codepoint < 160)) return nullptr;
		if (codepoint < m_Glyphs.size() && IsBakedCodepoint(codepoint) && m_GlyphIndices[codepoint] != 0) return &m_Glyphs[codepoint];
		if (!TextEncoding::IsScalar(codepoint)) codepoint = TextEncoding::k_ReplacementCodepoint;
		if (m_UnicodeCache) {
			if (const auto found = m_UnicodeCache->Glyphs.find(codepoint); found != m_UnicodeCache->Glyphs.end()) return &found->second;
		}
		const auto* font = reinterpret_cast<const stbtt_fontinfo*>(m_StbFontInfoStorage.data());
		const int index = codepoint < m_GlyphIndices.size() ? m_GlyphIndices[codepoint]
			: stbtt_FindGlyphIndex(font, static_cast<int>(codepoint));
		if (index == 0) {
			// Cache the replacement once, never aliases for every unsupported codepoint.
			codepoint = stbtt_FindGlyphIndex(font, TextEncoding::k_ReplacementCodepoint) != 0
				? TextEncoding::k_ReplacementCodepoint : '?';
		}
		if (codepoint < m_Glyphs.size() && IsBakedCodepoint(codepoint)) return &m_Glyphs[codepoint];
		if (!m_UnicodeCache) m_UnicodeCache = std::make_unique<FontUnicodeCache>();
		auto& cache = *m_UnicodeCache;
		if (const auto found = cache.Glyphs.find(codepoint); found != cache.Glyphs.end()) return &found->second;
		const GlyphMetrics* fallback = m_Glyphs.size() > '?' ? &m_Glyphs['?'] : nullptr;
		constexpr size_t k_MaxUnicodeAtlasBytes = 16 * 1024 * 1024;
		if (cache.AtCapacity || cache.Glyphs.size() >= k_MaxUnicodeCachedGlyphs || !m_TtfBuffer) return fallback;
		FontRaster::Glyph glyph;
		if (!FontRaster::Rasterize(*m_TtfBuffer, m_PixelSize, codepoint, glyph)) return fallback;
		if (!glyph.Bitmap.empty()) {
			wgpu::Device device = WebGPUBackend::GetDevice();
			wgpu::Queue queue = WebGPUBackend::GetQueue();
			if (!device || !queue) return fallback;
			FontUnicodeCache::Page* page = nullptr;
			int x = 0, y = 0;
			for (auto& candidate : cache.Pages) {
				if (candidate.Shelf.Place(glyph.Width, glyph.Height, x, y)) {
					page = &candidate;
					break;
				}
			}
			if (!page) {
				int side = 512;
				while (side < glyph.Width + 2 || side < glyph.Height + 2) side *= 2;
				const size_t bytes = static_cast<size_t>(side) * side;
				if (bytes > k_MaxUnicodeAtlasBytes - cache.Bytes) {
					cache.AtCapacity = true;
					FNK_CORE_WARN_TAG("Font", "Unicode atlas capacity reached for {} at {}px", m_Filepath, m_PixelSize);
					return fallback;
				}
				CpuBakeResult blank;
				blank.Ok = true;
				blank.Filepath = m_Filepath;
				blank.AtlasSide = side;
				blank.Bitmap.assign(bytes, 0);
				unsigned atlas = 0;
				int publishedSide = 0;
				if (!PublishAtlasGpu(blank, atlas, publishedSide)) return fallback;
				cache.Pages.push_back({ atlas, FontRaster::AtlasShelf(publishedSide) });
				cache.Bytes += bytes;
				page = &cache.Pages.back();
				if (!page->Shelf.Place(glyph.Width, glyph.Height, x, y)) return fallback;
			}
			auto* atlas = TryLookupAtlas(page->Atlas);
			if (!atlas) return fallback;
			const int pitch = (glyph.Width + 255) & ~255;
			std::vector<uint8_t> upload(static_cast<size_t>(pitch) * glyph.Height, 0);
			for (int row = 0; row < glyph.Height; ++row) {
				std::memcpy(upload.data() + static_cast<size_t>(row) * pitch,
					glyph.Bitmap.data() + static_cast<size_t>(row) * glyph.Width, glyph.Width);
			}
			wgpu::TexelCopyTextureInfo destination{};
			destination.texture = atlas->Texture;
			destination.origin = { static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0 };
			wgpu::TexelCopyBufferLayout layout{};
			layout.bytesPerRow = static_cast<uint32_t>(pitch);
			layout.rowsPerImage = static_cast<uint32_t>(glyph.Height);
			wgpu::Extent3D size{ static_cast<uint32_t>(glyph.Width), static_cast<uint32_t>(glyph.Height), 1 };
			queue.WriteTexture(&destination, upload.data(), upload.size(), &layout, &size);
			const float side = static_cast<float>(page->Shelf.GetSide());
			glyph.Metrics.U0 = x / side;
			glyph.Metrics.V0 = y / side;
			glyph.Metrics.U1 = (x + glyph.Width) / side;
			glyph.Metrics.V1 = (y + glyph.Height) / side;
			glyph.Metrics.AtlasTexture = page->Atlas;
		}
		return &cache.Glyphs.emplace(codepoint, glyph.Metrics).first->second;
	}

	size_t Font::GetAtlasByteSize() const {
		return static_cast<size_t>(m_AtlasWidth) * m_AtlasHeight
			+ (m_UnicodeCache ? m_UnicodeCache->Bytes : 0);
	}

	float Font::GetKerning(uint32_t a, uint32_t b) const {
		if (!m_HasKerning || m_StbFontInfoStorage.empty() || m_StbScale == 0.0f) return 0.0f;
		if (a - k_KernFirst < k_KernSpan && b - k_KernFirst < k_KernSpan
			&& m_GlyphIndices[a] != 0 && m_GlyphIndices[b] != 0) {
			if (m_AsciiKerning.empty()) return 0.0f;
			return m_AsciiKerning[size_t(a - k_KernFirst) * k_KernSpan + (b - k_KernFirst)] * m_StbScale;
		}
		const stbtt_fontinfo* font = reinterpret_cast<const stbtt_fontinfo*>(m_StbFontInfoStorage.data());
		auto resolveGlyph = [&](uint32_t codepoint) {
			if (!TextEncoding::IsScalar(codepoint)) codepoint = TextEncoding::k_ReplacementCodepoint;
			int index = codepoint < m_GlyphIndices.size() ? m_GlyphIndices[codepoint] : stbtt_FindGlyphIndex(font, int(codepoint));
			if (index == 0) {
				codepoint = TextEncoding::k_ReplacementCodepoint;
				index = stbtt_FindGlyphIndex(font, static_cast<int>(codepoint));
			}
			if (m_UnicodeCache && (m_UnicodeCache->AtCapacity || m_UnicodeCache->Glyphs.size() >= k_MaxUnicodeCachedGlyphs)
				&& !IsBakedCodepoint(codepoint)
				&& !m_UnicodeCache->Glyphs.contains(codepoint)) return m_GlyphIndices['?'];
			return index != 0 ? index : m_GlyphIndices['?'];
		};
		const int ga = resolveGlyph(a);
		const int gb = resolveGlyph(b);
		return stbtt_GetGlyphKernAdvance(font, ga, gb) * m_StbScale;
	}

	void Font::Cleanup() {
		// Join the worker before teardown to avoid an unjoined thread if Font is destroyed mid-bake (worker holds shared_ptr to its result, but join makes teardown deterministic).
		if (m_AsyncBake) {
			if (m_AsyncBake->Worker.joinable()) m_AsyncBake->Worker.join();
			m_AsyncBake.reset();
		}
		if (m_AtlasTexture != 0) {
			DeregisterAtlas(m_AtlasTexture);
			m_AtlasTexture = 0;
		}
		m_UnicodeCache.reset();
		m_TtfBuffer.reset();
		m_Glyphs.clear();
		m_AsciiKerning.clear();
		m_GlyphIndices.clear();
		m_HasKerning = false;
		m_AtlasWidth = 0;
		m_AtlasHeight = 0;
		m_PixelSize = 0.0f;
		m_Ascent = 0.0f;
		m_Descent = 0.0f;
		m_LineHeight = 0.0f;
		m_StbScale = 0.0f;
		m_StbFontInfoStorage.clear();
		m_Filepath.clear();
	}

}  // namespace Funke
