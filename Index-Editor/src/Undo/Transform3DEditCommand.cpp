#include <pch.hpp>
#include "Undo/Transform3DEditCommand.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Scene/Scene.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

namespace Index {

	void Transform3DEditCommand::Apply(Scene& scene, const State& state) const {
		EntityHandle handle = entt::null;
		if (!scene.TryResolveEntityRef(m_PersistentId, handle) || !scene.IsValid(handle)) return;
		Transform3DComponent* transform = nullptr;
		if (!scene.TryGetComponent<Transform3DComponent>(handle, transform) || !transform) return;

		transform->Position = state.Position;
		transform->Rotation = state.Rotation;
		transform->Scale = state.Scale;
		Transform3DHierarchySystem::Propagate(scene);
		scene.MarkDirty();
	}

}
