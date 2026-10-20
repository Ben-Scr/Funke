#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Audio/AudioManager.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"

// Audio sources.
// Split out of BuiltInComponentRegistration.cpp, which owns the call order.
namespace Funke::BuiltInComponentRegistrationInternal {
	namespace {
		bool IsSpatial(const Entity& e) {
			return e.GetComponent<AudioSourceComponent>().IsSpatial();
		}

		PropertyDescriptor MakeSpatialFloat(const char* name, const char* displayName, float AudioSpatialSettings::* field,
			double min, double max, float dragSpeed, const char* tooltip) {
			return Properties::MakeWith<float>(name, displayName,
				[field](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetSpatialSettings().*field; },
				[field](Entity& e, float v) {
					auto& src = e.GetComponent<AudioSourceComponent>();
					AudioSpatialSettings s = src.GetSpatialSettings();
					s.*field = v;
					src.SetSpatialSettings(s);
				},
				Properties::Meta::Clamp(min, max, dragSpeed).WithTooltip(tooltip).WithEnabledIf(IsSpatial));
		}
	}

	void RegisterAudio(ComponentRegistry& registry) {
		using Funke::Codegen::RegisterComponent;

		// ── Audio ───────────────────────────────────────────────────

		RegisterComponent<AudioSourceComponent>(registry, "Audio Source",
			ComponentCategory::Component, "Audio", "AudioSource",
			{
				Properties::MakeWith<bool>("PlayOnAwake", "Play On Awake",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetPlayOnAwake(); },
					[](Entity& e, bool v) {
						e.GetComponent<AudioSourceComponent>().SetPlayOnAwake(v);
					}),
				Properties::MakeWith<float>("Volume", "Volume",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetVolume(); },
					[](Entity& e, float v) {
						e.GetComponent<AudioSourceComponent>().SetVolume(v);
					},
					Properties::Meta::Clamp(0.0, 1.0, 0.01f)),
				Properties::MakeWith<float>("Pitch", "Pitch",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetPitch(); },
					[](Entity& e, float v) {
						e.GetComponent<AudioSourceComponent>().SetPitch(v);
					},
					Properties::Meta::Clamp(0.1, 3.0, 0.01f)),
				Properties::MakeWith<bool>("Loop", "Loop",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().IsLooping(); },
					[](Entity& e, bool v) {
						e.GetComponent<AudioSourceComponent>().SetLoop(v);
					}),
				Properties::MakeAudioRef("Clip", "Clip",
					[](const Entity& e) -> uint64_t {
						const auto& src = e.GetComponent<AudioSourceComponent>();
						const AudioHandle h = src.GetAudioHandle();
						return h.IsValid() ? AudioManager::GetAudioAssetUUID(h) : 0ull;
					},
					[](Entity& e, uint64_t uuid) {
						auto& src = e.GetComponent<AudioSourceComponent>();
						const AudioHandle h = uuid != 0
							? AudioManager::LoadAudioByUUID(uuid)
							: AudioHandle{};
						src.SetAudioHandle(h, UUID(uuid));
					}),
				Properties::MakeWith<bool>("Spatial", "Spatial",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().IsSpatial(); },
					[](Entity& e, bool v) { e.GetComponent<AudioSourceComponent>().SetSpatial(v); },
					Properties::Meta::Header("3D Sound").WithTooltip("Plays from this entity's position: quieter with distance "
						"from the main camera and panned to its side. Off = a plain 2D sound.")),
				MakeSpatialFloat("MinDistance", "Min Distance", &AudioSpatialSettings::MinDistance, 0.0, 10000.0, 0.1f,
					"Full volume up to this distance."),
				MakeSpatialFloat("MaxDistance", "Max Distance", &AudioSpatialSettings::MaxDistance, 0.0, 10000.0, 0.5f,
					"Linear rolloff is silent from here; logarithmic rolloff stops getting quieter here."),
				Properties::MakeWith<AudioRolloff>("Rolloff", "Rolloff",
					[](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetSpatialSettings().Rolloff; },
					[](Entity& e, AudioRolloff v) {
						auto& src = e.GetComponent<AudioSourceComponent>();
						AudioSpatialSettings s = src.GetSpatialSettings();
						s.Rolloff = v;
						src.SetSpatialSettings(s);
					},
					Properties::Meta::Tooltip("Logarithmic: volume Min Distance / distance, the natural falloff. "
						"Linear: from full volume at Min Distance to silence at Max Distance.").WithEnabledIf(IsSpatial)),
				MakeSpatialFloat("Panning", "Panning", &AudioSpatialSettings::Panning, 0.0, 1.0, 0.01f,
					"How far a sound to the side moves to that ear: 0 = not at all, 1 = silent on the other ear."),
				MakeSpatialFloat("DopplerLevel", "Doppler Level", &AudioSpatialSettings::DopplerLevel, 0.0, 10.0, 0.01f,
					"Pitch shift while the sound and the camera move towards or away from each other; 0 = off, 1 = physical."),
			});
	}
}
