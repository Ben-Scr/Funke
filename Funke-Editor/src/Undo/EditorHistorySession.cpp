#include <pch.hpp>
#include "Undo/EditorHistorySession.hpp"
#include "Scene/Scene.hpp"
#include <algorithm>

namespace Funke {
	void EditorHistorySession::SetBoundaryCallbacks(std::function<void(UndoStack&)> flush, std::function<void()> reset) {
		m_Flush = std::move(flush); m_Reset = std::move(reset);
	}
	UndoStack& EditorHistorySession::ForScene(Scene& scene, Scenes loaded) {
		for (const auto& weak : loaded)
			if (const auto owner = weak.lock(); owner && owner.get() == &scene) return m_Scenes.ForScene(owner);
		return m_Detached;
	}
	UndoStack& EditorHistorySession::Activate(Scene& scene, Scenes loaded) {
		auto& next = ForScene(scene, loaded);
		if (m_Context != &scene || m_Active != &next) {
			Deactivate(true);
			m_Context = &scene; m_Active = &next;
		}
		return next;
	}
	void EditorHistorySession::Deactivate(bool flush) {
		if (flush && m_Active && m_Flush) m_Flush(*m_Active);
		if (m_Reset) m_Reset();
		m_Active = nullptr; m_Context = nullptr;
	}
	void EditorHistorySession::Clear() {
		Deactivate(false); m_Scenes.Clear(); m_Detached.Clear();
	}
	bool EditorHistorySession::Forget(const Scene* scene, bool detached) {
		const bool hadHistory = m_Scenes.HasHistory(scene) || (detached && m_Detached.HasHistory());
		if (m_Context == scene) Deactivate(false);
		m_Scenes.Forget(scene);
		if (detached) m_Detached.Clear();
		return hadHistory;
	}
	std::vector<std::string> EditorHistorySession::Retain(Scenes loaded) {
		// Drop the active raw pointer before pruning its owning history.
		if (m_Active && m_Active != &m_Detached && std::none_of(loaded.begin(), loaded.end(), [&](const auto& weak) {
			const auto owner = weak.lock(); return owner && owner.get() == m_Context;
		})) Deactivate(false);
		return m_Scenes.Retain(loaded);
	}
	void EditorHistorySession::SuspendForPlay(Scenes loaded) {
		Deactivate(true);
		m_Suspended.clear();
		for (const auto& weak : loaded) if (const auto scene = weak.lock()) m_Suspended.emplace(scene->GetName(), m_Scenes.Take(scene));
	}
	bool EditorHistorySession::RestoreAfterPlay(Scenes loaded, bool restoreSucceeded) {
		Deactivate(false); m_Scenes.Clear();
		bool retained = true;
		for (auto& [name, history] : m_Suspended) {
			std::shared_ptr<Scene> destination;
			if (restoreSucceeded) for (const auto& weak : loaded) {
				if (auto scene = weak.lock(); scene && scene->GetName() == name) { destination = std::move(scene); break; }
			}
			if (destination) m_Scenes.Restore(destination, std::move(history));
			else if (history.HasHistory()) retained = false;
		}
		m_Suspended.clear();
		return retained;
	}
	void EditorHistorySession::DiscardSuspended() { m_Suspended.clear(); }
}
