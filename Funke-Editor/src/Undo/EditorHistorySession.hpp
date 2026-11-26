#pragma once
#include "Undo/UndoStack.hpp"
#include <functional>
#include <unordered_map>

namespace Funke {
	// Owns edit histories across context changes and the Play/Stop scene replacement.
	// The boundary callbacks know only the pending inspector edit, never the editor layer.
	class EditorHistorySession {
	public:
		using Scenes = std::span<const std::weak_ptr<Scene>>;
		void SetBoundaryCallbacks(std::function<void(UndoStack&)> flush, std::function<void()> reset);
		UndoStack& Activate(Scene& scene, Scenes loaded);
		UndoStack& ForScene(Scene& scene, Scenes loaded);
		void Deactivate(bool flush);
		void Clear();
		bool Forget(const Scene* scene, bool detached);
		std::vector<std::string> Retain(Scenes loaded);
		void SuspendForPlay(Scenes loaded);
		// False means nonempty suspended history was discarded after failed restoration.
		bool RestoreAfterPlay(Scenes loaded, bool restoreSucceeded);
		void DiscardSuspended();
	private:
		SceneUndoHistory m_Scenes;
		UndoStack m_Detached;
		UndoStack* m_Active = nullptr;
		const Scene* m_Context = nullptr;
		std::unordered_map<std::string, UndoStack> m_Suspended;
		std::function<void(UndoStack&)> m_Flush;
		std::function<void()> m_Reset;
	};
}
