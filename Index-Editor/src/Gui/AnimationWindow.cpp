#include "Gui/AnimationWindow.hpp"

#include "Animation/AnimationBinding.hpp"
#include "Animation/AnimatorController.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Editor/AnimationPreview.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/PropertyDescriptor.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

namespace Index {
	namespace {
		constexpr size_t k_MaxUndo = 100;
		constexpr float k_MinPixelsPerSecond = 20.0f;
		constexpr float k_MaxPixelsPerSecond = 6000.0f;

		ImU32 WithAlpha(ImVec4 color, float alpha) {
			color.w *= alpha;
			return ImGui::GetColorU32(color);
		}

		ImU32 TextColor(float alpha = 1.0f) { return WithAlpha(ImGui::GetStyleColorVec4(ImGuiCol_Text), alpha); }
		ImU32 DimColor(float alpha = 1.0f) { return WithAlpha(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), alpha); }

		bool IsSteppedType(PropertyType type) {
			switch (type) {
			case PropertyType::Bool:
			case PropertyType::Int8: case PropertyType::Int16: case PropertyType::Int32: case PropertyType::Int64:
			case PropertyType::UInt8: case PropertyType::UInt16: case PropertyType::UInt32: case PropertyType::UInt64:
			case PropertyType::IntVec2: case PropertyType::IntVec3: case PropertyType::IntVec4:
				return true;
			default:
				return false;
			}
		}

		ImU32 ChannelColor(const std::string& property) {
			const AnimationPropertyPath path = ParseAnimationProperty(property);
			if (path.Channel >= 0 && path.Channel <= 2) return ImGuiUtils::AxisColor(path.Channel);
			return ImGui::GetColorU32(EditorTheme::Colors::Highlight());
		}

		std::string ClipLabel(uint64_t guid) {
			const std::string path = AssetRegistry::ResolvePath(guid);
			return path.empty() ? std::string("Missing clip") : std::filesystem::path(path).stem().string();
		}

		void DrawDiamond(ImDrawList* draw, const ImVec2& center, float radius, ImU32 fill, ImU32 outline) {
			const ImVec2 points[4] = {
				{ center.x, center.y - radius }, { center.x + radius, center.y },
				{ center.x, center.y + radius }, { center.x - radius, center.y },
			};
			draw->AddConvexPolyFilled(points, 4, fill);
			draw->AddPolyline(points, 4, outline, ImDrawFlags_Closed, 1.0f);
		}

		float NiceStep(float pixelsPerUnit, float minPixels) {
			static const float k_Steps[] = { 0.001f, 0.002f, 0.005f, 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f,
				10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f };
			for (float step : k_Steps) {
				if (step * pixelsPerUnit >= minPixels) return step;
			}
			return 1000.0f;
		}

		// Visible fields of the C# scripts on entity, as (class, field, type tag, value text).
		struct ScriptField {
			std::string Class;
			std::string Name;
			PropertyType Type = PropertyType::None;
			std::string Value;
		};

		PropertyType ScriptFieldType(const std::string& tag) {
			if (tag == "float" || tag == "double") return PropertyType::Float;
			if (tag == "int" || tag == "short" || tag == "byte" || tag == "long") return PropertyType::Int32;
			if (tag == "bool") return PropertyType::Bool;
			if (tag == "color") return PropertyType::Color;
			if (tag == "vector2") return PropertyType::Vec2;
			if (tag == "vector3") return PropertyType::Vec3;
			if (tag == "vector4") return PropertyType::Vec4;
			return PropertyType::None;
		}

		std::vector<ScriptField> CollectScriptFields(Scene& scene, EntityHandle entity) {
			std::vector<ScriptField> out;
			const ScriptComponent* scripts = scene.GetRegistry().try_get<ScriptComponent>(entity);
			const auto& callbacks = ScriptEngine::GetCallbacks();
			if (!scripts || !callbacks.GetScriptFields) return out;
			for (const ScriptInstance& script : scripts->Scripts) {
				if (!script.HasManagedInstance()) continue;
				const char* json = callbacks.GetScriptFields(static_cast<int32_t>(script.GetGCHandle()));
				Json::Value root;
				if (!json || !Json::TryParse(json, root) || !root.IsArray()) continue;
				for (const Json::Value& field : root.GetArray()) {
					const Json::Value* name = field.FindMember("name");
					const Json::Value* type = field.FindMember("type");
					const Json::Value* value = field.FindMember("value");
					if (!name || !type) continue;
					const PropertyType propertyType = ScriptFieldType(type->AsStringOr());
					if (propertyType == PropertyType::None) continue;
					out.push_back({ script.GetClassName(), name->AsStringOr(), propertyType, value ? value->AsStringOr() : std::string{} });
				}
			}
			return out;
		}
	}

	// ── Target and clip ──────────────────────────────────────────────

	void AnimationWindow::OpenClip(uint64_t guid) {
		m_OpenedClip = guid;
		m_ClipGuid = guid;
	}

	void AnimationWindow::ResolveTarget(Scene* scene, EntityHandle selected) {
		m_Scene = scene;
		m_Root = entt::null;
		m_RootHasAnimator = false;
		m_ClipChoices.clear();
		if (scene && scene->GetRegistry().valid(selected)) {
			Entity entity = scene->GetEntity(selected);
			while (entity.IsValid()) {
				if (entity.HasComponent<AnimatorComponent>()) {
					m_Root = entity.GetHandle();
					m_RootHasAnimator = true;
					m_RootName = entity.GetName();
					break;
				}
				entity = entity.GetParent();
			}
			if (m_Root == entt::null) {
				m_Root = selected;
				m_RootName = scene->GetEntity(selected).GetName();
			}
		}
		if (m_RootHasAnimator) CollectClipChoices(*scene);

		auto inChoices = [this](uint64_t guid) {
			return std::any_of(m_ClipChoices.begin(), m_ClipChoices.end(), [guid](const ClipChoice& c) { return c.Guid == guid; });
		};
		if (m_OpenedClip != 0) {
			m_ClipGuid = m_OpenedClip;
		}
		else if (!inChoices(m_ClipGuid)) {
			m_ClipGuid = m_ClipChoices.empty() ? 0 : m_ClipChoices.front().Guid;
		}
		m_PreviewAllowed = m_RootHasAnimator && inChoices(m_ClipGuid);
	}

	void AnimationWindow::CollectClipChoices(Scene& scene) {
		const AnimatorComponent& animator = scene.GetRegistry().get<AnimatorComponent>(m_Root);
		auto add = [this](uint64_t guid, std::string label) {
			if (guid == 0) return;
			for (const ClipChoice& c : m_ClipChoices) if (c.Guid == guid) return;
			m_ClipChoices.push_back({ guid, std::move(label) });
		};
		if (animator.Controller != 0) {
			if (auto controller = AnimatorControllerLibrary::Get(animator.Controller)) {
				for (const AnimatorState& state : controller->States) add(state.Clip, ClipLabel(state.Clip) + "  (" + state.Name + ")");
			}
		}
		else {
			add(animator.Clip, ClipLabel(animator.Clip));
		}
	}

	void AnimationWindow::SyncWorkingClip() {
		if (m_ClipGuid == 0) {
			m_ClipLoaded = false;
			m_LoadedGuid = 0;
			return;
		}
		std::shared_ptr<const AnimationClip> clip = AnimationClipLibrary::Get(m_ClipGuid);
		if (!clip) {
			m_ClipLoaded = false;
			return;
		}
		if (m_LoadedGuid != m_ClipGuid) {
			m_Clip = *clip;
			m_LoadedPtr = clip;
			m_LoadedGuid = m_ClipGuid;
			m_ClipName = ClipLabel(m_ClipGuid);
			m_UndoStack.clear();
			m_RedoStack.clear();
			m_SelectedKeys.clear();
			m_SelectedTracks.clear();
			m_SelectedEvent = -1;
			m_Time = std::clamp(m_Time, 0.0f, std::max(m_Clip.Length, 0.0f));
			m_FrameOnNextDraw = true;
		}
		else if (clip != m_LoadedPtr && m_Drag == DragKind::None) {
			// Changed outside this window (file edited, hot reload).
			m_Clip = *clip;
			m_LoadedPtr = clip;
			m_SelectedKeys.clear();
		}
		m_ClipLoaded = true;
	}

	void AnimationWindow::BeginEdit() {
		m_UndoStack.push_back(m_Clip);
		if (m_UndoStack.size() > k_MaxUndo) m_UndoStack.erase(m_UndoStack.begin());
		m_RedoStack.clear();
	}

	void AnimationWindow::CommitEdit() {
		if (!CanEdit()) return;
		AnimationClipLibrary::Save(m_ClipGuid, m_Clip);
		m_LoadedPtr = AnimationClipLibrary::Get(m_ClipGuid);
	}

	void AnimationWindow::LiveEdit() {
		if (!CanEdit()) return;
		AnimationClipLibrary::Set(m_ClipGuid, m_Clip);
		m_LoadedPtr = AnimationClipLibrary::Get(m_ClipGuid);
	}

	void AnimationWindow::Undo() {
		if (m_UndoStack.empty()) return;
		m_RedoStack.push_back(m_Clip);
		m_Clip = m_UndoStack.back();
		m_UndoStack.pop_back();
		m_SelectedKeys.clear();
		CommitEdit();
	}

	void AnimationWindow::Redo() {
		if (m_RedoStack.empty()) return;
		m_UndoStack.push_back(m_Clip);
		m_Clip = m_RedoStack.back();
		m_RedoStack.pop_back();
		m_SelectedKeys.clear();
		CommitEdit();
	}

	void AnimationWindow::RequestNewClip() {
		IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project || !m_Scene || !m_Scene->GetRegistry().valid(m_Root)) return;

		namespace fs = std::filesystem;
		// Next to the clip being edited, so a character's clips stay together.
		const std::string currentClip = m_ClipGuid != 0 ? AssetRegistry::ResolvePath(m_ClipGuid) : std::string{};
		const fs::path folder = currentClip.empty() ? Path::FromUtf8(project->AssetsDirectory) : Path::FromUtf8(currentClip).parent_path();
		const std::string stem = m_RootName.empty() ? std::string("New Clip") : m_RootName;
		m_NewClipRoot = m_Root;
		m_NewClipDialog.Open(AssetSaveDialog::UniquePath(folder, stem, k_AnimationClipExtension), "Animation Clip", k_AnimationClipExtension);
	}

	void AnimationWindow::CreateClipAt(const std::filesystem::path& path) {
		if (!m_Scene || m_Root != m_NewClipRoot || !m_Scene->GetRegistry().valid(m_Root)) return;

		const std::string pathUtf8 = Path::ToUtf8(path);
		if (!AnimationClipLibrary::SaveToFile(pathUtf8, AnimationClip{})) return;
		const uint64_t guid = AssetRegistry::ImportAssetPathIncremental(pathUtf8);
		if (guid == 0) return;
		// A confirmed overwrite keeps the old GUID; drop its cached contents.
		AnimationClipLibrary::Invalidate(guid);

		Entity root = m_Scene->GetEntity(m_Root);
		if (!root.HasComponent<AnimatorComponent>()) root.AddComponent<AnimatorComponent>();
		AnimatorComponent& animator = root.GetComponent<AnimatorComponent>();
		auto addState = [&](AnimatorController& controller, uint64_t clip) {
			AnimatorState state;
			state.Name = ClipLabel(clip);
			for (int i = 1; controller.FindState(state.Name) >= 0; ++i) state.Name = ClipLabel(clip) + " " + std::to_string(i);
			state.Clip = clip;
			state.Position = { static_cast<float>(controller.States.size()) * 220.0f, 0.0f };
			controller.States.push_back(std::move(state));
		};

		if (animator.Controller != 0) {
			auto existing = AnimatorControllerLibrary::Get(animator.Controller);
			AnimatorController controller = existing ? *existing : AnimatorController{};
			const bool used = std::any_of(controller.States.begin(), controller.States.end(), [guid](const AnimatorState& state) { return state.Clip == guid; });
			if (!used) {
				addState(controller, guid);
				AnimatorControllerLibrary::Save(animator.Controller, controller);
			}
		}
		else if (animator.Clip == 0 || animator.Clip == guid) {
			animator.Clip = guid;
		}
		else {
			// A second clip needs a state machine to choose between them; it goes next to the new clip.
			const std::string stem = m_RootName.empty() ? std::string("New Controller") : m_RootName;
			const std::string controllerPath = Path::ToUtf8(AssetSaveDialog::UniquePath(path.parent_path(), stem, k_AnimatorControllerExtension));
			AnimatorController controller;
			addState(controller, animator.Clip);
			addState(controller, guid);
			if (!AnimatorControllerLibrary::SaveToFile(controllerPath, controller)) return;
			animator.Controller = AssetRegistry::ImportAssetPathIncremental(controllerPath);
			animator.Clip = 0;
		}
		animator.Runtime.Reset();
		m_Scene->MarkDirty();
		m_OpenedClip = 0;
		m_ClipGuid = guid;
	}

	// ── Rows, keys, tracks ───────────────────────────────────────────

	void AnimationWindow::BuildRows() {
		m_Rows.clear();
		std::vector<int> order(m_Clip.Tracks.size());
		for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
		std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
			const AnimationTrack& ta = m_Clip.Tracks[static_cast<size_t>(a)];
			const AnimationTrack& tb = m_Clip.Tracks[static_cast<size_t>(b)];
			if (ta.Path != tb.Path) return ta.Path < tb.Path;
			if (ta.Component != tb.Component) return ta.Component < tb.Component;
			// Channels in x, y, z, w / r, g, b, a order rather than alphabetically.
			const AnimationPropertyPath pa = ParseAnimationProperty(ta.Property);
			const AnimationPropertyPath pb = ParseAnimationProperty(tb.Property);
			if (pa.Base != pb.Base) return pa.Base < pb.Base;
			return pa.Channel < pb.Channel;
		});
		const ComponentRegistry& registry = SceneManager::GetComponentRegistry();
		std::string lastGroup;
		for (int index : order) {
			const AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(index)];
			const std::string group = track.Path + "\x1f" + track.Component;
			if (group != lastGroup || m_Rows.empty()) {
				const ComponentInfo* info = registry.FindBySerializedName(track.Component);
				const std::string componentName = info ? info->displayName : track.Component;
				const std::string entityName = track.Path.empty() ? (m_RootName.empty() ? std::string("Root") : m_RootName) : track.Path;
				m_Rows.push_back({ -1, entityName + " : " + componentName, track.Path, track.Component });
				lastGroup = group;
			}
			m_Rows.push_back({ index, track.Property, track.Path, track.Component });
		}
	}

	float AnimationWindow::TimeToX(float time) const { return m_TimelineX0 + (time - m_ViewStart) * m_PixelsPerSecond; }
	float AnimationWindow::XToTime(float x) const { return m_ViewStart + (x - m_TimelineX0) / m_PixelsPerSecond; }
	float AnimationWindow::FrameStep() const { return 1.0f / std::max(m_Clip.SampleRate, 1.0f); }

	float AnimationWindow::SnapTime(float time) const {
		if (ImGui::GetIO().KeyShift) return std::max(time, 0.0f);
		const float rate = std::max(m_Clip.SampleRate, 1.0f);
		return std::max(std::round(time * rate) / rate, 0.0f);
	}

	bool AnimationWindow::IsKeySelected(int track, int key) const {
		return std::find(m_SelectedKeys.begin(), m_SelectedKeys.end(), KeyRef{ track, key }) != m_SelectedKeys.end();
	}

	bool AnimationWindow::IsTrackSelected(int track) const {
		return std::find(m_SelectedTracks.begin(), m_SelectedTracks.end(), track) != m_SelectedTracks.end();
	}

	void AnimationWindow::SelectKey(int track, int key, bool additive) {
		const KeyRef ref{ track, key };
		const auto it = std::find(m_SelectedKeys.begin(), m_SelectedKeys.end(), ref);
		if (additive) {
			if (it != m_SelectedKeys.end()) m_SelectedKeys.erase(it);
			else m_SelectedKeys.push_back(ref);
			return;
		}
		if (it == m_SelectedKeys.end()) m_SelectedKeys = { ref };
	}

	std::string AnimationWindow::TrackError(const AnimationTrack& track) const {
		const std::string prefix = (track.Path.empty() ? std::string{} : track.Path + ": ") + track.Component + "." + track.Property + " - ";
		for (const std::string& error : m_Errors) {
			if (error.rfind(prefix, 0) == 0) return error.substr(prefix.size());
		}
		return {};
	}

	int AnimationWindow::FindOrAddTrack(const std::string& path, const std::string& component, const std::string& property) {
		for (size_t i = 0; i < m_Clip.Tracks.size(); ++i) {
			const AnimationTrack& t = m_Clip.Tracks[i];
			if (t.Path == path && t.Component == component && t.Property == property) return static_cast<int>(i);
		}
		AnimationTrack track;
		track.Path = path;
		track.Component = component;
		track.Property = property;
		m_Clip.Tracks.push_back(std::move(track));
		return static_cast<int>(m_Clip.Tracks.size()) - 1;
	}

	void AnimationWindow::SetCurveKey(AnimationTrack& track, float time, float value, PropertyType type) {
		const bool existing = std::any_of(track.Curve.Keys.begin(), track.Curve.Keys.end(),
			[time](const Keyframe& k) { return std::abs(k.Time - time) <= 0.0005f; });
		const int index = track.Curve.SetKey(time, value, IsSteppedType(type) ? KeyInterpolation::Step : KeyInterpolation::Bezier);
		if (!existing && track.Curve.Keys[static_cast<size_t>(index)].Interpolation == KeyInterpolation::Bezier) {
			track.Curve.SmoothTangents(index);
		}
	}

	void AnimationWindow::SetDiscreteKey(AnimationTrack& track, float time, const std::string& value) {
		for (DiscreteKeyframe& key : track.DiscreteKeys) {
			if (std::abs(key.Time - time) <= 0.0005f) {
				key.Value = value;
				return;
			}
		}
		const auto it = std::upper_bound(track.DiscreteKeys.begin(), track.DiscreteKeys.end(), time,
			[](float t, const DiscreteKeyframe& k) { return t < k.Time; });
		track.DiscreteKeys.insert(it, { time, value });
	}

	void AnimationWindow::SortTrackAndRemap(int trackIndex) {
		AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(trackIndex)];
		if (track.IsDiscrete()) {
			std::vector<std::pair<float, bool>> marks;
			for (size_t k = 0; k < track.DiscreteKeys.size(); ++k) marks.push_back({ track.DiscreteKeys[k].Time, IsKeySelected(trackIndex, static_cast<int>(k)) });
			std::vector<size_t> order(track.DiscreteKeys.size());
			for (size_t i = 0; i < order.size(); ++i) order[i] = i;
			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return track.DiscreteKeys[a].Time < track.DiscreteKeys[b].Time; });
			std::vector<DiscreteKeyframe> sorted;
			std::erase_if(m_SelectedKeys, [trackIndex](const KeyRef& r) { return r.Track == trackIndex; });
			for (size_t i = 0; i < order.size(); ++i) {
				sorted.push_back(track.DiscreteKeys[order[i]]);
				if (marks[order[i]].second) m_SelectedKeys.push_back({ trackIndex, static_cast<int>(i) });
			}
			track.DiscreteKeys = std::move(sorted);
			return;
		}
		std::vector<size_t> order(track.Curve.Keys.size());
		for (size_t i = 0; i < order.size(); ++i) order[i] = i;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return track.Curve.Keys[a].Time < track.Curve.Keys[b].Time; });
		std::vector<bool> selected(order.size());
		for (size_t i = 0; i < order.size(); ++i) selected[i] = IsKeySelected(trackIndex, static_cast<int>(i));
		std::vector<Keyframe> sorted;
		std::erase_if(m_SelectedKeys, [trackIndex](const KeyRef& r) { return r.Track == trackIndex; });
		for (size_t i = 0; i < order.size(); ++i) {
			sorted.push_back(track.Curve.Keys[order[i]]);
			if (selected[order[i]]) m_SelectedKeys.push_back({ trackIndex, static_cast<int>(i) });
		}
		// Keys dragged onto the same frame merge; the moved one wins.
		for (size_t i = 1; i < sorted.size();) {
			if (std::abs(sorted[i].Time - sorted[i - 1].Time) <= 0.0005f) {
				sorted.erase(sorted.begin() + static_cast<std::ptrdiff_t>(i - 1));
				std::erase_if(m_SelectedKeys, [trackIndex](const KeyRef& r) { return r.Track == trackIndex; });
			}
			else {
				++i;
			}
		}
		track.Curve.Keys = std::move(sorted);
	}

	void AnimationWindow::DeleteSelection() {
		if (m_SelectedEvent >= 0 && m_SelectedEvent < static_cast<int>(m_Clip.Events.size()) && m_SelectedKeys.empty()) {
			BeginEdit();
			m_Clip.Events.erase(m_Clip.Events.begin() + m_SelectedEvent);
			m_SelectedEvent = -1;
			CommitEdit();
			return;
		}
		if (m_SelectedKeys.empty()) return;
		BeginEdit();
		std::vector<KeyRef> keys = m_SelectedKeys;
		std::sort(keys.begin(), keys.end(), [](const KeyRef& a, const KeyRef& b) { return a.Track != b.Track ? a.Track < b.Track : a.Key > b.Key; });
		for (const KeyRef& ref : keys) {
			if (ref.Track < 0 || ref.Track >= static_cast<int>(m_Clip.Tracks.size())) continue;
			AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(ref.Track)];
			if (track.IsDiscrete()) {
				if (ref.Key < static_cast<int>(track.DiscreteKeys.size())) track.DiscreteKeys.erase(track.DiscreteKeys.begin() + ref.Key);
			}
			else {
				track.Curve.RemoveKey(ref.Key);
			}
		}
		m_SelectedKeys.clear();
		CommitEdit();
	}

	void AnimationWindow::AddKeysAtTime(const std::vector<int>& tracks) {
		if (tracks.empty()) return;
		BeginEdit();
		const float time = SnapTime(m_Time);
		for (int index : tracks) {
			if (index < 0 || index >= static_cast<int>(m_Clip.Tracks.size())) continue;
			AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(index)];
			if (track.IsDiscrete()) SetDiscreteKey(track, time, track.GetDiscreteValue(time));
			else SetCurveKey(track, time, track.Curve.Evaluate(time), PropertyType::Float);
		}
		CommitEdit();
	}

	void AnimationWindow::JumpToKey(int direction) {
		float best = direction > 0 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max();
		bool found = false;
		auto consider = [&](float t) {
			if (direction > 0 ? (t > m_Time + 0.0005f && t < best) : (t < m_Time - 0.0005f && t > best)) {
				best = t;
				found = true;
			}
		};
		for (const AnimationTrack& track : m_Clip.Tracks) {
			for (const Keyframe& k : track.Curve.Keys) consider(k.Time);
			for (const DiscreteKeyframe& k : track.DiscreteKeys) consider(k.Time);
		}
		for (const AnimationEvent& e : m_Clip.Events) consider(e.Time);
		if (found) m_Time = best;
		else m_Time = direction > 0 ? m_Clip.Length : 0.0f;
	}

	void AnimationWindow::FrameAll(bool values) {
		const float end = std::max({ m_Clip.Length, m_Clip.GetContentEndTime(), 0.1f });
		m_PixelsPerSecond = std::clamp((m_TimelineWidth - 24.0f) / end, k_MinPixelsPerSecond, k_MaxPixelsPerSecond);
		m_ViewStart = -12.0f / m_PixelsPerSecond;
		if (!values) return;
		float lo = std::numeric_limits<float>::max();
		float hi = -std::numeric_limits<float>::max();
		for (size_t i = 0; i < m_Clip.Tracks.size(); ++i) {
			const AnimationTrack& track = m_Clip.Tracks[i];
			if (track.IsDiscrete() || (!m_SelectedTracks.empty() && !IsTrackSelected(static_cast<int>(i)))) continue;
			for (float t = 0.0f; t <= end; t += end / 64.0f) {
				const float v = track.Curve.Evaluate(t);
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
		}
		if (lo > hi) { lo = -1.0f; hi = 1.0f; }
		if (hi - lo < 0.001f) { lo -= 0.5f; hi += 0.5f; }
		const float pad = (hi - lo) * 0.12f;
		m_ValueMin = lo - pad;
		m_ValueMax = hi + pad;
	}

	void AnimationWindow::ApplyRecordedChanges(std::vector<RecordedChange> changes) {
		if (changes.empty() || !CanEdit()) return;
		BeginEdit();
		const float time = SnapTime(m_Time);
		for (const RecordedChange& change : changes) {
			if (IsCurveAnimatable(change.Type)) {
				const int channels = AnimationChannelCount(change.Type);
				for (int c = 0; c < channels; ++c) {
					const std::string property = channels > 1 ? change.Property + "." + AnimationChannelSuffix(change.Type, c) : change.Property;
					const bool isNew = m_Clip.FindTrack(change.Path, change.Component, property) == nullptr;
					const int index = FindOrAddTrack(change.Path, change.Component, property);
					AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(index)];
					// A property animated for the first time keeps its old value at the start of the clip.
					if (isNew && time > 0.0f) SetCurveKey(track, 0.0f, ReadAnimationChannel(change.Before, c), change.Type);
					SetCurveKey(track, time, ReadAnimationChannel(change.After, c), change.Type);
				}
			}
			else {
				const bool isNew = m_Clip.FindTrack(change.Path, change.Component, change.Property) == nullptr;
				const int index = FindOrAddTrack(change.Path, change.Component, change.Property);
				AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(index)];
				if (isNew && time > 0.0f) SetDiscreteKey(track, 0.0f, change.Before.ToString());
				SetDiscreteKey(track, time, change.After.ToString());
			}
		}
		if (time > m_Clip.Length) m_Clip.Length = time;
		CommitEdit();
	}

	// ── Render ───────────────────────────────────────────────────────

	void AnimationWindow::Render(bool* pOpen, Scene* scene, EntityHandle selected, AnimationPreview& preview) {
		m_FocusedLastFrame = false;
		if (!pOpen || !*pOpen) {
			m_Recording = false;
			m_Playing = false;
			preview.SetRecording(false);
			return;
		}

		EditorUI::NextWindowDefaultSize(0.6f, 2.4f);
		if (!ImGui::Begin("Animation", pOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::End();
			preview.SetRecording(false);
			return;
		}
		m_FocusedLastFrame = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ResolveTarget(scene, selected);
		if (const std::optional<std::filesystem::path> path = m_NewClipDialog.Poll()) CreateClipAt(*path);
		SyncWorkingClip();
		m_Errors = preview.GetClipErrors();
		ApplyRecordedChanges(preview.TakeRecordedChanges());

		const bool gamePlaying = Application::GetIsPlaying();
		if (gamePlaying) {
			m_Recording = false;
			m_Playing = false;
		}
		if (m_Playing && CanEdit()) {
			m_Time += Time::GetDeltaTimeUnscaled();
			const float length = std::max(m_Clip.Length, 0.0f);
			if (m_Time > length) {
				if (m_Clip.Loop && length > 0.0f) m_Time = std::fmod(m_Time, length);
				else { m_Time = length; m_Playing = false; }
			}
		}

		RenderToolbar(scene);

		if (!scene) {
			EditorUI::EmptyState("No scene open");
		}
		else if (!CanEdit()) {
			if (m_ClipGuid != 0) {
				EditorUI::EmptyState("Clip could not be read", "Check the .anim file for JSON errors.");
			}
			else if (m_Root == entt::null) {
				EditorUI::EmptyState("Nothing selected", "Select an entity with an Animator to edit its clips.");
			}
			else {
				EditorUI::EmptyState(m_RootHasAnimator ? "No clip" : "No Animator",
					m_RootHasAnimator ? "Create a clip for this Animator." : "Create a clip to add an Animator to this entity.");
				const float width = ImGui::CalcTextSize("Create Clip...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
				ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
				if (EditorUI::Button("Create Clip...", EditorUI::ButtonVariant::Primary)) RequestNewClip();
			}
		}
		else {
			RenderBody(scene);
			HandleShortcuts();
		}

		const bool previewing = !gamePlaying && scene && m_PreviewAllowed && CanEdit() && scene->GetRegistry().valid(m_Root);
		if (previewing) preview.SetClipPose(scene, m_Root, m_ClipGuid, m_Time);
		preview.SetRecording(previewing && m_Recording);
		ImGui::End();
	}

	void AnimationWindow::RenderClipCombo(Scene* scene) {
		(void)scene;
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
		const std::string preview = m_ClipGuid != 0 ? ClipLabel(m_ClipGuid) : std::string("No clip");
		if (!ImGui::BeginCombo("##Clip", preview.c_str())) {
			EditorUI::Tooltip("Clip to edit");
			return;
		}
		for (const ClipChoice& choice : m_ClipChoices) {
			if (ImGui::Selectable(choice.Label.c_str(), choice.Guid == m_ClipGuid)) {
				m_ClipGuid = choice.Guid;
				m_OpenedClip = 0;
			}
		}
		if (m_OpenedClip != 0 && std::none_of(m_ClipChoices.begin(), m_ClipChoices.end(), [this](const ClipChoice& c) { return c.Guid == m_OpenedClip; })) {
			ImGui::Selectable((ClipLabel(m_OpenedClip) + "  (not used by this Animator)").c_str(), true);
		}
		if (!m_ClipChoices.empty() || m_OpenedClip != 0) ImGui::Separator();
		ImGui::BeginDisabled(m_Root == entt::null);
		if (ImGui::Selectable("New Clip...")) RequestNewClip();
		ImGui::EndDisabled();
		ImGui::EndCombo();
	}

	void AnimationWindow::RenderToolbar(Scene* scene) {
		EditorUI::BeginToolbar("##AnimationToolbar");
		const bool editable = CanEdit();
		ImGui::BeginDisabled(!editable || !m_PreviewAllowed || Application::GetIsPlaying());

		// Record: a dot in the error colour, filled while recording.
		{
			const float size = ImGui::GetFrameHeight();
			if (m_Recording) ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Colors::ActiveToggle());
			if (ImGui::Button("##Record", ImVec2(size, size))) m_Recording = !m_Recording;
			if (m_Recording) ImGui::PopStyleColor();
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
			const float radius = size * 0.22f;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImU32 color = ImGui::GetColorU32(EditorTheme::Colors::Error());
			if (m_Recording) draw->AddCircleFilled(center, radius, color);
			else draw->AddCircle(center, radius, color, 0, 1.6f);
			EditorUI::Tooltip(m_Recording ? "Recording: changes to animated entities become keys" : "Record: changes to animated entities become keys");
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!editable);
		ImGui::SameLine();
		if (EditorUI::IconButton("##PrevKey", "arrow_left", "Previous key (,)")) JumpToKey(-1);
		ImGui::SameLine();
		if (EditorUI::IconButton("##PlayPreview", m_Playing ? "pause" : "play", m_Playing ? "Pause preview (Space)" : "Play preview (Space)", m_Playing)) {
			if (!m_Playing && m_Time >= m_Clip.Length - 0.0001f) m_Time = 0.0f;
			m_Playing = !m_Playing;
		}
		ImGui::SameLine();
		if (EditorUI::IconButton("##NextKey", "arrow_right", "Next key (.)")) JumpToKey(1);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
		ImGui::DragFloat("##Time", &m_Time, 0.01f, 0.0f, std::max(m_Clip.Length, 0.0f), "%.2f s");
		m_Time = std::max(m_Time, 0.0f);
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Frame %d", static_cast<int>(std::round(m_Time * std::max(m_Clip.SampleRate, 1.0f))));
		ImGui::EndDisabled();

		ImGui::SameLine();
		EditorUI::ToolbarSeparator();
		ImGui::SameLine();
		RenderClipCombo(scene);

		ImGui::BeginDisabled(!editable);
		ImGui::SameLine();
		EditorUI::ToolbarSeparator();
		ImGui::SameLine();
		if (EditorUI::ToggleButton("Dopesheet", m_Mode == ViewMode::Dopesheet)) m_Mode = ViewMode::Dopesheet;
		ImGui::SameLine(0.0f, 2.0f);
		if (EditorUI::ToggleButton("Curves", m_Mode == ViewMode::Curves)) {
			m_Mode = ViewMode::Curves;
			m_FrameOnNextDraw = true;
		}

		ImGui::SameLine();
		EditorUI::ToolbarSeparator();
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Length");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
		float length = m_Clip.Length;
		if (ImGui::DragFloat("##Length", &length, 0.01f, 0.0f, 3600.0f, "%.2f s")) {
			if (ImGui::IsItemActivated() || !m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
			m_Clip.Length = std::max(length, 0.0f);
			LiveEdit();
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) { CommitEdit(); m_DragEditStarted = false; }
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Samples");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.0f);
		int rate = static_cast<int>(std::round(m_Clip.SampleRate));
		if (ImGui::DragInt("##Samples", &rate, 0.2f, 1, 240)) {
			BeginEdit();
			m_Clip.SampleRate = static_cast<float>(std::clamp(rate, 1, 240));
			CommitEdit();
		}
		EditorUI::Tooltip("Frames per second of the key grid");
		ImGui::SameLine();
		bool loop = m_Clip.Loop;
		if (ImGuiUtils::Switch("Loop", &loop)) {
			BeginEdit();
			m_Clip.Loop = loop;
			CommitEdit();
		}
		ImGui::EndDisabled();
		EditorUI::EndToolbar();
	}

	void AnimationWindow::RenderBody(Scene* scene) {
		BuildRows();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float rowHeight = ImGui::GetFrameHeight();
		const float rulerHeight = rowHeight;
		const float eventHeight = rowHeight * 0.85f;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float statusHeight = ImGui::GetTextLineHeightWithSpacing();
		m_TrackListWidth = std::clamp(m_TrackListWidth, 180.0f, std::max(180.0f, avail.x * 0.6f));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		m_TimelineX0 = origin.x + m_TrackListWidth + 1.0f;
		m_TimelineWidth = std::max(avail.x - m_TrackListWidth - 1.0f - style.ScrollbarSize, 50.0f);
		if (m_FrameOnNextDraw) {
			FrameAll(m_Mode == ViewMode::Curves);
			m_FrameOnNextDraw = false;
		}

		// Header: Add Property next to the ruler, Events label next to the event row.
		{
			ImGui::SetCursorScreenPos(origin);
			ImGui::BeginDisabled(!m_PreviewAllowed || !scene);
			if (EditorUI::Button("Add Property...", EditorUI::ButtonVariant::Default, ImVec2(m_TrackListWidth - style.ItemSpacing.x, rulerHeight))) {
				ImGui::OpenPopup("##AddProperty");
			}
			ImGui::EndDisabled();
			if (!m_PreviewAllowed) EditorUI::Tooltip("Select the Animator that uses this clip to add properties");
			RenderAddPropertyPopup(scene);
			ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rulerHeight));
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Events");
			RenderRuler(ImVec2(m_TimelineX0, origin.y), m_TimelineWidth, rulerHeight);
			RenderEventRow(ImVec2(m_TimelineX0, origin.y + rulerHeight), m_TimelineWidth, eventHeight);
		}

		const float bodyTop = origin.y + rulerHeight + eventHeight;
		const float bodyHeight = std::max(avail.y - rulerHeight - eventHeight - statusHeight, rowHeight);
		ImGui::SetCursorScreenPos(ImVec2(origin.x, bodyTop));

		if (m_Mode == ViewMode::Dopesheet) {
			ImGui::BeginChild("##Rows", ImVec2(avail.x, bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse);
			const float scrollY = ImGui::GetScrollY();
			if (ImGui::IsWindowHovered() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && ImGui::GetIO().MouseWheel != 0.0f) {
				ImGui::SetScrollY(scrollY - ImGui::GetIO().MouseWheel * rowHeight * 3.0f);
			}
			RenderTrackList(scene, m_TrackListWidth, rowHeight, scrollY);
			RenderDopesheet(ImVec2(m_TimelineX0, ImGui::GetWindowPos().y), m_TimelineWidth, bodyHeight, rowHeight, scrollY);
			ImGui::SetCursorPos(ImVec2(0.0f, static_cast<float>(m_Rows.size()) * rowHeight + rowHeight));
			ImGui::Dummy(ImVec2(1.0f, 1.0f));
			ImGui::EndChild();
		}
		else {
			ImGui::BeginChild("##Rows", ImVec2(m_TrackListWidth, bodyHeight));
			RenderTrackList(scene, m_TrackListWidth, rowHeight, ImGui::GetScrollY());
			ImGui::SetCursorPos(ImVec2(0.0f, static_cast<float>(m_Rows.size()) * rowHeight + rowHeight));
			ImGui::Dummy(ImVec2(1.0f, 1.0f));
			ImGui::EndChild();
			ImGui::SetCursorScreenPos(ImVec2(m_TimelineX0, bodyTop));
			RenderCurves(ImVec2(m_TimelineX0, bodyTop), m_TimelineWidth, bodyHeight);
		}

		// Splitter between track list and timeline.
		{
			ImGui::SetCursorScreenPos(ImVec2(origin.x + m_TrackListWidth - 3.0f, origin.y));
			ImGui::InvisibleButton("##Splitter", ImVec2(6.0f, rulerHeight + eventHeight + bodyHeight));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			if (ImGui::IsItemActive()) m_TrackListWidth += ImGui::GetIO().MouseDelta.x;
			ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x + m_TrackListWidth, origin.y),
				ImVec2(origin.x + m_TrackListWidth, bodyTop + bodyHeight), ImGui::GetColorU32(ImGuiCol_Separator));
		}

		// Status line.
		ImGui::SetCursorScreenPos(ImVec2(origin.x, bodyTop + bodyHeight + 2.0f));
		if (!m_Errors.empty()) {
			EditorUI::StatusText(EditorUI::Tone::Warning, (std::to_string(m_Errors.size()) + (m_Errors.size() == 1 ? " track can't be applied" : " tracks can't be applied") + " - see the highlighted rows").c_str());
		}
		else if (m_Recording) {
			EditorUI::StatusText(EditorUI::Tone::Accent, "Recording - changes below this Animator are keyed at the current time");
		}
		else if (!m_PreviewAllowed) {
			ImGui::TextDisabled("Preview needs the selected Animator to use this clip");
		}
		else if (m_Clip.Tracks.empty()) {
			ImGui::TextDisabled("Add a property or turn on Record and change the entity to create keys");
		}
		else {
			ImGui::TextDisabled("%s  -  %zu tracks, %zu events", m_ClipName.c_str(), m_Clip.Tracks.size(), m_Clip.Events.size());
		}

		RenderKeyContextMenu();
		RenderEventPopup();
	}

	void AnimationWindow::RenderTrackList(Scene* scene, float width, float rowHeight, float scrollY) {
		(void)scrollY;
		const ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const float valueWidth = std::min(ImGui::GetFontSize() * 6.0f, width * 0.4f);
		int removeTrack = -1;
		std::string removeGroupPath, removeGroupComponent;
		bool removeGroup = false;

		for (size_t r = 0; r < m_Rows.size(); ++r) {
			const Row& row = m_Rows[r];
			const float y = static_cast<float>(r) * rowHeight;
			ImGui::SetCursorPos(ImVec2(0.0f, y));
			ImGui::PushID(static_cast<int>(r));
			if (row.Track < 0) {
				const ImVec2 min = ImGui::GetCursorScreenPos();
				draw->AddRectFilled(min, ImVec2(min.x + width, min.y + rowHeight), ImGui::GetColorU32(ImGuiCol_FrameBg, 0.55f));
				ImGui::SetCursorPos(ImVec2(style.FramePadding.x, y));
				ImGui::AlignTextToFramePadding();
				ImGuiUtils::TextEllipsis(row.Label.c_str(), width - style.FramePadding.x * 2.0f);
				ImGui::SetCursorPos(ImVec2(0.0f, y));
				ImGui::InvisibleButton("##group", ImVec2(width, rowHeight));
				if (ImGui::BeginPopupContextItem("##GroupMenu")) {
					if (ImGui::MenuItem("Remove Tracks")) {
						removeGroup = true;
						removeGroupPath = row.Path;
						removeGroupComponent = row.Component;
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
				continue;
			}

			AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(row.Track)];
			const std::string error = TrackError(track);
			const bool selected = IsTrackSelected(row.Track);
			const ImVec2 rowMin = ImGui::GetCursorScreenPos();
			if (selected) draw->AddRectFilled(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight), ImGui::GetColorU32(EditorTheme::Colors::RowSelection()));

			ImGui::SetCursorPos(ImVec2(0.0f, y));
			if (ImGui::InvisibleButton("##track", ImVec2(width - valueWidth - style.ItemSpacing.x, rowHeight))) {
				if (ImGui::GetIO().KeyCtrl) {
					if (selected) std::erase(m_SelectedTracks, row.Track);
					else m_SelectedTracks.push_back(row.Track);
				}
				else {
					m_SelectedTracks = { row.Track };
				}
			}
			if (!error.empty()) EditorUI::Tooltip(error);
			if (ImGui::BeginPopupContextItem("##TrackMenu")) {
				if (ImGui::MenuItem("Add Key", "K")) AddKeysAtTime({ row.Track });
				if (ImGui::MenuItem("Remove Track")) removeTrack = row.Track;
				ImGui::EndPopup();
			}
			draw->AddCircleFilled(ImVec2(rowMin.x + style.FramePadding.x * 2.0f + 3.0f, rowMin.y + rowHeight * 0.5f), 3.0f,
				track.IsDiscrete() ? DimColor() : ChannelColor(track.Property));
			const ImVec4 labelColor = error.empty() ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : EditorTheme::Colors::Warning();
			const float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
			draw->PushClipRect(rowMin, ImVec2(rowMin.x + width - valueWidth - style.ItemSpacing.x, rowMin.y + rowHeight), true);
			draw->AddText(ImVec2(rowMin.x + style.FramePadding.x * 2.0f + 12.0f, textY), ImGui::GetColorU32(labelColor), row.Label.c_str());
			draw->PopClipRect();

			// Value at the playhead; editing it keys the property there.
			ImGui::SetCursorPos(ImVec2(width - valueWidth - style.ItemSpacing.x * 0.5f, y));
			ImGui::SetNextItemWidth(valueWidth);
			const float keyTime = SnapTime(m_Time);
			if (track.IsDiscrete()) {
				std::string value = track.GetDiscreteValue(m_Time);
				char buffer[256];
				std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
				ImGui::InputText("##value", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::IsItemDeactivatedAfterEdit() && value != buffer) {
					BeginEdit();
					SetDiscreteKey(track, keyTime, buffer);
					CommitEdit();
				}
			}
			else {
				float value = track.Curve.Evaluate(m_Time);
				const bool changed = ImGui::DragFloat("##value", &value, 0.01f, 0.0f, 0.0f, "%.3f");
				if (ImGui::IsItemActivated()) BeginEdit();
				if (changed) {
					SetCurveKey(track, keyTime, value, PropertyType::Float);
					LiveEdit();
				}
				if (ImGui::IsItemDeactivatedAfterEdit()) CommitEdit();
			}
			ImGui::PopID();
		}

		if (removeTrack >= 0 || removeGroup) {
			BeginEdit();
			if (removeTrack >= 0) {
				m_Clip.Tracks.erase(m_Clip.Tracks.begin() + removeTrack);
			}
			else {
				std::erase_if(m_Clip.Tracks, [&](const AnimationTrack& t) { return t.Path == removeGroupPath && t.Component == removeGroupComponent; });
			}
			m_SelectedKeys.clear();
			m_SelectedTracks.clear();
			CommitEdit();
		}
		(void)scene;
	}

	void AnimationWindow::RenderAddPropertyPopup(Scene* scene) {
		ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 18.0f, 0.0f), ImVec2(ImGui::GetFontSize() * 30.0f, ImGui::GetFontSize() * 30.0f));
		if (!ImGui::BeginPopup("##AddProperty")) return;
		if (!scene || !scene->GetRegistry().valid(m_Root)) {
			ImGui::TextDisabled("No Animator selected");
			ImGui::EndPopup();
			return;
		}

		auto hasTrack = [this](const std::string& path, const std::string& component, const std::string& property) {
			for (const AnimationTrack& t : m_Clip.Tracks) {
				if (t.Path == path && t.Component == component && ParseAnimationProperty(t.Property).Base == property) return true;
			}
			return false;
		};
		auto addTracks = [&](const std::string& path, const std::string& component, const std::string& property, PropertyType type, const PropertyValue& value) {
			BeginEdit();
			if (IsCurveAnimatable(type)) {
				const int channels = AnimationChannelCount(type);
				for (int c = 0; c < channels; ++c) {
					const std::string name = channels > 1 ? property + "." + AnimationChannelSuffix(type, c) : property;
					AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(FindOrAddTrack(path, component, name))];
					const float v = ReadAnimationChannel(value, c);
					SetCurveKey(track, 0.0f, v, type);
					if (m_Clip.Length > 0.0f) SetCurveKey(track, m_Clip.Length, v, type);
				}
			}
			else {
				AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(FindOrAddTrack(path, component, property))];
				SetDiscreteKey(track, 0.0f, value.ToString());
			}
			CommitEdit();
			ImGui::CloseCurrentPopup();
		};

		const std::vector<AnimatableProperty> properties = CollectAnimatableProperties(*scene, m_Root);
		std::map<std::string, std::vector<const AnimatableProperty*>> byPath;
		std::vector<std::string> pathOrder;
		for (const AnimatableProperty& p : properties) {
			if (!byPath.contains(p.Path)) pathOrder.push_back(p.Path);
			byPath[p.Path].push_back(&p);
		}

		for (const std::string& path : pathOrder) {
			const std::string label = path.empty() ? m_RootName : path;
			ImGui::SetNextItemOpen(path.empty(), ImGuiCond_Appearing);
			if (!ImGui::TreeNode(label.c_str())) continue;
			const ComponentInfo* lastInfo = nullptr;
			bool componentOpen = false;
			for (const AnimatableProperty* p : byPath[path]) {
				if (p->Info != lastInfo) {
					if (componentOpen) ImGui::TreePop();
					lastInfo = p->Info;
					componentOpen = ImGui::TreeNode(p->Info->displayName.c_str());
				}
				if (!componentOpen) continue;
				const bool exists = hasTrack(path, p->Info->serializedName, p->Descriptor->Name);
				ImGui::BeginDisabled(exists);
				const std::string display = p->Descriptor->DisplayName.empty() ? p->Descriptor->Name : p->Descriptor->DisplayName;
				if (ImGui::Selectable(display.c_str())) {
					Entity entity = scene->GetEntity(p->EntityId);
					addTracks(path, p->Info->serializedName, p->Descriptor->Name, p->Descriptor->Type, p->Descriptor->Get(entity));
				}
				ImGui::EndDisabled();
			}
			if (componentOpen) ImGui::TreePop();

			// C# script fields of this entity.
			Entity target = ResolveAnimationPath(scene->GetEntity(m_Root), path);
			if (target.IsValid()) {
				std::string lastClass;
				bool classOpen = false;
				for (const ScriptField& field : CollectScriptFields(*scene, target.GetHandle())) {
					if (field.Class != lastClass) {
						if (classOpen) ImGui::TreePop();
						lastClass = field.Class;
						classOpen = ImGui::TreeNode((field.Class + " (script)").c_str());
					}
					if (!classOpen) continue;
					const bool exists = hasTrack(path, field.Class, field.Name);
					ImGui::BeginDisabled(exists);
					if (ImGui::Selectable(field.Name.c_str())) {
						PropertyValue value = PropertyValue::FromString(field.Type, field.Value);
						value.Type = field.Type;
						addTracks(path, field.Class, field.Name, field.Type, value);
					}
					ImGui::EndDisabled();
				}
				if (classOpen) ImGui::TreePop();
			}
			ImGui::TreePop();
		}
		ImGui::EndPopup();
	}

	void AnimationWindow::RenderRuler(const ImVec2& origin, float width, float height) {
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 max(origin.x + width, origin.y + height);
		draw->AddRectFilled(origin, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
		draw->PushClipRect(origin, max, true);

		// Past the clip end is dimmed.
		const float endX = TimeToX(m_Clip.Length);
		if (endX < max.x) draw->AddRectFilled(ImVec2(std::max(endX, origin.x), origin.y), max, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.5f));

		const float major = NiceStep(m_PixelsPerSecond, ImGui::GetFontSize() * 4.0f);
		const float minor = major / 5.0f;
		const float tStart = std::floor(XToTime(origin.x) / minor) * minor;
		const float tEnd = XToTime(max.x);
		for (float t = tStart; t <= tEnd; t += minor) {
			const float x = TimeToX(t);
			const bool isMajor = std::abs(std::remainder(t, major)) < minor * 0.25f;
			draw->AddLine(ImVec2(x, max.y - (isMajor ? height * 0.45f : height * 0.2f)), ImVec2(x, max.y), DimColor(isMajor ? 0.9f : 0.5f));
			if (isMajor && t >= -0.0001f) {
				char label[32];
				std::snprintf(label, sizeof(label), major < 0.1f ? "%.2f" : (major < 1.0f ? "%.1f" : "%.0f"), std::abs(t) < 0.0001f ? 0.0f : t);
				draw->AddText(ImVec2(x + 3.0f, origin.y + 1.0f), DimColor(), label);
			}
		}
		const float playX = TimeToX(m_Time);
		const ImU32 accent = ImGui::GetColorU32(EditorTheme::Colors::Accent());
		draw->AddLine(ImVec2(playX, origin.y), ImVec2(playX, max.y), accent, 2.0f);
		draw->AddTriangleFilled(ImVec2(playX - 5.0f, origin.y), ImVec2(playX + 5.0f, origin.y), ImVec2(playX, origin.y + 6.0f), accent);
		draw->PopClipRect();

		ImGui::SetCursorScreenPos(origin);
		ImGui::InvisibleButton("##Ruler", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
		if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			m_Time = SnapTime(XToTime(ImGui::GetIO().MousePos.x));
			m_Playing = false;
		}
		if (ImGui::IsItemHovered()) {
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f) {
				const float anchor = XToTime(ImGui::GetIO().MousePos.x);
				m_PixelsPerSecond = std::clamp(m_PixelsPerSecond * std::pow(1.15f, wheel), k_MinPixelsPerSecond, k_MaxPixelsPerSecond);
				m_ViewStart = anchor - (ImGui::GetIO().MousePos.x - m_TimelineX0) / m_PixelsPerSecond;
			}
		}
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) m_ViewStart -= ImGui::GetIO().MouseDelta.x / m_PixelsPerSecond;
	}

	void AnimationWindow::RenderEventRow(const ImVec2& origin, float width, float height) {
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 max(origin.x + width, origin.y + height);
		draw->AddRectFilled(origin, max, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.45f));
		draw->PushClipRect(origin, max, true);
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		int hovered = -1;
		for (size_t i = 0; i < m_Clip.Events.size(); ++i) {
			const float x = TimeToX(m_Clip.Events[i].Time);
			const bool selected = static_cast<int>(i) == m_SelectedEvent;
			const ImU32 color = selected ? ImGui::GetColorU32(EditorTheme::Colors::Accent()) : TextColor(0.8f);
			const float w = height * 0.28f;
			draw->AddRectFilled(ImVec2(x - w, origin.y + 3.0f), ImVec2(x + w, max.y - height * 0.35f), color, 2.0f);
			draw->AddTriangleFilled(ImVec2(x - w, max.y - height * 0.35f), ImVec2(x + w, max.y - height * 0.35f), ImVec2(x, max.y - 2.0f), color);
			if (std::abs(mouse.x - x) <= w + 2.0f && mouse.y >= origin.y && mouse.y <= max.y) hovered = static_cast<int>(i);
		}
		const float playX = TimeToX(m_Time);
		draw->AddLine(ImVec2(playX, origin.y), ImVec2(playX, max.y), ImGui::GetColorU32(EditorTheme::Colors::Accent()), 1.0f);
		draw->PopClipRect();

		ImGui::SetCursorScreenPos(origin);
		ImGui::InvisibleButton("##Events", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		if (hovered >= 0) {
			const AnimationEvent& e = m_Clip.Events[static_cast<size_t>(hovered)];
			EditorUI::Tooltip(e.Argument.empty() ? e.Function + "()" : e.Function + "(\"" + e.Argument + "\")");
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
			m_SelectedEvent = hovered;
			if (hovered >= 0) {
				m_SelectedKeys.clear();
				m_Drag = DragKind::Event;
				m_DragEditStarted = false;
				m_DragTimeOffset = m_Clip.Events[static_cast<size_t>(hovered)].Time - XToTime(mouse.x);
			}
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			if (hovered < 0) {
				BeginEdit();
				AnimationEvent e;
				e.Time = SnapTime(XToTime(mouse.x));
				e.Function = "OnAnimationEvent";
				const auto it = std::upper_bound(m_Clip.Events.begin(), m_Clip.Events.end(), e.Time,
					[](float t, const AnimationEvent& ev) { return t < ev.Time; });
				m_SelectedEvent = static_cast<int>(m_Clip.Events.insert(it, e) - m_Clip.Events.begin());
				CommitEdit();
			}
			m_OpenEventPopup = m_SelectedEvent >= 0;
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			if (hovered >= 0) {
				m_SelectedEvent = hovered;
				m_OpenEventPopup = true;
			}
			else {
				m_PendingEventTime = SnapTime(XToTime(mouse.x));
				ImGui::OpenPopup("##EventRowMenu");
			}
		}
		if (ImGui::BeginPopup("##EventRowMenu")) {
			if (ImGui::MenuItem("Add Event")) {
				BeginEdit();
				AnimationEvent e;
				e.Time = std::max(m_PendingEventTime, 0.0f);
				e.Function = "OnAnimationEvent";
				const auto it = std::upper_bound(m_Clip.Events.begin(), m_Clip.Events.end(), e.Time,
					[](float t, const AnimationEvent& ev) { return t < ev.Time; });
				m_SelectedEvent = static_cast<int>(m_Clip.Events.insert(it, e) - m_Clip.Events.begin());
				CommitEdit();
				m_OpenEventPopup = true;
			}
			ImGui::EndPopup();
		}

		if (m_Drag == DragKind::Event) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_SelectedEvent >= 0 && m_SelectedEvent < static_cast<int>(m_Clip.Events.size())) {
				const float t = SnapTime(XToTime(mouse.x) + m_DragTimeOffset);
				AnimationEvent& e = m_Clip.Events[static_cast<size_t>(m_SelectedEvent)];
				if (std::abs(e.Time - t) > 0.00001f) {
					if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
					e.Time = t;
					LiveEdit();
				}
			}
			else {
				if (m_DragEditStarted && m_SelectedEvent >= 0 && m_SelectedEvent < static_cast<int>(m_Clip.Events.size())) {
					const AnimationEvent moved = m_Clip.Events[static_cast<size_t>(m_SelectedEvent)];
					std::stable_sort(m_Clip.Events.begin(), m_Clip.Events.end(), [](const AnimationEvent& a, const AnimationEvent& b) { return a.Time < b.Time; });
					const auto it = std::find(m_Clip.Events.begin(), m_Clip.Events.end(), moved);
					m_SelectedEvent = it != m_Clip.Events.end() ? static_cast<int>(it - m_Clip.Events.begin()) : -1;
					CommitEdit();
				}
				m_Drag = DragKind::None;
				m_DragEditStarted = false;
			}
		}
	}

	void AnimationWindow::RenderEventPopup() {
		if (m_OpenEventPopup) {
			ImGui::OpenPopup("Animation Event");
			m_OpenEventPopup = false;
		}
		if (!ImGui::BeginPopup("Animation Event")) return;
		if (m_SelectedEvent < 0 || m_SelectedEvent >= static_cast<int>(m_Clip.Events.size())) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		AnimationEvent& e = m_Clip.Events[static_cast<size_t>(m_SelectedEvent)];
		ImGui::TextDisabled("Calls a public method on the scripts of the Animator's entity.");
		char function[128];
		char argument[256];
		std::snprintf(function, sizeof(function), "%s", e.Function.c_str());
		std::snprintf(argument, sizeof(argument), "%s", e.Argument.c_str());
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
		ImGui::InputText("Function", function, sizeof(function));
		const bool functionDone = ImGui::IsItemDeactivatedAfterEdit();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
		ImGui::InputText("Argument", argument, sizeof(argument));
		const bool argumentDone = ImGui::IsItemDeactivatedAfterEdit();
		if ((functionDone || argumentDone) && (e.Function != function || e.Argument != argument)) {
			BeginEdit();
			m_Clip.Events[static_cast<size_t>(m_SelectedEvent)].Function = function;
			m_Clip.Events[static_cast<size_t>(m_SelectedEvent)].Argument = argument;
			CommitEdit();
		}
		ImGui::TextDisabled("At %.2f s", e.Time);
		if (EditorUI::Button("Delete Event", EditorUI::ButtonVariant::Danger)) {
			BeginEdit();
			m_Clip.Events.erase(m_Clip.Events.begin() + m_SelectedEvent);
			m_SelectedEvent = -1;
			CommitEdit();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void AnimationWindow::RenderDopesheet(const ImVec2& origin, float width, float height, float rowHeight, float scrollY) {
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 clipMin(origin.x, origin.y);
		const ImVec2 clipMax(origin.x + width, origin.y + height);
		draw->PushClipRect(clipMin, clipMax, true);
		const float contentTop = origin.y - scrollY;
		const ImVec2 mouse = ImGui::GetIO().MousePos;

		// Grid and clip end.
		const float major = NiceStep(m_PixelsPerSecond, ImGui::GetFontSize() * 4.0f);
		for (float t = std::floor(XToTime(clipMin.x) / major) * major; t <= XToTime(clipMax.x); t += major) {
			draw->AddLine(ImVec2(TimeToX(t), clipMin.y), ImVec2(TimeToX(t), clipMax.y), ImGui::GetColorU32(ImGuiCol_Separator, 0.5f));
		}
		const float endX = TimeToX(m_Clip.Length);
		if (endX < clipMax.x) draw->AddRectFilled(ImVec2(std::max(endX, clipMin.x), clipMin.y), clipMax, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.45f));

		const float radius = std::max(rowHeight * 0.26f, 4.0f);
		KeyRef hoveredKey;
		int hoveredRow = -1;
		for (size_t r = 0; r < m_Rows.size(); ++r) {
			const Row& row = m_Rows[r];
			const float y0 = contentTop + static_cast<float>(r) * rowHeight;
			const float yc = y0 + rowHeight * 0.5f;
			if (y0 > clipMax.y || y0 + rowHeight < clipMin.y) continue;
			if (mouse.y >= y0 && mouse.y < y0 + rowHeight) hoveredRow = static_cast<int>(r);
			if (row.Track < 0) {
				draw->AddRectFilled(ImVec2(clipMin.x, y0), ImVec2(clipMax.x, y0 + rowHeight), ImGui::GetColorU32(ImGuiCol_FrameBg, 0.35f));
				// Group summary: one marker per key time of its tracks.
				std::vector<float> times;
				for (size_t rr = r + 1; rr < m_Rows.size() && m_Rows[rr].Track >= 0; ++rr) {
					const AnimationTrack& t = m_Clip.Tracks[static_cast<size_t>(m_Rows[rr].Track)];
					for (const Keyframe& k : t.Curve.Keys) times.push_back(k.Time);
					for (const DiscreteKeyframe& k : t.DiscreteKeys) times.push_back(k.Time);
				}
				std::sort(times.begin(), times.end());
				times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return std::abs(a - b) < 0.0005f; }), times.end());
				for (float t : times) DrawDiamond(draw, ImVec2(TimeToX(t), yc), radius * 0.8f, DimColor(), DimColor());
				continue;
			}
			if (IsTrackSelected(row.Track)) draw->AddRectFilled(ImVec2(clipMin.x, y0), ImVec2(clipMax.x, y0 + rowHeight), WithAlpha(EditorTheme::Colors::RowSelection(), 0.6f));
			const AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(row.Track)];
			const int keyCount = track.IsDiscrete() ? static_cast<int>(track.DiscreteKeys.size()) : static_cast<int>(track.Curve.Keys.size());
			for (int k = 0; k < keyCount; ++k) {
				const float t = track.IsDiscrete() ? track.DiscreteKeys[static_cast<size_t>(k)].Time : track.Curve.Keys[static_cast<size_t>(k)].Time;
				const ImVec2 center(TimeToX(t), yc);
				const bool selected = IsKeySelected(row.Track, k);
				const ImU32 fill = selected ? ImGui::GetColorU32(EditorTheme::Colors::Accent()) : TextColor(0.85f);
				if (track.IsDiscrete()) {
					draw->AddRectFilled(ImVec2(center.x - radius * 0.75f, center.y - radius * 0.75f), ImVec2(center.x + radius * 0.75f, center.y + radius * 0.75f), fill, 1.0f);
				}
				else {
					DrawDiamond(draw, center, radius, fill, ImGui::GetColorU32(ImGuiCol_WindowBg));
				}
				if (std::abs(mouse.x - center.x) <= radius + 1.0f && std::abs(mouse.y - center.y) <= radius + 1.0f) hoveredKey = { row.Track, k };
			}
		}

		const float playX = TimeToX(m_Time);
		draw->AddLine(ImVec2(playX, clipMin.y), ImVec2(playX, clipMax.y), ImGui::GetColorU32(EditorTheme::Colors::Accent()), 2.0f);
		if (m_Drag == DragKind::Box) {
			const ImVec2 a(std::min(m_DragStart.x, mouse.x), std::min(m_DragStart.y, mouse.y));
			const ImVec2 b(std::max(m_DragStart.x, mouse.x), std::max(m_DragStart.y, mouse.y));
			draw->AddRectFilled(a, b, WithAlpha(EditorTheme::Colors::Accent(), 0.15f));
			draw->AddRect(a, b, WithAlpha(EditorTheme::Colors::Accent(), 0.8f));
		}
		draw->PopClipRect();

		const bool hovered = ImGui::IsWindowHovered() && mouse.x >= clipMin.x && mouse.x < clipMax.x && mouse.y >= clipMin.y && mouse.y < clipMax.y;
		ImGuiIO& io = ImGui::GetIO();
		if (hovered) {
			if (io.KeyCtrl && io.MouseWheel != 0.0f) {
				const float anchor = XToTime(mouse.x);
				m_PixelsPerSecond = std::clamp(m_PixelsPerSecond * std::pow(1.15f, io.MouseWheel), k_MinPixelsPerSecond, k_MaxPixelsPerSecond);
				m_ViewStart = anchor - (mouse.x - m_TimelineX0) / m_PixelsPerSecond;
			}
			else if (io.KeyShift && io.MouseWheel != 0.0f) {
				m_ViewStart -= io.MouseWheel * 60.0f / m_PixelsPerSecond;
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) m_Drag = DragKind::Pan;

			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				if (hoveredKey.Track >= 0) {
					SelectKey(hoveredKey.Track, hoveredKey.Key, io.KeyCtrl);
					m_SelectedEvent = -1;
					if (IsKeySelected(hoveredKey.Track, hoveredKey.Key)) {
						m_Drag = DragKind::Keys;
						m_DragEditStarted = false;
						m_DragStart = mouse;
						m_DragOriginTimes.clear();
						for (const KeyRef& ref : m_SelectedKeys) {
							const AnimationTrack& t = m_Clip.Tracks[static_cast<size_t>(ref.Track)];
							m_DragOriginTimes.push_back(t.IsDiscrete() ? t.DiscreteKeys[static_cast<size_t>(ref.Key)].Time : t.Curve.Keys[static_cast<size_t>(ref.Key)].Time);
						}
					}
				}
				else {
					if (!io.KeyCtrl) m_SelectedKeys.clear();
					m_Drag = DragKind::Box;
					m_DragStart = mouse;
				}
			}
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoveredKey.Track < 0 && hoveredRow >= 0 && m_Rows[static_cast<size_t>(hoveredRow)].Track >= 0) {
				const int trackIndex = m_Rows[static_cast<size_t>(hoveredRow)].Track;
				AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(trackIndex)];
				const float t = SnapTime(XToTime(mouse.x));
				BeginEdit();
				if (track.IsDiscrete()) SetDiscreteKey(track, t, track.GetDiscreteValue(t));
				else SetCurveKey(track, t, track.Curve.Evaluate(t), PropertyType::Float);
				CommitEdit();
				m_Drag = DragKind::None;
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoveredKey.Track >= 0) {
				SelectKey(hoveredKey.Track, hoveredKey.Key, false);
				m_ContextKey = hoveredKey;
				m_OpenKeyContext = true;
			}
		}

		switch (m_Drag) {
		case DragKind::Pan:
			if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_ViewStart -= io.MouseDelta.x / m_PixelsPerSecond;
			else m_Drag = DragKind::None;
			break;
		case DragKind::Keys:
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				float delta = XToTime(mouse.x) - XToTime(m_DragStart.x);
				if (!io.KeyShift) delta = std::round(delta / FrameStep()) * FrameStep();
				if (std::abs(mouse.x - m_DragStart.x) < 2.0f && !m_DragEditStarted) break;
				if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
				for (size_t i = 0; i < m_SelectedKeys.size() && i < m_DragOriginTimes.size(); ++i) {
					AnimationTrack& t = m_Clip.Tracks[static_cast<size_t>(m_SelectedKeys[i].Track)];
					const float time = std::max(m_DragOriginTimes[i] + delta, 0.0f);
					if (t.IsDiscrete()) t.DiscreteKeys[static_cast<size_t>(m_SelectedKeys[i].Key)].Time = time;
					else t.Curve.Keys[static_cast<size_t>(m_SelectedKeys[i].Key)].Time = time;
				}
				LiveEdit();
			}
			else {
				if (m_DragEditStarted) {
					std::vector<int> tracks;
					for (const KeyRef& ref : m_SelectedKeys) if (std::find(tracks.begin(), tracks.end(), ref.Track) == tracks.end()) tracks.push_back(ref.Track);
					for (int t : tracks) SortTrackAndRemap(t);
					CommitEdit();
				}
				m_Drag = DragKind::None;
				m_DragEditStarted = false;
			}
			break;
		case DragKind::Box:
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				const ImVec2 a(std::min(m_DragStart.x, mouse.x), std::min(m_DragStart.y, mouse.y));
				const ImVec2 b(std::max(m_DragStart.x, mouse.x), std::max(m_DragStart.y, mouse.y));
				if (b.x - a.x > 2.0f || b.y - a.y > 2.0f) {
					for (size_t r = 0; r < m_Rows.size(); ++r) {
						const Row& row = m_Rows[r];
						if (row.Track < 0) continue;
						const float yc = contentTop + (static_cast<float>(r) + 0.5f) * rowHeight;
						if (yc < a.y || yc > b.y) continue;
						const AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(row.Track)];
						const int keyCount = track.IsDiscrete() ? static_cast<int>(track.DiscreteKeys.size()) : static_cast<int>(track.Curve.Keys.size());
						for (int k = 0; k < keyCount; ++k) {
							const float x = TimeToX(track.IsDiscrete() ? track.DiscreteKeys[static_cast<size_t>(k)].Time : track.Curve.Keys[static_cast<size_t>(k)].Time);
							if (x >= a.x && x <= b.x && !IsKeySelected(row.Track, k)) m_SelectedKeys.push_back({ row.Track, k });
						}
					}
				}
				else if (!io.KeyCtrl) {
					// A plain click on empty timeline moves the playhead.
					m_Time = SnapTime(XToTime(mouse.x));
				}
				m_Drag = DragKind::None;
			}
			break;
		default:
			break;
		}
	}

	void AnimationWindow::RenderCurves(const ImVec2& origin, float width, float height) {
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 max(origin.x + width, origin.y + height);
		ImGui::InvisibleButton("##CurveCanvas", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();
		draw->AddRectFilled(origin, max, ImGui::GetColorU32(ImGuiCol_ChildBg));
		draw->PushClipRect(origin, max, true);
		const float valueSpan = std::max(m_ValueMax - m_ValueMin, 0.0001f);
		auto valueToY = [&](float v) { return max.y - (v - m_ValueMin) / valueSpan * height; };
		auto yToValue = [&](float y) { return m_ValueMin + (max.y - y) / height * valueSpan; };
		const float pixelsPerValue = height / valueSpan;

		// Grid.
		const float timeStep = NiceStep(m_PixelsPerSecond, ImGui::GetFontSize() * 4.0f);
		for (float t = std::floor(XToTime(origin.x) / timeStep) * timeStep; t <= XToTime(max.x); t += timeStep) {
			draw->AddLine(ImVec2(TimeToX(t), origin.y), ImVec2(TimeToX(t), max.y), ImGui::GetColorU32(ImGuiCol_Separator, 0.5f));
		}
		const float valueStep = NiceStep(pixelsPerValue, ImGui::GetFontSize() * 2.5f);
		for (float v = std::floor(m_ValueMin / valueStep) * valueStep; v <= m_ValueMax; v += valueStep) {
			const float y = valueToY(v);
			draw->AddLine(ImVec2(origin.x, y), ImVec2(max.x, y), ImGui::GetColorU32(ImGuiCol_Separator, std::abs(v) < valueStep * 0.01f ? 1.0f : 0.5f));
			char label[32];
			std::snprintf(label, sizeof(label), "%g", std::abs(v) < valueStep * 0.01f ? 0.0f : v);
			draw->AddText(ImVec2(origin.x + 4.0f, y - ImGui::GetTextLineHeight()), DimColor(), label);
		}
		const float endX = TimeToX(m_Clip.Length);
		if (endX < max.x) draw->AddRectFilled(ImVec2(std::max(endX, origin.x), origin.y), max, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.45f));

		std::vector<int> visible;
		for (size_t i = 0; i < m_Clip.Tracks.size(); ++i) {
			if (m_Clip.Tracks[i].IsDiscrete()) continue;
			if (!m_SelectedTracks.empty() && !IsTrackSelected(static_cast<int>(i))) continue;
			visible.push_back(static_cast<int>(i));
		}

		const ImVec2 mouse = ImGui::GetIO().MousePos;
		KeyRef hoveredKey;
		int hoveredTangent = 0;
		const float handleLength = ImGui::GetFontSize() * 3.0f;
		auto tangentScreen = [&](float slope, float sign) {
			ImVec2 dir(m_PixelsPerSecond * sign, -slope * pixelsPerValue * sign);
			const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
			return len > 0.0f ? ImVec2(dir.x / len * handleLength, dir.y / len * handleLength) : ImVec2(handleLength * sign, 0.0f);
		};

		for (int trackIndex : visible) {
			const AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(trackIndex)];
			const ImU32 color = ChannelColor(track.Property);
			std::vector<ImVec2> points;
			for (float x = origin.x; x <= max.x; x += 2.0f) points.push_back(ImVec2(x, valueToY(track.Curve.Evaluate(XToTime(x)))));
			if (points.size() > 1) draw->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, 1.6f);

			for (size_t k = 0; k < track.Curve.Keys.size(); ++k) {
				const Keyframe& key = track.Curve.Keys[k];
				const ImVec2 p(TimeToX(key.Time), valueToY(key.Value));
				const bool selected = IsKeySelected(trackIndex, static_cast<int>(k));
				draw->AddCircleFilled(p, selected ? 5.0f : 4.0f, selected ? ImGui::GetColorU32(EditorTheme::Colors::Accent()) : color);
				draw->AddCircle(p, selected ? 5.0f : 4.0f, ImGui::GetColorU32(ImGuiCol_WindowBg));
				if (std::abs(mouse.x - p.x) <= 6.0f && std::abs(mouse.y - p.y) <= 6.0f) hoveredKey = { trackIndex, static_cast<int>(k) };

				// Tangent handles of a selected bezier key.
				const bool outBezier = key.Interpolation == KeyInterpolation::Bezier && k + 1 < track.Curve.Keys.size();
				const bool inBezier = k > 0 && track.Curve.Keys[k - 1].Interpolation == KeyInterpolation::Bezier;
				if (!selected || m_SelectedKeys.size() != 1) continue;
				const ImU32 handleColor = ImGui::GetColorU32(EditorTheme::Colors::Warning());
				if (outBezier) {
					const ImVec2 d = tangentScreen(key.OutTangent, 1.0f);
					const ImVec2 h(p.x + d.x, p.y + d.y);
					draw->AddLine(p, h, handleColor);
					draw->AddCircleFilled(h, 3.5f, handleColor);
					if (std::abs(mouse.x - h.x) <= 5.0f && std::abs(mouse.y - h.y) <= 5.0f) hoveredTangent = 2;
				}
				if (inBezier) {
					const ImVec2 d = tangentScreen(key.InTangent, -1.0f);
					const ImVec2 h(p.x + d.x, p.y + d.y);
					draw->AddLine(p, h, handleColor);
					draw->AddCircleFilled(h, 3.5f, handleColor);
					if (std::abs(mouse.x - h.x) <= 5.0f && std::abs(mouse.y - h.y) <= 5.0f) hoveredTangent = 1;
				}
			}
		}
		const float playX = TimeToX(m_Time);
		draw->AddLine(ImVec2(playX, origin.y), ImVec2(playX, max.y), ImGui::GetColorU32(EditorTheme::Colors::Accent()), 2.0f);
		if (visible.empty()) {
			const char* hint = m_Clip.Tracks.empty() ? "No curves yet" : "Select tracks to show their curves";
			const ImVec2 size = ImGui::CalcTextSize(hint);
			draw->AddText(ImVec2(origin.x + (width - size.x) * 0.5f, origin.y + (height - size.y) * 0.5f), DimColor(), hint);
		}
		draw->PopClipRect();

		ImGuiIO& io = ImGui::GetIO();
		if (hovered) {
			if (io.MouseWheel != 0.0f) {
				if (io.KeyShift) {
					const float anchor = yToValue(mouse.y);
					const float scale = std::pow(1.15f, -io.MouseWheel);
					m_ValueMin = anchor - (anchor - m_ValueMin) * scale;
					m_ValueMax = anchor + (m_ValueMax - anchor) * scale;
				}
				else {
					const float anchor = XToTime(mouse.x);
					m_PixelsPerSecond = std::clamp(m_PixelsPerSecond * std::pow(1.15f, io.MouseWheel), k_MinPixelsPerSecond, k_MaxPixelsPerSecond);
					m_ViewStart = anchor - (mouse.x - m_TimelineX0) / m_PixelsPerSecond;
				}
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) m_Drag = DragKind::Pan;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				if (hoveredTangent != 0 && m_SelectedKeys.size() == 1) {
					m_Drag = hoveredTangent == 1 ? DragKind::InTangent : DragKind::OutTangent;
					m_DragKey = m_SelectedKeys.front();
					m_DragEditStarted = false;
				}
				else if (hoveredKey.Track >= 0) {
					SelectKey(hoveredKey.Track, hoveredKey.Key, io.KeyCtrl);
					if (IsKeySelected(hoveredKey.Track, hoveredKey.Key)) {
						m_Drag = DragKind::CurveKey;
						m_DragEditStarted = false;
						m_DragStart = mouse;
						m_DragOriginTimes.clear();
						m_DragOriginValues.clear();
						for (const KeyRef& ref : m_SelectedKeys) {
							const Keyframe& key = m_Clip.Tracks[static_cast<size_t>(ref.Track)].Curve.Keys[static_cast<size_t>(ref.Key)];
							m_DragOriginTimes.push_back(key.Time);
							m_DragOriginValues.push_back(key.Value);
						}
					}
				}
				else if (!io.KeyCtrl) {
					m_SelectedKeys.clear();
					m_Time = SnapTime(XToTime(mouse.x));
				}
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoveredKey.Track >= 0) {
				SelectKey(hoveredKey.Track, hoveredKey.Key, false);
				m_ContextKey = hoveredKey;
				m_OpenKeyContext = true;
			}
		}

		switch (m_Drag) {
		case DragKind::Pan:
			if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
				m_ViewStart -= io.MouseDelta.x / m_PixelsPerSecond;
				const float dv = io.MouseDelta.y / pixelsPerValue;
				m_ValueMin += dv;
				m_ValueMax += dv;
			}
			else {
				m_Drag = DragKind::None;
			}
			break;
		case DragKind::CurveKey:
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				if (!m_DragEditStarted && std::abs(mouse.x - m_DragStart.x) < 2.0f && std::abs(mouse.y - m_DragStart.y) < 2.0f) break;
				if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
				float dt = XToTime(mouse.x) - XToTime(m_DragStart.x);
				if (!io.KeyShift) dt = std::round(dt / FrameStep()) * FrameStep();
				const float dv = (m_DragStart.y - mouse.y) / pixelsPerValue;
				for (size_t i = 0; i < m_SelectedKeys.size() && i < m_DragOriginTimes.size(); ++i) {
					Keyframe& key = m_Clip.Tracks[static_cast<size_t>(m_SelectedKeys[i].Track)].Curve.Keys[static_cast<size_t>(m_SelectedKeys[i].Key)];
					key.Time = std::max(m_DragOriginTimes[i] + dt, 0.0f);
					key.Value = m_DragOriginValues[i] + dv;
				}
				LiveEdit();
			}
			else {
				if (m_DragEditStarted) {
					std::vector<int> tracks;
					for (const KeyRef& ref : m_SelectedKeys) if (std::find(tracks.begin(), tracks.end(), ref.Track) == tracks.end()) tracks.push_back(ref.Track);
					for (int t : tracks) SortTrackAndRemap(t);
					CommitEdit();
				}
				m_Drag = DragKind::None;
				m_DragEditStarted = false;
			}
			break;
		case DragKind::InTangent:
		case DragKind::OutTangent:
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				if (m_DragKey.Track < 0 || m_DragKey.Track >= static_cast<int>(m_Clip.Tracks.size())) { m_Drag = DragKind::None; break; }
				AnimationCurve& curve = m_Clip.Tracks[static_cast<size_t>(m_DragKey.Track)].Curve;
				if (m_DragKey.Key < 0 || m_DragKey.Key >= static_cast<int>(curve.Keys.size())) { m_Drag = DragKind::None; break; }
				Keyframe& key = curve.Keys[static_cast<size_t>(m_DragKey.Key)];
				const float kx = TimeToX(key.Time);
				const float ky = valueToY(key.Value);
				float dx = (mouse.x - kx) / m_PixelsPerSecond;
				const float dy = (ky - mouse.y) / pixelsPerValue;
				const bool out = m_Drag == DragKind::OutTangent;
				// The handle stays on its side of the key.
				dx = out ? std::max(dx, 0.0001f) : std::min(dx, -0.0001f);
				if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
				const float slope = dy / dx;
				if (out) key.OutTangent = slope;
				else key.InTangent = slope;
				// Ctrl breaks the tangent; otherwise both sides stay aligned.
				if (!io.KeyCtrl) key.InTangent = key.OutTangent = slope;
				LiveEdit();
			}
			else {
				if (m_DragEditStarted) CommitEdit();
				m_Drag = DragKind::None;
				m_DragEditStarted = false;
			}
			break;
		default:
			break;
		}
	}

	void AnimationWindow::RenderKeyContextMenu() {
		if (m_OpenKeyContext) {
			ImGui::OpenPopup("##KeyMenu");
			m_OpenKeyContext = false;
		}
		if (!ImGui::BeginPopup("##KeyMenu")) return;
		auto setInterpolation = [this](KeyInterpolation interpolation, bool smooth, bool flat) {
			BeginEdit();
			for (const KeyRef& ref : m_SelectedKeys) {
				AnimationTrack& track = m_Clip.Tracks[static_cast<size_t>(ref.Track)];
				if (track.IsDiscrete() || ref.Key >= static_cast<int>(track.Curve.Keys.size())) continue;
				Keyframe& key = track.Curve.Keys[static_cast<size_t>(ref.Key)];
				key.Interpolation = interpolation;
				if (smooth) track.Curve.SmoothTangents(ref.Key);
				if (flat) key.InTangent = key.OutTangent = 0.0f;
			}
			CommitEdit();
		};
		ImGui::TextDisabled(m_SelectedKeys.size() == 1 ? "1 key" : "%zu keys", m_SelectedKeys.size());
		if (ImGui::MenuItem("Smooth")) setInterpolation(KeyInterpolation::Bezier, true, false);
		if (ImGui::MenuItem("Flat")) setInterpolation(KeyInterpolation::Bezier, false, true);
		if (ImGui::MenuItem("Linear")) setInterpolation(KeyInterpolation::Linear, false, false);
		if (ImGui::MenuItem("Constant")) setInterpolation(KeyInterpolation::Step, false, false);
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Keys", "Del")) DeleteSelection();
		ImGui::EndPopup();
	}

	void AnimationWindow::HandleShortcuts() {
		if (!m_FocusedLastFrame || ImGui::GetIO().WantTextInput) return;
		const bool ctrl = ImGui::GetIO().KeyCtrl;
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
			if (ImGui::GetIO().KeyShift) Redo();
			else Undo();
			return;
		}
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
			Redo();
			return;
		}
		if (ctrl) return;
		if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
			if (!m_Playing && m_Time >= m_Clip.Length - 0.0001f) m_Time = 0.0f;
			m_Playing = !m_Playing;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) DeleteSelection();
		if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
			std::vector<int> tracks = m_SelectedTracks;
			if (tracks.empty()) for (size_t i = 0; i < m_Clip.Tracks.size(); ++i) tracks.push_back(static_cast<int>(i));
			AddKeysAtTime(tracks);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Comma)) m_Time = std::max(SnapTime(m_Time) - FrameStep(), 0.0f);
		if (ImGui::IsKeyPressed(ImGuiKey_Period)) m_Time = SnapTime(m_Time) + FrameStep();
		if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) m_Time = 0.0f;
		if (ImGui::IsKeyPressed(ImGuiKey_End, false)) m_Time = m_Clip.Length;
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameAll(m_Mode == ViewMode::Curves);
	}

}
