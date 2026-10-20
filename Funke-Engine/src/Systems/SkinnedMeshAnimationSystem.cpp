#include "pch.hpp"
#include "Systems/SkinnedMeshAnimationSystem.hpp"

#include "Animation/AnimationCulling.hpp"
#include "Animation/AnimationViewer.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Funke {
	namespace {
		float ClipDuration(const SkinnedMeshData& data, int clip) {
			return clip >= 0 && static_cast<size_t>(clip) < data.Clips.size() ? data.Clips[static_cast<size_t>(clip)].Duration : 0.0f;
		}

		GraphicsSettings ProjectGraphics() {
			const Application* application = Application::GetInstance();
			return application ? application->GetEffectiveGraphicsSettings() : GraphicsSettings{};
		}
	}

	int SkinnedMeshAnimationSystem::ResolveClip(const SkinnedMeshRendererComponent& renderer, const SkinnedMeshData& data) {
		if (renderer.Clip.empty()) return data.Clips.empty() ? -1 : 0;
		return data.FindClip(renderer.Clip);
	}

	void SkinnedMeshAnimationSystem::Step(SkinnedMeshRendererComponent& renderer, const SkinnedMeshData* data, float dt) {
		if (!data) return;
		const float delta = dt * renderer.Speed;
		if (renderer.Fade < 1.0f) {
			if (renderer.PreviousClip >= 0) {
				SkeletalAnimation::AdvanceTime(renderer.PreviousTime, delta, ClipDuration(*data, renderer.PreviousClip), renderer.Loop);
			}
			// The fade runs in real time, independent of Speed.
			renderer.Fade = renderer.FadeDuration > 0.0f ? std::min(1.0f, renderer.Fade + std::abs(dt) / renderer.FadeDuration) : 1.0f;
			if (renderer.Fade >= 1.0f) renderer.PreviousClip = -1;
		}
		if (!renderer.Playing) return;
		const int clip = ResolveClip(renderer, *data);
		if (clip < 0) return;
		if (!SkeletalAnimation::AdvanceTime(renderer.Time, delta, ClipDuration(*data, clip), renderer.Loop)) {
			renderer.Playing = false;
		}
	}

	void SkinnedMeshAnimationSystem::Play(SkinnedMeshRendererComponent& renderer, const SkinnedMeshData* data, const std::string& clip, float crossFade) {
		renderer.Awoken = true;
		if (renderer.Playing && renderer.Clip == clip) return;
		const int current = data ? ResolveClip(renderer, *data) : -1;
		if (current >= 0 && crossFade > 0.0f) {
			renderer.PreviousClip = current;
			renderer.PreviousTime = renderer.Time;
			renderer.Fade = 0.0f;
			renderer.FadeDuration = crossFade;
		}
		else {
			renderer.PreviousClip = -1;
			renderer.Fade = 1.0f;
		}
		renderer.Clip = clip;
		renderer.Time = 0.0f;
		if (data && renderer.Speed < 0.0f) renderer.Time = ClipDuration(*data, ResolveClip(renderer, *data));
		renderer.Playing = true;
	}

	SkeletalPoseRequest SkinnedMeshAnimationSystem::MakePoseRequest(const SkinnedMeshRendererComponent& renderer, const SkinnedMeshData& data) {
		SkeletalPoseRequest request;
		request.Clip = ResolveClip(renderer, data);
		request.Time = renderer.Time;
		if (renderer.Fade < 1.0f && renderer.PreviousClip >= 0) {
			request.PreviousClip = renderer.PreviousClip;
			request.PreviousTime = renderer.PreviousTime;
			request.Fade = renderer.Fade;
		}
		return request;
	}

	SkeletalPoseRequest SkinnedMeshAnimationSystem::GetDrawnPose(const SkinnedMeshRendererComponent& renderer, const SkinnedMeshData& data) {
		if (!renderer.PoseHeld) return MakePoseRequest(renderer, data);
		SkeletalPoseRequest request;
		request.Clip = renderer.HeldClip;
		request.Time = renderer.HeldTime;
		request.PreviousClip = renderer.HeldPreviousClip;
		request.PreviousTime = renderer.HeldPreviousTime;
		request.Fade = renderer.HeldFade;
		return request;
	}

	void SkinnedMeshAnimationSystem::HoldPose(SkinnedMeshRendererComponent& renderer, const SkinnedMeshData& data) {
		const SkeletalPoseRequest request = MakePoseRequest(renderer, data);
		renderer.PoseHeld = true;
		renderer.HeldClip = request.Clip;
		renderer.HeldTime = request.Time;
		renderer.HeldPreviousClip = request.PreviousClip;
		renderer.HeldPreviousTime = request.PreviousTime;
		renderer.HeldFade = request.Fade;
	}

	void SkinnedMeshAnimationSystem::Update(Scene& scene) {
		if (!Application::GetIsPlaying()) return;
		auto& registry = scene.GetRegistry();
		auto view = registry.view<SkinnedMeshRendererComponent>(entt::exclude<DisabledTag>);
		if (view.begin() == view.end()) return;
		FUNKE_PROFILE_SCOPE("SkinnedMeshAnimation");
		const float dt = Time::GetDeltaTime();
		const GraphicsSettings project = ProjectGraphics();
		const uint64_t frame = static_cast<uint64_t>(Time::GetFrameCount());
		std::optional<AnimationViewer> viewer;
		uint32_t posed = 0;
		for (auto [entity, renderer] : view.each()) {
			const MeshAsset* asset = MeshLibrary::Get(renderer.Model);
			const SkinnedMeshData* data = asset ? asset->Skin.get() : nullptr;
			// Play doesn't reload scenes, so per-session state starts here (scripts may have called Play already).
			if (!renderer.Awoken) {
				renderer.Awoken = true;
				renderer.Playing = renderer.PlayOnAwake;
				renderer.PreviousClip = -1;
				renderer.Fade = 1.0f;
				renderer.Time = data && renderer.Speed < 0.0f ? ClipDuration(*data, ResolveClip(renderer, *data)) : 0.0f;
				renderer.PoseHeld = false;
				renderer.PoseGate = {};
				if (data) ++posed;
				continue;
			}

			const AnimationCullingSettings culling = renderer.GetCulling();
			if (!data || !AnimationCulling::NeedsCamera(culling, project)) {
				renderer.PoseHeld = false;
				Step(renderer, data, dt);
				if (data) ++posed;
				continue;
			}

			if (!viewer) viewer = AnimationViewer::Find(scene);
			const auto* transform = registry.try_get<Transform3DComponent>(entity);
			const glm::mat4 world = transform ? transform->WorldMatrix : glm::mat4(1.0f);
			// The bind-pose box; the margin in AnimationBounds covers the limbs a clip moves out of it.
			const bool visible = culling.CullingMode == AnimationCullingMode::AlwaysAnimate
				|| AnimationBounds::FromLocalBox(asset->BoundsMin, asset->BoundsMax, world).IsVisible(*viewer);
			uint32_t interval = 1;
			float halfRateDistance = 0.0f;
			float quarterRateDistance = 0.0f;
			if (viewer->Has3D && AnimationCulling::ResolveDistanceRate(culling, project, halfRateDistance, quarterRateDistance)) {
				interval = AnimationCulling::UpdateInterval(glm::distance(viewer->Position3D, glm::vec3(world[3])), halfRateDistance, quarterRateDistance);
			}
			const uint32_t phase = AnimationCulling::PhaseOf(static_cast<uint32_t>(entt::to_integral(entity)));
			const AnimationPoseGate::Update update = renderer.PoseGate.Plan(culling.CullingMode, visible, interval,
				AnimationCulling::IsDue(frame, phase, interval), renderer.PoseHeld);
			if (update.Advance) Step(renderer, data, dt);
			if (update.Live) {
				renderer.PoseHeld = false;
				++posed;
			}
			else if (update.Capture) {
				HoldPose(renderer, *data);
				++posed;
			}
		}
		FUNKE_PROFILE_VALUE("Skinned Poses", static_cast<float>(posed));
	}

	void SkinnedMeshAnimationSystem::OnPreRender(Scene& scene) {
		if (Application::GetIsPlaying()) return;
		auto view = scene.GetRegistry().view<SkinnedMeshRendererComponent>(entt::exclude<DisabledTag>);
		if (view.begin() == view.end()) return;
		const float dt = Time::GetUnscaledDeltaTime();
		for (auto [entity, renderer] : view.each()) {
			// Only runtime fields change here, so nothing leaks into the saved scene.
			renderer.Awoken = false;
			renderer.PreviousClip = -1;
			renderer.Fade = 1.0f;
			renderer.Playing = renderer.PreviewInEditMode;
			renderer.PoseHeld = false;
			renderer.PoseGate = {};
			if (!renderer.PreviewInEditMode) {
				renderer.Time = 0.0f;
				continue;
			}
			const MeshAsset* asset = MeshLibrary::Get(renderer.Model);
			Step(renderer, asset ? asset->Skin.get() : nullptr, dt);
		}
	}
}
