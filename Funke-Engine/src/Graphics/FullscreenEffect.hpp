#pragma once

#include "Core/Export.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace Funke {

	class Framebuffer;

	class FUNKE_API FullscreenEffect {
	public:
		FullscreenEffect();
		~FullscreenEffect();
		FullscreenEffect(const FullscreenEffect&) = delete;
		FullscreenEffect& operator=(const FullscreenEffect&) = delete;

		// Group 0: source texture (0), nearest sampler (1), uniform block (2); entries vs_main/fs_main.
		bool Initialize(std::string_view wgsl, uint32_t uniformBytes);
		bool IsInitialized() const;
		// The returned image belongs to this pass and is valid until its next Render or Shutdown.
		const Framebuffer* Render(const Framebuffer& source, const void* uniforms, size_t uniformBytes);
		void Shutdown();

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
