#pragma once
#include "Undo/EditCommand.hpp"
#include "Scene/EntityHandle.hpp"
#include "Serialization/Json.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Funke {
	class Scene;

	// A whole entity subtree, keyed by persistent ids so it can be rebuilt with the
	// same identity (entity refs, other undo entries and selection-by-id keep working).
	struct EntitySubtreeSnapshot {
		uint64_t RootId = 0;
		uint64_t ParentId = 0;   // 0 = scene root
		int SiblingIndex = -1;   // position in the parent's child list; -1 = append
		std::vector<Json::Value> Entities; // root first; the root carries no parent link

		static EntitySubtreeSnapshot Capture(Scene& scene, EntityHandle root);
	};

	// Undo step for edits that change which entities/components exist or where they
	// sit in the hierarchy: create, delete, duplicate, paste, reparent, add/remove
	// component, prefab unpack. Applying a state destroys every affected subtree
	// and rebuilds the snapshots of that state.
	class StructureEditCommand : public EditCommand {
	public:
		StructureEditCommand(std::string label,
			std::vector<EntitySubtreeSnapshot> before,
			std::vector<EntitySubtreeSnapshot> after);

		void Undo(Scene& scene) override { Apply(scene, m_Before); }
		void Redo(Scene& scene) override { Apply(scene, m_After); }
		const char* Name() const override { return "Structure"; }
		std::string Label() const override { return m_Label; }

	private:
		void Apply(Scene& scene, const std::vector<EntitySubtreeSnapshot>& state) const;

		std::string m_Label;
		std::vector<EntitySubtreeSnapshot> m_Before;
		std::vector<EntitySubtreeSnapshot> m_After;
	};

	// Snapshot the affected roots, run the edit, then Finish() with the roots as
	// they exist afterwards (newly created entities, the same entity after a
	// component change, ...). Descendants of another listed root are ignored.
	class StructureEditRecorder {
	public:
		StructureEditRecorder(Scene& scene, std::span<const EntityHandle> rootsBefore, std::string label);

		// Null when nothing was captured on either side.
		std::unique_ptr<EditCommand> Finish(Scene& scene, std::span<const EntityHandle> rootsAfter);

	private:
		std::string m_Label;
		std::vector<EntitySubtreeSnapshot> m_Before;
	};
}
