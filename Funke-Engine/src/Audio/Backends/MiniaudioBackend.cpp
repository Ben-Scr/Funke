#include "pch.hpp"
#include "Audio/Backends/MiniaudioBackend.hpp"

namespace Funke {

	bool MiniaudioBackend::Initialize() {
		if (m_Initialized) {
			return true;
		}

		ma_engine_config headless = ma_engine_config_init();
		headless.noDevice = MA_TRUE;
		headless.channels = 2;
		headless.sampleRate = 48000;
		const ma_result result = ma_engine_init(m_Headless ? &headless : nullptr, &m_Engine);
		if (result != MA_SUCCESS) {
			FNK_CORE_ERROR("[{}] MiniaudioBackend: ma_engine_init failed. Error: {}",
				ErrorCodeToString(FunkeErrorCode::LoadFailed), static_cast<int>(result));
			return false;
		}

		m_Initialized = true;
		return true;
	}

	void MiniaudioBackend::Shutdown() {
		if (!m_Initialized) {
			return;
		}

		ma_engine_uninit(&m_Engine);
		m_Initialized = false;
	}

	uint64_t MiniaudioBackend::ReadFrames(float* interleaved, uint64_t frameCount) {
		if (!m_Initialized || !m_Headless) {
			return 0;
		}

		ma_uint64 read = 0;
		ma_engine_read_pcm_frames(&m_Engine, interleaved, frameCount, &read);
		return read;
	}

	void MiniaudioBackend::SetMasterVolume(float volume) {
		if (!m_Initialized) {
			return;
		}

		ma_engine_set_volume(&m_Engine, volume);
	}

	void MiniaudioBackend::SetSuspended(bool suspended) {
		if (!m_Initialized) {
			return;
		}

		// Stop/start the device thread so playback freezes in place and later resumes from the same cursor.
		ma_device* device = ma_engine_get_device(&m_Engine);
		if (!device) {
			return;
		}

		if (suspended) {
			if (ma_device_is_started(device)) {
				ma_device_stop(device);
			}
		}
		else if (!ma_device_is_started(device)) {
			ma_device_start(device);
		}
	}

}
