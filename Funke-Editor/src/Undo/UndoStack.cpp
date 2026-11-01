#include <pch.hpp>
#include "Undo/UndoStack.hpp"

#include <algorithm>

namespace Funke {

	void UndoStack::Push(std::unique_ptr<EditCommand> command) {
		if (!command) return;
		m_State->Redo.clear();
		auto& undo = m_State->Undo;
		if (!undo.empty() && !undo.back().Linked && undo.back().Command->TryMergeWith(*command)) return;
		undo.push_back({ std::move(command), {}, false });
		if (undo.size() > k_MaxDepth) undo.erase(undo.begin());
	}

	void UndoStack::PushLinked(UndoStack& other, std::unique_ptr<EditCommand> command) {
		if (!command) return;
		if (this == &other) { Push(std::move(command)); return; }
		m_State->Undo.reserve(m_State->Undo.size() + 1);
		other.m_State->Undo.reserve(other.m_State->Undo.size() + 1);
		std::shared_ptr<EditCommand> shared = std::move(command);
		m_State->Redo.clear();
		other.m_State->Redo.clear();
		m_State->Undo.push_back({ shared, other.m_State, true });
		other.m_State->Undo.push_back({ shared, m_State, true });
		for (const auto& state : { m_State, other.m_State }) {
			if (state->Undo.size() > k_MaxDepth) state->Undo.erase(state->Undo.begin());
		}
	}

	bool UndoStack::CanApply(const std::shared_ptr<State>& state, bool redo) {
		if (!state) return false;
		const auto& entries = redo ? state->Redo : state->Undo;
		if (entries.empty() || !entries.back().Command->IsAvailable()) return false;
		const auto& entry = entries.back();
		if (!entry.Linked) return true;
		const auto partner = entry.Partner.lock();
		if (!partner) return false;
		const auto& peers = redo ? partner->Redo : partner->Undo;
		return !peers.empty() && peers.back().Command == entry.Command;
	}

	bool UndoStack::CanUndo() const { return CanApply(m_State, false); }
	bool UndoStack::CanRedo() const { return CanApply(m_State, true); }

	std::string UndoStack::Apply(Scene& scene, bool redo) {
		if (!CanApply(m_State, redo)) return {};
		auto& from = redo ? m_State->Redo : m_State->Undo;
		auto& to = redo ? m_State->Undo : m_State->Redo;
		const Entry entry = from.back();
		const auto partner = entry.Linked ? entry.Partner.lock() : nullptr;
		to.reserve(to.size() + 1);
		if (partner) {
			auto& peerTo = redo ? partner->Undo : partner->Redo;
			peerTo.reserve(peerTo.size() + 1);
		}
		const std::string label = entry.Command->Label();
		try {
			if (redo) entry.Command->Redo(scene);
			else entry.Command->Undo(scene);
		}
		catch (const std::exception& error) {
			FNK_CORE_ERROR_TAG("Undo", "{} failed: {}", label, error.what());
			return {};
		}
		to.push_back(entry);
		from.pop_back();
		if (partner) {
			auto& peerFrom = redo ? partner->Redo : partner->Undo;
			auto& peerTo = redo ? partner->Undo : partner->Redo;
			peerTo.push_back(peerFrom.back());
			peerFrom.pop_back();
		}
		return label;
	}

	std::string UndoStack::Undo(Scene& scene) { return Apply(scene, false); }
	std::string UndoStack::Redo(Scene& scene) { return Apply(scene, true); }

	void UndoStack::Clear() {
		if (!m_State) m_State = std::make_shared<State>();
		m_State->Undo.clear();
		m_State->Redo.clear();
	}

	void UndoStack::BindScene(const std::shared_ptr<Scene>& scene) {
		const std::weak_ptr<Scene> replacement(scene);
		if (!m_State->Owner.owner_before(replacement) && !replacement.owner_before(m_State->Owner)) return;
		for (const auto* entries : { &m_State->Undo, &m_State->Redo }) {
			for (const auto& entry : *entries) entry.Command->RebindScene(m_State->Owner, scene);
		}
		m_State->Owner = scene;
	}

	UndoStack& SceneUndoHistory::ForScene(const std::shared_ptr<Scene>& scene) {
		auto& stack = m_Stacks[std::weak_ptr<Scene>(scene)];
		stack.BindScene(scene);
		return stack;
	}

	UndoStack SceneUndoHistory::Take(const std::shared_ptr<Scene>& scene) {
		const auto it = m_Stacks.find(std::weak_ptr<Scene>(scene));
		if (it == m_Stacks.end()) return {};
		UndoStack result = std::move(it->second);
		m_Stacks.erase(it);
		return result;
	}

	void SceneUndoHistory::Restore(const std::shared_ptr<Scene>& scene, UndoStack stack) {
		if (scene) {
			stack.BindScene(scene);
			m_Stacks.insert_or_assign(std::weak_ptr<Scene>(scene), std::move(stack));
		}
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
