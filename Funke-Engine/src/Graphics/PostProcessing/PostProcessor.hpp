#pragma once

#include "Core/Export.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Graphics/Shader.hpp"

#include <webgpu/webgpu_cpp.h>

#include <memory>

// PostProcessor: sits between Renderer2D and present; reads an RGBA16F FBO and writes caller-supplied swap-chain or editor-panel view. This header pulls in <webgpu/webgpu_cpp.h> because the dst TextureView cannot be round-tripped through a raw pointer.

namespace Funke {

	class Framebuffer;
	struct PostProcessing2DComponent;

	class FUNKE_API PostProcessor {
	public:
		PostProcessor();
		~PostProcessor();
		PostProcessor(const PostProcessor&) = delete;
		PostProcessor& operator=(const PostProcessor&) = delete;

		bool Initialize();

		// Release Dawn handles. Idempotent.
		void Shutdown();

		bool IsInitialized() const;

		// Opens a RenderPass with LoadOp::Load so a prior clear on dstView is preserved; dst view comes straight from BeginRenderToCurrentTarget (swap chain or editor FBO).
		void Blit(const Framebuffer& src,
			wgpu::TextureView dstView,
			wgpu::TextureFormat dstFormat,
			uint32_t dstWidth,
			uint32_t dstHeight,
			UpscaleFilterMode filter = UpscaleFilterMode::Linear);

		// Dispatches to the enabled effect pass or falls through to Blit. settings == nullptr (no PostProcessing2DComponent on camera) is a valid passthrough case.
		void Run(const Framebuffer& src,
			wgpu::TextureView dstView,
			wgpu::TextureFormat dstFormat,
			uint32_t dstWidth,
			uint32_t dstHeight,
			const PostProcessing2DComponent* settings,
			PostProcessingQualityLevel quality = PostProcessingQualityLevel::High,
			bool clearDestination = false);

		// Public only so anonymous-namespace pipeline-cache builders in PostProcessor.cpp can take Impl& params.
		struct Impl;

	private:
		std::unique_ptr<Impl> m_Impl;

		// Owned here (not in pImpl) so Shader dtor runs after pImpl pipelines are destroyed — pImpl pipelines captured the ShaderModule by value at creation time.
		std::unique_ptr<Shader> m_BlitShader;
		std::unique_ptr<Shader> m_BloomThresholdShader;
		std::unique_ptr<Shader> m_BloomBlurShader;
		// Bloom composite, color grading, lens distortion, chromatic aberration, vignette and grain in one pass.
		std::unique_ptr<Shader> m_CompositeShader;
		std::unique_ptr<Shader> m_PixelatedShader;
	};

} // namespace Funke
