#include <doctest/doctest.h>
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/Json.hpp"

using namespace Funke;

TEST_CASE("AudioSource registry serialization agrees across entity paste and reset") {
	auto scene = Scene::CreateDetachedScene("AudioSerialization");
	auto source = scene->CreateEntity("Source");
	auto& audio = source.AddComponent<AudioSourceComponent>();
	audio.SetVolume(0.35f);
	audio.SetPitch(1.5f);
	audio.SetLoop(true);
	audio.SetPlayOnAwake(true);
	audio.SetSpatial(true);
	audio.SetAudioAssetId(UUID(18446744073709550000ull));
	const auto component = SceneSerializer::SerializeComponent(*scene, source.GetHandle(), "AudioSource");
	REQUIRE(component.FindMember("clipAsset"));
	CHECK(component.FindMember("clipAsset")->AsStringOr() == "18446744073709550000");
	const auto full = SceneSerializer::SerializeEntityFull(*scene, source.GetHandle());
	auto loaded = SceneSerializer::DeserializeEntityFromValue(*scene, full);
	REQUIRE(loaded != entt::null);
	auto pasted = scene->CreateEntity("Pasted");
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, pasted.GetHandle(), "AudioSource", component));
	CHECK(SceneSerializer::SerializeComponent(*scene, loaded, "AudioSource") == component);
	CHECK(SceneSerializer::SerializeComponent(*scene, pasted.GetHandle(), "AudioSource") == component);

	REQUIRE(SceneSerializer::ResetComponent(*scene, pasted.GetHandle(), "AudioSource"));
	const auto& reset = pasted.GetComponent<AudioSourceComponent>();
	CHECK(reset.GetVolume() == 1.0f);
	CHECK(reset.GetPitch() == 1.0f);
	CHECK_FALSE(reset.IsLooping());
	CHECK_FALSE(reset.GetPlayOnAwake());
	CHECK_FALSE(reset.IsSpatial());
	CHECK(static_cast<uint64_t>(reset.GetAudioAssetId()) == 0);
	CHECK_FALSE(reset.GetAudioHandle().IsValid());
}
