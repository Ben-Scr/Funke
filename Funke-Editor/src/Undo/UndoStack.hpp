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

		bool CanUndo(std::string* reason = nullptr) const;
		bool CanRedo(std::string* reason = nullptr) const;
		bool HasHistory() const { return m_State && (!m_State->Undo.empty() || !m_State->Redo.empty()); }
		const std::string& GetLastError() const { return m_LastError; }

	private:
		// Oldest entries drop past this depth; per-drag/per-edit churn would
		// otherwise make the history an unbounded memory sink.
		static constexpr std::size_t k_MaxDepth = 256;

		struct State;
		struct Entry {
			std::shared_ptr<EditCommand> Command;
			std::weak_ptr<State> Partner;
			bool Linked = false;
			std::string PartnerName;
		};
		struct State {
			std::weak_ptr<Scene> Owner;
			std::string OwnerName;
			std::vector<Entry> Undo;
			std::vector<Entry> Redo;
		};
		friend class SceneUndoHistory;
		void BindScene(const std::shared_ptr<Scene>& scene);
		static bool CanApply(const std::shared_ptr<State>& state, bool redo, std::string* reason);
		std::string Apply(Scene& scene, bool redo);
		std::shared_ptr<State> m_State = std::make_shared<State>();
		std::string m_LastError;
	};

	class SceneUndoHistory {
	public:
		UndoStack& ForScene(const std::shared_ptr<Scene>& scene);
		UndoStack Take(const std::shared_ptr<Scene>& scene);
		void Restore(const std::shared_ptr<Scene>& scene, UndoStack stack);
		std::vector<std::string> Retain(std::span<const std::weak_ptr<Scene>> scenes);
		bool HasHistory(const Scene* scene) const;
		void Forget(const Scene* scene);
		void Clear();

	private:
		// Ownership distinguishes replacement scenes even when names, IDs or addresses are reused.
		std::map<std::weak_ptr<Scene>, UndoStack, std::owner_less<std::weak_ptr<Scene>>> m_Stacks;
	};
}
