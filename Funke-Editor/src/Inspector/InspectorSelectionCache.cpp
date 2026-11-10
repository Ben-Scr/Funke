#include <pch.hpp>
#include "Inspector/InspectorSelectionCache.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Tags.hpp"
#include "Scene/Scene.hpp"
#include "Scene/ComponentRegistry.hpp"

namespace Funke {
	void InspectorSelectionCache::Recompute(Scene& scene, const ComponentRegistry& registry, std::vector<EntityHandle> handles) {
		// Runs once per selection version bump; avoids O(N×M) sweep over (selection × components) every frame.
		Handles = std::move(handles);
		CommonComponentTypes.clear();
		PartialComponents.clear();

		if (Handles.empty()) {
			NameUniform = true;
			FirstName.clear();
			EnabledUniform = true;
			EnabledFirst = true;
			StaticUniform = true;
			StaticFirst = false;
			return;
		}

		std::vector<Entity> entities;
		entities.reserve(Handles.size());
		for (EntityHandle h : Handles) {
			entities.push_back(scene.GetEntity(h));
		}

		{
			// Empty FirstName means no NameComponent; InputText renders a hint instead of pre-filling to avoid accidental creation.
			Entity& first = entities[0];
			FirstName = first.HasComponent<NameComponent>()
				? first.GetComponent<NameComponent>().Name
				: std::string{};
			NameUniform = true;
			const std::string_view firstView = FirstName;
			for (std::size_t i = 1; i < entities.size(); ++i) {
				Entity& e = entities[i];
				const std::string_view name = e.HasComponent<NameComponent>()
					? std::string_view(e.GetComponent<NameComponent>().Name)
					: std::string_view{};
				if (name != firstView) { NameUniform = false; break; }
			}
		}

		{
			auto enabledOf = [](Entity& e) {
				return !e.HasComponent<DisabledTag>() || e.HasComponent<InheritedDisabledTag>();
			};
			auto staticOf = [](Entity& e) { return e.HasComponent<StaticTag>(); };

			EnabledFirst = enabledOf(entities[0]);
			StaticFirst = staticOf(entities[0]);
			EnabledUniform = true;
			StaticUniform = true;
			for (std::size_t i = 1; i < entities.size(); ++i) {
				if (EnabledUniform && enabledOf(entities[i]) != EnabledFirst) {
					EnabledUniform = false;
				}
				if (StaticUniform && staticOf(entities[i]) != StaticFirst) {
					StaticUniform = false;
				}
				if (!EnabledUniform && !StaticUniform) break;
			}
		}

		registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component) return;
			if (info.displayName == "Name") return;

			std::size_t haveCount = 0;
			for (const Entity& e : entities) {
				if (info.has(e)) ++haveCount;
			}
			if (haveCount == 0) return;
			if (haveCount < entities.size()) {
				PartialComponents.push_back(info.displayName);
			}
			else {
				CommonComponentTypes.insert(typeId);
			}
		});
	}
}
