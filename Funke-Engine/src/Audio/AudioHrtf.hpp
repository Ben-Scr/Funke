#pragma once
#include "Audio/AudioSpatial.hpp"
#include "Core/Export.hpp"

#include <miniaudio.h>
#include <memory>

namespace Funke {

	// Control methods run on the engine thread; miniaudio owns the processing thread.
	class FUNKE_API AudioHrtf {
	public:
		AudioHrtf();
		~AudioHrtf();
		AudioHrtf(const AudioHrtf&) = delete;
		AudioHrtf& operator=(const AudioHrtf&) = delete;

		bool Initialize(ma_engine* engine, ma_sound* sound, const AudioListenerState& listener, const glm::vec3& position);
		bool Initialize(uint32_t sampleRate, const AudioListenerState& listener, const glm::vec3& position);
		// Standalone interleaved stereo processing; never call while attached to a running engine.
		void Process(const float* input, float* output, uint32_t frames);
		void Update(const AudioListenerState& listener, const glm::vec3& position, bool enabled);
		void SetPaused(bool paused);
		bool HasTail() const;
		uint32_t GetTailFrameCount() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
