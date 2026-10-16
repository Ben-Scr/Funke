#pragma once

#include "Core/Export.hpp"
#include "Components/Tags.hpp"

namespace Funke {

	// General
	class FUNKE_API Transform2DComponent;
	struct RectTransform2DComponent;
	struct NameComponent;
	struct UUIDComponent;
	struct EntityMetaDataComponent;
	struct PrefabInstanceComponent;
	struct HierarchyComponent;
	struct FUNKE_API AnimatorComponent;

	// Graphics
	struct FUNKE_API SpriteRendererComponent;
	struct ImageComponent;
	class FUNKE_API Camera2DComponent;
	class FUNKE_API ParticleSystem2DComponent;
	class FUNKE_API ParticleSystem3DComponent;
	struct FUNKE_API PostProcessing2DComponent;
	struct FUNKE_API TextRendererComponent;

	// Physics
	class FUNKE_API BoxCollider2DComponent;
	class FUNKE_API CircleCollider2DComponent;
	class FUNKE_API PolygonCollider2DComponent;
	class FUNKE_API Rigidbody2DComponent;
	struct FUNKE_API FastBody2DComponent;
	struct FUNKE_API FastBoxCollider2DComponent;
	struct FUNKE_API FastCircleCollider2DComponent;

	// Audio
	class FUNKE_API AudioSourceComponent;

	// Scripting
	struct ScriptComponent;

	// UI
	struct InteractableComponent;
	struct ButtonComponent;
	struct SliderComponent;
	struct InputFieldComponent;
	struct DropdownComponent;
	struct ToggleComponent;
	struct ScrollbarComponent;
	struct ScrollRectComponent;
	struct MaskComponent;
	struct ContentSizeFitterComponent;
	struct WidthConstraintComponent;
	struct CircularSliderComponent;
	struct HorizontalLayoutGroupComponent;
	struct VerticalLayoutGroupComponent;
	struct GridLayoutGroupComponent;
	struct FUNKE_API LocalizedTextComponent;

}
