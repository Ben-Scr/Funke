#pragma once

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Scene/Scene.hpp"

namespace Funke::ScriptTransform2D {
	inline const Transform2DComponent* GetParentTransform(Scene& scene, EntityHandle handle)
	{
		if (!scene.HasComponent<HierarchyComponent>(handle)) return nullptr;
		const EntityHandle parent = scene.GetComponent<HierarchyComponent>(handle).Parent;
		if (parent == entt::null || !scene.IsValid(parent) || !scene.HasComponent<Transform2DComponent>(parent)) {
			return nullptr;
		}
		return &scene.GetComponent<Transform2DComponent>(parent);
	}

	inline Vec2 WorldPositionToLocal(const Transform2DComponent* parent, const Vec2& worldPosition)
	{
		if (!parent) return worldPosition;
		Vec2 local = Rotate(worldPosition - parent->Position, -parent->Rotation);
		if (std::abs(parent->Scale.x) > 0.00001f) local.x /= parent->Scale.x;
		if (std::abs(parent->Scale.y) > 0.00001f) local.y /= parent->Scale.y;
		return local;
	}

	inline void SetWorldPosition(Scene& scene, EntityHandle handle, Transform2DComponent& transform, const Vec2& position)
	{
		transform.LocalPosition = WorldPositionToLocal(GetParentTransform(scene, handle), position);
		transform.Position = position;
		transform.MarkDirty();
	}
}
