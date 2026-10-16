#pragma once

#include "Animation/AnimationBinding.hpp"
#include "Animation/AnimatorRuntime.hpp"
#include "Inspector/PropertyValue.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Funke {
	class Scene;
	struct ComponentInfo;
	struct PropertyDescriptor;

	// A component property below an Animator that a track can drive.
	struct AnimatableProperty {
		EntityHandle EntityId = entt::null;
		std::string Path;
		const ComponentInfo* Info = nullptr;
		const PropertyDescriptor* Descriptor = nullptr;
	};

	// True for property types the Animator can drive (curves or discrete keys).
	bool IsAnimatableType(PropertyType type);
	// Every animatable property of root and its descendants, root first.
	std::vector<AnimatableProperty> CollectAnimatableProperties(Scene& scene, EntityHandle root);

	// A property the user changed while recording, with the value from before the change.
	struct RecordedChange {
		std::string Path;
		std::string Component;
		std::string Property;
		PropertyType Type = PropertyType::None;
		PropertyValue Before;
		PropertyValue After;
	};

	// Edit-mode animation preview. BeginFrame writes the previewed poses before the panels draw, EndFrame (after the
	// viewports rendered) puts the authored values back, so saving, the play snapshot and autosave never see an animated
	// value and the scene is not dirtied. Edits made in between are detected by comparing values.
	class AnimationPreview {
	public:
		// The Animation window's clip at time on root; cleared every frame, so the window sets it while it previews.
		void SetClipPose(Scene* scene, EntityHandle root, uint64_t clipGuid, float time);
		// While set, changes to properties below the clip pose's root are reported instead of kept.
		void SetRecording(bool recording) { m_Recording = recording; }
		std::vector<RecordedChange> TakeRecordedChanges() { return std::move(m_Recorded); }
		// Track errors of the clip pose ("Arm: no child entity ...").
		const std::vector<std::string>& GetClipErrors() const { return m_ClipErrors; }

		void BeginFrame(Scene* scene);
		void EndFrame();
		// Drops edit-mode Animator state (Play, scene change).
		void Reset();

	private:
		struct Written {
			AnimationTarget Target;
			PropertyValue Authored;
			PropertyValue Posed;
		};

		struct Watched {
			AnimatableProperty Property;
			PropertyValue Value;
		};

		void CaptureTargets(Scene& scene, const AnimationTargetSet& targets);
		void ApplyEditModeAnimators(Scene& scene);
		void ApplyClipPose(Scene& scene);

		Scene* m_Scene = nullptr;
		Scene* m_ClipScene = nullptr;
		EntityHandle m_ClipRoot = entt::null;
		uint64_t m_ClipGuid = 0;
		float m_ClipTime = 0.0f;
		bool m_ClipRequested = false;
		std::unique_ptr<AnimatorRuntime> m_ClipRuntime;
		uint64_t m_ClipRuntimeGuid = 0;
		std::vector<std::string> m_ClipErrors;

		std::unordered_map<EntityHandle, std::unique_ptr<AnimatorRuntime>> m_EditModeRuntimes;

		bool m_Recording = false;
		std::vector<Written> m_Written;
		std::vector<Watched> m_Watched;
		std::vector<RecordedChange> m_Recorded;
	};

}
