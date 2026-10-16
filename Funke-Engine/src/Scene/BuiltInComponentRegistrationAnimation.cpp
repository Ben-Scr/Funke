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
		};
		info.serialize = [](Entity e) -> Json::Value {
			const auto& a = e.GetComponent<AnimatorComponent>();
			Json::Value v = Json::Value::MakeObject();
			if (a.Controller != 0) v.AddMember("controllerAsset", Json::Value(std::to_string(a.Controller)));
			if (a.Clip != 0) v.AddMember("clipAsset", Json::Value(std::to_string(a.Clip)));
			v.AddMember("speed", Json::Value(a.Speed));
			v.AddMember("updateMode", Json::Value(static_cast<int>(a.UpdateMode)));
			if (a.ApplyInEditMode) v.AddMember("applyInEditMode", Json::Value(true));
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
			// Loaded data replaces whatever was playing.
			a.Runtime.Reset();
		};
		registry.Register<AnimatorComponent>(info);
	}
}
