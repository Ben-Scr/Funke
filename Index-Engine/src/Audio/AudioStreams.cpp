#include "pch.hpp"
#include "Audio/AudioStreams.hpp"

#include "Audio/AudioManager.hpp"
#include "Audio/IAudioBackend.hpp"

#include <miniaudio.h>

#include <filesystem>
#include <memory>
#include <unordered_map>

namespace Index {

	namespace {
		struct StreamSlot {
			ma_sound Sound{};
			float LengthSeconds = 0.0f;
		};

		std::unordered_map<uint32_t, std::unique_ptr<StreamSlot>> s_Streams;
		uint32_t s_NextStreamId = 1;

		StreamSlot* Find(uint32_t stream) {
			const auto it = s_Streams.find(stream);
			return it != s_Streams.end() ? it->second.get() : nullptr;
		}

		ma_engine* GetEngine() {
			if (!AudioManager::IsInitialized() || !AudioManager::GetBackend()) {
				return nullptr;
			}
			return AudioManager::GetBackend()->GetMiniaudioEngine();
		}
	}

	uint32_t AudioStreams::Open(std::string_view path) {
		ma_engine* engine = GetEngine();
		if (!engine || path.empty()) {
			return 0;
		}

		auto slot = std::make_unique<StreamSlot>();
		const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
		// User music is full of non-ASCII names; the narrow overload would go through the ANSI code page on Windows.
		const std::filesystem::path filePath(std::u8string(path.begin(), path.end()));
#ifdef IDX_PLATFORM_WINDOWS
		const ma_result result = ma_sound_init_from_file_w(engine, filePath.c_str(), flags, nullptr, nullptr, &slot->Sound);
#else
		const ma_result result = ma_sound_init_from_file(engine, filePath.c_str(), flags, nullptr, nullptr, &slot->Sound);
#endif
		if (result != MA_SUCCESS) {
			IDX_CORE_WARN_TAG("Audio", "Could not open audio stream '{}' (miniaudio error {}).", path, static_cast<int>(result));
			return 0;
		}

		// MP3 length needs a full scan of the file, so it's measured once here instead of every query.
		if (ma_sound_get_length_in_seconds(&slot->Sound, &slot->LengthSeconds) != MA_SUCCESS) {
			slot->LengthSeconds = 0.0f;
		}

		const uint32_t id = s_NextStreamId++;
		s_Streams.emplace(id, std::move(slot));
		return id;
	}

	void AudioStreams::Close(uint32_t stream) {
		const auto it = s_Streams.find(stream);
		if (it == s_Streams.end()) {
			return;
		}
		ma_sound_uninit(&it->second->Sound);
		s_Streams.erase(it);
	}

	void AudioStreams::CloseAll() {
		for (auto& [id, slot] : s_Streams) {
			ma_sound_uninit(&slot->Sound);
		}
		s_Streams.clear();
	}

	bool AudioStreams::IsValid(uint32_t stream) {
		return Find(stream) != nullptr;
	}

	bool AudioStreams::Play(uint32_t stream) {
		StreamSlot* slot = Find(stream);
		if (!slot) {
			return false;
		}
		if (ma_sound_at_end(&slot->Sound)) {
			ma_sound_seek_to_pcm_frame(&slot->Sound, 0);
		}
		return ma_sound_start(&slot->Sound) == MA_SUCCESS;
	}

	void AudioStreams::Pause(uint32_t stream) {
		if (StreamSlot* slot = Find(stream)) {
			ma_sound_stop(&slot->Sound);
		}
	}

	void AudioStreams::Stop(uint32_t stream) {
		if (StreamSlot* slot = Find(stream)) {
			ma_sound_stop(&slot->Sound);
			ma_sound_seek_to_pcm_frame(&slot->Sound, 0);
		}
	}

	bool AudioStreams::IsPlaying(uint32_t stream) {
		const StreamSlot* slot = Find(stream);
		return slot && ma_sound_is_playing(&slot->Sound);
	}

	bool AudioStreams::IsAtEnd(uint32_t stream) {
		const StreamSlot* slot = Find(stream);
		return slot && ma_sound_at_end(&slot->Sound);
	}

	float AudioStreams::GetCursorSeconds(uint32_t stream) {
		StreamSlot* slot = Find(stream);
		float cursor = 0.0f;
		if (!slot || ma_sound_get_cursor_in_seconds(&slot->Sound, &cursor) != MA_SUCCESS) {
			return 0.0f;
		}
		return cursor;
	}

	float AudioStreams::GetLengthSeconds(uint32_t stream) {
		const StreamSlot* slot = Find(stream);
		return slot ? slot->LengthSeconds : 0.0f;
	}

	void AudioStreams::Seek(uint32_t stream, float seconds) {
		StreamSlot* slot = Find(stream);
		if (!slot) {
			return;
		}
		float target = std::max(0.0f, seconds);
		if (slot->LengthSeconds > 0.0f) {
			target = std::min(target, slot->LengthSeconds);
		}
		ma_sound_seek_to_second(&slot->Sound, target);
	}

	void AudioStreams::SetVolume(uint32_t stream, float volume) {
		if (StreamSlot* slot = Find(stream)) {
			ma_sound_set_volume(&slot->Sound, std::max(0.0f, volume));
		}
	}

	float AudioStreams::GetVolume(uint32_t stream) {
		const StreamSlot* slot = Find(stream);
		return slot ? ma_sound_get_volume(&slot->Sound) : 0.0f;
	}

	void AudioStreams::SetLooping(uint32_t stream, bool looping) {
		if (StreamSlot* slot = Find(stream)) {
			ma_sound_set_looping(&slot->Sound, looping ? MA_TRUE : MA_FALSE);
		}
	}

	bool AudioStreams::IsLooping(uint32_t stream) {
		const StreamSlot* slot = Find(stream);
		return slot && ma_sound_is_looping(&slot->Sound);
	}

}
