#pragma once

#include "Animation/AnimationClip.hpp"
#include "Gui/AssetSaveDialog.hpp"
#include "Scene/EntityHandle.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Index {
	class Scene;
	class AnimationPreview;
	struct RecordedChange;

	// Edits .anim clips of the selected Animator: dopesheet and curve views, events, record mode and a scrubbing preview
	// in the Editor View (through AnimationPreview, so the scene keeps its authored values).
	class AnimationWindow {
	public:
		void Render(bool* pOpen, Scene* scene, EntityHandle selected, AnimationPreview& preview);
		// From the Content Browser: shows this clip (previewed on the selected Animator when it uses it).
		void OpenClip(uint64_t guid);
		// Its own Ctrl+Z, Delete, Space ... win over the editor's entity shortcuts while it has focus.
		bool IsFocused() const { return m_FocusedLastFrame; }

	private:
		enum class ViewMode { Dopesheet, Curves };
		enum class DragKind { None, Scrub, Keys, Box, Pan, Event, CurveKey, InTangent, OutTangent };

		struct KeyRef {
			int Track = -1;
			int Key = -1;
			bool operator==(const KeyRef&) const = default;
		};

		struct Row {
			int Track = -1;
			// Group header rows (entity path + component) have Track == -1.
			std::string Label;
			std::string Path;
			std::string Component;
		};

		void ResolveTarget(Scene* scene, EntityHandle selected);
		void CollectClipChoices(Scene& scene);
		void SyncWorkingClip();
		void ApplyRecordedChanges(std::vector<RecordedChange> changes);

		void RenderToolbar(Scene* scene);
		void RenderClipCombo(Scene* scene);
		void RenderBody(Scene* scene);
		void RenderTrackList(Scene* scene, float width, float rowHeight, float scrollY);
		void RenderAddPropertyPopup(Scene* scene);
		void RenderRuler(const ImVec2& origin, float width, float height);
		void RenderEventRow(const ImVec2& origin, float width, float height);
		void RenderDopesheet(const ImVec2& origin, float width, float height, float rowHeight, float scrollY);
		void RenderCurves(const ImVec2& origin, float width, float height);
		void RenderKeyContextMenu();
		void RenderEventPopup();
		void HandleShortcuts();

		void BuildRows();
		float TimeToX(float time) const;
		float XToTime(float x) const;
		float SnapTime(float time) const;
		float FrameStep() const;
		void FrameAll(bool values);
		bool IsKeySelected(int track, int key) const;
		void SelectKey(int track, int key, bool additive);
		void DeleteSelection();
		void AddKeysAtTime(const std::vector<int>& tracks);
		void SetCurveKey(AnimationTrack& track, float time, float value, PropertyType type);
		void SetDiscreteKey(AnimationTrack& track, float time, const std::string& value);
		int FindOrAddTrack(const std::string& path, const std::string& component, const std::string& property);
		void JumpToKey(int direction);
		void SortTrackAndRemap(int track);
		// Asks where to save the clip; CreateClipAt runs once the dialog answers.
		void RequestNewClip();
		void CreateClipAt(const std::filesystem::path& path);
		bool IsTrackSelected(int track) const;
		std::string TrackError(const AnimationTrack& track) const;

		// Edits: Begin snapshots for undo, Commit writes the .anim (Live only updates the shared instance during drags).
		void BeginEdit();
		void CommitEdit();
		void LiveEdit();
		void Undo();
		void Redo();

		bool CanEdit() const { return m_ClipGuid != 0 && m_ClipLoaded; }

		// Target
		Scene* m_Scene = nullptr;
		EntityHandle m_Root = entt::null;
		std::string m_RootName;
		bool m_RootHasAnimator = false;
		uint64_t m_OpenedClip = 0;
		struct ClipChoice {
			uint64_t Guid = 0;
			std::string Label;
		};
		std::vector<ClipChoice> m_ClipChoices;
		AssetSaveDialog m_NewClipDialog;
		EntityHandle m_NewClipRoot = entt::null;

		// Clip being edited
		uint64_t m_ClipGuid = 0;
		uint64_t m_LoadedGuid = 0;
		bool m_PreviewAllowed = false;
		std::vector<std::string> m_Errors;
		std::string m_ClipName;
		AnimationClip m_Clip;
		std::shared_ptr<const AnimationClip> m_LoadedPtr;
		bool m_ClipLoaded = false;
		std::vector<AnimationClip> m_UndoStack;
		std::vector<AnimationClip> m_RedoStack;
		std::vector<Row> m_Rows;

		// Playback
		float m_Time = 0.0f;
		bool m_Playing = false;
		bool m_Recording = false;

		// View
		ViewMode m_Mode = ViewMode::Dopesheet;
		float m_ViewStart = -0.05f;
		float m_PixelsPerSecond = 300.0f;
		float m_ValueMin = -1.0f;
		float m_ValueMax = 1.0f;
		float m_TrackListWidth = 300.0f;
		float m_TimelineX0 = 0.0f;
		float m_TimelineWidth = 1.0f;
		bool m_FrameOnNextDraw = true;

		// Selection and dragging
		std::vector<KeyRef> m_SelectedKeys;
		std::vector<int> m_SelectedTracks;
		int m_SelectedEvent = -1;
		DragKind m_Drag = DragKind::None;
		bool m_DragEditStarted = false;
		ImVec2 m_DragStart{ 0.0f, 0.0f };
		float m_DragTimeOffset = 0.0f;
		float m_DragValueOffset = 0.0f;
		std::vector<float> m_DragOriginTimes;
		std::vector<float> m_DragOriginValues;
		KeyRef m_DragKey;
		KeyRef m_ContextKey;
		bool m_OpenKeyContext = false;
		bool m_OpenEventPopup = false;
		float m_PendingEventTime = -1.0f;
		bool m_FocusedLastFrame = false;
	};

}
