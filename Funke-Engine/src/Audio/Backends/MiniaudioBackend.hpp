#pragma once

#include "Audio/IAudioBackend.hpp"

#include <miniaudio.h>

namespace Funke {

	// Default IAudioBackend implementation. Wraps a single ma_engine instance and
	// owns its lifetime. Installed by AudioManager::Initialize() when no other
	// backend was registered first.
	class FUNKE_API MiniaudioBackend : public IAudioBackend {
	public:
		MiniaudioBackend() = default;
		// Headless: no output device, 48 kHz stereo mixed only when ReadFrames pulls it, so tests measure what would be heard in silence.
		explicit MiniaudioBackend(bool headless) : m_Headless(headless) {}
		~MiniaudioBackend() override = default;

		MiniaudioBackend(const MiniaudioBackend&) = delete;
		MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;

		bool Initialize() override;
		void Shutdown() override;
		void SetMasterVolume(float volume) override;
		void SetSuspended(bool suspended) override;
		ma_engine* GetMiniaudioEngine() override { return m_Initialized ? &m_Engine : nullptr; }

		// Headless only: mixes the next frames (interleaved stereo float) and returns how many were read.
		uint64_t ReadFrames(float* interleaved, uint64_t frameCount);

	private:
		ma_engine m_Engine{};
		bool m_Initialized = false;
		bool m_Headless = false;
	};

}
