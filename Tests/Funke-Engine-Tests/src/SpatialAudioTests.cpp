#include <doctest/doctest.h>

#include "Audio/AudioManager.hpp"
#include "Audio/AudioSpatial.hpp"
#include "Audio/Backends/MiniaudioBackend.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "DevTools/DevConsole.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Systems/AudioUpdateSystem.hpp"

#if FUNKE_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#endif

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

using namespace Funke;

namespace {
	// Mono 48 kHz at a constant 0.5: each speaker then carries 0.5 times its gain.
	std::filesystem::path WriteConstantWav(const std::string& name, uint32_t frames) {
		const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
		std::ofstream out(path, std::ios::binary);
		const auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
		const auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
		out.write("RIFF", 4); u32(36 + frames * 2); out.write("WAVE", 4);
		out.write("fmt ", 4); u32(16); u16(1); u16(1); u32(48000); u32(48000 * 2); u16(2); u16(16);
		out.write("data", 4); u32(frames * 2);
		const int16_t half = 16384;
		for (uint32_t i = 0; i < frames; ++i) out.write(reinterpret_cast<const char*>(&half), sizeof(half));
		return path;
	}

	struct Speakers {
		float Left = 0.0f;
		float Right = 0.0f;
	};

	// Mixes a whole clip and averages its second half, past miniaudio's gain smoothing.
	Speakers Mix(MiniaudioBackend& backend, uint32_t frames) {
		std::vector<float> mix(static_cast<size_t>(frames) * 2, 0.0f);
		REQUIRE(backend.ReadFrames(mix.data(), frames) == frames);
		Speakers out;
		for (uint32_t i = frames / 2; i < frames; ++i) {
			out.Left += mix[i * 2];
			out.Right += mix[i * 2 + 1];
		}
		out.Left /= static_cast<float>(frames / 2);
		out.Right /= static_cast<float>(frames / 2);
		return out;
	}

	struct AudioConcurrencyScope {
		MiniaudioBackend* Backend = nullptr;
		std::vector<std::filesystem::path> Files;
		float PreviousVolume = AudioManager::GetMasterVolume();
		bool PreviouslyMuted = AudioManager::IsMuted();

		AudioConcurrencyScope() {
			auto backend = std::make_unique<MiniaudioBackend>(true);
			Backend = backend.get();
			AudioManager::SetBackend(std::move(backend));
			AudioManager::SetMasterVolume(1.0f);
			AudioManager::SetMuted(false);
			AudioManager::SetMaxConcurrentSounds(1);
			AudioManager::SetMaxSoundsPerFrame(16);
		}

		~AudioConcurrencyScope() {
			if (AudioManager::IsInitialized()) AudioManager::Shutdown();
			AudioManager::SetMaxConcurrentSounds(AudioManager::MAX_CONCURRENT_SOUNDS);
			AudioManager::SetMaxSoundsPerFrame(AudioManager::MAX_SOUNDS_PER_FRAME);
			AudioManager::SetMasterVolume(PreviousVolume);
			AudioManager::SetMuted(PreviouslyMuted);
			std::error_code ec;
			for (const auto& file : Files) std::filesystem::remove(file, ec);
		}

		AudioHandle Load(const char* name, uint32_t frames) {
			Files.push_back(WriteConstantWav(name, frames));
			return AudioManager::LoadAudio(Files.back().string());
		}
	};
}

TEST_CASE("Audio decodes Ogg Vorbis clips through the headless backend") {
	const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	const auto fixture = root / "External" / "miniaudio" / "data" / "48000-stereo.ogg";
	REQUIRE(std::filesystem::is_regular_file(fixture));

	AudioConcurrencyScope audio;
	REQUIRE(AudioManager::Initialize());
	const AudioHandle clip = AudioManager::LoadAudio(fixture.string());
	REQUIRE(clip.IsValid());
	REQUIRE(AudioManager::IsAudioLoaded(clip));
	AudioManager::PlayOneShot(clip);
	REQUIRE(AudioManager::GetActiveSoundCount() == 1);

	constexpr uint32_t k_Frames = 48000;
	std::vector<float> mixed(static_cast<size_t>(k_Frames) * 2, 0.0f);
	REQUIRE(audio.Backend->ReadFrames(mixed.data(), k_Frames) == k_Frames);
	double energy = 0.0;
	for (float sample : mixed) energy += static_cast<double>(sample) * sample;
	CHECK(std::isfinite(energy));
	CHECK(energy > 0.001);
}

TEST_CASE("Audio one-shot concurrency limit holds across immediate starts") {
	AudioConcurrencyScope audio;
	REQUIRE(AudioManager::Initialize());
	constexpr uint32_t k_Frames = 8192;
	const AudioHandle first = audio.Load("FunkeAudioLimitImmediateA.wav", k_Frames);
	const AudioHandle second = audio.Load("FunkeAudioLimitImmediateB.wav", k_Frames);
	REQUIRE(first.IsValid());
	REQUIRE(second.IsValid());

	AudioManager::PlayOneShot(first);
	AudioManager::PlayOneShot(second);
	CHECK(AudioManager::GetActiveSoundCount() == 1);
	const Speakers heard = Mix(*audio.Backend, k_Frames);
	CHECK(heard.Left == doctest::Approx(0.5f).epsilon(0.001));
	CHECK(heard.Right == doctest::Approx(0.5f).epsilon(0.001));
}

TEST_CASE("Audio one-shot concurrency limit counts queued starts before new requests") {
	AudioConcurrencyScope audio;
	REQUIRE(AudioManager::Initialize());
	constexpr uint32_t k_Frames = 8192;
	const AudioHandle blocker = audio.Load("FunkeAudioLimitBlocker.wav", k_Frames);
	const AudioHandle queued = audio.Load("FunkeAudioLimitQueued.wav", k_Frames);
	const AudioHandle immediate = audio.Load("FunkeAudioLimitAfterQueue.wav", k_Frames);
	REQUIRE(blocker.IsValid());
	REQUIRE(queued.IsValid());
	REQUIRE(immediate.IsValid());
	AudioSourceComponent source(blocker);
	source.Play();
	AudioManager::Update();
	AudioManager::PlayOneShot(queued);
	source.Stop();
	AudioManager::Update();
	AudioManager::PlayOneShot(immediate);
	CHECK(AudioManager::GetActiveSoundCount() == 1);
	const Speakers heard = Mix(*audio.Backend, k_Frames);
	CHECK(heard.Left == doctest::Approx(0.5f).epsilon(0.001));
	CHECK(heard.Right == doctest::Approx(0.5f).epsilon(0.001));
}

TEST_CASE("Audio one-shot concurrency limit uses current AudioSource playback state") {
	AudioConcurrencyScope audio;
	REQUIRE(AudioManager::Initialize());
	constexpr uint32_t k_Frames = 8192;
	const AudioHandle sourceAudio = audio.Load("FunkeAudioLimitSource.wav", k_Frames);
	const AudioHandle oneShot = audio.Load("FunkeAudioLimitSourceOneShot.wav", k_Frames);
	REQUIRE(sourceAudio.IsValid());
	REQUIRE(oneShot.IsValid());
	AudioSourceComponent source(sourceAudio);
	source.SetVolume(0.25f);
	float expected = 0.125f;

	SUBCASE("a source started this frame occupies the only voice") {
		source.Play();
	}
	SUBCASE("a source resumed this frame occupies the only voice") {
		source.Play();
		source.Pause();
		AudioManager::Update();
		source.Resume();
	}
	SUBCASE("pausing a source releases its voice immediately") {
		source.Play();
		AudioManager::Update();
		source.Pause();
		expected = 0.5f;
	}
	SUBCASE("stopping a source releases its voice immediately") {
		source.Play();
		AudioManager::Update();
		source.Stop();
		expected = 0.5f;
	}
	SUBCASE("a finished source releases its voice before the next frame update") {
		source.Play();
		AudioManager::Update();
		Mix(*audio.Backend, k_Frames + 1024);
		REQUIRE_FALSE(source.IsPlaying());
		expected = 0.5f;
	}

	AudioManager::PlayOneShot(oneShot);
	CHECK(AudioManager::GetActiveSoundCount() == 1);
	const Speakers heard = Mix(*audio.Backend, k_Frames);
	CHECK(heard.Left == doctest::Approx(expected).epsilon(0.001));
	CHECK(heard.Right == doctest::Approx(expected).epsilon(0.001));
}

TEST_CASE("Audio queued one-shots expire while playback is blocked") {
	AudioConcurrencyScope audio;
	REQUIRE(AudioManager::Initialize());
	const AudioHandle clip = audio.Load("FunkeAudioQueueExpiry.wav", 8192);
	REQUIRE(clip.IsValid());
	AudioSourceComponent source(clip);
	source.SetLoop(true);
	source.Play();

	SUBCASE("all voices are occupied") {}
	SUBCASE("the frame start budget is zero") {
		AudioManager::SetMaxConcurrentSounds(2);
		AudioManager::SetMaxSoundsPerFrame(0);
	}
	SUBCASE("the voice budget is zero") {
		AudioManager::SetMaxConcurrentSounds(0);
	}

	for (uint32_t i = 0; i < AudioManager::MAX_QUEUED_SOUNDS * 8; ++i) {
		AudioManager::PlayOneShot(clip);
	}
	CHECK(AudioManager::GetQueuedSoundCount() <= AudioManager::MAX_QUEUED_SOUNDS);
	REQUIRE(AudioManager::GetQueuedSoundCount() > 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	AudioManager::Update();
	CHECK(AudioManager::GetQueuedSoundCount() == 0);
	CHECK(AudioManager::GetActiveSoundCount() == 1);
}

TEST_CASE("Audio queue overflow preserves the earliest equal-priority sound") {
	AudioConcurrencyScope audio;
	REQUIRE(AudioManager::Initialize());
	constexpr uint32_t k_Frames = 8192;
	const AudioHandle blocker = audio.Load("FunkeAudioQueueOverflowBlocker.wav", k_Frames);
	const AudioHandle queued = audio.Load("FunkeAudioQueueOverflow.wav", k_Frames);
	REQUIRE(blocker.IsValid());
	REQUIRE(queued.IsValid());
	AudioSourceComponent source(blocker);
	source.Play();
	AudioManager::PlayOneShot(queued, 0.25f);
	for (uint32_t i = 0; i < AudioManager::MAX_QUEUED_SOUNDS * 8; ++i) {
		AudioManager::PlayOneShot(queued);
	}
	CHECK(AudioManager::GetQueuedSoundCount() == AudioManager::MAX_QUEUED_SOUNDS);
	source.Stop();
	AudioManager::Update();
	CHECK(AudioManager::GetActiveSoundCount() == 1);
	const Speakers heard = Mix(*audio.Backend, k_Frames);
	CHECK(heard.Left == doctest::Approx(0.125f).epsilon(0.001));
	CHECK(heard.Right == doctest::Approx(0.125f).epsilon(0.001));
}

TEST_CASE("Audio queue releases requests for unloaded clips while voices are occupied") {
	AudioConcurrencyScope audio;
	REQUIRE(AudioManager::Initialize());
	const AudioHandle blocker = audio.Load("FunkeAudioQueueUnloadBlocker.wav", 8192);
	const AudioHandle queued = audio.Load("FunkeAudioQueueUnload.wav", 8192);
	REQUIRE(blocker.IsValid());
	REQUIRE(queued.IsValid());
	AudioSourceComponent source(blocker);
	source.Play();
	AudioManager::PlayOneShot(queued);
	REQUIRE(AudioManager::GetQueuedSoundCount() == 1);
	AudioManager::UnloadAudio(queued);
	AudioManager::Update();
	CHECK(AudioManager::GetQueuedSoundCount() == 0);
	CHECK(AudioManager::GetActiveSoundCount() == 1);
}

TEST_CASE("Spatial audio fades like miniaudio's inverse and linear models") {
	AudioSpatialSettings log;
	log.MinDistance = 2.0f;
	log.MaxDistance = 20.0f;
	CHECK(AudioSpatial::Attenuation(1.0f, log) == doctest::Approx(1.0f));
	CHECK(AudioSpatial::Attenuation(2.0f, log) == doctest::Approx(1.0f));
	CHECK(AudioSpatial::Attenuation(4.0f, log) == doctest::Approx(0.5f));
	CHECK(AudioSpatial::Attenuation(10.0f, log) == doctest::Approx(0.2f));
	// Held from Max Distance on.
	CHECK(AudioSpatial::Attenuation(400.0f, log) == doctest::Approx(0.1f));

	AudioSpatialSettings linear = log;
	linear.Rolloff = AudioRolloff::Linear;
	CHECK(AudioSpatial::Attenuation(2.0f, linear) == doctest::Approx(1.0f));
	CHECK(AudioSpatial::Attenuation(11.0f, linear) == doctest::Approx(0.5f));
	CHECK(AudioSpatial::Attenuation(20.0f, linear) == doctest::Approx(0.0f));
	CHECK(AudioSpatial::Attenuation(25.0f, linear) == doctest::Approx(0.0f));

	AudioSpatialSettings flat;
	flat.MinDistance = flat.MaxDistance = 5.0f;
	CHECK(AudioSpatial::Attenuation(50.0f, flat) == doctest::Approx(1.0f));
}

TEST_CASE("Spatial audio pans to the side the sound is on") {
	AudioListenerState listener;   // at the origin, looking down -Z
	AudioSpatialSettings settings;
	settings.MinDistance = 1.0f;
	settings.MaxDistance = 50.0f;
	settings.Panning = 0.8f;

	const AudioSpatial::Result right = AudioSpatial::Evaluate(listener, { 5.0f, 0.0f, 0.0f }, settings);
	CHECK(right.Distance == doctest::Approx(5.0f));
	CHECK(right.Gain == doctest::Approx(0.2f));
	CHECK(right.Pan == doctest::Approx(0.8f));
	CHECK(right.Right == doctest::Approx(0.2f));
	CHECK(right.Left == doctest::Approx(0.2f * 0.2f));

	const AudioSpatial::Result left = AudioSpatial::Evaluate(listener, { -5.0f, 0.0f, 0.0f }, settings);
	CHECK(left.Pan == doctest::Approx(-0.8f));
	CHECK(left.Left > left.Right);

	// In front and behind: both ears at full gain, unlike miniaudio's own panning which halves a sound in front.
	const AudioSpatial::Result front = AudioSpatial::Evaluate(listener, { 0.0f, 0.0f, -2.0f }, settings);
	CHECK(front.Pan == doctest::Approx(0.0f));
	CHECK(front.Left == doctest::Approx(0.5f));
	CHECK(front.Right == doctest::Approx(0.5f));
	CHECK(AudioSpatial::Evaluate(listener, { 0.0f, 0.0f, 2.0f }, settings).Pan == doctest::Approx(0.0f));

	// Halfway to the side: half the panning.
	CHECK(AudioSpatial::Pan(listener, { 3.0f, 0.0f, -3.0f * std::sqrt(3.0f) }, 1.0f) == doctest::Approx(0.5f));

	// The listener turned to face +X: +Z is now on its right.
	AudioListenerState turned;
	turned.Forward = { 1.0f, 0.0f, 0.0f };
	CHECK(AudioSpatial::Pan(turned, { 0.0f, 0.0f, 4.0f }, 0.8f) == doctest::Approx(0.8f));

	CHECK(AudioSpatial::Pan(listener, { 5.0f, 0.0f, 0.0f }, 0.0f) == doctest::Approx(0.0f));
	const AudioSpatial::Result atListener = AudioSpatial::Evaluate(listener, { 0.0f, 0.0f, 0.0f }, settings);
	CHECK(atListener.Pan == doctest::Approx(0.0f));
	CHECK(atListener.Gain == doctest::Approx(1.0f));
}

TEST_CASE("Spatial audio settings are made usable") {
	AudioSpatialSettings broken;
	broken.MinDistance = -3.0f;
	broken.MaxDistance = std::nanf("");
	broken.Panning = 7.0f;
	broken.DopplerLevel = -1.0f;
	broken.Rolloff = static_cast<AudioRolloff>(9);
	const AudioSpatialSettings fixed = AudioSpatial::Sanitize(broken);
	CHECK(fixed.MinDistance == doctest::Approx(0.01f));
	CHECK(fixed.MaxDistance == doctest::Approx(AudioSpatialSettings{}.MaxDistance));
	CHECK(fixed.Panning == doctest::Approx(1.0f));
	CHECK(fixed.DopplerLevel == doctest::Approx(0.0f));
	CHECK(fixed.Rolloff == AudioRolloff::Logarithmic);

	AudioSpatialSettings inverted;
	inverted.MinDistance = 10.0f;
	inverted.MaxDistance = 4.0f;
	CHECK(AudioSpatial::Sanitize(inverted).MaxDistance == doctest::Approx(10.0f));
}

TEST_CASE("audio.spatial tells how a sound at a position would be heard") {
	DevConsole::ResetForTests();
	DevConsole::RegisterBuiltInCommands();
	const AudioListenerState previous = AudioManager::GetListener();
	AudioManager::SetListener(AudioListenerState{});

	const DevCommandResult probe = DevConsole::Execute("audio.spatial 5 0 0");
	CHECK(probe.Result == DevCommandResult::Status::Ok);
	CHECK(probe.Output.find("probe at (5.00, 0.00, 0.00): distance 5.00, gain 0.200, pan +0.80, left 0.040, right 0.200") != std::string::npos);

	const DevCommandResult linear = DevConsole::Execute("audio.spatial -9 0 0 1 17 linear 1");
	CHECK(linear.Output.find("distance 9.00, gain 0.500, pan -1.00, left 0.500, right 0.000") != std::string::npos);

	CHECK(DevConsole::Execute("audio.spatial 1 2").Result == DevCommandResult::Status::Error);
	CHECK(DevConsole::Execute("audio.spatial 1 2 3 1 16 cubic").Result == DevCommandResult::Status::Error);
	// Without a position: the listener and the playing sounds.
	CHECK(DevConsole::Execute("audio.spatial").Output.find("no spatial sound playing") != std::string::npos);

	AudioManager::SetListener(previous);
	DevConsole::ResetForTests();
}

TEST_CASE("Spatial sounds reach the speakers with the gains audio.spatial reports") {
	// Headless miniaudio: nothing is played, the mix is read back.
	auto owned = std::make_unique<MiniaudioBackend>(true);
	MiniaudioBackend& backend = *owned;
	AudioManager::SetBackend(std::move(owned));
	REQUIRE(AudioManager::Initialize());
	const AudioListenerState previous = AudioManager::GetListener();
	AudioManager::SetListener(AudioListenerState{});

	// One clip per case: the same clip twice in a row would be throttled.
	constexpr uint32_t k_Frames = 8192;
	std::vector<std::filesystem::path> files;
	const auto load = [&](const char* name) {
		files.push_back(WriteConstantWav(name, k_Frames));
		return AudioManager::LoadAudio(files.back().string());
	};
	const AudioHandle right = load("FunkeSpatialRight.wav");
	const AudioHandle left = load("FunkeSpatialLeft.wav");
	const AudioHandle outOfReach = load("FunkeSpatialOutOfReach.wav");
	const AudioHandle flat = load("FunkeSpatialFlat.wav");
	REQUIRE(right.IsValid());
	REQUIRE(flat.IsValid());

	// Logarithmic 1..50 with panning 0.8, five blocks to the right.
	const AudioSpatialSettings settings;
	AudioManager::PlayOneShotAt(right, { 5.0f, 0.0f, 0.0f }, 1.0f, settings);
	const AudioSpatial::Result expected = AudioSpatial::Evaluate(AudioManager::GetListener(), { 5.0f, 0.0f, 0.0f }, settings);
	Speakers heard = Mix(backend, k_Frames);
	CHECK(heard.Left == doctest::Approx(0.5f * expected.Left).epsilon(0.001));
	CHECK(heard.Right == doctest::Approx(0.5f * expected.Right).epsilon(0.001));
	CHECK(heard.Right == doctest::Approx(0.1f).epsilon(0.001));

	// Linear, fully to the left, halfway out: half the volume on the left, nothing on the right.
	AudioSpatialSettings linear;
	linear.MinDistance = 1.0f;
	linear.MaxDistance = 17.0f;
	linear.Rolloff = AudioRolloff::Linear;
	linear.Panning = 1.0f;
	AudioManager::Update();
	AudioManager::PlayOneShotAt(left, { -9.0f, 0.0f, 0.0f }, 1.0f, linear);
	heard = Mix(backend, k_Frames);
	CHECK(heard.Left == doctest::Approx(0.25f).epsilon(0.001));
	CHECK(std::abs(heard.Right) < 0.001f);

	// Past Max Distance a Linear sound isn't started at all.
	AudioManager::Update();
	AudioManager::PlayOneShotAt(outOfReach, { 0.0f, 0.0f, -30.0f }, 1.0f, linear);
	heard = Mix(backend, k_Frames);
	CHECK(std::abs(heard.Left) < 0.001f);
	CHECK(std::abs(heard.Right) < 0.001f);

	// A plain one-shot stays as authored, wherever the listener is.
	AudioListenerState away;
	away.Position = { 100.0f, 0.0f, 0.0f };
	away.Forward = { 1.0f, 0.0f, 0.0f };
	AudioManager::SetListener(away);
	AudioManager::Update();
	AudioManager::PlayOneShot(flat, 1.0f);
	heard = Mix(backend, k_Frames);
	CHECK(heard.Left == doctest::Approx(0.5f).epsilon(0.001));
	CHECK(heard.Right == doctest::Approx(0.5f).epsilon(0.001));

	AudioManager::SetListener(previous);
	AudioManager::Shutdown();
	std::error_code ec;
	for (const std::filesystem::path& file : files) std::filesystem::remove(file, ec);
}

TEST_CASE("AudioSource spatial fields round-trip and are only written when set") {
	auto scene = Scene::CreateDetachedScene("SpatialAudioSerialize");
	Entity source = scene->CreateEntity("Torch");
	auto& audio = source.AddComponent<AudioSourceComponent>();
	const Json::Value plain = SceneSerializer::SerializeComponent(*scene, source.GetHandle(), "AudioSource");
	CHECK(plain.FindMember("spatial") == nullptr);
	CHECK(plain.FindMember("minDistance") == nullptr);
	CHECK(plain.FindMember("panning") == nullptr);

	AudioSpatialSettings settings;
	settings.MinDistance = 3.0f;
	settings.MaxDistance = 30.0f;
	settings.Rolloff = AudioRolloff::Linear;
	settings.Panning = 0.5f;
	settings.DopplerLevel = 1.0f;
	audio.SetSpatialSettings(settings);
	audio.SetSpatial(true);

	// Scene load path.
	const Json::Value full = SceneSerializer::SerializeEntityFull(*scene, source.GetHandle());
	const EntityHandle loaded = SceneSerializer::DeserializeEntityFromValue(*scene, full);
	REQUIRE(loaded != entt::null);
	REQUIRE(scene->HasComponent<AudioSourceComponent>(loaded));
	CHECK(scene->GetComponent<AudioSourceComponent>(loaded).IsSpatial());
	CHECK(scene->GetComponent<AudioSourceComponent>(loaded).GetSpatialSettings() == settings);

	// Inspector paste path.
	const Json::Value component = SceneSerializer::SerializeComponent(*scene, source.GetHandle(), "AudioSource");
	Entity pasted = scene->CreateEntity("Pasted");
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, pasted.GetHandle(), "AudioSource", component));
	CHECK(pasted.GetComponent<AudioSourceComponent>().IsSpatial());
	CHECK(pasted.GetComponent<AudioSourceComponent>().GetSpatialSettings() == settings);

	// Reset: everything back to a plain 2D sound.
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, pasted.GetHandle(), "AudioSource", Json::Value::MakeObject()));
	CHECK_FALSE(pasted.GetComponent<AudioSourceComponent>().IsSpatial());
	CHECK(pasted.GetComponent<AudioSourceComponent>().GetSpatialSettings() == AudioSpatialSettings{});
}

#if FUNKE_WITH_3D
TEST_CASE("The audio listener follows the main Camera3D") {
	auto scene = Scene::CreateDetachedScene("SpatialAudioListener");
	AudioListenerState listener;
	CHECK_FALSE(AudioUpdateSystem::FindListener(*scene, listener));

	Entity camera = scene->CreateEntity("Camera");
	auto& transform = camera.AddComponent<Transform3DComponent>();
	transform.Position = { 1.0f, 2.0f, 3.0f };
	transform.Rotation = { 0.0f, 90.0f, 0.0f };
	camera.AddComponent<Camera3DComponent>();
	REQUIRE(AudioUpdateSystem::FindListener(*scene, listener));
	CHECK(listener.Position.x == doctest::Approx(1.0f));
	CHECK(listener.Position.y == doctest::Approx(2.0f));
	CHECK(listener.Position.z == doctest::Approx(3.0f));
	// Yawed 90 degrees left: looking down -X.
	CHECK(listener.Forward.x == doctest::Approx(-1.0f).epsilon(1e-4));
	CHECK(listener.Forward.z == doctest::Approx(0.0f).epsilon(1e-4));
	CHECK(listener.Up.y == doctest::Approx(1.0f).epsilon(1e-4));
	// A sound further down -Z is now on the listener's right.
	CHECK(AudioSpatial::Pan(listener, { 1.0f, 2.0f, -10.0f }, 1.0f) == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("Spatial AudioSources take their entity's position and velocity") {
	auto scene = Scene::CreateDetachedScene("SpatialAudioSync");
	Entity entity = scene->CreateEntity("Minecart");
	auto& transform = entity.AddComponent<Transform3DComponent>();
	auto& audio = entity.AddComponent<AudioSourceComponent>();
	transform.Position = { 1.0f, 0.0f, 0.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	AudioUpdateSystem::SyncPosition(scene->GetRegistry(), entity.GetHandle(), audio, 0.0f);
	CHECK(audio.GetWorldPosition().x == doctest::Approx(1.0f));
	CHECK(glm::length(audio.GetVelocity()) == doctest::Approx(0.0f));

	transform.Position = { 3.0f, 0.0f, 0.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	AudioUpdateSystem::SyncPosition(scene->GetRegistry(), entity.GetHandle(), audio, 0.5f);
	CHECK(audio.GetVelocity().x == doctest::Approx(4.0f));

	// A teleport is not supersonic motion for the doppler effect.
	transform.Position = { 3000.0f, 0.0f, 0.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	AudioUpdateSystem::SyncPosition(scene->GetRegistry(), entity.GetHandle(), audio, 0.016f);
	CHECK(glm::length(audio.GetVelocity()) == doctest::Approx(100.0f));
}
#endif
