#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Assets/AssetKind.hpp"
#include "Components/General/AnimatorComponent.hpp"

#include <algorithm>
#include <charconv>

// Animator: keyframe clips and state machines over any component property.
namespace Funke::BuiltInComponentRegistrationInternal {
	namespace {
		uint64_t ReadGuid(const Json::Value* v) {
			if (!v) return 0;
			if (v->IsNumber()) return v->AsUInt64Or(0);
			const std::string text = v->AsStringOr();
			uint64_t guid = 0;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), guid);
			return result.ec == std::errc() ? guid : 0;
		}

		template <uint64_t AnimatorComponent::* Member>
		PropertyDescriptor MakeAnimatorAssetRef(const char* name, const char* displayName, AssetKind kind, const char* tooltip) {
			PropertyDescriptor desc;
			desc.Name = name;
			desc.DisplayName = displayName;
			desc.Type = PropertyType::AssetRef;
			desc.Metadata.AssetKindFilter = kind;
			desc.Metadata.Tooltip = tooltip;
			desc.Get = [](const Entity& e) -> PropertyValue {
				PropertyValue v;
				v.Type = PropertyType::AssetRef;
				v.UIntValue = e.GetComponent<AnimatorComponent>().*Member;
				return v;
			};
			desc.Set = [kind](Entity& e, const PropertyValue& v) {
				// Re-read on assignment so an asset created moments ago (or edited on disk) is picked up.
				if (kind == AssetKind::AnimatorController) AnimatorControllerLibrary::Invalidate(v.UIntValue);
				else AnimationClipLibrary::Invalidate(v.UIntValue);
				e.GetComponent<AnimatorComponent>().*Member = v.UIntValue;
			};
			return desc;
		}
	}

	void RegisterAnimation(ComponentRegistry& registry) {
		ComponentInfo info{ "Animator", "Animation", ComponentCategory::Component };
		info.serializedName = "Animator";
		info.properties = {
			MakeAnimatorAssetRef<&AnimatorComponent::Controller>("Controller", "Controller", AssetKind::AnimatorController,
				"An .animctrl state machine. Wins over Clip."),
			MakeAnimatorAssetRef<&AnimatorComponent::Clip>("Clip", "Clip", AssetKind::AnimationClip,
				"An .anim clip played on its own when there is no Controller."),
			Properties::MakeWith<float>("Speed", "Speed",
				[](const Entity& e) { return e.GetComponent<AnimatorComponent>().Speed; },
				[](Entity& e, float v) { e.GetComponent<AnimatorComponent>().Speed = v; },
				Properties::Meta::DragSpeed(0.01f)),
			Properties::Make("UpdateMode", "Update Mode", &AnimatorComponent::UpdateMode,
				Properties::Meta::Tooltip("Normal follows Time.TimeScale, Unscaled Time ignores it, Fixed steps with physics.")),
			Properties::Make("ApplyInEditMode", "Apply In Edit Mode", &AnimatorComponent::ApplyInEditMode,
				Properties::Meta::Tooltip("Previews the animation in the editor without Play. The scene keeps its authored values.")),
			WithEnumLabels(Properties::Make("CullingMode", "Culling Mode", &AnimatorComponent::CullingMode,
				Properties::Meta::Header("Optimization").WithTooltip("When no renderer below the Animator is in the game camera's view: "
					"Always Animate keeps going, Cull Pose runs only the clock, transitions and events, Cull Completely pauses.")),
				{ "Always Animate", "Cull Pose", "Cull Completely" }),
			WithEnumLabels(Properties::Make("DistanceRate", "Distance Rate", &AnimatorComponent::DistanceRate,
				Properties::Meta::Tooltip("Updates every 2nd / 4th frame far from the game camera; the skipped time is added to the next "
					"update. Project Setting follows Project Settings > Graphics > Animation. Not in Fixed update mode.")),
				{ "Project Setting", "Off", "Custom" }),
			Properties::Make("HalfRateDistance", "Half Rate From", &AnimatorComponent::HalfRateDistance,
				Properties::Meta::DragSpeed(0.5f).WithDragMin(0.0).WithTooltip("World units from the camera from which it updates every 2nd frame.")
					.WithEnabledIf([](const Entity& e) { return e.GetComponent<AnimatorComponent>().DistanceRate == AnimationDistanceRate::Custom; })),
			Properties::Make("QuarterRateDistance", "Quarter Rate From", &AnimatorComponent::QuarterRateDistance,
				Properties::Meta::DragSpeed(0.5f).WithDragMin(0.0).WithTooltip("World units from the camera from which it updates every 4th frame.")
					.WithEnabledIf([](const Entity& e) { return e.GetComponent<AnimatorComponent>().DistanceRate == AnimationDistanceRate::Custom; })),
		};
		info.serialize = [](Entity e) -> Json::Value {
			const auto& a = e.GetComponent<AnimatorComponent>();
			Json::Value v = Json::Value::MakeObject();
			if (a.Controller != 0) v.AddMember("controllerAsset", Json::Value(std::to_string(a.Controller)));
			if (a.Clip != 0) v.AddMember("clipAsset", Json::Value(std::to_string(a.Clip)));
			v.AddMember("speed", Json::Value(a.Speed));
			v.AddMember("updateMode", Json::Value(static_cast<int>(a.UpdateMode)));
			if (a.ApplyInEditMode) v.AddMember("applyInEditMode", Json::Value(true));
			// Only when changed: an older prefab without the keys would otherwise show every instance as overridden.
			const AnimatorComponent d{};
			if (a.CullingMode != d.CullingMode) v.AddMember("cullingMode", Json::Value(static_cast<int>(a.CullingMode)));
			if (a.DistanceRate != d.DistanceRate) v.AddMember("distanceRate", Json::Value(static_cast<int>(a.DistanceRate)));
			if (a.HalfRateDistance != d.HalfRateDistance) v.AddMember("halfRateDistance", Json::Value(a.HalfRateDistance));
			if (a.QuarterRateDistance != d.QuarterRateDistance) v.AddMember("quarterRateDistance", Json::Value(a.QuarterRateDistance));
			return v;
		};
		info.deserialize = [](Entity e, const Json::Value& v) {
			auto& a = e.GetComponent<AnimatorComponent>();
			const AnimatorComponent d{};
			a.Controller = ReadGuid(v.FindMember("controllerAsset"));
			a.Clip = ReadGuid(v.FindMember("clipAsset"));
			const Json::Value* m = nullptr;
			a.Speed = (m = v.FindMember("speed")) ? static_cast<float>(m->AsDoubleOr(d.Speed)) : d.Speed;
			const int mode = (m = v.FindMember("updateMode")) ? m->AsIntOr(0) : 0;
			a.UpdateMode = mode >= 0 && mode <= static_cast<int>(AnimatorUpdateMode::Fixed) ? static_cast<AnimatorUpdateMode>(mode) : d.UpdateMode;
			a.ApplyInEditMode = (m = v.FindMember("applyInEditMode")) ? m->AsBoolOr(false) : d.ApplyInEditMode;
			const int culling = (m = v.FindMember("cullingMode")) ? m->AsIntOr(0) : 0;
			a.CullingMode = culling >= 0 && culling <= static_cast<int>(AnimationCullingMode::CullCompletely)
				? static_cast<AnimationCullingMode>(culling) : d.CullingMode;
			const int rate = (m = v.FindMember("distanceRate")) ? m->AsIntOr(0) : 0;
			a.DistanceRate = rate >= 0 && rate <= static_cast<int>(AnimationDistanceRate::Custom)
				? static_cast<AnimationDistanceRate>(rate) : d.DistanceRate;
			a.HalfRateDistance = (m = v.FindMember("halfRateDistance"))
				? std::max(static_cast<float>(m->AsDoubleOr(d.HalfRateDistance)), 0.0f) : d.HalfRateDistance;
			a.QuarterRateDistance = (m = v.FindMember("quarterRateDistance"))
				? std::max(static_cast<float>(m->AsDoubleOr(d.QuarterRateDistance)), 0.0f) : d.QuarterRateDistance;
			// Loaded data replaces whatever was playing.
			a.Runtime.Reset();
		};
		registry.Register<AnimatorComponent>(info);
	}
}
