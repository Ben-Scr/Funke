#pragma once
#include "Undo/EditCommand.hpp"
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace Funke {
	class Scene;

	// Commands are recorded after their changes have been applied.
	class UndoStack {
	public:
		void Push(std::unique_ptr<EditCommand> command);
		// Undo/Redo return the affected command's Label() (empty if nothing to do),
		// so the caller can surface it in the toast.
		std::string Undo(Scene& scene);
		std::string Redo(Scene& scene);
		void Clear();

		bool CanUndo() const { return !m_Undo.empty(); }
		bool CanRedo() const { return !m_Redo.empty(); }

	private:
		// Oldest entries drop past this depth; per-drag/per-edit churn would
		// otherwise make the history an unbounded memory sink.
		static constexpr std::size_t k_MaxDepth = 256;

		std::vector<std::unique_ptr<EditCommand>> m_Undo;
		std::vector<std::unique_ptr<EditCommand>> m_Redo;
	};

	class SceneUndoHistory {
	public:
		UndoStack& ForScene(const std::shared_ptr<Scene>& scene);
		void Retain(std::span<const std::weak_ptr<Scene>> scenes);
		void Forget(const Scene* scene);
		void Clear();

	private:
		// Ownership distinguishes replacement scenes even when names, IDs or addresses are reused.
		std::map<std::weak_ptr<Scene>, UndoStack, std::owner_less<std::weak_ptr<Scene>>> m_Stacks;
	};
}
