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
		UndoStack() = default;
		UndoStack(UndoStack&&) noexcept = default;
		UndoStack& operator=(UndoStack&&) noexcept = default;
		UndoStack(const UndoStack&) = delete;
		UndoStack& operator=(const UndoStack&) = delete;
		void Push(std::unique_ptr<EditCommand> command);
		void PushLinked(UndoStack& other, std::unique_ptr<EditCommand> command);
		// Undo/Redo return the affected command's Label() (empty if nothing to do),
		// so the caller can surface it in the toast.
		std::string Undo(Scene& scene);
		std::string Redo(Scene& scene);
		void Clear();

		bool CanUndo() const;
		bool CanRedo() const;

	private:
		// Oldest entries drop past this depth; per-drag/per-edit churn would
		// otherwise make the history an unbounded memory sink.
		static constexpr std::size_t k_MaxDepth = 256;

		struct State;
		struct Entry {
			std::shared_ptr<EditCommand> Command;
			std::weak_ptr<State> Partner;
			bool Linked = false;
		};
		struct State {
			std::weak_ptr<Scene> Owner;
			std::vector<Entry> Undo;
			std::vector<Entry> Redo;
		};
		friend class SceneUndoHistory;
		void BindScene(const std::shared_ptr<Scene>& scene);
		static bool CanApply(const std::shared_ptr<State>& state, bool redo);
		std::string Apply(Scene& scene, bool redo);
		std::shared_ptr<State> m_State = std::make_shared<State>();
	};

	class SceneUndoHistory {
	public:
		UndoStack& ForScene(const std::shared_ptr<Scene>& scene);
		UndoStack Take(const std::shared_ptr<Scene>& scene);
		void Restore(const std::shared_ptr<Scene>& scene, UndoStack stack);
		void Retain(std::span<const std::weak_ptr<Scene>> scenes);
		void Forget(const Scene* scene);
		void Clear();

	private:
		// Ownership distinguishes replacement scenes even when names, IDs or addresses are reused.
		std::map<std::weak_ptr<Scene>, UndoStack, std::owner_less<std::weak_ptr<Scene>>> m_Stacks;
	};
}
