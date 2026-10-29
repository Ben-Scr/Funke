#include <pch.hpp>
#include "Undo/UndoStack.hpp"

#include <algorithm>

namespace Funke {

	void UndoStack::Push(std::unique_ptr<EditCommand> command) {
		if (!command) return;

		// A fresh edit invalidates the redo branch.
		m_Redo.clear();

		if (!m_Undo.empty() && m_Undo.back()->TryMergeWith(*command)) {
			return;
		}

		m_Undo.push_back(std::move(command));

		if (m_Undo.size() > k_MaxDepth) {
			m_Undo.erase(m_Undo.begin(), m_Undo.begin() + (m_Undo.size() - k_MaxDepth));
		}
	}

	std::string UndoStack::Undo(Scene& scene) {
		if (m_Undo.empty()) return {};
		std::unique_ptr<EditCommand> command = std::move(m_Undo.back());
		m_Undo.pop_back();
		std::string label = command->Label();
		command->Undo(scene);
		m_Redo.push_back(std::move(command));
		return label;
	}

	std::string UndoStack::Redo(Scene& scene) {
		if (m_Redo.empty()) return {};
		std::unique_ptr<EditCommand> command = std::move(m_Redo.back());
		m_Redo.pop_back();
		std::string label = command->Label();
		command->Redo(scene);
		m_Undo.push_back(std::move(command));
		return label;
	}

	void UndoStack::Clear() {
		m_Undo.clear();
		m_Redo.clear();
	}

	UndoStack& SceneUndoHistory::ForScene(const std::shared_ptr<Scene>& scene) {
		return m_Stacks[std::weak_ptr<Scene>(scene)];
	}

	UndoStack SceneUndoHistory::Take(const std::shared_ptr<Scene>& scene) {
		const auto it = m_Stacks.find(std::weak_ptr<Scene>(scene));
		if (it == m_Stacks.end()) return {};
		UndoStack result = std::move(it->second);
		m_Stacks.erase(it);
		return result;
	}

	void SceneUndoHistory::Restore(const std::shared_ptr<Scene>& scene, UndoStack stack) {
		if (scene) m_Stacks.insert_or_assign(std::weak_ptr<Scene>(scene), std::move(stack));
	}

	void SceneUndoHistory::Retain(std::span<const std::weak_ptr<Scene>> scenes) {
		std::erase_if(m_Stacks, [scenes](const auto& entry) {
			return entry.first.expired() || std::none_of(scenes.begin(), scenes.end(), [&](const auto& scene) {
				return !scene.owner_before(entry.first) && !entry.first.owner_before(scene);
			});
		});
	}

	void SceneUndoHistory::Forget(const Scene* scene) {
		std::erase_if(m_Stacks, [scene](const auto& entry) {
			const auto owner = entry.first.lock();
			return owner && owner.get() == scene;
		});
	}

	void SceneUndoHistory::Clear() {
		m_Stacks.clear();
	}
}
