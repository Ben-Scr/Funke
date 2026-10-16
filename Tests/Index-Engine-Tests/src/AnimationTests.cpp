#include <doctest/doctest.h>

#include "Animation/AnimationBinding.hpp"
#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Animation/AnimatorRuntime.hpp"
#include "Animation/AnimatorStateMachine.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Inspector/PropertyDescriptor.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/AnimatorSystem.hpp"

#include <string>
#include <vector>

using namespace Index;

namespace {
	AnimationCurve MakeCurve(std::initializer_list<std::pair<float, float>> keys, KeyInterpolation interpolation = KeyInterpolation::Linear) {
		AnimationCurve curve;
		for (const auto& [t, v] : keys) curve.SetKey(t, v, interpolation);
		return curve;
	}

	// Idle (1 s loop) -> Run on IsRunning, Run -> Idle on !IsRunning, Any State -> Jump (0.5 s, once) on the Jump trigger,
	// Jump -> Idle at its end.
	AnimatorController MakeLocomotion() {
		AnimatorController c;
		c.Parameters = {
			{ "IsRunning", AnimatorParameterType::Bool },
			{ "Jump", AnimatorParameterType::Trigger },
			{ "Speed", AnimatorParameterType::Float, 0.0f },
		};
		c.States = { { "Idle", 1 }, { "Run", 2 }, { "Jump", 3, 1.0f, AnimatorStateLoop::Once } };

		AnimatorTransition toRun;
		toRun.From = 0; toRun.To = 1; toRun.Duration = 0.2f;
		toRun.Conditions = { { "IsRunning", AnimatorConditionMode::If } };
		AnimatorTransition toIdle;
		toIdle.From = 1; toIdle.To = 0; toIdle.Duration = 0.2f;
		toIdle.Conditions = { { "IsRunning", AnimatorConditionMode::IfNot } };
		AnimatorTransition jump;
		jump.From = AnimatorTransition::k_AnyState; jump.To = 2; jump.Duration = 0.0f;
		jump.Conditions = { { "Jump", AnimatorConditionMode::If } };
		AnimatorTransition land;
		land.From = 2; land.To = 0; land.Duration = 0.0f; land.HasExitTime = true; land.ExitTime = 1.0f;
		c.Transitions = { toRun, toIdle, jump, land };
		return c;
	}

	std::vector<AnimatorStateClipInfo> LocomotionClips() {
		return { { 1.0f, true }, { 0.5f, true }, { 0.5f, false } };
	}
}

TEST_CASE("Animation curves interpolate linear, step and bezier segments and clamp outside the keys") {
	const AnimationCurve linear = MakeCurve({ { 0.0f, 0.0f }, { 1.0f, 10.0f } });
	CHECK(linear.Evaluate(-1.0f) == doctest::Approx(0.0f));
	CHECK(linear.Evaluate(0.25f) == doctest::Approx(2.5f));
	CHECK(linear.Evaluate(5.0f) == doctest::Approx(10.0f));

	const AnimationCurve step = MakeCurve({ { 0.0f, 1.0f }, { 1.0f, 2.0f } }, KeyInterpolation::Step);
	CHECK(step.Evaluate(0.99f) == doctest::Approx(1.0f));
	CHECK(step.Evaluate(1.0f) == doctest::Approx(2.0f));

	// Flat tangents make an ease-in-out: halfway stays halfway, a quarter in is below linear.
	AnimationCurve bezier = MakeCurve({ { 0.0f, 0.0f }, { 1.0f, 1.0f } }, KeyInterpolation::Bezier);
	CHECK(bezier.Evaluate(0.5f) == doctest::Approx(0.5f));
	CHECK(bezier.Evaluate(0.25f) < 0.25f);
	// Slope-matching tangents reproduce the straight line.
	bezier.Keys[0].OutTangent = 1.0f;
	bezier.Keys[1].InTangent = 1.0f;
	CHECK(bezier.Evaluate(0.25f) == doctest::Approx(0.25f));
}

TEST_CASE("SetKey keeps keys sorted and replaces a key at the same time") {
	AnimationCurve curve;
	curve.SetKey(1.0f, 1.0f);
	curve.SetKey(0.0f, 0.0f);
	curve.SetKey(0.5f, 7.0f);
	const int replaced = curve.SetKey(0.5f, 3.0f);
	REQUIRE(curve.Keys.size() == 3);
	CHECK(replaced == 1);
	CHECK(curve.Keys[1].Value == doctest::Approx(3.0f));
	CHECK(curve.Keys.front().Time == doctest::Approx(0.0f));
}

TEST_CASE("Animation clips round-trip through JSON") {
	AnimationClip clip;
	clip.Length = 2.0f;
	clip.Loop = false;
	clip.SampleRate = 30.0f;
	AnimationTrack position;
	position.Path = "Arm/Hand";
	position.Component = "Transform3D";
	position.Property = "Position.x";
	position.Curve = MakeCurve({ { 0.0f, 1.0f }, { 2.0f, -1.0f } });
	position.Curve.Keys[0].Interpolation = KeyInterpolation::Bezier;
	position.Curve.Keys[0].OutTangent = 0.5f;
	AnimationTrack sprite;
	sprite.Component = "SpriteRenderer";
	sprite.Property = "Texture";
	sprite.DiscreteKeys = { { 0.0f, "123456789012345678" }, { 1.0f, "42" } };
	clip.Tracks = { position, sprite };
	clip.Events = { { 0.5f, "OnFootstep", "left" } };

	const AnimationClip loaded = AnimationClipFromJson(Json::Parse(Json::Stringify(AnimationClipToJson(clip))));
	CHECK(loaded == clip);
}

TEST_CASE("Animator controllers round-trip through JSON with transitions referencing states by name") {
	const AnimatorController controller = MakeLocomotion();
	const Json::Value json = AnimatorControllerToJson(controller);
	const AnimatorController loaded = AnimatorControllerFromJson(Json::Parse(Json::Stringify(json)));
	CHECK(loaded == controller);

	// Removing a state drops its transitions and shifts the others.
	AnimatorController edited = loaded;
	edited.RemoveState(0);
	CHECK(edited.States.size() == 2);
	CHECK(edited.Transitions.size() == 1);
	CHECK(edited.Transitions[0].From == AnimatorTransition::k_AnyState);
	CHECK(edited.Transitions[0].To == 1);
}

TEST_CASE("Bool parameters switch Idle and Run with a crossfade") {
	const AnimatorController controller = MakeLocomotion();
	const std::vector<AnimatorStateClipInfo> clips = LocomotionClips();
	AnimatorStateMachine machine;
	machine.Reset(controller);
	CHECK(machine.GetCurrentState() == 0);

	machine.Update(controller, clips, 0.1f);
	CHECK(machine.GetCurrentState() == 0);
	CHECK_FALSE(machine.IsInTransition());

	REQUIRE(machine.SetBool("IsRunning", true));
	machine.Update(controller, clips, 0.016f);
	CHECK(machine.IsInTransition());
	CHECK(machine.GetNextState() == 1);

	machine.Update(controller, clips, 0.1f);
	std::vector<AnimatorPlaybackSample> samples;
	machine.GetSamples(clips, samples);
	REQUIRE(samples.size() == 2);
	CHECK(samples[0].Weight == doctest::Approx(0.5f));
	CHECK(samples[1].Weight == doctest::Approx(0.5f));

	machine.Update(controller, clips, 0.15f);
	CHECK_FALSE(machine.IsInTransition());
	CHECK(machine.GetCurrentState() == 1);

	CHECK_FALSE(machine.SetBool("Missing", true));
	CHECK_FALSE(machine.SetFloat("IsRunning", 1.0f));
}

TEST_CASE("Any State triggers are consumed and exit time returns to Idle") {
	const AnimatorController controller = MakeLocomotion();
	const std::vector<AnimatorStateClipInfo> clips = LocomotionClips();
	AnimatorStateMachine machine;
	machine.Reset(controller);

	REQUIRE(machine.SetTrigger("Jump"));
	machine.Update(controller, clips, 0.016f);
	CHECK(machine.GetCurrentState() == 2);
	CHECK_FALSE(machine.GetBool("Jump"));

	// Not restarted while playing: the trigger was consumed and CanTransitionToSelf is off.
	machine.Update(controller, clips, 0.2f);
	CHECK(machine.GetCurrentState() == 2);
	machine.Update(controller, clips, 0.35f);
	CHECK(machine.GetCurrentState() == 0);
}

TEST_CASE("Exit time of a looping state fires when the loop crosses it") {
	AnimatorController controller;
	controller.States = { { "A", 1 }, { "B", 2 } };
	AnimatorTransition t;
	t.From = 0; t.To = 1; t.HasExitTime = true; t.ExitTime = 0.75f; t.Duration = 0.0f;
	controller.Transitions = { t };
	const std::vector<AnimatorStateClipInfo> clips = { { 1.0f, true }, { 1.0f, true } };

	AnimatorStateMachine machine;
	machine.Reset(controller);
	machine.Update(controller, clips, 0.5f);
	CHECK(machine.GetCurrentState() == 0);
	machine.Update(controller, clips, 0.3f);
	CHECK(machine.GetCurrentState() == 1);
}

TEST_CASE("Play jumps to a normalized time and CrossFade blends") {
	const AnimatorController controller = MakeLocomotion();
	const std::vector<AnimatorStateClipInfo> clips = LocomotionClips();
	AnimatorStateMachine machine;
	machine.Reset(controller);

	REQUIRE(machine.Play(1, 0.5f, clips));
	CHECK(machine.GetCurrentNormalizedTime(clips) == doctest::Approx(0.5f));
	REQUIRE(machine.CrossFade(0, 0.4f));
	machine.Update(controller, clips, 0.1f);
	CHECK(machine.GetTransitionProgress() == doctest::Approx(0.25f));
}

TEST_CASE("Animation events fire once per crossing, on every loop, and at time 0 on entry") {
	AnimationClip clip;
	clip.Length = 1.0f;
	clip.Events = { { 0.0f, "Start" }, { 0.5f, "Mid" } };
	std::vector<std::string> fired;
	auto collect = [&](const AnimationEvent& e) { fired.push_back(e.Function); };

	AnimatorStateMachine::ForEachEventInWindow(clip, true, { 0, 0.0f, 0.25f, true }, collect);
	CHECK(fired == std::vector<std::string>{ "Start" });

	fired.clear();
	AnimatorStateMachine::ForEachEventInWindow(clip, true, { 0, 0.25f, 0.6f, false }, collect);
	CHECK(fired == std::vector<std::string>{ "Mid" });

	fired.clear();
	AnimatorStateMachine::ForEachEventInWindow(clip, true, { 0, 0.6f, 1.55f, false }, collect);
	CHECK(fired == std::vector<std::string>{ "Start", "Mid" });

	fired.clear();
	AnimatorStateMachine::ForEachEventInWindow(clip, false, { 0, 0.6f, 3.0f, false }, collect);
	CHECK(fired.empty());
}

TEST_CASE("Animation property paths split off vector channels") {
	CHECK(ParseAnimationProperty("Position.x").Base == "Position");
	CHECK(ParseAnimationProperty("Position.x").Channel == 0);
	CHECK(ParseAnimationProperty("Color.a").Channel == 3);
	CHECK(ParseAnimationProperty("SpriteName").Channel == -1);
	CHECK(ParseAnimationProperty("Stats.Speed").Base == "Stats.Speed");
}

TEST_CASE("Animator components round-trip through their registry serializer") {
	const ComponentInfo* info = SceneManager::GetComponentRegistryOrBuiltIn().FindBySerializedName("Animator");
	REQUIRE(info);
	REQUIRE(info->serialize);
	REQUIRE(info->deserialize);

	auto scene = Scene::CreateDetachedScene("AnimatorSerialize");
	Entity source = scene->CreateEntity("Source");
	AnimatorComponent& animator = source.AddComponent<AnimatorComponent>();
	animator.Controller = 12345678901234567890ull;
	animator.Clip = 42;
	animator.Speed = 1.5f;
	animator.UpdateMode = AnimatorUpdateMode::Fixed;
	animator.ApplyInEditMode = true;
	const Json::Value json = info->serialize(source);
	CHECK(json.FindMember("controllerAsset") != nullptr);

	Entity target = scene->CreateEntity("Target");
	target.AddComponent<AnimatorComponent>();
	info->deserialize(target, Json::Parse(Json::Stringify(json)));
	const AnimatorComponent& loaded = target.GetComponent<AnimatorComponent>();
	CHECK(loaded.Controller == 12345678901234567890ull);
	CHECK(loaded.Clip == 42);
	CHECK(loaded.Speed == doctest::Approx(1.5f));
	CHECK(loaded.UpdateMode == AnimatorUpdateMode::Fixed);
	CHECK(loaded.ApplyInEditMode);

	// Reset Component deserializes an empty object: everything back to defaults.
	info->deserialize(target, Json::Value::MakeObject());
	CHECK(target.GetComponent<AnimatorComponent>().Controller == 0);
	CHECK(target.GetComponent<AnimatorComponent>().Speed == doctest::Approx(1.0f));
}

#if INDEX_WITH_3D
TEST_CASE("Animator drives child transforms through the component registry and blends a crossfade") {
	auto scene = Scene::CreateDetachedScene("AnimatorBinding");
	Entity root = scene->CreateEntity("Root");
	root.AddComponent<Transform3DComponent>();
	Entity arm = scene->CreateEntity("Arm");
	arm.AddComponent<Transform3DComponent>();
	arm.SetParent(root);

	constexpr uint64_t k_IdleGuid = 0xA11CE0001ull;
	constexpr uint64_t k_WaveGuid = 0xA11CE0002ull;
	constexpr uint64_t k_ControllerGuid = 0xA11CE0003ull;

	AnimationClip idle;
	idle.Length = 1.0f;
	AnimationTrack idleY;
	idleY.Path = "Arm";
	idleY.Component = "Transform3D";
	idleY.Property = "Position.y";
	idleY.Curve = MakeCurve({ { 0.0f, 0.0f }, { 1.0f, 0.0f } });
	idle.Tracks = { idleY };
	AnimationClipLibrary::Set(k_IdleGuid, idle);

	AnimationClip wave;
	wave.Length = 1.0f;
	AnimationTrack waveY = idleY;
	waveY.Curve = MakeCurve({ { 0.0f, 2.0f }, { 1.0f, 2.0f } });
	AnimationTrack missing;
	missing.Path = "Leg";
	missing.Component = "Transform3D";
	missing.Property = "Position.x";
	missing.Curve = MakeCurve({ { 0.0f, 5.0f } });
	wave.Tracks = { waveY, missing };
	AnimationClipLibrary::Set(k_WaveGuid, wave);

	AnimatorController controller;
	controller.Parameters = { { "Wave", AnimatorParameterType::Trigger } };
	controller.States = { { "Idle", k_IdleGuid }, { "Wave", k_WaveGuid } };
	AnimatorTransition toWave;
	toWave.From = 0; toWave.To = 1; toWave.Duration = 1.0f;
	toWave.Conditions = { { "Wave", AnimatorConditionMode::If } };
	controller.Transitions = { toWave };
	AnimatorControllerLibrary::Set(k_ControllerGuid, controller);

	AnimatorComponent& animator = root.AddComponent<AnimatorComponent>();
	animator.Controller = k_ControllerGuid;

	AnimatorSystem::Step(*scene, root.GetHandle(), animator, 0.1f, false);
	CHECK(arm.GetComponent<Transform3DComponent>().Position.y == doctest::Approx(0.0f));

	AnimatorRuntime* runtime = animator.Runtime.Get();
	REQUIRE(runtime);
	runtime->SetTrigger("Wave");
	AnimatorSystem::Step(*scene, root.GetHandle(), animator, 0.0f, false);
	AnimatorSystem::Step(*scene, root.GetHandle(), animator, 0.5f, false);
	CHECK(arm.GetComponent<Transform3DComponent>().Position.y == doctest::Approx(1.0f));
	// The track to a missing child is reported, not fatal.
	REQUIRE(runtime->GetBindingErrors().size() == 1);
	CHECK(runtime->GetBindingErrors()[0].find("Leg") != std::string::npos);

	AnimatorSystem::Step(*scene, root.GetHandle(), animator, 0.6f, false);
	CHECK(runtime->GetCurrentStateName() == "Wave");
	CHECK(arm.GetComponent<Transform3DComponent>().Position.y == doctest::Approx(2.0f));

	// A copied component starts without playback state.
	AnimatorComponent copy = animator;
	CHECK(copy.Runtime.Get() == nullptr);

	AnimationClipLibrary::Invalidate(k_IdleGuid);
	AnimationClipLibrary::Invalidate(k_WaveGuid);
	AnimatorControllerLibrary::Invalidate(k_ControllerGuid);
}
#endif

TEST_CASE("Texture reference values keep the sprite slice through text and comparison") {
	const PropertyValue slice = PropertyValue::FromString(PropertyType::TextureRef, "4242|slice|run_1");
	CHECK(slice.UIntValue == 4242);
	CHECK(slice.StringValue == "run_1");
	CHECK(slice.ToString() == "4242|slice|run_1");
	CHECK(slice != PropertyValue::FromString(PropertyType::TextureRef, "4242|slice|run_0"));
	CHECK(slice != PropertyValue::FromString(PropertyType::TextureRef, "4242"));
	CHECK(PropertyValue::FromString(PropertyType::TextureRef, "4242").ToString() == "4242");
	CHECK(PropertyValue::FromString(PropertyType::TextureRef, "").ToString().empty());

	// Image shares the slice-aware Texture property with SpriteRenderer.
	const ComponentInfo* imageInfo = SceneManager::GetComponentRegistryOrBuiltIn().FindBySerializedName("Image");
	REQUIRE(imageInfo);
	const PropertyDescriptor* imageTexture = AnimationTargetSet::FindDescriptor(*imageInfo, "Texture");
	REQUIRE(imageTexture);
	auto scene = Scene::CreateDetachedScene("ImageSlice");
	Entity image = scene->CreateEntity("Image");
	ImageComponent& component = image.AddComponent<ImageComponent>();
	component.TextureAssetId = UUID(4242);
	component.SpriteName = "button_on";
	CHECK(imageTexture->Get(image).ToString() == "4242|slice|button_on");
}

#if INDEX_WITH_2D
TEST_CASE("Animator Texture tracks switch between slices of one sprite sheet") {
	auto scene = Scene::CreateDetachedScene("AnimatorSlices");
	Entity root = scene->CreateEntity("Root");
	Entity body = scene->CreateEntity("Body");
	body.SetParent(root);

	constexpr uint64_t k_SheetGuid = 0x5EE7000001ull;
	constexpr uint64_t k_ClipGuid = 0x5EE7000002ull;
	const std::string sheet = std::to_string(k_SheetGuid);

	SpriteRendererComponent& authored = body.AddComponent<SpriteRendererComponent>();
	authored.TextureAssetId = UUID(k_SheetGuid);
	authored.SpriteName = "run_0";

	const ComponentInfo* info = SceneManager::GetComponentRegistryOrBuiltIn().FindBySerializedName("SpriteRenderer");
	REQUIRE(info);
	const PropertyDescriptor* texture = AnimationTargetSet::FindDescriptor(*info, "Texture");
	REQUIRE(texture);

	// Record and Add Property key the property's value: it must carry the slice, so a slice-only change is a change.
	const PropertyValue run0 = texture->Get(body);
	CHECK(run0.ToString() == sheet + "|slice|run_0");
	body.GetComponent<SpriteRendererComponent>().SpriteName = "run_1";
	CHECK(texture->Get(body) != run0);
	// Writing the read value back (preview restore, undo) keeps the slice.
	texture->Set(body, run0);
	CHECK(body.GetComponent<SpriteRendererComponent>().SpriteName == "run_0");

	AnimationClip clip;
	clip.Length = 0.3f;
	AnimationTrack track;
	track.Path = "Body";
	track.Component = "SpriteRenderer";
	track.Property = "Texture";
	track.DiscreteKeys = { { 0.0f, run0.ToString() }, { 0.1f, sheet + "|slice|run_1" }, { 0.2f, sheet + "|slice|run_2" } };
	clip.Tracks = { track };
	AnimationClipLibrary::Set(k_ClipGuid, clip);

	AnimatorComponent& animator = root.AddComponent<AnimatorComponent>();
	animator.Clip = k_ClipGuid;

	AnimatorSystem::Step(*scene, root.GetHandle(), animator, 0.05f, false);
	AnimatorRuntime* runtime = animator.Runtime.Get();
	REQUIRE(runtime);
	CHECK(runtime->GetBindingErrors().empty());
	CHECK(body.GetComponent<SpriteRendererComponent>().SpriteName == "run_0");

	AnimatorSystem::Step(*scene, root.GetHandle(), animator, 0.1f, false);
	CHECK(body.GetComponent<SpriteRendererComponent>().SpriteName == "run_1");
	CHECK(body.GetComponent<SpriteRendererComponent>().TextureAssetId == UUID(k_SheetGuid));

	AnimatorSystem::Step(*scene, root.GetHandle(), animator, 0.1f, false);
	CHECK(body.GetComponent<SpriteRendererComponent>().SpriteName == "run_2");

	// Looping back to the first key.
	AnimatorSystem::Step(*scene, root.GetHandle(), animator, 0.1f, false);
	CHECK(body.GetComponent<SpriteRendererComponent>().SpriteName == "run_0");

	AnimationClipLibrary::Invalidate(k_ClipGuid);
}
#endif
