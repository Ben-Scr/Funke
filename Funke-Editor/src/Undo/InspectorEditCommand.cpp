#include <pch.hpp>
#include "Undo/InspectorEditCommand.hpp"

#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"

namespace Funke {

	bool InspectorEditCommand::HasChange() const {
		for (const Entry& e : m_Entries) {
			if (e.Before != e.After) return true;
		}
		return false;
	}

	void InspectorEditCommand::Undo(Scene& scene) { Restore(scene, false); }
	void InspectorEditCommand::Redo(Scene& scene) { Restore(scene, true); }

	void InspectorEditCommand::Restore(Scene& scene, bool redo) {
		if (!m_Apply) return;
		for (const Entry& e : m_Entries) {
			if (e.SceneProperty) {
				Entity placeholder = Entity::MakeScenePlaceholder(scene);
				m_Apply(placeholder, redo ? e.After : e.Before);
				continue;
			}
			EntityHandle handle = entt::null;
			if (!scene.TryResolveEntityRef(e.PersistentId, handle)) continue;
			Entity entity = scene.GetEntity(handle);
			m_Apply(entity, redo ? e.After : e.Before);
		}
		scene.MarkDirty();
	}
}
