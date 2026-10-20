#pragma once
#include "Animation/AnimationCulling.hpp"
#include "Core/Export.hpp"
#include "Collections/Color.hpp"

#include <cstdint>
#include <string>

namespace Funke {

	// Draws an animated model (.gltf/.glb mesh asset with a skin or node animation) and plays one of its clips,
	// cross-fading when the clip changes. Skinning runs on the CPU, so materials, shadows and custom shaders behave
	// exactly as on a MeshRenderer.
	struct FUNKE_API SkinnedMeshRendererComponent {
		// Mesh asset GUID (.gltf/.glb); a model without a skeleton draws static.
		uint64_t Model = 0;
		// .material asset GUID; 0 = the default material (white, roughness 0.5, lit).
		uint64_t Material = 0;
		Color Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Without a material: false draws the flat color and ignores lights. A material's own Lit flag wins.
		bool Lit = true;
		bool CastShadows = true;

		// Clip name from the model; empty = its first clip. Edit mode shows the clip's first frame.
		std::string Clip;
		float Speed = 1.0f;
		bool Loop = true;
		bool PlayOnAwake = true;
		// Editor only: plays the clip in edit mode as a preview.
		bool PreviewInEditMode = false;
		// Off screen (outside the game camera's view): keep animating, keep only the play head (no skinning), or pause.
		AnimationCullingMode CullingMode = AnimationCullingMode::AlwaysAnimate;
		// Fewer pose updates (skinning) far from the camera: every 2nd / 4th frame.
		AnimationDistanceRate DistanceRate = AnimationDistanceRate::ProjectSetting;
		// With DistanceRate Custom: world units from the camera from which the pose updates every 2nd / 4th frame.
		float HalfRateDistance = 20.0f;
		float QuarterRateDistance = 40.0f;

		// Runtime state; not serialized, so every Play session starts fresh.
		float Time = 0.0f;
		// Clip index being faded out, -1 = none.
		int PreviousClip = -1;
		float PreviousTime = 0.0f;
		// 0..1 progress of the cross-fade from PreviousClip to Clip.
		float Fade = 1.0f;
		float FadeDuration = 0.0f;
		bool Playing = false;
		// Set on the first play-mode update, which applies PlayOnAwake.
		bool Awoken = false;
		// While culling or the distance rate holds the drawn pose back, the renderer skins the Held* pose; the play head
		// above keeps the exact time.
		bool PoseHeld = false;
		int HeldClip = -1;
		float HeldTime = 0.0f;
		int HeldPreviousClip = -1;
		float HeldPreviousTime = 0.0f;
		float HeldFade = 1.0f;
		AnimationPoseGate PoseGate;

		AnimationCullingSettings GetCulling() const { return { CullingMode, DistanceRate, HalfRateDistance, QuarterRateDistance }; }
	};

}
