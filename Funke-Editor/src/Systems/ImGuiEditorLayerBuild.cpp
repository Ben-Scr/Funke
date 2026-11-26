#include "Systems/ImGuiEditorLayer.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Build/GameBuilder.hpp"
#include "Core/Application.hpp"
#include "Editor/BuildAssetPruning.hpp"
#include "Editor/PlatformBuilder.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Gui/ReorderUI.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/BuildOutputSafety.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <unordered_set>

namespace Funke {
	namespace {
		std::vector<FunkeProject::BuildSceneEntry>::iterator FindBuildScene(
			std::vector<FunkeProject::BuildSceneEntry>& sceneList, const std::string& sceneName)
		{
			return std::find_if(sceneList.begin(), sceneList.end(),
				[&](const FunkeProject::BuildSceneEntry& entry) { return entry.Name == sceneName; });
		}

		// Builds/<Platform> follows the target platform; a folder the user named themselves is left alone.
		std::string PlatformBuildOutputDirectory(const std::string& current, const std::string& projectRoot,
			BuildPlatform platform)
		{
			const char* platformFolder = FunkeBuildProfile::PlatformToString(platform);
			if (current.empty()) return Path::Combine(projectRoot, "Builds", platformFolder);

			std::filesystem::path path = Path::FromUtf8(current);
			if (!path.has_filename()) path = path.parent_path();
			const std::string leaf = Path::ToUtf8(path.filename());
			const auto equalsIgnoreCase = [](std::string_view a, std::string_view b) {
				return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
					return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
				});
			};
			for (BuildPlatform candidate : FunkeBuildProfile::AllPlatforms()) {
				if (!equalsIgnoreCase(leaf, FunkeBuildProfile::PlatformToString(candidate))) continue;
				if (candidate == platform) return current;
				return Path::ToUtf8(path.parent_path() / platformFolder);
			}
			return current;
		}

		std::string FirstEnabledBuildScene(const std::vector<FunkeProject::BuildSceneEntry>& sceneList) {
			for (const FunkeProject::BuildSceneEntry& entry : sceneList) {
				if (entry.Enabled) return entry.Name;
			}
			return {};
		}
	}

	void ImGuiEditorLayer::ReportBuildProgress(float progress, std::string_view stage) {
		// Update progress and stage as a pair under the same lock so the UI
		// thread can never observe a stage that doesn't match the bar fraction.
		std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
		m_BuildProgress = std::clamp(progress, 0.0f, 1.0f);
		m_BuildStage.assign(stage);
	}

	bool ImGuiEditorLayer::RequestCommandLineBuild(const std::string& profileName, const std::string& outputDirectory, std::string& outError) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			outError = "No project loaded (pass --project=<path>).";
			return false;
		}
		if (m_BuildState != 0 || m_ReserializeState != 0) {
			outError = "Another build or reserialization is already running.";
			return false;
		}

		const std::string resolvedProfile = GameBuilder::ResolveProfileName(*project, profileName, std::nullopt, outError);
		if (resolvedProfile.empty()) {
			return false;
		}
		project->ActiveBuildProfileName = resolvedProfile;
		if (!project->Save()) {
			outError = "Failed to save project.json.";
			return false;
		}

		std::filesystem::path output = Path::FromUtf8(outputDirectory.empty()
			? GameBuilder::DefaultOutputDirectory(*project, resolvedProfile)
			: outputDirectory);
		std::error_code absoluteError;
		output = std::filesystem::absolute(output, absoluteError);
		m_BuildOutputDir = Path::ToUtf8(output);
		std::snprintf(m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer), "%s", m_BuildOutputDir.c_str());

		m_BuildAndPlay = false;
		m_CommandLineBuild = true;
		m_BuildStartTime = std::chrono::steady_clock::now();
		m_BuildState = 1;
		FNK_INFO_TAG("Build", "Command-line build queued: profile '{}', output '{}'", resolvedProfile, m_BuildOutputDir);
		return true;
	}

	void ImGuiEditorLayer::ExecuteBuildAsync() {
		m_BuildPlayUrl.clear();
		m_BuildLaunchError.clear();
		{
			std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
			m_BuildProgress = 0.0f;
		}
		m_BuildSucceeded.store(true, std::memory_order_relaxed);
		ReportBuildProgress(0.0f, "Starting build");

		FunkeProject* project = ProjectManager::GetCurrentProject();
		auto completeWithoutWorker = [this]() {
			m_BuildFuture = std::async(std::launch::async, []() {});
		};
		if (!project) {
			FNK_ERROR_TAG("Build", "No project loaded.");
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "No project loaded");
			completeWithoutWorker();
			return;
		}

		// Freeze authored disk state before the worker starts copying it.
		if (m_PrefabEditScene && (m_PrefabEditScene->IsDirty() || m_PrefabEditDirty)
			&& !SavePrefabEditChanges()) {
			FNK_ERROR_TAG("Build", "Failed to save the open prefab edit before building.");
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "Prefab save failed");
			completeWithoutWorker();
			return;
		}
		if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()
			&& !SavePrefabInspectorChanges()) {
			FNK_ERROR_TAG("Build", "Failed to save the open prefab inspector before building.");
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "Prefab save failed");
			completeWithoutWorker();
			return;
		}

		bool sceneSaveFailed = false;
		SceneManager::ForeachLoadedScene([&](Scene& scene) {
			if (sceneSaveFailed || !scene.IsDirty()) return;
			if (!SceneSerializer::SaveToFile(scene, project->GetSceneFilePath(scene.GetName()))) {
				FNK_ERROR_TAG("Build", "Failed to save scene '{}' before building.", scene.GetName());
				sceneSaveFailed = true;
				return;
			}
			scene.ClearDirty();
		});
		if (sceneSaveFailed) {
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "Scene save failed");
			completeWithoutWorker();
			return;
		}

		Scene* active = SceneManager::GetActiveScene();
		if (active) project->LastOpenedScene = active->GetName();
		GameBuilder::Request request;
		std::string prepareError;
		if (!GameBuilder::Prepare(*project, request, prepareError)) {
			FNK_ERROR_TAG("Build", "{}", prepareError);
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, prepareError);
			completeWithoutWorker();
			return;
		}
		if (!project->Save()) {
			FNK_ERROR_TAG("Build", "Failed to save project.json before building.");
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "Project save failed");
			completeWithoutWorker();
			return;
		}

		request.OutputDirectory = m_BuildOutputDirBuffer;
		m_BuildStartTime = std::chrono::steady_clock::now();
		m_BuiltExecutablePath.clear();
		const bool playAfterBuild = m_BuildAndPlay;
		m_BuildFuture = std::async(std::launch::async, [this, request = std::move(request), playAfterBuild]() mutable {
			const GameBuilder::Result result = GameBuilder::Build(std::move(request),
				[this](float progress, std::string_view stage) { ReportBuildProgress(progress, stage); });
			m_BuildOutputDir = result.OutputDirectory;
			m_BuiltExecutablePath = result.ExecutablePath;
			if (result.Succeeded && playAfterBuild && !result.WebEntryPath.empty()) {
				ReportBuildProgress(1.0f, "Starting Web preview");
				const PlatformBuilder::WebPreview preview = PlatformBuilder::StartWebPreview(
					Path::FromUtf8(result.WebPreviewScript), Path::FromUtf8(result.WebEntryPath), m_WebPreview);
				m_BuildLaunchError = preview.Error;
				if (preview.Error.empty()) {
					m_WebPreview = preview;
					m_BuildPlayUrl = preview.Url;
				}
				ReportBuildProgress(1.0f, "Build complete");
			}
			m_BuildSucceeded.store(result.Succeeded, std::memory_order_relaxed);
		});
	}

	void ImGuiEditorLayer::RenderBuildPanel() {
		if (!m_ShowBuildPanel) return;
#if defined(FUNKE_WEB_EDITOR)
		EditorUI::NextWindowDefaultSize();
		ImGui::Begin("Build", &m_ShowBuildPanel);
		ImGui::TextWrapped("Export a standalone Web game with your scenes, assets and compiled C# scripts.");
		if (EditorUI::Button("Export Web game")) {
			EM_ASM({ if (Module.funkeOpenBuild) Module.funkeOpenBuild(); });
		}
		ImGui::End();
		return;
#endif

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		// Panes and footer are sized to the window, so the window itself never scrolls.
		ImGui::Begin("Build", &m_ShowBuildPanel, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		FunkeProject* project = ProjectManager::GetCurrentProject();
		std::vector<FunkeBuildProfile> diskProfiles;
		if (project) {
			// Reconcile the build scene list against on-disk scenes only when the registry actually
			// changed AND is already clean. NEVER force a rebuild here: the old AssetRegistry::Sync()
			// ran the full ~9k-.meta directory walk on the UI thread — that was the ~1s freeze when
			// this panel opened. While the registry is mid-edit (dirty) we keep the cached list; the
			// next consumer rebuilds it and bumps the change version, and we pick it up then.
			// Until then the list persisted in project.json is shown as-is, so it is never blank.
			std::vector<FunkeProject::BuildSceneEntry>& sceneList = project->BuildSceneList;
			bool sceneListChanged = false;
			const uint64_t registryVersion = AssetRegistry::GetChangeVersion();
			if (registryVersion != m_BuildSceneListVersion && !AssetRegistry::IsDirty()) {
				m_BuildSceneListVersion = registryVersion;

				std::vector<std::string> diskScenes;
				diskScenes.reserve(sceneList.size() + 4);
				for (const AssetRegistry::Record& record : AssetRegistry::FindAll(AssetKind::Scene)) {
					diskScenes.push_back(std::filesystem::path(record.Path).stem().string());
				}

				// Drop entries whose .scene file no longer exists.
				const size_t sizeBefore = sceneList.size();
				sceneList.erase(
					std::remove_if(sceneList.begin(), sceneList.end(),
						[&](const FunkeProject::BuildSceneEntry& entry) {
							return std::find(diskScenes.begin(), diskScenes.end(), entry.Name) == diskScenes.end();
						}),
					sceneList.end());
				sceneListChanged |= sceneList.size() != sizeBefore;

				// New scenes on disk join the build by default; opting out is a checkbox.
				for (const std::string& stem : diskScenes) {
					if (FindBuildScene(sceneList, stem) == sceneList.end()) {
						sceneList.push_back({ stem, true });
						sceneListChanged = true;
					}
				}
			}

			// StartupScene is always the first enabled entry, so the [Startup] badge and the
			// runtime's first-loaded scene agree with project.json.
			auto startupIt = FindBuildScene(sceneList, project->StartupScene);
			if (startupIt != sceneList.end() && startupIt->Enabled) {
				const auto firstEnabledIt = std::find_if(sceneList.begin(), sceneList.end(),
					[](const FunkeProject::BuildSceneEntry& entry) { return entry.Enabled; });
				if (startupIt != firstEnabledIt) {
					const auto insertAt = std::distance(sceneList.begin(), firstEnabledIt);
					FunkeProject::BuildSceneEntry startupEntry = std::move(*startupIt);
					sceneList.erase(startupIt);
					sceneList.insert(sceneList.begin() + insertAt, std::move(startupEntry));
					sceneListChanged = true;
				}
			}
			// A missing StartupScene is only trusted as "gone" once the list was reconciled with disk.
			else if (startupIt != sceneList.end() || m_BuildSceneListVersion != 0) {
				const std::string firstEnabled = FirstEnabledBuildScene(sceneList);
				if (!firstEnabled.empty() && firstEnabled != project->StartupScene) {
					project->StartupScene = firstEnabled;
					sceneListChanged = true;
				}
			}

			if (sceneListChanged) {
				project->Save();
			}

			if (!project->GetBuildProfilesDirectory().empty()) {
				std::error_code ec;
				const std::string dir = project->GetBuildProfilesDirectory();
				if (std::filesystem::exists(dir)) {
					const std::string ext(FunkeBuildProfile::FileExtension);
					for (const auto& entry : std::filesystem::directory_iterator(
							 dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
						if (ec) { ec.clear(); continue; }
						if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
						if (entry.path().extension().string() != ext) continue;
						diskProfiles.push_back(FunkeBuildProfile::Load(entry.path().string()));
					}
					std::sort(diskProfiles.begin(), diskProfiles.end(),
						[](const FunkeBuildProfile& a, const FunkeBuildProfile& b) {
							return a.Name < b.Name;
						});
				}
			}

			// Clear stale ActiveBuildProfileName if the file was deleted
			// externally — keeps the UI honest after a manual delete.
			if (!project->ActiveBuildProfileName.empty()) {
				bool stillExists = false;
				for (const auto& p : diskProfiles) {
					if (p.Name == project->ActiveBuildProfileName) { stillExists = true; break; }
				}
				if (!stillExists) {
					project->ActiveBuildProfileName.clear();
					project->Save();
				}
			}

			// Default to the first profile for this desktop; leave empty if none exists (don't silently target another OS).
			if (project->ActiveBuildProfileName.empty()) {
				for (const auto& p : diskProfiles) {
					if (BuildPlatformSupport::IsHostDesktop(p.Platform)) {
						project->ActiveBuildProfileName = p.Name;
						project->Save();
						break;
					}
				}
			}
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float frameH = ImGui::GetFrameHeight();
		constexpr float k_CardPad = 8.0f;
		// Footer = separator + spacing + one button row; the body takes the rest.
		const float footerH = std::ceil(frameH + style.ItemSpacing.y * 3.0f + 1.0f);
		const float bodyH = std::max(frameH * 4.0f, ImGui::GetContentRegionAvail().y - footerH);

		auto sectionTitle = [](const char* label) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::SeparatorText(label);
			ImGui::PopStyleColor();
		};
		// BeginCard draws no inner padding, so pad the content here.
		const float cardPadY = std::max(0.0f, k_CardPad - style.ItemSpacing.y);
		auto beginCard = [&](const char* id) {
			EditorUI::BeginCard(id);
			ImGui::Dummy(ImVec2(0.0f, cardPadY));
			ImGui::Indent(k_CardPad);
		};
		auto endCard = [&]() {
			ImGui::Unindent(k_CardPad);
			ImGui::Dummy(ImVec2(0.0f, cardPadY));
			EditorUI::EndCard();
		};
		float labelW = 0.0f;
		for (const char* label : { "Active profile", "Platform", "Rendering API", "Build profile", "Output directory", "Defines", "Project" }) {
			labelW = std::max(labelW, ImGui::CalcTextSize(label).x);
		}
		labelW += style.ItemSpacing.x * 2.0f;
		auto rowLabel = [&](const char* label) {
			const float x = ImGui::GetCursorPosX();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", label);
			ImGui::SameLine(x + labelW);
		};

		if (!project) {
			ImGui::BeginChild("##BuildBody", ImVec2(0.0f, bodyH));
			EditorUI::EmptyState("No project loaded");
			ImGui::EndChild();
		}
		else {
			const float leftW = std::floor((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.45f);

			// ── Left: scenes in build ──
			ImGui::BeginChild("##BuildScenesPane", ImVec2(leftW, bodyH), ImGuiChildFlags_None,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			sectionTitle("Scenes in build");
			const float footerRowH = frameH + style.ItemSpacing.y;
			const float listH = std::max(frameH * 2.0f, ImGui::GetContentRegionAvail().y - footerRowH);
			std::vector<FunkeProject::BuildSceneEntry>& sceneList = project->BuildSceneList;
			// Row edits are applied after the loop so the list isn't mutated mid-iteration.
			int moveFrom = -1;
			int moveTo = -1;
			bool sceneListEdited = false;

			EditorUI::BeginCard("##BuildSceneCard");
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(k_CardPad, k_CardPad));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			ImGui::BeginChild("##BuildSceneList", ImVec2(0.0f, listH), ImGuiChildFlags_AlwaysUseWindowPadding);
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();

			if (sceneList.empty()) {
				EditorUI::EmptyState("No scenes", "Create a scene, or drag a .scene asset here.");
			}
			else {
				const float gripW = ImGui::GetFontSize();
				const float indexW = ImGui::CalcTextSize("00").x + style.ItemSpacing.x;
				const float startupBadgeW = ImGui::CalcTextSize("Startup").x + 12.0f; // Badge pads 6px per side
				const int sceneCount = static_cast<int>(sceneList.size());
				int startupIndex = -1;
				for (int i = 0; i < sceneCount; i++) {
					if (sceneList[i].Enabled) { startupIndex = i; break; }
				}
				// Shown index is the runtime build index, so excluded rows don't consume one.
				int buildIndex = 0;
				float lastRowBottom = ImGui::GetCursorScreenPos().y;
				const float listLeft = ImGui::GetCursorScreenPos().x;
				const float listRight = listLeft + ImGui::GetContentRegionAvail().x;
				const auto acceptReorder = [&](const ImRect& bounds, ImGuiID targetId, int slot, float markerY) {
					if (!ImGui::BeginDragDropTargetCustom(bounds, targetId)) return;
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_LIST_ITEM",
						ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
						if (payload->DataSize == sizeof(int)) {
							const int source = *static_cast<const int*>(payload->Data);
							const int destination = ReorderUI::DestinationIndex(source, slot, sceneCount);
							if (destination >= 0 && destination != source) {
								ReorderUI::DrawInsertionMarker(listLeft, listRight, markerY);
								if (payload->IsDelivery()) { moveFrom = source; moveTo = destination; }
							}
						}
					}
					ImGui::EndDragDropTarget();
				};
				for (int i = 0; i < sceneCount; i++) {
					FunkeProject::BuildSceneEntry& entry = sceneList[i];
					ImGui::PushID(i);
					const bool isStartup = (i == startupIndex);

					const ImVec2 rowMin = ImGui::GetCursorScreenPos();
					const auto dragSource = [&] {
						if (ImGui::BeginDragDropSource()) {
							ImGui::SetDragDropPayload("SCENE_LIST_ITEM", &i, sizeof(int));
							ImGui::Text("Move: %s", entry.Name.c_str());
							ImGui::EndDragDropSource();
						}
					};
					ReorderUI::DragHandle("##SceneDrag", ImVec2(gripW, frameH));
					dragSource();
					ImGui::SameLine();
					const float rowX = ImGui::GetCursorPosX();
					ImGui::AlignTextToFramePadding();
					if (entry.Enabled) ImGui::TextDisabled("%d", buildIndex++);
					else ImGui::TextDisabled(" ");
					ImGui::SameLine(rowX + indexW);

					if (ImGui::Checkbox("##IncludeInBuild", &entry.Enabled)) {
						sceneListEdited = true;
					}
					EditorUI::Tooltip(entry.Enabled ? "Included in the build" : "Excluded from the build");
					ImGui::SameLine();

					const float nameW = ImGui::GetContentRegionAvail().x
						- (isStartup ? startupBadgeW + style.ItemSpacing.x : 0.0f);
					const std::string sceneItemId = std::to_string(i);
					if (!entry.Enabled) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					}
					ImGuiUtils::SelectableEllipsis(entry.Name, sceneItemId.c_str(), false, 0,
						ImVec2(std::max(1.0f, nameW), 0.0f));
					if (!entry.Enabled) {
						ImGui::PopStyleColor();
					}

					if (ImGui::BeginPopupContextItem("##BuildSceneRowMenu")) {
						if (ImGui::MenuItem(entry.Enabled ? "Exclude from build" : "Include in build")) {
							entry.Enabled = !entry.Enabled;
							sceneListEdited = true;
						}
						if (ImGui::MenuItem("Set as startup scene", nullptr, false, !isStartup)) {
							entry.Enabled = true;
							moveFrom = i;
							moveTo = 0;
							sceneListEdited = true;
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) {
							moveFrom = i;
							moveTo = i - 1;
						}
						if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < sceneCount)) {
							moveFrom = i;
							moveTo = i + 1;
						}
						ImGui::EndPopup();
					}

					dragSource();

					if (isStartup) {
						ImGui::SameLine();
						EditorUI::Badge("Startup", EditorUI::Tone::Success);
						EditorUI::Tooltip("Loaded first when the built game starts.");
					}

					lastRowBottom = rowMin.y + frameH;
					const bool after = ImGui::GetMousePos().y >= (rowMin.y + lastRowBottom) * 0.5f;
					const float gap = style.ItemSpacing.y * 0.5f;
					acceptReorder(ImRect(ImVec2(listLeft, rowMin.y - gap), ImVec2(listRight, lastRowBottom + gap)),
						ImGui::GetID("##SceneDrop"), i + (after ? 1 : 0), after ? lastRowBottom + gap : rowMin.y - gap);
					ImGui::PopID();
				}
				// The empty area below the last row is also an explicit append target.
				const float tailTop = lastRowBottom + style.ItemSpacing.y * 0.5f;
				const float tailBottom = ImGui::GetWindowPos().y + ImGui::GetWindowContentRegionMax().y;
				if (tailBottom > tailTop) {
					acceptReorder(ImRect(ImVec2(listLeft, tailTop), ImVec2(listRight, tailBottom)),
						ImGui::GetID("##SceneAppend"), sceneCount, tailTop);
				}
			}
			ImGui::EndChild();

			// The whole list area takes asset drops; rows inside take reorder drops.
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
					// Use 1-arg ctor: payload includes trailing '\0' in DataSize, so (data,DataSize) embeds it and breaks extension comparison.
					std::string droppedPath(static_cast<const char*>(payload->Data));
					if (std::filesystem::path(droppedPath).extension() == ".scene") {
						const std::string sceneName = std::filesystem::path(droppedPath).stem().string();
						auto it = FindBuildScene(sceneList, sceneName);
						if (it == sceneList.end()) {
							sceneList.push_back({ sceneName, true });
						}
						else {
							it->Enabled = true;
						}
						sceneListEdited = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			EditorUI::EndCard();

			Scene* openScene = SceneManager::GetActiveScene();
			const std::string openSceneName = openScene ? openScene->GetName() : std::string();
			const auto openSceneIt = FindBuildScene(sceneList, openSceneName);
			const bool canAddOpenScene = !openSceneName.empty()
				&& (openSceneIt == sceneList.end() || !openSceneIt->Enabled);
			ImGui::BeginDisabled(!canAddOpenScene);
			if (ImGui::Button("Add Open Scene")) {
				if (openSceneIt != sceneList.end()) {
					openSceneIt->Enabled = true;
					sceneListEdited = true;
				}
				else if (const std::string scenePath = project->GetSceneFilePath(openSceneName);
					!scenePath.empty() && File::Exists(scenePath)) {
					sceneList.push_back({ openSceneName, true });
					sceneListEdited = true;
				}
				else {
					FNK_WARN_TAG("Build", "Scene '{}' has no .scene file yet; save it before adding it to the build.",
						openSceneName);
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGuiUtils::TextDisabledEllipsis("Drag to reorder, right-click for options.");

			const int sceneCount = static_cast<int>(sceneList.size());
			if (moveFrom >= 0 && moveFrom < sceneCount && moveTo >= 0 && moveTo < sceneCount && moveFrom != moveTo) {
				FunkeProject::BuildSceneEntry moved = std::move(sceneList[moveFrom]);
				sceneList.erase(sceneList.begin() + moveFrom);
				sceneList.insert(sceneList.begin() + moveTo, std::move(moved));
				sceneListEdited = true;
			}
			if (sceneListEdited) {
				if (std::string firstEnabled = FirstEnabledBuildScene(sceneList); !firstEnabled.empty()) {
					project->StartupScene = std::move(firstEnabled);
				}
				project->Save();
			}
			ImGui::EndChild();

			// ── Right: target + settings ──
			ImGui::SameLine();
			ImGui::BeginChild("##BuildTargetPane", ImVec2(0.0f, bodyH));

			sectionTitle("Target");
			beginCard("##BuildTargetCard");

			rowLabel("Active profile");
			const float manageW = ImGui::CalcTextSize("Manage…").x + style.FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(std::max(1.0f,
				ImGui::GetContentRegionAvail().x - manageW - style.ItemInnerSpacing.x - k_CardPad));
			const float profileIconSize = ImGui::GetFontSize();
			const float profileTextOffset = profileIconSize + style.ItemInnerSpacing.x;
			if (EditorUI::BeginCombo("##ActiveBuildProfile", nullptr, ImGuiComboFlags_CustomPreview)) {
				auto profileOption = [&](const char* name, const FunkeBuildProfile* profile, bool selected) {
					ImGui::PushID(name);
					const ImVec2 rowMin = ImGui::GetCursorScreenPos();
					const float rowWidth = std::max(ImGui::GetContentRegionAvail().x,
						profileTextOffset + ImGui::CalcTextSize(name).x);
					const bool clicked = ImGui::Selectable("##Profile", selected, 0, ImVec2(rowWidth, profileIconSize));
					if (selected) ImGui::SetItemDefaultFocus();
					if (profile) EditorIcons::DrawPlatform(ImGui::GetWindowDrawList(), profile->Platform,
						rowMin, profileIconSize, style.Alpha);
					ImGui::RenderTextClipped(ImVec2(rowMin.x + profileTextOffset, rowMin.y),
						ImVec2(rowMin.x + rowWidth, rowMin.y + profileIconSize), name, nullptr, nullptr);
					ImGui::PopID();
					return clicked;
				};
				if (profileOption("<none>", nullptr, project->ActiveBuildProfileName.empty())) {
					if (!project->ActiveBuildProfileName.empty()) {
						project->ActiveBuildProfileName.clear();
						project->Save();
					}
				}
				for (const auto& p : diskProfiles) {
					const bool selected = (project->ActiveBuildProfileName == p.Name);
					if (profileOption(p.Name.c_str(), &p, selected)) {
						if (project->ActiveBuildProfileName != p.Name) {
							project->ActiveBuildProfileName = p.Name;
							project->Save();
						}
					}
				}
				EditorUI::EndCombo();
			}
			const FunkeBuildProfile* active = nullptr;
			for (const auto& p : diskProfiles) {
				if (p.Name == project->ActiveBuildProfileName) { active = &p; break; }
			}
			if (ImGui::BeginComboPreview()) {
				if (active) {
					EditorIcons::DrawPlatform(ImGui::GetWindowDrawList(), active->Platform,
						ImGui::GetCursorScreenPos(), profileIconSize, style.Alpha);
					ImGui::Dummy(ImVec2(profileIconSize, profileIconSize));
					ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
				}
				ImGui::TextUnformatted(project->ActiveBuildProfileName.empty()
					? "<none>" : project->ActiveBuildProfileName.c_str());
				ImGui::EndComboPreview();
			}
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			if (ImGui::Button("Manage…")) {
				OpenPanel("Build Profiles", m_ShowBuildProfilesPanel);
			}
			EditorUI::Tooltip("Open the Build Profiles window.");

			// Echo what Build will actually target without flipping over to the Build Profiles window.
			if (!project->ActiveBuildProfileName.empty()) {
				if (active) {
					rowLabel("Platform");
					ImGui::Text("%s", FunkeBuildProfile::PlatformDisplayName(active->Platform));
					rowLabel("Rendering API");
					ImGui::Text("%s", FunkeProject::RenderBackendToString(active->RenderBackend));
					if (!BuildPlatformSupport::IsAvailable(active->Platform)) {
						const std::string reason = "[!] " + BuildPlatformSupport::UnavailableReason(active->Platform);
						ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - k_CardPad);
						EditorUI::StatusText(EditorUI::Tone::Warning, reason.c_str());
						ImGui::PopTextWrapPos();
					}
				}
			}
			else {
				EditorUI::EmptyState("No active profile", "Pick one above or click 'Manage…'.");
			}
			endCard();

			ImGui::Spacing();
			sectionTitle("Settings");
			beginCard("##BuildSettingsCard");
			const float valueX = ImGui::GetCursorPosX() + labelW;

			rowLabel("Configuration");
			ImGui::SetNextItemWidth(-k_CardPad);
			constexpr FunkeProject::BuildProfile k_Configurations[] = {
				FunkeProject::BuildProfile::Debug,
				FunkeProject::BuildProfile::Development,
				FunkeProject::BuildProfile::Release,
			};
			const bool profileSetsConfiguration = active && active->Configuration.has_value();
			const FunkeProject::BuildProfile shownConfiguration = profileSetsConfiguration
				? *active->Configuration : project->ActiveBuildProfile;
			ImGui::BeginDisabled(profileSetsConfiguration);
			if (EditorUI::BeginCombo("##BuildConfiguration", FunkeProject::BuildProfileToString(shownConfiguration))) {
				for (const FunkeProject::BuildProfile configuration : k_Configurations) {
					if (ImGui::Selectable(FunkeProject::BuildProfileToString(configuration), configuration == shownConfiguration)
						&& configuration != project->ActiveBuildProfile) {
						project->ActiveBuildProfile = configuration;
						// Let the overlays follow the new configuration (Release = off, otherwise on).
						project->ShowRuntimeStats.reset();
						project->ShowRuntimeLogs.reset();
						project->Save();
					}
				}
				EditorUI::EndCombo();
			}
			ImGui::EndDisabled();
			if (profileSetsConfiguration) {
				EditorUI::Tooltip("Set by the active build profile. Change it in Build Profiles.");
			}
			else {
				EditorUI::Tooltip("Debug: debug binaries and symbols for attaching a debugger.\n"
					"Development: optimized, with overlays and the dev console.\nRelease: the shipped game.");
			}

			if (active && m_BuildState == 0) {
				const std::string retargeted = PlatformBuildOutputDirectory(
					m_BuildOutputDirBuffer, project->RootDirectory, active->Platform);
				if (retargeted != m_BuildOutputDirBuffer) {
					std::snprintf(m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer), "%s", retargeted.c_str());
				}
			}

			rowLabel("Output directory");
			ImGui::SetNextItemWidth(-k_CardPad);
			ImGui::InputText("##BuildOutputDir", m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer));

			rowLabel("Defines");
			const float addBtnWidth = ImGui::CalcTextSize("Add").x
				+ style.FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x
				- addBtnWidth - style.ItemInnerSpacing.x - k_CardPad));
			const bool entryEnter = ImGui::InputTextWithHint("##NewCustomDefine",
				"Add a symbol (e.g. STEAM_BUILD)…",
				m_CustomDefineEntryBuffer, sizeof(m_CustomDefineEntryBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			bool addClicked = ImGui::Button("Add##CustomDefine") || entryEnter;
			const bool entryReady = m_CustomDefineEntryBuffer[0] != '\0';
			if (addClicked && entryReady) {
				std::string newDef(m_CustomDefineEntryBuffer);
				// Trim whitespace + reject duplicates / empty strings.
				while (!newDef.empty() && std::isspace(static_cast<unsigned char>(newDef.back()))) newDef.pop_back();
				std::size_t firstNonSpace = 0;
				while (firstNonSpace < newDef.size()
					&& std::isspace(static_cast<unsigned char>(newDef[firstNonSpace]))) ++firstNonSpace;
				newDef.erase(0, firstNonSpace);
				if (!newDef.empty()
					&& std::find(project->CustomDefines.begin(), project->CustomDefines.end(), newDef) == project->CustomDefines.end()) {
					project->CustomDefines.push_back(std::move(newDef));
					project->Save();
				}
				m_CustomDefineEntryBuffer[0] = '\0';
			}

			if (project->CustomDefines.empty()) {
				ImGui::SetCursorPosX(valueX);
				ImGui::TextDisabled("No custom defines.");
			}
			else {
				// Defines flow as chips under the input, wrapping at the card edge.
				const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - k_CardPad;
				int removeIdx = -1;
				for (int i = 0; i < static_cast<int>(project->CustomDefines.size()); ++i) {
					ImGui::PushID(i);
					const std::string& define = project->CustomDefines[i];
					const float chipW = ImGui::CalcTextSize(define.c_str()).x + 12.0f + style.ItemInnerSpacing.x + frameH;
					if (i > 0 && ImGui::GetItemRectMax().x + style.ItemSpacing.x + chipW <= rightEdge) {
						ImGui::SameLine();
					}
					else {
						ImGui::SetCursorPosX(valueX);
					}
					EditorUI::Badge(define.c_str());
					ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
					if (EditorUI::IconButton("##RemoveDefine", "clear", "Remove define", false, false, 0.0f, "x")) {
						removeIdx = i;
					}
					ImGui::PopID();
				}
				if (removeIdx >= 0) {
					project->CustomDefines.erase(project->CustomDefines.begin() + removeIdx);
					project->Save();
				}
			}
			if (active && !active->Defines.empty()) {
				std::string profileDefines;
				for (const std::string& define : active->Defines) profileDefines += (profileDefines.empty() ? "" : " ") + define;
				ImGui::SetCursorPosX(valueX);
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - k_CardPad);
				ImGui::TextDisabled("Profile adds: %s", profileDefines.c_str());
				ImGui::PopTextWrapPos();
			}

			rowLabel("Project");
			if (EditorUI::Button("Open Project Settings")) {
				OpenPanel("Project Settings", m_ShowProjectSettings);
			}
			endCard();

			ImGui::EndChild();
		}

		// ── Footer: status on the left, actions on the right ──
		ImGui::Separator();
		ImGui::Spacing();

		FunkeBuildProfile activeProfile;
		bool activeProfileResolved = false;
		if (project && !project->ActiveBuildProfileName.empty()) {
			const std::string profilePath =
				(std::filesystem::path(project->GetBuildProfilesDirectory()) /
				 (project->ActiveBuildProfileName + std::string(FunkeBuildProfile::FileExtension))).string();
			if (std::filesystem::exists(profilePath)) {
				activeProfile = FunkeBuildProfile::Load(profilePath);
				activeProfileResolved = true;
			}
		}

		const bool platformAvailable = activeProfileResolved
			&& BuildPlatformSupport::IsAvailable(activeProfile.Platform);

		const float buttonW = std::max(110.0f, ImGui::CalcTextSize("Build and Play").x + style.FramePadding.x * 4.0f);
		const float buttonsW = buttonW * 2.0f + style.ItemSpacing.x;
		const float footerAvail = ImGui::GetContentRegionAvail().x;
		const float buttonsX = ImGui::GetCursorPosX() + std::max(0.0f, footerAvail - buttonsW);
		const float statusW = std::max(1.0f, footerAvail - buttonsW - style.ItemSpacing.x * 2.0f);

		if (m_BuildState > 0) {
			float progress = 0.0f;
			std::string stage;
			{
				std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
				progress = m_BuildProgress;
				stage = m_BuildStage;
			}
			ImGui::ProgressBar(progress, ImVec2(statusW, 0.0f), stage.empty() ? "Building..." : stage.c_str());
		}
		else {
			auto statusLine = [&](EditorUI::Tone tone, const std::string& text) {
				ImGui::AlignTextToFramePadding();
				ImGui::PushStyleColor(ImGuiCol_Text, EditorUI::ToneColor(tone));
				ImGuiUtils::TextEllipsis(text, statusW);
				ImGui::PopStyleColor();
			};
			// Explain a disabled Build first (disabled buttons have no tooltip); otherwise echo the last result.
			if (!project) {
				statusLine(EditorUI::Tone::Neutral, "No project loaded.");
			}
			else if (Application::GetIsPlaying()) {
				statusLine(EditorUI::Tone::Neutral, "Exit play mode to build.");
			}
			else if (!activeProfileResolved) {
				statusLine(EditorUI::Tone::Neutral, "Select an active build profile to enable Build.");
			}
			else if (!platformAvailable) {
				statusLine(EditorUI::Tone::Warning,
					"[!] Cannot build: " + BuildPlatformSupport::UnavailableReason(activeProfile.Platform));
			}
			else {
				std::string lastStage;
				{
					std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
					lastStage = m_BuildStage;
				}
				if (lastStage.empty()) {
					statusLine(EditorUI::Tone::Neutral, "Ready to build.");
				}
				else if (!m_BuildLaunchError.empty()) {
					statusLine(EditorUI::Tone::Warning, "Build complete; play failed: " + m_BuildLaunchError);
				}
				else if (m_BuildSucceeded.load(std::memory_order_relaxed)) {
					statusLine(EditorUI::Tone::Success, "Last build complete: " + m_BuildOutputDir);
				}
				else {
					statusLine(EditorUI::Tone::Error, "Last build failed: " + lastStage);
				}
			}
		}
		ImGui::SameLine(buttonsX);

		bool canBuild = project && !Application::GetIsPlaying() && m_BuildState == 0
			&& activeProfileResolved && platformAvailable;
		if (!canBuild) ImGui::BeginDisabled();

		if (EditorUI::Button("Build and Play", EditorUI::ButtonVariant::Default, ImVec2(buttonW, 0.0f))) {
			m_BuildState = 1;
			m_BuildAndPlay = true;
			m_BuildStartTime = std::chrono::steady_clock::now();
		}
		ImGui::SameLine();
		if (EditorUI::Button("Build", EditorUI::ButtonVariant::Primary, ImVec2(buttonW, 0.0f))) {
			m_BuildState = 1;
			m_BuildAndPlay = false;
			m_BuildStartTime = std::chrono::steady_clock::now();
		}

		if (!canBuild) ImGui::EndDisabled();
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderBuildProfilesPanel() {
		if (!m_ShowBuildProfilesPanel) return;
#if defined(FUNKE_WEB_EDITOR)
		EditorUI::NextWindowDefaultSize();
		ImGui::Begin("Build Profiles", &m_ShowBuildProfilesPanel);
		EditorUI::EmptyState("Web / WebGPU", "Use Build to export a standalone browser game. Desktop and mobile builds require the desktop editor.");
		ImGui::End();
		return;
#endif
		if (!m_BuildProfilesPanelInitialized) {
			m_BuildProfilesPanel.Initialize();
			m_BuildProfilesPanelInitialized = true;
		}
		m_BuildProfilesPanel.Render(&m_ShowBuildProfilesPanel);
	}
}
