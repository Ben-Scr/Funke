#include <pch.hpp>
#include "Undo/UndoStack.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>

namespace Funke {

	void UndoStack::Push(std::unique_ptr<EditCommand> command) {
		if (!command) return;
		m_LastError.clear();
		m_State->Redo.clear();
		auto& undo = m_State->Undo;
		if (!undo.empty() && !undo.back().Linked && undo.back().Command->TryMergeWith(*command)) return;
		undo.push_back({ std::move(command), {}, false, {} });
		if (undo.size() > k_MaxDepth) undo.erase(undo.begin());
	}

	void UndoStack::PushLinked(UndoStack& other, std::unique_ptr<EditCommand> command) {
		if (!command) return;
		if (this == &other) { Push(std::move(command)); return; }
		m_State->Undo.reserve(m_State->Undo.size() + 1);
		other.m_State->Undo.reserve(other.m_State->Undo.size() + 1);
		std::shared_ptr<EditCommand> shared = std::move(command);
		Entry ownEntry{ shared, other.m_State, true, other.m_State->OwnerName };
		Entry peerEntry{ shared, m_State, true, m_State->OwnerName };
		m_LastError.clear();
		other.m_LastError.clear();
		m_State->Redo.clear();
		other.m_State->Redo.clear();
		m_State->Undo.push_back(std::move(ownEntry));
		other.m_State->Undo.push_back(std::move(peerEntry));
		for (const auto& state : { m_State, other.m_State }) {
			if (state->Undo.size() > k_MaxDepth) state->Undo.erase(state->Undo.begin());
		}
	}

	bool UndoStack::CanApply(const std::shared_ptr<State>& state, bool redo, std::string* reason) {
		if (reason) reason->clear();
		if (!state) return false;
		const auto& entries = redo ? state->Redo : state->Undo;
		if (entries.empty()) return false;
		const auto& entry = entries.back();
		auto blocked = [&](const std::string& detail) {
			if (reason) *reason = entry.Command->Label() + ": " + detail;
			return false;
		};
		if (!entry.Command->IsAvailable()) return reason ? blocked(entry.Command->GetUnavailableReason()) : false;
		if (!entry.Linked) return true;
		const auto partner = entry.Partner.lock();
		if (partner) {
			const auto& peers = redo ? partner->Redo : partner->Undo;
			if (!peers.empty() && peers.back().Command == entry.Command) return true;
		}
		if (!reason) return false;
		const std::string name = partner ? partner->OwnerName : entry.PartnerName;
		const std::string context = name.empty() ? "the partner scene" : "scene '" + name + "'";
		if (!partner) return blocked("The history for " + context + " was discarded. Reopening a scene does not restore its history.");
		const auto& peers = redo ? partner->Redo : partner->Undo;
		const auto match = std::find_if(peers.rbegin(), peers.rend(), [&](const Entry& peer) { return peer.Command == entry.Command; });
		if (match == peers.rend()) return blocked("The linked step is no longer in the " + std::string(redo ? "redo" : "undo")
			+ " history for " + context + ". It cannot be applied from this history alone.");
		return blocked(std::string(redo ? "Redo " : "Undo ") + std::to_string(std::distance(peers.rbegin(), match))
			+ " preceding step(s) in " + context + " first, starting with '" + peers.back().Command->Label() + "'.");
	}

	bool UndoStack::CanUndo(std::string* reason) const { return CanApply(m_State, false, reason); }
	bool UndoStack::CanRedo(std::string* reason) const { return CanApply(m_State, true, reason); }

	std::string UndoStack::Apply(Scene& scene, bool redo) {
		if (!CanApply(m_State, redo, &m_LastError)) return {};
		auto& from = redo ? m_State->Redo : m_State->Undo;
		auto& to = redo ? m_State->Undo : m_State->Redo;
		// Diagnostic strings must be copied before the command changes either scene.
		Entry entry = from.back();
		const auto partner = entry.Linked ? entry.Partner.lock() : nullptr;
		Entry peerEntry;
		to.reserve(to.size() + 1);
		if (partner) {
			peerEntry = (redo ? partner->Redo : partner->Undo).back();
			auto& peerTo = redo ? partner->Undo : partner->Redo;
			peerTo.reserve(peerTo.size() + 1);
		}
		const std::string label = entry.Command->Label();
		try {
			if (redo) entry.Command->Redo(scene);
			else entry.Command->Undo(scene);
		}
		catch (const std::exception& error) {
			m_LastError = label + ": " + error.what();
			FNK_CORE_ERROR_TAG("Undo", "{} failed: {}", label, error.what());
			return {};
		}
		to.push_back(std::move(entry));
		from.pop_back();
		if (partner) {
			auto& peerFrom = redo ? partner->Redo : partner->Undo;
			auto& peerTo = redo ? partner->Undo : partner->Redo;
			peerTo.push_back(std::move(peerEntry));
			peerFrom.pop_back();
		}
		return label;
	}

	std::string UndoStack::Undo(Scene& scene) { return Apply(scene, false); }
	std::string UndoStack::Redo(Scene& scene) { return Apply(scene, true); }

	void UndoStack::Clear() {
		m_LastError.clear();
		if (!m_State) m_State = std::make_shared<State>();
		m_State->Undo.clear();
		m_State->Redo.clear();
	}

	void UndoStack::BindScene(const std::shared_ptr<Scene>& scene) {
		m_State->OwnerName = scene ? scene->GetName() : std::string{};
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

	std::vector<std::string> SceneUndoHistory::Retain(std::span<const std::weak_ptr<Scene>> scenes) {
		std::vector<std::string> discarded;
		std::erase_if(m_Stacks, [&](const auto& entry) {
			const bool remove = entry.first.expired() || std::none_of(scenes.begin(), scenes.end(), [&](const auto& scene) {
				return !scene.owner_before(entry.first) && !entry.first.owner_before(scene);
			});
			if (remove && entry.second.HasHistory()) discarded.push_back(entry.second.m_State->OwnerName);
			return remove;
		});
		return discarded;
	}

	bool SceneUndoHistory::HasHistory(const Scene* scene) const {
		return std::any_of(m_Stacks.begin(), m_Stacks.end(), [scene](const auto& entry) {
			const auto owner = entry.first.lock();
			return owner && owner.get() == scene && entry.second.HasHistory();
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
