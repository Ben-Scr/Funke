#include "pch.hpp"

#include "Assets/AssetRegistry.hpp"
#include "AudioManager.hpp"
#include "Audio.hpp"
#include "Audio/AudioStreams.hpp"
#include  <Math/Common.hpp>

#include "Serialization/FileStamp.hpp"
#include "Serialization/Path.hpp"

#include "Audio/IAudioBackend.hpp"
#include "Audio/Backends/MiniaudioBackend.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Platform.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#define MINIAUDIO_IMPLEMENTATION
// Playback only: no encoders, no waveform/noise generators.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
// miniaudio CoInitializes the calling (main) thread; its MTA default makes OleInitialize fail there, which breaks DoDragDrop. 0x2 = COINIT_APARTMENTTHREADED.
#define MA_COINIT_VALUE 0x2
#include <miniaudio.h>

#include <filesystem>
#include <unordered_set>

namespace Funke {
	namespace {
		// Low 8 bits = (index+1), high 24 bits = generation; 0 is always invalid. 8-bit index caps at 255 concurrent sounds (above the 128 config max).
		constexpr uint32_t k_AudioInstanceIndexBits = 8u;
		constexpr uint32_t k_AudioInstanceIndexMask = (1u << k_AudioInstanceIndexBits) - 1u;
		constexpr uint32_t k_AudioInstanceGenerationMask = (1u << (32u - k_AudioInstanceIndexBits)) - 1u;
		static_assert(128u <= k_AudioInstanceIndexMask,
			"index field width must hold the maximum configurable MAX_CONCURRENT_SOUNDS cap");

		uint32_t EncodeAudioInstanceId(uint32_t index, uint32_t generation) {
			return ((generation & k_AudioInstanceGenerationMask) << k_AudioInstanceIndexBits)
				| ((index + 1u) & k_AudioInstanceIndexMask);
		}

		uint32_t DecodeAudioInstanceIndex(uint32_t instanceId) {
			return (instanceId & k_AudioInstanceIndexMask) - 1u;
		}

		uint32_t DecodeAudioInstanceGeneration(uint32_t instanceId) {
			return (instanceId >> k_AudioInstanceIndexBits) & k_AudioInstanceGenerationMask;
		}

		bool DecodedAudioInstanceIsValid(uint32_t instanceId) {
			return (instanceId & k_AudioInstanceIndexMask) != 0u;
		}

		std::string NormalizeAudioPath(std::filesystem::path path)
		{
			if (path.empty()) {
				return {};
			}

			std::error_code ec;
			if (std::filesystem::exists(path, ec)) {
				std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
				if (!ec) {
					return canonicalPath.make_preferred().string();
				}
				ec.clear();
			}

			const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
			if (ec) {
				return path.lexically_normal().make_preferred().string();
			}

			return absolutePath.lexically_normal().make_preferred().string();
		}
	}

	std::unique_ptr<IAudioBackend> AudioManager::s_Backend;
	bool AudioManager::s_IsInitialized = false;

	void AudioManager::SetBackend(std::unique_ptr<IAudioBackend> backend) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined,
			"AudioManager::SetBackend must be called on the main thread");
		FNK_CORE_ASSERT(!s_IsInitialized, FunkeErrorCode::Undefined,
			"AudioManager::SetBackend must be called before Initialize");
		s_Backend = std::move(backend);
	}

	// Internal accessor: backends that don't speak miniaudio return nullptr. The
	// remaining ma_* call sites in this file fall back to a quiet no-op when the
	// active backend doesn't provide an engine pointer.
	static ma_engine* GetActiveMiniaudioEngine() {
		return AudioManager::GetBackend() ? AudioManager::GetBackend()->GetMiniaudioEngine() : nullptr;
	}
	std::unordered_map<AudioHandle::HandleType, std::unique_ptr<Audio>> AudioManager::s_audioMap;
	std::unordered_map<std::string, AudioHandle::HandleType> AudioManager::s_audioPathToHandle;
	std::unordered_map<std::string, AudioHandle::HandleType> AudioManager::s_audioRawPathToHandle;
	AudioHandle::HandleType AudioManager::s_nextHandle = 1;
	std::vector<std::unique_ptr<AudioManager::SoundInstance>> AudioManager::s_soundInstances;
	std::vector<uint32_t> AudioManager::s_freeInstanceIndices;
	float AudioManager::s_masterVolume = 1.0f;
	bool AudioManager::s_Muted = false;
	bool AudioManager::s_PlayInBackground = false;
	std::string AudioManager::s_RootPath = Path::Combine("FunkeAssets", "Audio");
	AudioListenerState AudioManager::s_Listener;

	uint32_t AudioManager::s_maxConcurrentSounds = MAX_CONCURRENT_SOUNDS;
	uint32_t AudioManager::s_maxSoundsPerFrame = MAX_SOUNDS_PER_FRAME;
	uint32_t AudioManager::s_soundsPlayedThisFrame = 0;
	uint32_t AudioManager::s_activeSoundCount = 0;
	std::priority_queue<AudioManager::SoundRequest> AudioManager::s_soundQueue;
	std::unordered_map<AudioHandle::HandleType, AudioManager::SoundLimitData> AudioManager::s_soundLimits;


	bool AudioManager::Initialize() {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::Initialize must be called on the main thread");
		if (s_IsInitialized) {
			FNK_CORE_WARN("AudioManager already initialized");
			return true;
		}

		std::string audioDir = Path::ResolveFunkeAssets("Audio");
		if (audioDir.empty()) {
			// Builds leave the folder out when no built-in sound is referenced.
			FNK_CORE_TRACE("FunkeAssets/Audio not found");
			audioDir = Path::Combine(Path::ExecutableDir(), "FunkeAssets", "Audio");
		}
		s_RootPath = audioDir;

		// Install the default backend if no package overrode it via SetBackend().
		if (!s_Backend) {
			s_Backend = std::make_unique<MiniaudioBackend>();
		}

		if (!s_Backend->Initialize()) {
			FNK_CORE_ERROR("[{}] AudioManager: backend Initialize failed",
				ErrorCodeToString(FunkeErrorCode::LoadFailed));
			s_Backend.reset();
			return false;
		}

		s_soundInstances.reserve(256);
		s_freeInstanceIndices.reserve(256);

		UpdateListener();

		s_IsInitialized = true;
		// A volume or mute set before the backend existed (startup code, --mute) would otherwise be lost.
		s_Backend->SetMasterVolume(s_Muted ? 0.0f : s_masterVolume);
		return true;
	}

	void AudioManager::Shutdown() {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::Shutdown must be called on the main thread");
		if (!s_IsInitialized) {
			FNK_CORE_WARN("AudioManager isn't initialized");
			return;
		}

		// Streams hold ma_sounds on the backend's engine and must go before it.
		AudioStreams::CloseAll();
		UnloadAllAudio();

		s_soundInstances.clear();
		s_freeInstanceIndices.clear();
		s_soundLimits.clear();
		s_activeSoundCount = 0;
		s_soundsPlayedThisFrame = 0;
		s_soundQueue = {};
		s_audioPathToHandle.clear();
		s_audioRawPathToHandle.clear();

		if (s_Backend) {
			s_Backend->Shutdown();
			s_Backend.reset();
		}
		s_IsInitialized = false;
	}

	void AudioManager::Update() {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::Update must be called on the main thread");
		if (!s_IsInitialized) {
			return;
		}

		s_soundsPlayedThisFrame = 0;
		CleanupFinishedSounds();
		RecalculateActiveSoundCount();
		ProcessSoundQueue();
		UpdateListener();
		UpdateSoundInstances();
	}

	bool AudioManager::CanPlaySound(const AudioHandle& audioHandle, float priority) {
		if (priority >= 2.0f) {
			return true;
		}


		if (s_activeSoundCount >= s_maxConcurrentSounds) {
			return priority > 1.5f;
		}

		if (s_soundsPlayedThisFrame >= s_maxSoundsPerFrame) {
			return priority > 1.8f;
		}

		return true;
	}

	void AudioManager::ProcessSoundQueue() {
		// Stale (>200ms) and throttled requests skip the start-budget; only successful starts count against maxStartsPerFrame.
		const uint32_t maxStartsPerFrame = 4;
		uint32_t startsThisCall = 0;
		uint32_t requeueGuard = 0;
		const uint32_t requeueGuardLimit = static_cast<uint32_t>(s_soundQueue.size()) + 16;

		// inFlightStarts is loop-bound only; do NOT mutate s_activeSoundCount here — RecalculateActiveSoundCount is the single source of truth.
		uint32_t inFlightStarts = 0;

		while (!s_soundQueue.empty() && startsThisCall < maxStartsPerFrame &&
			s_soundsPlayedThisFrame < s_maxSoundsPerFrame &&
			(s_activeSoundCount + inFlightStarts) < s_maxConcurrentSounds) {

			if (++requeueGuard > requeueGuardLimit) break;

			SoundRequest request = s_soundQueue.top();
			s_soundQueue.pop();

			auto now = std::chrono::steady_clock::now();
			auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - request.RequestTime);
			if (age.count() > 200) {
				continue; // dropped — too stale to bother
			}

			if (IsThrottled(request.Handle)) {
				continue; // skip but don't burn the start-budget
			}

			if (StartOneShotInstance(request.Handle, request.Volume, request.Spatial ? &request : nullptr)) {
				s_soundsPlayedThisFrame++;
				ThrottleSound(request.Handle);
				inFlightStarts++;
				startsThisCall++;
			}
		}
	}

	void AudioManager::ThrottleSound(const AudioHandle& audioHandle) {
		auto& limitData = s_soundLimits[audioHandle.GetHandle()];
		limitData.LastPlayTime = std::chrono::steady_clock::now();
		limitData.FramePlayCount++;
	}

	bool AudioManager::IsThrottled(const AudioHandle& audioHandle) {
		auto it = s_soundLimits.find(audioHandle.GetHandle());
		if (it == s_soundLimits.end()) {
			return false;
		}

		auto now = std::chrono::steady_clock::now();
		auto timeSinceLastPlay = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - it->second.LastPlayTime);

		return timeSinceLastPlay.count() < (MIN_SOUND_INTERVAL * 1000);
	}

	void AudioManager::SetMaxConcurrentSounds(uint32_t maxSounds) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::SetMaxConcurrentSounds must be called on the main thread");
		s_maxConcurrentSounds = Min(maxSounds, 128u);
	}

	void AudioManager::SetMaxSoundsPerFrame(uint32_t maxPerFrame) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::SetMaxSoundsPerFrame must be called on the main thread");
		s_maxSoundsPerFrame = Min(maxPerFrame, 16u);
	}

	uint32_t AudioManager::GetActiveSoundCount() {
		return s_activeSoundCount;
	}

	AudioHandle AudioManager::LoadAudio(const std::string_view& path) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::LoadAudio must be called on the main thread");
		if (!s_IsInitialized) {
			FNK_CORE_ERROR("[{}] AudioManager not initialized", ErrorCodeToString(FunkeErrorCode::NotInitialized));
			return AudioHandle();
		}

		const std::string requestedPath(path);

		// Probe raw-string cache first to avoid filesystem hits on repeated LoadAudio calls.
		if (const AudioHandle existing = FindAudioByRawPath(requestedPath); existing.IsValid()) {
			return existing;
		}

		const std::string fullpath = NormalizeAudioPath(std::filesystem::path(requestedPath));
		if (const AudioHandle existing = FindAudioByPath(fullpath); existing.IsValid()) {
			// Promote into the raw-cache so the next LoadAudio for this exact
			// input string skips normalization too.
			s_audioRawPathToHandle[requestedPath] = existing.GetHandle();
			return existing;
		}

		const std::string rootPath = NormalizeAudioPath(std::filesystem::path(Path::Combine(s_RootPath, requestedPath)));
		if (!rootPath.empty() && rootPath != fullpath) {
			if (const AudioHandle existing = FindAudioByPath(rootPath); existing.IsValid()) {
				s_audioRawPathToHandle[requestedPath] = existing.GetHandle();
				return existing;
			}
		}

		std::string resolvedPath = fullpath;
		auto audio = std::make_unique<Audio>();
		if (!audio->LoadFromFile(resolvedPath)) {
			resolvedPath = rootPath;
			audio = std::make_unique<Audio>();
			if (resolvedPath.empty() || !audio->LoadFromFile(resolvedPath)) {
				std::string ext = std::filesystem::path(requestedPath).extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (ext == ".ogg" || ext == ".opus") {
					// Default miniaudio decodes wav/mp3/flac only — Vorbis/Opus aren't compiled in,
					// so these fail to load. Surface the real reason instead of a generic error.
					FNK_CORE_ERROR("[{}] AudioManager: '{}' is {} (Vorbis/Opus), which this build's audio backend cannot decode. Convert it to .wav, .mp3, or .flac.",
						ErrorCodeToString(FunkeErrorCode::LoadFailed), requestedPath, ext);
				}
				else {
					FNK_CORE_ERROR("[{}] AudioManager: Failed to load audio: {}", ErrorCodeToString(FunkeErrorCode::LoadFailed), requestedPath);
				}
				return AudioHandle();
			}
		}

		KnownFileStamps::Record(resolvedPath);
		AudioHandle::HandleType id = GenerateHandle();
		if (!RegisterAudioData(*audio)) {
			FNK_CORE_WARN_TAG("AudioManager", "Falling back to on-demand audio loading for '{}'", resolvedPath);
		}
		s_audioMap[id] = std::move(audio);
		s_audioPathToHandle[resolvedPath] = id;
		s_audioRawPathToHandle[requestedPath] = id;
		return AudioHandle(id);
	}

	AudioHandle AudioManager::LoadAudioByUUID(uint64_t assetId) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::LoadAudioByUUID must be called on the main thread");
		if (assetId == 0) {
			return AudioHandle();
		}

		// IsAudio/ResolvePath each self-recover once on a miss (a single bounded rescan, then
		// cache the miss). Don't MarkDirty()+Sync() here: MarkDirty clears that negative cache,
		// so a dangling GUID would force a full O(N) Assets/ rescan ×3 and re-arm every other
		// missing GUID to rescan too.
		if (!AssetRegistry::IsAudio(assetId)) {
			return AudioHandle();
		}
		const std::string path = AssetRegistry::ResolvePath(assetId);
		if (path.empty()) {
			return AudioHandle();
		}

		return LoadAudio(path);
	}

	void AudioManager::UnloadAudio(const AudioHandle& audioHandle) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::UnloadAudio must be called on the main thread");
		if (!audioHandle.IsValid()) {
			return;
		}

		auto it = s_audioMap.find(audioHandle.GetHandle());
		if (it != s_audioMap.end()) {
			if (it->second) {
				s_audioPathToHandle.erase(it->second->GetFilepath());
				// Eagerly purge raw-path cache entries for this handle; lazy self-heal in FindAudioByRawPath could alias a stale handle after id reuse.
				const auto unloadedHandleValue = audioHandle.GetHandle();
				for (auto rit = s_audioRawPathToHandle.begin(); rit != s_audioRawPathToHandle.end(); ) {
					if (rit->second == unloadedHandleValue) rit = s_audioRawPathToHandle.erase(rit);
					else ++rit;
				}
			}

			// DO NOT REORDER: (1) recycle all SoundInstances (uninit ma_sound), (2) unregister resource, (3) erase Audio. Prevents audio thread from reading a freed PCM buffer.
			for (size_t i = 0; i < s_soundInstances.size(); ++i) {
				auto& slot = s_soundInstances[i];
				if (slot && slot->IsValid && slot->AudioHandle == audioHandle) {
					RecycleSoundInstance(static_cast<uint32_t>(i));
				}
			}

			UnregisterAudioData(*it->second);
			s_audioMap.erase(it);
			s_soundLimits.erase(audioHandle.GetHandle());
		}
	}

	void AudioManager::UnloadAllAudio() {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::UnloadAllAudio must be called on the main thread");

		for (size_t i = 0; i < s_soundInstances.size(); ++i) {
			auto& slot = s_soundInstances[i];
			if (slot && slot->IsValid) {
				RecycleSoundInstance(static_cast<uint32_t>(i));
			}
		}

		for (const auto& [id, audio] : s_audioMap) {
			if (audio) {
				UnregisterAudioData(*audio);
			}
		}

		s_audioMap.clear();
		s_audioPathToHandle.clear();
		s_audioRawPathToHandle.clear();
		s_nextHandle = 1;
		s_soundLimits.clear();
		s_soundQueue = {};
		s_activeSoundCount = 0;
	}

	size_t AudioManager::ReloadChangedFromDisk() {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::ReloadChangedFromDisk must be called on the main thread");
		if (!s_IsInitialized) {
			return 0;
		}

		size_t reloadedCount = 0;
		for (auto& [id, audio] : s_audioMap) {
			if (!audio || !KnownFileStamps::HasChangedOnDisk(audio->GetFilepath())) {
				continue;
			}

			const std::string path = audio->GetFilepath();
			KnownFileStamps::Record(path);
			auto reloaded = std::make_unique<Audio>();
			// A half-written file fails to decode; keep the old clip, the write's completion re-triggers.
			if (!reloaded->LoadFromFile(path)) {
				continue;
			}

			// Handle stays the same so AudioSourceComponents keep their clip; playing sources restart below.
			const AudioHandle handle(id);
			std::vector<AudioSourceComponent*> restartSources;
			SceneManager::ForeachLoadedScene([&handle, &restartSources](Scene& scene) {
				auto sources = scene.GetRegistry().view<AudioSourceComponent>();
				for (auto entity : sources) {
					auto& source = sources.get<AudioSourceComponent>(entity);
					if (source.GetAudioHandle() == handle && source.IsPlaying()) {
						restartSources.push_back(&source);
					}
				}
			});

			// Same order as UnloadAudio: the audio thread must never read a freed PCM buffer.
			for (size_t i = 0; i < s_soundInstances.size(); ++i) {
				auto& slot = s_soundInstances[i];
				if (slot && slot->IsValid && slot->AudioHandle == handle) {
					RecycleSoundInstance(static_cast<uint32_t>(i));
				}
			}
			UnregisterAudioData(*audio);
			audio = std::move(reloaded);
			if (!RegisterAudioData(*audio)) {
				FNK_CORE_WARN_TAG("AudioManager", "Falling back to on-demand audio loading for '{}'", path);
			}

			for (AudioSourceComponent* source : restartSources) {
				source->SetInstanceId(0);
				PlayAudioSource(*source);
			}
			++reloadedCount;
		}
		return reloadedCount;
	}

	size_t AudioManager::PurgeUnreferenced() {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::PurgeUnreferenced must be called on the main thread");
		if (!s_IsInitialized) {
			FNK_CORE_WARN("AudioManager isn't initialized");
			return 0;
		}

		// AudioSourceComponent is the only component holding an AudioHandle today.
		std::unordered_set<AudioHandle::HandleType> referenced;
		referenced.reserve(s_audioMap.size());

		SceneManager::ForeachLoadedScene([&referenced](Scene& scene) {
			entt::registry& registry = scene.GetRegistry();

			auto sources = registry.view<AudioSourceComponent>();
			for (auto entity : sources) {
				const auto& source = sources.get<AudioSourceComponent>(entity);
				const AudioHandle& handle = source.GetAudioHandle();
				if (handle.IsValid()) {
					referenced.insert(handle.GetHandle());
				}
			}
		});

		// Collect doomed handles before freeing — can't mutate s_audioMap mid-iteration.
		std::vector<AudioHandle> toFree;
		toFree.reserve(s_audioMap.size());
		for (const auto& [id, audio] : s_audioMap) {
			if (referenced.find(id) == referenced.end()) {
				toFree.emplace_back(id);
			}
		}

		for (const AudioHandle& handle : toFree) {
			UnloadAudio(handle);
		}

		const size_t freedCount = toFree.size();
		FNK_CORE_INFO_TAG("AudioManager", "Purged {} unreferenced audio entries", freedCount);
		return freedCount;
	}

	void AudioManager::PlayAudioSource(AudioSourceComponent& source) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::PlayAudioSource must be called on the main thread");
		if (!s_IsInitialized) {
			FNK_CORE_WARN("AudioManager not initialized");
			return;
		}
		if (!source.GetAudioHandle().IsValid()) {
			FNK_CORE_WARN("[{}] Invalid AudioHandle", ErrorCodeToString(FunkeErrorCode::InvalidHandle));
			return;
		}

		if (source.GetInstanceId() != 0) {
			StopAudioSource(source);
		}

		uint32_t instanceId = CreateSoundInstance(source.GetAudioHandle());
		if (instanceId == 0) {
			FNK_CORE_ERROR("[{}] Failed to create sound instance", ErrorCodeToString(FunkeErrorCode::LoadFailed));
			return;
		}

		source.SetInstanceId(instanceId);
		SoundInstance* instance = GetSoundInstance(instanceId);

		if (instance) {
			ma_sound_set_volume(&instance->Sound, source.GetVolume());
			ma_sound_set_pitch(&instance->Sound, source.GetPitch());
			ma_sound_set_looping(&instance->Sound, source.IsLooping());
			ApplySpatial(*instance, source.IsSpatial(), source.GetSpatialSettings(), source.GetWorldPosition(), source.GetVelocity());


			ma_result result = ma_sound_start(&instance->Sound);
			if (result != MA_SUCCESS) {
				FNK_CORE_ERROR("[{}] Failed to start sound playback. Error: {}", ErrorCodeToString(FunkeErrorCode::LoadFailed), static_cast<int>(result));
				source.SetInstanceId(0);
				DestroySoundInstance(instanceId);
			}
		}
		else {
			FNK_CORE_ERROR("[{}] Failed to retrieve sound instance after creation", ErrorCodeToString(FunkeErrorCode::NullReference));
			source.SetInstanceId(0);
			DestroySoundInstance(instanceId);
		}
	}

	void AudioManager::PauseAudioSource(AudioSourceComponent& source) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::PauseAudioSource must be called on the main thread");
		if (!s_IsInitialized || source.GetInstanceId() == 0) {
			return;
		}

		SoundInstance* instance = GetSoundInstance(source.GetInstanceId());
		if (instance && instance->IsValid) {
			ma_sound_stop(&instance->Sound);
		}
	}

	void AudioManager::StopAudioSource(AudioSourceComponent& source) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::StopAudioSource must be called on the main thread");
		if (!s_IsInitialized || source.GetInstanceId() == 0) {
			return;
		}

		DestroySoundInstance(source.GetInstanceId());
		source.SetInstanceId(0);
	}

	void AudioManager::ResumeAudioSource(AudioSourceComponent& source) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::ResumeAudioSource must be called on the main thread");
		if (!s_IsInitialized || source.GetInstanceId() == 0) {
			return;
		}

		SoundInstance* instance = GetSoundInstance(source.GetInstanceId());
		if (instance && instance->IsValid) {
			ma_sound_start(&instance->Sound);
		}
	}

	void AudioManager::SetMasterVolume(float volume) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::SetMasterVolume must be called on the main thread");
		s_masterVolume = Max(0.0f, Min(1.0f, volume));

		if (s_IsInitialized && s_Backend) {
			s_Backend->SetMasterVolume(s_Muted ? 0.0f : s_masterVolume);
		}
	}

	void AudioManager::SetMuted(bool muted) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::SetMuted must be called on the main thread");
		s_Muted = muted;

		if (s_IsInitialized && s_Backend) {
			s_Backend->SetMasterVolume(s_Muted ? 0.0f : s_masterVolume);
		}
	}

	void AudioManager::PauseAll() {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::PauseAll must be called on the main thread");
		if (s_IsInitialized && s_Backend) {
			s_Backend->SetSuspended(true);
		}
	}

	void AudioManager::ResumeAll() {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::ResumeAll must be called on the main thread");
		if (s_IsInitialized && s_Backend) {
			s_Backend->SetSuspended(false);
		}
	}

	void AudioManager::SetPlayInBackground(bool enabled) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::SetPlayInBackground must be called on the main thread");
		if (s_PlayInBackground == enabled) {
			return;
		}
		s_PlayInBackground = enabled;
		Platform::SetBackgroundAudioService(enabled);
		// Toggled while already in the background: apply now instead of waiting for the next pause transition.
		if (Application::IsPaused() && s_IsInitialized && s_Backend) {
			s_Backend->SetSuspended(!enabled);
		}
	}

	void AudioManager::PlayOneShot(const AudioHandle& audioHandle, float volume) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::PlayOneShot must be called on the main thread");
		if (!s_IsInitialized || !audioHandle.IsValid()) {
			return;
		}

		SoundRequest request{};
		request.Handle = audioHandle;
		request.Volume = volume;
		request.Priority = 1.0f;
		request.RequestTime = std::chrono::steady_clock::now();
		RequestOneShot(request);
	}

	void AudioManager::PlayOneShotAt(const AudioHandle& audioHandle, const glm::vec3& position, float volume, const AudioSpatialSettings& settings) {
		FNK_CORE_ASSERT(Application::IsMainThread(), FunkeErrorCode::Undefined, "AudioManager::PlayOneShotAt must be called on the main thread");
		if (!s_IsInitialized || !audioHandle.IsValid()) {
			return;
		}

		SoundRequest request{};
		request.Handle = audioHandle;
		request.Volume = volume;
		request.Priority = 1.0f;
		request.Position = position;
		request.RequestTime = std::chrono::steady_clock::now();
		request.Spatial = true;
		request.Settings = AudioSpatial::Sanitize(settings);
		// A voice spent on silence would push audible sounds out of the concurrency budget.
		if (volume * AudioSpatial::Attenuation(glm::distance(position, s_Listener.Position), request.Settings) < 0.001f) {
			return;
		}
		RequestOneShot(request);
	}

	void AudioManager::RequestOneShot(const SoundRequest& request) {
		if (IsThrottled(request.Handle) || !CanPlaySound(request.Handle, request.Priority)) {
			s_soundQueue.push(request);
			return;
		}

		if (StartOneShotInstance(request.Handle, request.Volume, request.Spatial ? &request : nullptr)) {
			s_soundsPlayedThisFrame++;
			ThrottleSound(request.Handle);
		}
	}

	void AudioManager::SetListener(const AudioListenerState& listener) {
		s_Listener = listener;
		UpdateListener();
	}

	void AudioManager::ApplySpatial(SoundInstance& instance, bool spatial, const AudioSpatialSettings& settings,
		const glm::vec3& position, const glm::vec3& velocity) {
		ma_sound* sound = &instance.Sound;
		instance.Spatial = spatial;
		if (!spatial) {
			// A plain 2D sound: wherever the listener goes, it plays as authored.
			ma_sound_set_spatialization_enabled(sound, MA_FALSE);
			ma_sound_set_pan(sound, 0.0f);
			return;
		}
		instance.Settings = AudioSpatial::Sanitize(settings);
		instance.Position = position;
		ma_sound_set_spatialization_enabled(sound, MA_TRUE);
		ma_sound_set_positioning(sound, ma_positioning_absolute);
		ma_sound_set_position(sound, position.x, position.y, position.z);
		ma_sound_set_velocity(sound, velocity.x, velocity.y, velocity.z);
		ma_sound_set_attenuation_model(sound, instance.Settings.Rolloff == AudioRolloff::Linear ? ma_attenuation_model_linear : ma_attenuation_model_inverse);
		ma_sound_set_rolloff(sound, 1.0f);
		ma_sound_set_min_distance(sound, instance.Settings.MinDistance);
		ma_sound_set_max_distance(sound, instance.Settings.MaxDistance);
		// miniaudio's own directional panning plays a sound in front at half volume; the balance pan below doesn't.
		ma_sound_set_directional_attenuation_factor(sound, 0.0f);
		ma_sound_set_doppler_factor(sound, instance.Settings.DopplerLevel);
		ma_sound_set_pan(sound, AudioSpatial::Pan(s_Listener, position, instance.Settings.Panning));
	}

	void AudioManager::UpdateSourceSpatial(const AudioSourceComponent& source) {
		if (!s_IsInitialized || source.GetInstanceId() == 0) {
			return;
		}
		SoundInstance* instance = GetSoundInstance(source.GetInstanceId());
		if (!instance || !instance->IsValid) {
			return;
		}
		if (instance->Spatial != source.IsSpatial() || (source.IsSpatial() && !(instance->Settings == source.GetSpatialSettings()))) {
			ApplySpatial(*instance, source.IsSpatial(), source.GetSpatialSettings(), source.GetWorldPosition(), source.GetVelocity());
			return;
		}
		if (!instance->Spatial) {
			return;
		}
		const glm::vec3& position = source.GetWorldPosition();
		const glm::vec3& velocity = source.GetVelocity();
		instance->Position = position;
		ma_sound_set_position(&instance->Sound, position.x, position.y, position.z);
		ma_sound_set_velocity(&instance->Sound, velocity.x, velocity.y, velocity.z);
	}

	std::vector<AudioManager::SpatialSoundInfo> AudioManager::GetSpatialSounds() {
		std::vector<SpatialSoundInfo> sounds;
		for (const auto& slot : s_soundInstances) {
			if (!slot || !slot->IsValid || !slot->Spatial || !ma_sound_is_playing(&slot->Sound)) continue;
			SpatialSoundInfo info;
			info.Name = std::filesystem::path(GetAudioName(slot->AudioHandle)).filename().string();
			info.Position = slot->Position;
			info.Volume = ma_sound_get_volume(&slot->Sound);
			info.Heard = AudioSpatial::Evaluate(s_Listener, slot->Position, slot->Settings);
			sounds.push_back(std::move(info));
		}
		return sounds;
	}

	bool AudioManager::RegisterAudioData(const Audio& audio) {
		if (!s_IsInitialized || !audio.IsLoaded() || audio.GetData() == nullptr || audio.GetFrameCount() == 0) {
			return false;
		}

		ma_engine* engine = GetActiveMiniaudioEngine();
		if (!engine) {
			return false;
		}
		ma_resource_manager* resourceManager = ma_engine_get_resource_manager(engine);
		if (!resourceManager) {
			return false;
		}

		const ma_result result = ma_resource_manager_register_decoded_data(
			resourceManager,
			audio.GetFilepath().c_str(),
			audio.GetData(),
			audio.GetFrameCount(),
			audio.GetFormat(),
			audio.GetChannels(),
			audio.GetSampleRate());
		if (result != MA_SUCCESS) {
			FNK_CORE_WARN_TAG("AudioManager", "Failed to register decoded audio data for '{}': {}", audio.GetFilepath(), static_cast<int>(result));
			return false;
		}

		return true;
	}

	void AudioManager::UnregisterAudioData(const Audio& audio) {
		if (!s_IsInitialized || audio.GetFilepath().empty()) {
			return;
		}

		if (ma_engine* engine = GetActiveMiniaudioEngine()) {
			if (ma_resource_manager* resourceManager = ma_engine_get_resource_manager(engine)) {
				ma_resource_manager_unregister_data(resourceManager, audio.GetFilepath().c_str());
			}
		}
	}

	bool AudioManager::IsAudioLoaded(const AudioHandle& audioHandle) {
		if (!audioHandle.IsValid()) {
			return false;
		}

		return s_audioMap.find(audioHandle.GetHandle()) != s_audioMap.end();
	}

	const Audio* AudioManager::GetAudio(const AudioHandle& audioHandle) {
		if (!audioHandle.IsValid()) {
			return nullptr;
		}

		auto it = s_audioMap.find(audioHandle.GetHandle());
		return (it != s_audioMap.end()) ? it->second.get() : nullptr;
	}

	std::string AudioManager::GetAudioName(const AudioHandle& audioHandle) {
		const Audio* audio = GetAudio(audioHandle);
		if (!audio) return "";
		return audio->GetFilepath();
	}

	uint64_t AudioManager::GetAudioAssetUUID(const AudioHandle& audioHandle) {
		const Audio* audio = GetAudio(audioHandle);
		if (!audio) {
			return 0;
		}

		return AssetRegistry::GetOrCreateAssetUUID(audio->GetFilepath());
	}

	AudioHandle::HandleType AudioManager::GenerateHandle() {
		return s_nextHandle++;
	}

	AudioHandle AudioManager::FindAudioByPath(const std::string& path) {
		auto pathIt = s_audioPathToHandle.find(path);
		if (pathIt == s_audioPathToHandle.end()) {
			return AudioHandle();
		}

		auto audioIt = s_audioMap.find(pathIt->second);
		if (audioIt != s_audioMap.end() && audioIt->second && audioIt->second->GetFilepath() == path) {
			return AudioHandle(pathIt->second);
		}

		s_audioPathToHandle.erase(pathIt);
		return AudioHandle();
	}

	AudioHandle AudioManager::FindAudioByRawPath(const std::string& rawPath) {
		// Raw key skips NormalizeAudioPath; stale entries (audio unloaded) are erased and caller falls through to normalized lookup.
		auto it = s_audioRawPathToHandle.find(rawPath);
		if (it == s_audioRawPathToHandle.end()) {
			return AudioHandle();
		}

		auto audioIt = s_audioMap.find(it->second);
		if (audioIt != s_audioMap.end() && audioIt->second) {
			return AudioHandle(it->second);
		}

		s_audioRawPathToHandle.erase(it);
		return AudioHandle();
	}

	uint32_t AudioManager::CreateSoundInstance(const AudioHandle& audioHandle) {
		if (!audioHandle.IsValid()) {
			return 0;
		}

		const Audio* audio = GetAudio(audioHandle);
		if (!audio || !audio->IsLoaded()) {
			return 0;
		}

		ma_engine* engine = GetActiveMiniaudioEngine();
		if (!engine) {
			return 0;
		}

		uint32_t index;
		uint32_t reuseGeneration = 0;
		bool freshSlot = false;

		if (!s_freeInstanceIndices.empty()) {
			index = s_freeInstanceIndices.back();
			s_freeInstanceIndices.pop_back();
			// Carry the slot's prior generation across the recycle so a stale handle
			// minted before this point fails GetSoundInstance.
			if (index < s_soundInstances.size() && s_soundInstances[index]) {
				reuseGeneration = s_soundInstances[index]->Generation;
			}
			s_soundInstances[index] = std::make_unique<SoundInstance>();
			s_soundInstances[index]->Generation = reuseGeneration;
		}
		else {
			// The id's 8-bit index field holds index+1, so slot 254 is the last encodable one; past it ids
			// would wrap onto slot 0 and one entity's Stop/volume would hit another entity's sound.
			if (s_soundInstances.size() >= k_AudioInstanceIndexMask) {
				FNK_CORE_WARN("AudioManager: Too many live sound instances ({}); not creating another",
					s_soundInstances.size());
				return 0;
			}
			index = static_cast<uint32_t>(s_soundInstances.size());
			s_soundInstances.emplace_back(std::make_unique<SoundInstance>());
			freshSlot = true;
		}

		SoundInstance& instance = *s_soundInstances[index];
		const ma_uint32 dataSourceFlags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE;
		ma_result result = ma_resource_manager_data_source_init(
			ma_engine_get_resource_manager(engine),
			audio->GetFilepath().c_str(),
			dataSourceFlags,
			nullptr,
			&instance.DataSource);
		if (result != MA_SUCCESS) {
			FNK_CORE_WARN("[{}] AudioManager: Failed to create sound data source. Error: {}", ErrorCodeToString(FunkeErrorCode::LoadFailed), static_cast<int>(result));
			if (freshSlot && index == s_soundInstances.size() - 1) {
				s_soundInstances.pop_back();
			}
			else {
				// Capture bumped generation before reset(); slot must be non-null with correct generation for next reuse.
				const uint32_t nextGeneration = reuseGeneration + 1u;
				s_soundInstances[index].reset();
				s_soundInstances[index] = std::make_unique<SoundInstance>();
				s_soundInstances[index]->Generation = nextGeneration;
				s_soundInstances[index]->IsValid = false;
				s_freeInstanceIndices.push_back(index);
			}
			return 0;
		}

		instance.HasDataSource = true;
		result = ma_sound_init_from_data_source(engine, &instance.DataSource, 0, nullptr, &instance.Sound);
		if (result != MA_SUCCESS) {
			ma_resource_manager_data_source_uninit(&instance.DataSource);
			instance.HasDataSource = false;
			FNK_CORE_WARN("[{}] AudioManager: Failed to create sound instance. Error: {}", ErrorCodeToString(FunkeErrorCode::LoadFailed), static_cast<int>(result));
			if (freshSlot && index == s_soundInstances.size() - 1) {
				s_soundInstances.pop_back();
			}
			else {
				const uint32_t nextGeneration = reuseGeneration + 1u;
				s_soundInstances[index].reset();
				s_soundInstances[index] = std::make_unique<SoundInstance>();
				s_soundInstances[index]->Generation = nextGeneration;
				s_soundInstances[index]->IsValid = false;
				s_freeInstanceIndices.push_back(index);
			}
			return 0;
		}

		instance.AudioHandle = audioHandle;
		instance.IsValid = true;

		return EncodeAudioInstanceId(index, instance.Generation);
	}

	void AudioManager::DestroySoundInstance(uint32_t instanceId) {
		if (!DecodedAudioInstanceIsValid(instanceId)) {
			return;
		}

		const uint32_t index = DecodeAudioInstanceIndex(instanceId);
		if (index >= s_soundInstances.size()) {
			return;
		}

		// Stale handle (generation mismatch) must be a no-op, not a free of the live sound.
		auto& slot = s_soundInstances[index];
		if (!slot || (slot->Generation & k_AudioInstanceGenerationMask)
				!= DecodeAudioInstanceGeneration(instanceId)) {
			return;
		}

		RecycleSoundInstance(index);
	}

	AudioManager::SoundInstance* AudioManager::GetSoundInstance(uint32_t instanceId) {
		if (!DecodedAudioInstanceIsValid(instanceId)) {
			return nullptr;
		}

		const uint32_t index = DecodeAudioInstanceIndex(instanceId);
		if (index >= s_soundInstances.size()) {
			return nullptr;
		}

		auto& slot = s_soundInstances[index];
		if (!slot || !slot->IsValid) {
			return nullptr;
		}
		// Mask to encoded width: a counter that has rolled past 2^24 still matches the id's low 24 bits.
		if ((slot->Generation & k_AudioInstanceGenerationMask) != DecodeAudioInstanceGeneration(instanceId)) {
			return nullptr;
		}

		return slot.get();
	}

	void AudioManager::RecycleSoundInstance(uint32_t index) {
		if (index >= s_soundInstances.size()) {
			return;
		}

		auto& slot = s_soundInstances[index];
		if (!slot || !slot->IsValid) {
			return;
		}

		SoundInstance& instance = *slot;
		ma_sound_stop(&instance.Sound);
		ma_sound_uninit(&instance.Sound);
		if (instance.HasDataSource) {
			ma_resource_manager_data_source_uninit(&instance.DataSource);
			instance.HasDataSource = false;
		}

		// Bump generation before reset() so stale handles see the old generation, not zero from a fresh alloc.
		const uint32_t nextGeneration = instance.Generation + 1u;

		slot.reset();

		// Sentinel slot carries the bumped generation so CreateSoundInstance's reuseGeneration read is correct on next reuse.
		slot = std::make_unique<SoundInstance>();
		slot->Generation = nextGeneration;
		slot->IsValid = false;
		s_freeInstanceIndices.push_back(index);
	}

	void AudioManager::CleanupFinishedSounds() {
		for (size_t i = 0; i < s_soundInstances.size(); ++i) {
			auto& slot = s_soundInstances[i];
			if (!slot || !slot->IsValid) continue;
			SoundInstance& instance = *slot;

			// ma_sound_at_end required: "not playing && not looping" also matches paused sounds and would break Resume().
			if (!ma_sound_is_playing(&instance.Sound)
				&& !ma_sound_is_looping(&instance.Sound)
				&& ma_sound_at_end(&instance.Sound) == MA_TRUE) {
				RecycleSoundInstance(static_cast<uint32_t>(i));
			}
		}
	}

	void AudioManager::UpdateListener() {
		ma_engine* engine = GetActiveMiniaudioEngine();
		if (!engine) {
			return;
		}
		const AudioListenerState& l = s_Listener;
		ma_engine_listener_set_position(engine, 0, l.Position.x, l.Position.y, l.Position.z);
		ma_engine_listener_set_direction(engine, 0, l.Forward.x, l.Forward.y, l.Forward.z);
		ma_engine_listener_set_world_up(engine, 0, l.Up.x, l.Up.y, l.Up.z);
		ma_engine_listener_set_velocity(engine, 0, l.Velocity.x, l.Velocity.y, l.Velocity.z);
	}

	void AudioManager::UpdateSoundInstances() {
		if (!s_IsInitialized) {
			return;
		}
		// The listener turns and moves; the side a sound comes from changes with it.
		for (const auto& slot : s_soundInstances) {
			if (!slot || !slot->IsValid || !slot->Spatial) continue;
			ma_sound_set_pan(&slot->Sound, AudioSpatial::Pan(s_Listener, slot->Position, slot->Settings.Panning));
		}
	}

	void AudioManager::RecalculateActiveSoundCount() {
		s_activeSoundCount = 0;
		for (const auto& slot : s_soundInstances) {
			if (slot && slot->IsValid && ma_sound_is_playing(&slot->Sound)) {
				s_activeSoundCount++;
			}
		}
	}

	bool AudioManager::StartOneShotInstance(const AudioHandle& audioHandle, float volume, const SoundRequest* spatial) {
		const uint32_t instanceId = CreateSoundInstance(audioHandle);
		if (instanceId == 0) {
			FNK_CORE_WARN("[{}] Failed to create one-shot sound instance", ErrorCodeToString(FunkeErrorCode::LoadFailed));
			return false;
		}

		SoundInstance* instance = GetSoundInstance(instanceId);
		if (!instance) {
			FNK_CORE_WARN("[{}] Failed to retrieve one-shot sound instance", ErrorCodeToString(FunkeErrorCode::NullReference));
			DestroySoundInstance(instanceId);
			return false;
		}

		ma_sound_set_volume(&instance->Sound, volume);
		if (spatial) ApplySpatial(*instance, true, spatial->Settings, spatial->Position, glm::vec3(0.0f));
		else ApplySpatial(*instance, false, {}, glm::vec3(0.0f), glm::vec3(0.0f));
		const ma_result result = ma_sound_start(&instance->Sound);
		if (result != MA_SUCCESS) {
			FNK_CORE_WARN("[{}] Failed to start one-shot sound. Error: {}", ErrorCodeToString(FunkeErrorCode::LoadFailed), static_cast<int>(result));
			DestroySoundInstance(instanceId);
			return false;
		}

		return true;
	}

}
