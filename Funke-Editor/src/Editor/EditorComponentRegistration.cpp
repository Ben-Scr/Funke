#include <pch.hpp>
#include "Editor/EditorComponentRegistration.hpp"

#include "Gui/ComponentInspectors.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Core/EngineModules.hpp"
#include <imgui.h>
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptComponentInspector.hpp"

#include "Components/Forward.hpp"
#include "Components/General/General.hpp"
#include "Components/UI/UI.hpp"
#include "Components/Tags.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"

#include <span>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace Funke {
	namespace {
		using InspectorFn = void (*)(std::span<const Entity>);

		struct InspectorBinding {
			std::type_index type;
			InspectorFn inspector;
		};

		template<typename T>
		InspectorBinding Bind(InspectorFn inspector) {
			return InspectorBinding{ std::type_index(typeid(T)), inspector };
		}

		void AttachInspector(ComponentRegistry& registry, std::type_index type, InspectorFn inspector) {
			bool attached = false;
			registry.ForEachComponentInfo([&](const std::type_index& id, ComponentInfo& info) {
				if (id == type) {
					info.drawInspector = inspector;
					attached = true;
				}
			});
			FNK_CORE_ASSERT(attached, FunkeErrorCode::InvalidArgument,
				"AttachInspector: component type not registered. Register it in BuiltInComponentRegistration.cpp before attaching an inspector.");
		}
	}

	void DispatchComponentInspector(const ComponentInfo& info, std::span<const Entity> entities) {
		const bool inactive = !info.IsModuleActive();
		if (inactive && info.engineModule) {
			const std::string module(EngineModules::GetId(*info.engineModule));
			ImGui::TextWrapped("%s is inactive. These settings are preserved. Enable the module in Package Manager to use this component.", module.c_str());
		}
		ImGui::BeginDisabled(inactive);
		if (info.drawInspector) info.drawInspector(entities);
		else if (!info.properties.empty()) PropertyDrawer::DrawAll(entities, info.properties, info.displayName);
		ImGui::EndDisabled();
	}

	void RegisterEditorComponentInspectors(ComponentRegistry& registry) {
		const InspectorBinding bindings[] = {
			// Hybrid: properties + extras (texture preview, runtime read-outs).
			Bind<SpriteRendererComponent>(DrawSpriteRendererInspector),
			Bind<Transform2DComponent>(DrawTransform2DInspector),
			Bind<Camera2DComponent>(DrawCamera2DInspector),

			Bind<ButtonComponent>(DrawButtonInspector),
			Bind<SliderComponent>(DrawSliderInspector),
			Bind<ToggleComponent>(DrawToggleInspector),
			Bind<InputFieldComponent>(DrawInputFieldInspector),
			Bind<DropdownComponent>(DrawDropdownInspector),

			// Custom-only: variant types + per-shape branches don't map cleanly.
			Bind<ParticleSystem2DComponent>(DrawParticleSystem2DInspector),
			Bind<ParticleSystem3DComponent>(DrawParticleSystem3DInspector),
			Bind<SkinnedMeshRendererComponent>(DrawSkinnedMeshRendererInspector),

			Bind<RectTransform2DComponent>(DrawRectTransform2DInspector),

			// Custom-only: per-script field rendering goes through its own
			// PropertyDrawer-driven path (see ScriptComponentInspector.cpp).
			Bind<ScriptComponent>(DrawScriptComponentInspector),
		};

		for (const auto& b : bindings)
			AttachInspector(registry, b.type, b.inspector);
	}
}
