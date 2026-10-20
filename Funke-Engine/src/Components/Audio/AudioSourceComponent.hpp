#pragma once
#include "Audio/AudioHandle.hpp"
#include "Audio/AudioSpatial.hpp"
#include "Core/Export.hpp"
#include "Core/UUID.hpp"

#include <glm/glm.hpp>

namespace Funke {

	class FUNKE_API AudioSourceComponent {
	public:
		AudioSourceComponent() = default;
		AudioSourceComponent(const AudioHandle& audioHandle);


		void Play();
		void Pause();
		void Stop();
		void Resume();
		void Destroy();

		// Single source of the "play on awake" rule, shared by scene-start (systems / editor) and runtime prefab spawns.
		void PlayOnAwakeIfEnabled();


		void SetVolume(float volume);
		void SetPitch(float pitch);
		void SetLoop(bool loop);
		void SetAudioHandle(const AudioHandle& audioHandle, UUID assetId = UUID(0));

		void PlayOneShot();


		float GetVolume() const { return m_Volume; }
		float GetPitch() const { return m_Pitch; }
		bool IsLooping() const { return m_Loop; }
		bool IsPlaying() const;
		bool IsPaused() const;
		bool IsValid() const;

		const AudioHandle& GetAudioHandle() const { return m_AudioHandle; }
		UUID GetAudioAssetId() const { return m_AudioAssetId; }
		void SetAudioAssetId(UUID assetId) { m_AudioAssetId = assetId; }

		uint32_t GetInstanceId() const { return m_InstanceId; }
		void SetInstanceId(uint32_t id) { m_InstanceId = id; }

	private:
		AudioHandle m_AudioHandle;
		UUID m_AudioAssetId{ 0 };
		uint32_t m_InstanceId = 0;


		float m_Volume = 1.0f;
		float m_Pitch = 1.0f;
		bool m_Loop = false;
		bool m_PlayOnAwake = false;
	public:
		bool GetPlayOnAwake() const { return m_PlayOnAwake; }
		void SetPlayOnAwake(bool playOnAwake) { m_PlayOnAwake = playOnAwake; }

		// Plays from the entity's position: quieter with distance from the main camera and panned to its side.
		// Off (default) = a plain 2D sound the camera never affects.
		void SetSpatial(bool spatial);
		bool IsSpatial() const { return m_Spatial; }
		void SetSpatialSettings(const AudioSpatialSettings& settings);
		const AudioSpatialSettings& GetSpatialSettings() const { return m_SpatialSettings; }

		// Runtime, written every frame by AudioUpdateSystem from the entity's transform; a sound started now begins there.
		void SetWorldPosition(const glm::vec3& position, const glm::vec3& velocity);
		const glm::vec3& GetWorldPosition() const { return m_WorldPosition; }
		const glm::vec3& GetVelocity() const { return m_Velocity; }
		bool HasWorldPosition() const { return m_HasWorldPosition; }

	private:
		bool m_Spatial = false;
		AudioSpatialSettings m_SpatialSettings;
		glm::vec3 m_WorldPosition{ 0.0f };
		glm::vec3 m_Velocity{ 0.0f };
		bool m_HasWorldPosition = false;
	};
}
