#pragma once

#include "Animation/AnimatorController.hpp"
#include "Gui/AssetSaveDialog.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Funke {
	class Scene;
	class AnimatorRuntime;

	// Node graph editor for .animctrl state machines: states, Entry / Any State, transitions with conditions, and the
	// parameter list. In Play mode it follows the selected Animator and shows its live state and parameters.
	class AnimatorWindow {
	public:
		void Render(bool* pOpen, Scene* scene, EntityHandle selected);
		void OpenController(uint64_t guid);
		bool IsFocused() const { return m_FocusedLastFrame; }
		// Clip of a state the user double-clicked, for the Animation window; 0 when none.
		uint64_t TakeOpenClipRequest() { const uint64_t guid = m_OpenClipRequest; m_OpenClipRequest = 0; return guid; }

	private:
		static constexpr int k_NoNode = -100;
		static constexpr int k_EntryNode = -2;
		static constexpr int k_AnyNode = AnimatorTransition::k_AnyState;

		void ResolveTarget(Scene* scene, EntityHandle selected);
		void SyncWorkingCopy();
		// Asks where to save the controller; CreateControllerAt runs once the dialog answers.
		void RequestNewController();
		void CreateControllerAt(const std::filesystem::path& path);

		void RenderParameters(float width, float height);
		void RenderGraph(const ImVec2& origin, const ImVec2& size);
		void RenderDetails(float width, float height);
		void RenderStateDetails(int state);
		void RenderTransitionDetails(int transition);
		void HandleShortcuts();

		ImVec2 NodeSize() const;
		ImVec2 NodeScreenPos(int node) const;
		glm::vec2 NodePosition(int node) const;
		void SetNodePosition(int node, const glm::vec2& position);
		int HitNode(const ImVec2& point) const;
		int HitTransition(const ImVec2& point) const;
		// Screen segment of a transition as drawn (both directions of a pair sit side by side); false if degenerate.
		bool TransitionSegment(int transition, ImVec2& outFrom, ImVec2& outTo) const;
		void FrameAll(const ImVec2& canvasSize);
		void CreateState(const glm::vec2& position, uint64_t clip, const std::string& name);
		void DeleteSelection();
		AnimatorRuntime* LiveRuntime() const;

		void BeginEdit();
		void CommitEdit();
		void LiveEdit();
		void Undo();
		void Redo();

		Scene* m_Scene = nullptr;
		EntityHandle m_Animator = entt::null;
		uint64_t m_OpenedController = 0;
		AssetSaveDialog m_NewControllerDialog;
		EntityHandle m_NewControllerTarget = entt::null;
		uint64_t m_ControllerGuid = 0;
		uint64_t m_LoadedGuid = 0;
		std::string m_ControllerName;
		AnimatorController m_Controller;
		std::shared_ptr<const AnimatorController> m_LoadedPtr;
		bool m_Loaded = false;
		std::vector<AnimatorController> m_UndoStack;
		std::vector<AnimatorController> m_RedoStack;

		ImVec2 m_Pan{ 300.0f, 200.0f };
		float m_Zoom = 1.0f;
		ImVec2 m_CanvasOrigin{ 0.0f, 0.0f };
		int m_SelectedNode = k_NoNode;
		int m_SelectedTransition = -1;
		int m_MakingTransitionFrom = k_NoNode;
		int m_DraggingNode = k_NoNode;
		bool m_DragEditStarted = false;
		bool m_Panning = false;
		ImVec2 m_ContextPosition{ 0.0f, 0.0f };
		int m_ContextNode = k_NoNode;
		int m_ContextTransition = -1;
		bool m_FocusedLastFrame = false;
		bool m_FrameOnNextDraw = true;
		uint64_t m_OpenClipRequest = 0;
		float m_ParameterWidth = 230.0f;
		float m_DetailsWidth = 290.0f;
	};

}
