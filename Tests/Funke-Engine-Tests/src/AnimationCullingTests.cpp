#include <doctest/doctest.h>

#include "Animation/AnimationClip.hpp"
#include "Animation/AnimationCulling.hpp"
#include "Animation/AnimationViewer.hpp"
#include "Animation/AnimatorRuntime.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Project/FunkeProject.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Systems/AnimatorSystem.hpp"

#if FUNKE_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Systems/SkinnedMeshAnimationSystem.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <glm/gtc/matrix_transform.hpp>
#endif

#include <chrono>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace Funke;

namespace {
	struct TempDirectory {
		std::filesystem::path Root;
		explicit TempDirectory(const char* prefix) {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(suffix));
			std::filesystem::create_directories(Root / "Assets");
		}
		~TempDirectory() {
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}
	};

	uint32_t PhaseOf(const Entity& entity) {
		return AnimationCulling::PhaseOf(static_cast<uint32_t>(entt::to_integral(entity.GetHandle())));
	}
}

TEST_CASE("Animation distance rate updates every frame, every 2nd or every 4th frame by distance") {
	CHECK(AnimationCulling::UpdateInterval(5.0f, 20.0f, 40.0f) == 1);
	CHECK(AnimationCulling::UpdateInterval(20.0f, 20.0f, 40.0f) == 2);
	CHECK(AnimationCulling::UpdateInterval(39.9f, 20.0f, 40.0f) == 2);
	CHECK(AnimationCulling::UpdateInterval(40.0f, 20.0f, 40.0f) == 4);
	CHECK(AnimationCulling::UpdateInterval(5000.0f, 20.0f, 40.0f) == 4);
	// A quarter distance below the half distance wins from there on.
	CHECK(AnimationCulling::UpdateInterval(15.0f, 20.0f, 10.0f) == 4);
	CHECK(AnimationCulling::UpdateInterval(std::nanf(""), 20.0f, 40.0f) == 1);
}

TEST_CASE("Rate-limited animations update once per interval, spread over the frames") {
	for (uint32_t stride : { 1u, 4u, 5u }) {
		for (uint32_t interval : { 1u, 2u, 4u }) {
			constexpr uint32_t k_Count = 400;
			std::vector<uint32_t> perFrame(interval, 0);
			for (uint32_t i = 0; i < k_Count; ++i) {
				const uint32_t phase = AnimationCulling::PhaseOf(1000 + i * stride);
				uint32_t due = 0;
				for (uint64_t frame = 64; frame < 64 + interval; ++frame) {
					if (!AnimationCulling::IsDue(frame, phase, interval)) continue;
					++due;
					++perFrame[frame % interval];
				}
				CHECK(due == 1);
			}
			// Not all at once: every frame takes about its share.
			for (uint32_t count : perFrame) {
				CHECK(count >= k_Count / interval * 3 / 4);
				CHECK(count <= k_Count / interval * 5 / 4);
			}
		}
	}
}

TEST_CASE("Skipped frames add their time to the next step") {
	AnimationStepClock clock;
	constexpr uint32_t k_Phase = 1;
	float stepped = 0.0f;
	std::vector<float> steps;
	for (uint64_t frame = 0; frame < 40; ++frame) {
		const AnimationStepClock::Step step = clock.Advance(0.01f, AnimationCullingMode::AlwaysAnimate, true, 4, frame, k_Phase);
		if (!step.Run) continue;
		CHECK(step.WritePose);
		steps.push_back(step.DeltaTime);
		stepped += step.DeltaTime;
	}
	// The first frame steps at once, then every 4th frame (3, 7, ... 39) with four frames of time.
	REQUIRE(steps.size() == 11);
	CHECK(steps[0] == doctest::Approx(0.01f));
	CHECK(steps[1] == doctest::Approx(0.03f));
	for (size_t i = 2; i < steps.size(); ++i) CHECK(steps[i] == doctest::Approx(0.04f));
	CHECK(stepped + clock.PendingTime == doctest::Approx(0.40f));
}

TEST_CASE("Cull Pose keeps the clock running off screen, Cull Completely pauses it") {
	AnimationStepClock pose;
	AnimationStepClock::Step step = pose.Advance(0.1f, AnimationCullingMode::CullPose, true, 1, 0, 0);
	CHECK(step.Run);
	CHECK(step.WritePose);
	float hiddenTime = 0.0f;
	int hiddenSteps = 0;
	for (uint64_t frame = 1; frame <= 8; ++frame) {
		step = pose.Advance(0.1f, AnimationCullingMode::CullPose, false, 1, frame, 0);
		if (!step.Run) continue;
		// Hidden: clock and events at most every 4th frame, never a pose.
		CHECK_FALSE(step.WritePose);
		hiddenTime += step.DeltaTime;
		++hiddenSteps;
	}
	CHECK(hiddenSteps == 2);
	CHECK(hiddenTime + pose.PendingTime == doctest::Approx(0.8f));
	// Back in view: the pose is written at once, even off its frame.
	step = pose.Advance(0.1f, AnimationCullingMode::CullPose, true, 4, 9, 0);
	CHECK(step.Run);
	CHECK(step.WritePose);

	AnimationStepClock paused;
	CHECK(paused.Advance(0.1f, AnimationCullingMode::CullCompletely, true, 1, 0, 0).Run);
	for (uint64_t frame = 1; frame <= 10; ++frame) {
		CHECK_FALSE(paused.Advance(0.1f, AnimationCullingMode::CullCompletely, false, 1, frame, 0).Run);
	}
	CHECK(paused.PendingTime == doctest::Approx(0.0f));
	step = paused.Advance(0.1f, AnimationCullingMode::CullCompletely, true, 1, 11, 0);
	CHECK(step.Run);
	CHECK(step.WritePose);
	CHECK(step.DeltaTime == doctest::Approx(0.1f));

	// Hidden from its first frame on: nothing plays until it shows up.
	AnimationStepClock late;
	CHECK_FALSE(late.Advance(0.1f, AnimationCullingMode::CullCompletely, false, 1, 0, 0).Run);
	CHECK(late.Advance(0.1f, AnimationCullingMode::CullCompletely, true, 1, 1, 0).Run);

	// Always Animate ignores visibility.
	AnimationStepClock always;
	CHECK(always.Advance(0.1f, AnimationCullingMode::AlwaysAnimate, false, 1, 0, 0).WritePose);
	CHECK(always.Advance(0.1f, AnimationCullingMode::AlwaysAnimate, false, 1, 1, 0).WritePose);
}

TEST_CASE("Skinned pose gate draws live near the camera and holds the pose otherwise") {
	AnimationPoseGate gate;
	AnimationPoseGate::Update update = gate.Plan(AnimationCullingMode::CullPose, true, 1, true, false);
	CHECK(update.Advance);
	CHECK(update.Live);
	CHECK_FALSE(update.Capture);

	// Far away: captured on its frames, held in between.
	update = gate.Plan(AnimationCullingMode::AlwaysAnimate, true, 4, false, false);
	CHECK_FALSE(update.Live);
	CHECK(update.Capture);
	CHECK_FALSE(gate.Plan(AnimationCullingMode::AlwaysAnimate, true, 4, false, true).Capture);
	CHECK(gate.Plan(AnimationCullingMode::AlwaysAnimate, true, 4, true, true).Capture);

	// Hidden under Cull Pose: the play head runs on, the drawn pose stays.
	update = gate.Plan(AnimationCullingMode::CullPose, false, 1, true, true);
	CHECK(update.Advance);
	CHECK_FALSE(update.Live);
	CHECK_FALSE(update.Capture);
	// Back in view far away: captured at once instead of waiting for its frame.
	CHECK(gate.Plan(AnimationCullingMode::CullPose, true, 4, false, true).Capture);
	gate.Plan(AnimationCullingMode::CullPose, false, 1, false, true);
	CHECK(gate.Plan(AnimationCullingMode::CullPose, true, 1, false, true).Live);

	// Cull Completely pauses the play head too.
	CHECK_FALSE(gate.Plan(AnimationCullingMode::CullCompletely, false, 1, true, true).Advance);
}

TEST_CASE("Distance rate follows the component or the project setting") {
	GraphicsSettings project;
	AnimationCullingSettings settings;
	float half = 0.0f;
	float quarter = 0.0f;
	CHECK(settings.DistanceRate == AnimationDistanceRate::ProjectSetting);
	CHECK_FALSE(AnimationCulling::ResolveDistanceRate(settings, project, half, quarter));
	CHECK_FALSE(AnimationCulling::NeedsCamera(settings, project));

	project.AnimationDistanceRateEnabled = true;
	project.AnimationHalfRateDistance = 10.0f;
	project.AnimationQuarterRateDistance = 30.0f;
	REQUIRE(AnimationCulling::ResolveDistanceRate(settings, project, half, quarter));
	CHECK(half == doctest::Approx(10.0f));
	CHECK(quarter == doctest::Approx(30.0f));

	settings.DistanceRate = AnimationDistanceRate::Off;
	CHECK_FALSE(AnimationCulling::ResolveDistanceRate(settings, project, half, quarter));

	settings.DistanceRate = AnimationDistanceRate::Custom;
	settings.HalfRateDistance = 5.0f;
	settings.QuarterRateDistance = 6.0f;
	project.AnimationDistanceRateEnabled = false;
	REQUIRE(AnimationCulling::ResolveDistanceRate(settings, project, half, quarter));
	CHECK(half == doctest::Approx(5.0f));
	CHECK(quarter == doctest::Approx(6.0f));

	settings = {};
	settings.CullingMode = AnimationCullingMode::CullPose;
	CHECK(AnimationCulling::NeedsCamera(settings, project));

	GraphicsSettings broken;
	broken.AnimationHalfRateDistance = -3.0f;
	broken.AnimationQuarterRateDistance = std::nanf("");
	const GraphicsSettings clamped = ClampGraphicsSettings(broken);
	CHECK(clamped.AnimationHalfRateDistance == doctest::Approx(0.0f));
	CHECK(clamped.AnimationQuarterRateDistance == doctest::Approx(GraphicsSettings{}.AnimationQuarterRateDistance));
}

TEST_CASE("2D animation bounds are culled against the camera's view rectangle") {
	AnimationViewer viewer;
	viewer.Set2D(AABB({ -8.0f, -5.0f }, { 8.0f, 5.0f }), { 0.0f, 0.0f });
	AnimationBounds sprite;
	sprite.Kind = AnimationBounds::Space::World2D;
	sprite.Box = AABB({ 7.5f, 0.0f }, { 9.0f, 1.0f });
	CHECK(sprite.IsVisible(viewer));
	sprite.Box = AABB({ 20.0f, 20.0f }, { 21.0f, 21.0f });
	CHECK_FALSE(sprite.IsVisible(viewer));
	// Without a camera of its dimension, or without anything drawn, an animation counts as visible.
	CHECK(sprite.IsVisible(AnimationViewer{}));
	CHECK(AnimationBounds{}.IsVisible(viewer));
}

#if FUNKE_WITH_3D
TEST_CASE("3D animation bounds are culled against the camera frustum") {
	Camera3DComponent camera;
	AnimationViewer viewer;
	// At the origin, looking down -Z.
	viewer.Set3D(camera.GetProjectionMatrix(16.0f / 9.0f) * Camera3DComponent::ViewFromWorld(glm::mat4(1.0f)), glm::vec3(0.0f));
	const auto at = [](const glm::vec3& position) {
		return AnimationBounds::FromLocalBox(glm::vec3(-0.5f), glm::vec3(0.5f), glm::translate(glm::mat4(1.0f), position));
	};
	CHECK(at({ 0.0f, 0.0f, -10.0f }).IsVisible(viewer));
	CHECK_FALSE(at({ 0.0f, 0.0f, 10.0f }).IsVisible(viewer));
	CHECK_FALSE(at({ -40.0f, 0.0f, -10.0f }).IsVisible(viewer));
	CHECK_FALSE(at({ 0.0f, 0.0f, -(camera.FarClip + 20.0f) }).IsVisible(viewer));
	// Just past the left edge: the margin for moving limbs keeps it.
	const float halfWidth = 10.0f * std::tan(glm::radians(camera.FieldOfView * 0.5f)) * 16.0f / 9.0f;
	CHECK(at({ -(halfWidth + 1.0f), 0.0f, -10.0f }).IsVisible(viewer));
	CHECK(at({ 0.0f, 0.0f, 0.0f }).Radius > glm::length(glm::vec3(0.5f)));
}

TEST_CASE("Animators far from the camera update every 4th frame and keep their tempo") {
	auto scene = Scene::CreateDetachedScene("AnimatorDistanceRate");
	Entity camera = scene->CreateEntity("Camera");
	camera.AddComponent<Transform3DComponent>();
	camera.AddComponent<Camera3DComponent>();
	Entity root = scene->CreateEntity("Walker");
	root.AddComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, -100.0f };
	Entity body = scene->CreateEntity("Body");
	body.AddComponent<Transform3DComponent>();
	body.SetParent(root);
	Transform3DHierarchySystem::Propagate(*scene);

	// Body x = 100 * clip time, so after frame f (0.01 s each) at full rate it is f + 1.
	constexpr uint64_t k_ClipGuid = 0xA11C0C0001ull;
	AnimationClip clip;
	clip.Length = 10.0f;
	AnimationTrack track;
	track.Path = "Body";
	track.Component = "Transform3D";
	track.Property = "Position.x";
	track.Curve.SetKey(0.0f, 0.0f, KeyInterpolation::Linear);
	track.Curve.SetKey(10.0f, 1000.0f, KeyInterpolation::Linear);
	clip.Tracks = { track };
	AnimationClipLibrary::Set(k_ClipGuid, clip);

	AnimatorComponent& animator = root.AddComponent<AnimatorComponent>();
	animator.Clip = k_ClipGuid;
	animator.DistanceRate = AnimationDistanceRate::Custom;
	animator.HalfRateDistance = 20.0f;
	animator.QuarterRateDistance = 40.0f;
	const GraphicsSettings project;
	const uint32_t phase = PhaseOf(root);

	auto bodyX = [&] { return body.GetComponent<Transform3DComponent>().Position.x; };
	int writes = 0;
	for (uint64_t frame = 0; frame < 24; ++frame) {
		const float before = bodyX();
		const uint32_t posed = AnimatorSystem::StepScene(*scene, false, 0.01f, 0.01f, frame, project);
		const bool due = frame == 0 || AnimationCulling::IsDue(frame, phase, 4);
		CHECK(posed == (due ? 1u : 0u));
		if (due) {
			// The skipped frames' time is in the step: the pose is where full rate would have it.
			CHECK(bodyX() == doctest::Approx(static_cast<float>(frame + 1)));
			++writes;
		}
		else {
			CHECK(bodyX() == doctest::Approx(before));
		}
	}
	// The first frame plus one in four.
	CHECK(writes >= 6);
	CHECK(writes <= 7);

	// Switching the rate off at runtime adds the time the skipped frames still owed.
	animator.DistanceRate = AnimationDistanceRate::Off;
	CHECK(AnimatorSystem::StepScene(*scene, false, 0.01f, 0.01f, 24, project) == 1u);
	CHECK(bodyX() == doctest::Approx(25.0f));
	animator.DistanceRate = AnimationDistanceRate::Custom;

	// Close to the camera it writes every frame again.
	root.GetComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, -5.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	for (uint64_t frame = 25; frame < 28; ++frame) {
		CHECK(AnimatorSystem::StepScene(*scene, false, 0.01f, 0.01f, frame, project) == 1u);
		CHECK(bodyX() == doctest::Approx(static_cast<float>(frame + 1)));
	}

	// The project setting applies to Animators left on Project Setting.
	animator.DistanceRate = AnimationDistanceRate::ProjectSetting;
	root.GetComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, -100.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	GraphicsSettings rated;
	rated.AnimationDistanceRateEnabled = true;
	uint32_t projectWrites = 0;
	for (uint64_t frame = 28; frame < 36; ++frame) projectWrites += AnimatorSystem::StepScene(*scene, false, 0.01f, 0.01f, frame, rated);
	CHECK(projectWrites == 2u);
	// The last of those writes was on one of the final four frames.
	CHECK(bodyX() >= 33.0f - 1e-3f);
	CHECK(bodyX() <= 36.0f + 1e-3f);

	AnimationClipLibrary::Invalidate(k_ClipGuid);
}

TEST_CASE("Culled Animators keep their clock off screen and show the caught-up pose in view") {
	auto scene = Scene::CreateDetachedScene("AnimatorCulling");
	Entity camera = scene->CreateEntity("Camera");
	camera.AddComponent<Transform3DComponent>();
	camera.AddComponent<Camera3DComponent>();
	Entity root = scene->CreateEntity("Walker");
	// Behind the camera.
	root.AddComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, 20.0f };
	Entity body = scene->CreateEntity("Body");
	body.AddComponent<Transform3DComponent>();
	body.AddComponent<MeshRendererComponent>();
	body.SetParent(root);
	Transform3DHierarchySystem::Propagate(*scene);

	constexpr uint64_t k_ClipGuid = 0xA11C0C0002ull;
	AnimationClip clip;
	clip.Length = 10.0f;
	AnimationTrack track;
	track.Path = "Body";
	track.Component = "Transform3D";
	track.Property = "Position.x";
	track.Curve.SetKey(0.0f, 0.0f, KeyInterpolation::Linear);
	track.Curve.SetKey(10.0f, 10.0f, KeyInterpolation::Linear);
	clip.Tracks = { track };
	AnimationClipLibrary::Set(k_ClipGuid, clip);

	AnimatorComponent& animator = root.AddComponent<AnimatorComponent>();
	animator.Clip = k_ClipGuid;
	animator.CullingMode = AnimationCullingMode::CullPose;
	const GraphicsSettings project;
	auto bodyX = [&] { return body.GetComponent<Transform3DComponent>().Position.x; };

	for (uint64_t frame = 0; frame < 10; ++frame) {
		CHECK(AnimatorSystem::StepScene(*scene, false, 0.1f, 0.1f, frame, project) == 0u);
	}
	CHECK(bodyX() == doctest::Approx(0.0f));
	AnimatorRuntime* runtime = animator.Runtime.Get();
	REQUIRE(runtime);
	// The clock ran on (at the hidden rate), so the next visible pose is current.
	CHECK(runtime->GetStateMachine().GetCurrentNormalizedTime(runtime->GetClipInfos()) > 0.05f);

	root.GetComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, -20.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	// The cached bounds refresh within a few frames; the first visible step writes the caught-up pose.
	uint64_t frame = 10;
	for (; frame < 14; ++frame) {
		if (AnimatorSystem::StepScene(*scene, false, 0.1f, 0.1f, frame, project) == 1u) break;
	}
	REQUIRE(frame < 14);
	CHECK(bodyX() == doctest::Approx(static_cast<float>(frame + 1) * 0.1f));

	// Cull Completely pauses: nothing moves while hidden and the clip resumes where it stopped.
	animator.CullingMode = AnimationCullingMode::CullCompletely;
	const float shown = bodyX();
	root.GetComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, 20.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	const uint64_t hiddenFrom = frame + 1;
	for (frame = hiddenFrom; frame < hiddenFrom + 20; ++frame) AnimatorSystem::StepScene(*scene, false, 0.1f, 0.1f, frame, project);
	root.GetComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, -20.0f };
	Transform3DHierarchySystem::Propagate(*scene);
	for (int i = 0; i < 4; ++i, ++frame) AnimatorSystem::StepScene(*scene, false, 0.1f, 0.1f, frame, project);
	// Up to four frames of bounds refresh delay plus the step after it, never the 20 hidden frames.
	CHECK(bodyX() > shown);
	CHECK(bodyX() < shown + 0.75f);

	AnimationClipLibrary::Invalidate(k_ClipGuid);
}

TEST_CASE("Skinned meshes keep the exact play head while the drawn pose is held") {
	SkinnedMeshData data;
	SkeletalClip walk;
	walk.Name = "Walk";
	walk.Duration = 2.0f;
	SkeletalClip run;
	run.Name = "Run";
	run.Duration = 1.0f;
	data.Clips = { walk, run };

	SkinnedMeshRendererComponent renderer;
	renderer.Awoken = true;
	renderer.Playing = true;
	SkinnedMeshAnimationSystem::Step(renderer, &data, 0.25f);
	SkinnedMeshAnimationSystem::HoldPose(renderer, data);
	SkinnedMeshAnimationSystem::Step(renderer, &data, 0.5f);
	CHECK(renderer.Time == doctest::Approx(0.75f));
	CHECK(SkinnedMeshAnimationSystem::MakePoseRequest(renderer, data).Time == doctest::Approx(0.75f));
	CHECK(SkinnedMeshAnimationSystem::GetDrawnPose(renderer, data).Time == doctest::Approx(0.25f));
	CHECK(SkinnedMeshAnimationSystem::GetDrawnPose(renderer, data).Clip == 0);

	// A cross-fade in flight is held with both clips while the play head finishes it.
	SkinnedMeshAnimationSystem::Play(renderer, &data, "Run", 0.5f);
	SkinnedMeshAnimationSystem::Step(renderer, &data, 0.1f);
	SkinnedMeshAnimationSystem::HoldPose(renderer, data);
	const SkeletalPoseRequest held = SkinnedMeshAnimationSystem::GetDrawnPose(renderer, data);
	CHECK(held.Clip == 1);
	CHECK(held.PreviousClip == 0);
	CHECK(held.Fade == doctest::Approx(0.2f));
	SkinnedMeshAnimationSystem::Step(renderer, &data, 0.4f);
	CHECK(SkinnedMeshAnimationSystem::MakePoseRequest(renderer, data).PreviousClip == -1);
	CHECK(SkinnedMeshAnimationSystem::GetDrawnPose(renderer, data) == held);

	renderer.PoseHeld = false;
	CHECK(SkinnedMeshAnimationSystem::GetDrawnPose(renderer, data) == SkinnedMeshAnimationSystem::MakePoseRequest(renderer, data));
}
#endif

TEST_CASE("Animator culling fields round-trip and are only written when changed") {
	const ComponentInfo* info = SceneManager::GetComponentRegistryOrBuiltIn().FindBySerializedName("Animator");
	REQUIRE(info);
	auto scene = Scene::CreateDetachedScene("AnimatorCullingSerialize");
	Entity source = scene->CreateEntity("Source");
	AnimatorComponent& animator = source.AddComponent<AnimatorComponent>();
	const Json::Value defaults = info->serialize(source);
	CHECK(defaults.FindMember("cullingMode") == nullptr);
	CHECK(defaults.FindMember("distanceRate") == nullptr);
	CHECK(defaults.FindMember("halfRateDistance") == nullptr);

	animator.CullingMode = AnimationCullingMode::CullCompletely;
	animator.DistanceRate = AnimationDistanceRate::Custom;
	animator.HalfRateDistance = 12.5f;
	animator.QuarterRateDistance = 33.0f;
	const Json::Value json = info->serialize(source);
	Entity target = scene->CreateEntity("Target");
	target.AddComponent<AnimatorComponent>();
	info->deserialize(target, Json::Parse(Json::Stringify(json)));
	const AnimatorComponent& loaded = target.GetComponent<AnimatorComponent>();
	CHECK(loaded.CullingMode == AnimationCullingMode::CullCompletely);
	CHECK(loaded.DistanceRate == AnimationDistanceRate::Custom);
	CHECK(loaded.HalfRateDistance == doctest::Approx(12.5f));
	CHECK(loaded.QuarterRateDistance == doctest::Approx(33.0f));

	// Unknown enum values and missing keys load the defaults.
	info->deserialize(target, Json::Parse(R"({"cullingMode": 9, "distanceRate": -1})"));
	CHECK(target.GetComponent<AnimatorComponent>().CullingMode == AnimationCullingMode::AlwaysAnimate);
	CHECK(target.GetComponent<AnimatorComponent>().DistanceRate == AnimationDistanceRate::ProjectSetting);
	CHECK(target.GetComponent<AnimatorComponent>().HalfRateDistance == doctest::Approx(AnimatorComponent{}.HalfRateDistance));
}

#if FUNKE_WITH_3D
TEST_CASE("Skinned Mesh Renderer culling fields round-trip and are only written when changed") {
	const ComponentInfo* info = SceneManager::GetComponentRegistryOrBuiltIn().FindBySerializedName("SkinnedMeshRenderer");
	REQUIRE(info);
	auto scene = Scene::CreateDetachedScene("SkinnedCullingSerialize");
	Entity source = scene->CreateEntity("Source");
	source.AddComponent<Transform3DComponent>();
	SkinnedMeshRendererComponent& renderer = source.AddComponent<SkinnedMeshRendererComponent>();
	CHECK(info->serialize(source).FindMember("cullingMode") == nullptr);

	renderer.CullingMode = AnimationCullingMode::CullPose;
	renderer.DistanceRate = AnimationDistanceRate::Off;
	renderer.QuarterRateDistance = 80.0f;
	const Json::Value json = info->serialize(source);
	CHECK(json.FindMember("halfRateDistance") == nullptr);
	Entity target = scene->CreateEntity("Target");
	target.AddComponent<Transform3DComponent>();
	target.AddComponent<SkinnedMeshRendererComponent>();
	info->deserialize(target, Json::Parse(Json::Stringify(json)));
	const SkinnedMeshRendererComponent& loaded = target.GetComponent<SkinnedMeshRendererComponent>();
	CHECK(loaded.CullingMode == AnimationCullingMode::CullPose);
	CHECK(loaded.DistanceRate == AnimationDistanceRate::Off);
	CHECK(loaded.HalfRateDistance == doctest::Approx(SkinnedMeshRendererComponent{}.HalfRateDistance));
	CHECK(loaded.QuarterRateDistance == doctest::Approx(80.0f));
}
#endif

TEST_CASE("FunkeProject round-trips the animation distance rate through project.json") {
	TempDirectory dir("FunkeAnimationRateRT_");
	FunkeProject project;
	project.Name = "Animation Rate RT";
	project.RootDirectory = dir.Root.string();
	project.ProjectFilePath = Path::Combine(dir.Root.string(), "project.json");
	project.Graphics.AnimationDistanceRateEnabled = true;
	project.Graphics.AnimationHalfRateDistance = 15.0f;
	project.Graphics.AnimationQuarterRateDistance = 45.0f;
	REQUIRE(project.Save());

	const FunkeProject loaded = FunkeProject::Load(dir.Root.string());
	CHECK(loaded.Graphics.AnimationDistanceRateEnabled);
	CHECK(loaded.Graphics.AnimationHalfRateDistance == doctest::Approx(15.0f));
	CHECK(loaded.Graphics.AnimationQuarterRateDistance == doctest::Approx(45.0f));

	// Projects saved before the setting existed keep animating every frame.
	REQUIRE(File::WriteAllText(project.ProjectFilePath, R"({"name":"Animation Rate RT","graphics":{"renderScale":0.5}})"));
	const FunkeProject legacy = FunkeProject::Load(dir.Root.string());
	CHECK_FALSE(legacy.Graphics.AnimationDistanceRateEnabled);
	CHECK(legacy.Graphics.AnimationHalfRateDistance == doctest::Approx(GraphicsSettings{}.AnimationHalfRateDistance));
}
