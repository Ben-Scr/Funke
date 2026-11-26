#include "pch.hpp"
#if FUNKE_WITH_3D
#include "Scene/BuiltInComponentRegistrationShared.hpp"
#include "Components/Graphics/CameraFollowRigComponent.hpp"

namespace Funke::BuiltInComponentRegistrationInternal {
	void RegisterCameraRig(ComponentRegistry& registry) {
		ComponentInfo info{"Camera Follow Rig", "3D", ComponentCategory::Component};
		info.serializedName = "CameraFollowRig";
		info.properties = {
			MakeUiEntityRef<CameraFollowRigComponent>("Target", "Target", &CameraFollowRigComponent::Target),
			Properties::Make("Offset", "Offset", &CameraFollowRigComponent::Offset),
			Properties::Make("Damping", "Damping", &CameraFollowRigComponent::Damping, Properties::Meta::Clamp(0.0, 100.0)),
			Properties::Make("Orbit", "Orbit", &CameraFollowRigComponent::Orbit),
			Properties::Make("Yaw", "Yaw", &CameraFollowRigComponent::Yaw),
			Properties::Make("Pitch", "Pitch", &CameraFollowRigComponent::Pitch, Properties::Meta::Clamp(-89.0, 89.0)),
			Properties::Make("Distance", "Distance", &CameraFollowRigComponent::Distance, Properties::Meta::Clamp(0.01, 100000.0)),
			Properties::Make("LookAtTarget", "Look At Target", &CameraFollowRigComponent::LookAtTarget)
		};
		info.serialize = [](Entity entity) {
			const auto& rig = entity.GetComponent<CameraFollowRigComponent>();
			Json::Value value = Json::Value::MakeObject();
			value.AddMember("target", UIEntityHandleToJson(entity, rig.Target));
			value.AddMember("offsetX", rig.Offset.x); value.AddMember("offsetY", rig.Offset.y); value.AddMember("offsetZ", rig.Offset.z);
			value.AddMember("damping", rig.Damping); value.AddMember("orbit", rig.Orbit);
			value.AddMember("yaw", rig.Yaw); value.AddMember("pitch", rig.Pitch); value.AddMember("distance", rig.Distance);
			value.AddMember("lookAtTarget", rig.LookAtTarget);
			return value;
		};
		info.deserialize = [](Entity entity, const Json::Value& value) {
			auto& rig = entity.GetComponent<CameraFollowRigComponent>();
			if (const auto* v = value.FindMember("target")) UIEntityHandleFromJson(entity, *v, rig.Target);
			auto number = [&](const char* key, float fallback) { const auto* v = value.FindMember(key); return v ? static_cast<float>(v->AsDoubleOr(fallback)) : fallback; };
			rig.Offset = {number("offsetX", 0), number("offsetY", 2), number("offsetZ", 5)};
			rig.Damping = number("damping", 8); rig.Yaw = number("yaw", 0); rig.Pitch = number("pitch", 15); rig.Distance = number("distance", 5);
			if (const auto* v = value.FindMember("orbit")) rig.Orbit = v->AsBoolOr(false);
			if (const auto* v = value.FindMember("lookAtTarget")) rig.LookAtTarget = v->AsBoolOr(true);
			rig.Initialized = false; rig.LastTarget = entt::null;
		};
		registry.Register<CameraFollowRigComponent>(info);
	}
}

#endif
