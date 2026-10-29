#pragma once

#include "AudioHandle.hpp"
#include "Audio/AudioSpatial.hpp"
#include "Core/Export.hpp"

#include <miniaudio.h>
#include <glm/vec3.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Funke {
	class AudioSourceComponent;
	class Audio;
	class IAudioBackend;

	/// AudioManager — global audio facade. THREAD CONTRACT: all public statics are main-thread-only; calling from a worker thread is UB.
	class FUNKE_API AudioManager {
	public:
		static constexpr uint32_t MAX_CONCURRENT_SOUNDS = 64;
		static constexpr uint32_t MAX_SOUNDS_PER_FRAME = 8;
		static constexpr uint32_t MAX_QUEUED_SOUNDS = 256;
		static constexpr float MIN_SOUND_INTERVAL = 0.1f;

		static bool Initialize();
		static void Shutdown();
		static void Update();

		static void SetBackend(std::unique_ptr<IAudioBackend> backend);
		static IAudioBackend* GetBackend() { return s_Backend.get(); }
		using TimeSource = std::chrono::steady_clock::time_point (*)();
		// Set before Initialize; nullptr restores the steady clock after a test.
		static void SetTimeSourceForTests(TimeSource timeSource);


		static AudioHandle LoadAudio(const std::string_view& path);
		static AudioHandle LoadAudioByUUID(uint64_t assetId);
		static AudioHandle FindLoadedAudio(std::string_view path);
		static AudioHandle LoadDecodedAudio(std::unique_ptr<Audio> audio);
		static void UnloadAudio(const AudioHandle& audioHandle);
		static void UnloadAllAudio();

		/// Frees Audio entries not referenced by any Scene component. Returns the number of entries freed.
		static size_t PurgeUnreferenced();

		/// Re-decodes loaded clips whose file changed outside the engine, keeping their handles. Returns how many were reloaded.
		static size_t ReloadChangedFromDisk();


		static void PlayAudioSource(AudioSourceComponent& source);
		static void PauseAudioSource(AudioSourceComponent& source);
		static void StopAudioSource(AudioSourceComponent& source);
		static void ResumeAudioSource(AudioSourceComponent& source);


		static void SetMasterVolume(float volume);
		static float GetMasterVolume() { return s_masterVolume; }
		// Silences the output but keeps the master volume a game sets (automated runs, --mute).
		static void SetMuted(bool muted);
		static bool IsMuted() { return s_Muted; }

		/// Global suspend/resume of all audio output, used when the application is background-paused.
		/// Freezes every playing sound in place; ResumeAll continues from the same point (a pause, not a stop).
		static void PauseAll();
		static void ResumeAll();

		/// Background audio (music players, podcasts): while enabled, sounds keep playing and the game keeps
		/// updating (without rendering) when the app is minimized, unfocused or sent to the background.
		/// On Android this also runs a foreground media service, which the APK must declare (build option backgroundAudio).
		static void SetPlayInBackground(bool enabled);
		static bool GetPlayInBackground() { return s_PlayInBackground; }


		// Deferred one-shots expire after 200 ms; overflow keeps the highest-priority, oldest requests.
		static void PlayOneShot(const AudioHandle& audioHandle, float volume = 1.0f);
		// From a world position: quieter with distance from the listener and panned to its side. Not started when it
		// would be silent there (beyond MaxDistance with Linear rolloff).
		static void PlayOneShotAt(const AudioHandle& audioHandle, const glm::vec3& position, float volume = 1.0f,
			const AudioSpatialSettings& settings = {});

		// Spatial sounds are heard from here; AudioUpdateSystem follows the main camera every frame.
		static void SetListener(const AudioListenerState& listener);
		static const AudioListenerState& GetListener() { return s_Listener; }
		// Brings a playing source's sound in line with the component: spatial on/off, settings, position.
		static void UpdateSourceSpatial(const AudioSourceComponent& source);

		struct SpatialSoundInfo {
			std::string Name;
			glm::vec3 Position{ 0.0f };
			float Volume = 1.0f;
			AudioSpatial::Result Heard;
		};
		// Playing spatial sounds with what the listener hears of them (the audio.spatial console command).
		static std::vector<SpatialSoundInfo> GetSpatialSounds();

		static void SetMaxConcurrentSounds(uint32_t maxSounds);
		static void SetMaxSoundsPerFrame(uint32_t maxPerFrame);
		static uint32_t GetActiveSoundCount();
		static uint32_t GetQueuedSoundCount();


		static bool IsInitialized() { return s_IsInitialized; }
		static bool IsAudioLoaded(const AudioHandle& audioHandle);
		static const Audio* GetAudio(const AudioHandle& audioHandle);
		static std::string GetAudioName(const AudioHandle& audioHandle);
		static uint64_t GetAudioAssetUUID(const AudioHandle& audioHandle);


		struct SoundInstance {
			ma_resource_manager_data_source DataSource{};
			ma_sound Sound{};
			AudioHandle AudioHandle;
			uint32_t Generation = 0;
			bool HasDataSource = false;
			bool IsValid = false;
			// Spatial sounds re-pan every frame from this position as the listener moves.
			bool Spatial = false;
			glm::vec3 Position{ 0.0f };
			AudioSpatialSettings Settings;
		};

		static SoundInstance* GetSoundInstance(uint32_t instanceId);

	private:
		AudioManager() = delete;
		~AudioManager() = delete;
		AudioManager(const AudioManager&) = delete;
		AudioManager& operator=(const AudioManager&) = delete;


		static std::unique_ptr<IAudioBackend> s_Backend;
		static bool s_IsInitialized;

		struct SoundRequest {
			AudioHandle Handle;
			float Volume;
			float Priority;
			glm::vec3 Position;
			std::chrono::steady_clock::time_point RequestTime;
			bool Spatial = false;
			AudioSpatialSettings Settings;

			bool operator<(const SoundRequest& other) const {
				return Priority == other.Priority ? RequestTime > other.RequestTime : Priority < other.Priority;
			}
		};

		struct SoundLimitData {
			std::chrono::steady_clock::time_point LastPlayTime;
			uint32_t FramePlayCount;
		};


		static uint32_t s_maxConcurrentSounds;
		static uint32_t s_maxSoundsPerFrame;
		static uint32_t s_soundsPlayedThisFrame;
		static uint32_t s_activeSoundCount;


		static std::vector<SoundRequest> s_soundQueue;
		static std::unordered_map<AudioHandle::HandleType, SoundLimitData> s_soundLimits;


		static bool CanPlaySound(const AudioHandle& audioHandle, float priority);
		static void ProcessSoundQueue();
		static void PruneSoundQueue(std::chrono::steady_clock::time_point now);
		static void QueueSound(const SoundRequest& request);
		static void ThrottleSound(const AudioHandle& audioHandle);
		static bool IsThrottled(const AudioHandle& audioHandle);


		static std::unordered_map<AudioHandle::HandleType, std::unique_ptr<Audio>> s_audioMap;
		static std::unordered_map<std::string, AudioHandle::HandleType> s_audioPathToHandle;
		static std::unordered_map<std::string, AudioHandle::HandleType> s_audioRawPathToHandle;
		static AudioHandle::HandleType s_nextHandle;

		// unique_ptr keeps slot addresses stable — a bare vector would relocate ma_sound on growth and crash mid-playback.
		static std::vector<std::unique_ptr<SoundInstance>> s_soundInstances;
		static std::vector<uint32_t> s_freeInstanceIndices;


		static float s_masterVolume;
		static bool s_Muted;
		static bool s_PlayInBackground;
		static std::string s_RootPath;
		static AudioListenerState s_Listener;


		static AudioHandle::HandleType GenerateHandle();
		static AudioHandle FindAudioByPath(const std::string& path);
		static AudioHandle FindAudioByRawPath(const std::string& rawPath);
		static bool RegisterAudioData(const Audio& audio);
		static void UnregisterAudioData(const Audio& audio);
		static uint32_t CreateSoundInstance(const AudioHandle& audioHandle);
		static void DestroySoundInstance(uint32_t instanceId);
		static void RecycleSoundInstance(uint32_t index);
		static void CleanupFinishedSounds();
		static void RecalculateActiveSoundCount();
		// spatial: the request's position and settings; null = a plain 2D one-shot.
		static bool StartOneShotInstance(const AudioHandle& audioHandle, float volume, const SoundRequest* spatial = nullptr);
		static void RequestOneShot(const SoundRequest& request);
		static void ApplySpatial(SoundInstance& instance, bool spatial, const AudioSpatialSettings& settings,
			const glm::vec3& position, const glm::vec3& velocity);

		static void UpdateListener();
		static void UpdateSoundInstances();

		friend class AudioSourceComponent;
	};
}
