#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/General/General.hpp"
#include "Math/Trigonometry.hpp"

#include <cmath>

// Transform, naming and identity components.
// Split out of BuiltInComponentRegistration.cpp, which owns the call order.
namespace Index::BuiltInComponentRegistrationInternal {
	void RegisterGeneral(ComponentRegistry& registry) {
		using Index::Codegen::RegisterComponent;

		// ── General ─────────────────────────────────────────────────

		RegisterComponent<Transform2DComponent>(registry, "Transform 2D",
			ComponentCategory::Component, "General", "Transform2D",
			{
				Properties::MakeWith<Vec2>("Position", "Position",
					[](const Entity& e) { return e.GetComponent<Transform2DComponent>().LocalPosition; },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<Transform2DComponent>().SetPosition(v);
					}),
				Properties::MakeWith<float>("Rotation", "Rotation",
					[](const Entity& e) {
						return Degrees(e.GetComponent<Transform2DComponent>().LocalRotation);
					},
					[](Entity& e, float v) {
						e.GetComponent<Transform2DComponent>().SetRotation(Radians(v));
					},
					Properties::Meta::DragSpeed(1.0f)),
				Properties::MakeWith<Vec2>("Scale", "Scale",
					[](const Entity& e) { return e.GetComponent<Transform2DComponent>().LocalScale; },
					[](Entity& e, const Vec2& v) {
						e.GetComponent<Transform2DComponent>().SetScale(v);
					}),
			});

		// Inspector is fully custom (DrawRectTransform2DInspector) so this
		// component declares no PropertyDescriptors — they would be ignored
		// anyway since custom drawInspector wins over properties.
		RegisterComponent<RectTransform2DComponent>(registry, "Rect Transform 2D",
			ComponentCategory::Component, "UI", "RectTransform2D");

		RegisterComponent<NameComponent>(registry, "Name",
			ComponentCategory::Component, "General", "name",
			{
				Properties::Make("Name", "Name", &NameComponent::Name),
			});

		RegisterComponent<UUIDComponent>(registry, "UUID", ComponentCategory::Tag);
		RegisterComponent<EntityMetaDataComponent>(registry, "Entity Metadata", ComponentCategory::Tag);
		RegisterComponent<PrefabInstanceComponent>(registry, "Prefab Instance", ComponentCategory::Tag);
	}
}
