#include <pch.hpp>
#include "Systems/ImGuiEditorLayer.hpp"

#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Editor/ApplicationEditorAccess.hpp"
#include "Editor/GameView2DLayer.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Graphics/Renderer3D.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#include "Systems/UILayoutSystem.hpp"

#include <filesystem>
#include <optional>

namespace Funke {

	std::string ImGuiEditorLayer::GetBridgeBusyReason() const {
		if (!Application::IsStartupLoadComplete() || m_Splash.IsCovering()) return "the editor is still loading";
		if (m_BuildState != 0) return "a game build is running";
		if (m_ReserializeState != 0) return "scenes and prefabs are being reserialized";
		if (m_PendingEditorAction != PendingEditorAction::None || m_ShowUnsavedWorkDialog || m_UnsavedWorkDialogOpen) {
			return "the editor is waiting for an answer in the 'unsaved changes' dialog";
		}
		if (!m_PendingRecovery.empty()) return "the editor is showing the 'Recover Unsaved Changes' dialog";
		if (m_ShowPlayModeRestoreError) return "the editor is showing the 'Play Mode Restore Failed' dialog";
		return {};
	}

	bool ImGuiEditorLayer::GetGameViewSurfaceSize(int& outWidth, int& outHeight) const {
		if (m_BridgeSurfaceWidth > 0 && m_BridgeSurfaceHeight > 0) {
			outWidth = m_BridgeSurfaceWidth;
			outHeight = m_BridgeSurfaceHeight;
			return true;
		}
		if (m_IsGameViewActive && m_LastGameViewFbW > 0 && m_LastGameViewFbH > 0) {
			outWidth = m_LastGameViewFbW;
			outHeight = m_LastGameViewFbH;
			return true;
		}
		if (m_HiddenGameViewSize.x <= 0.0f || m_HiddenGameViewSize.y <= 0.0f) return false;
		// The window minus its padding and the toolbar row, fitted to the aspect preset like RenderGameView does.
		const ImGuiStyle& style = ImGui::GetStyle();
		Vec2 size{ m_HiddenGameViewSize.x - style.WindowPadding.x * 2.0f,
			m_HiddenGameViewSize.y - style.WindowPadding.y * 2.0f - ImGui::GetFrameHeightWithSpacing() };
		if (size.x < 1.0f || size.y < 1.0f) return false;
		if (const float aspect = GetGameViewAspect(); aspect > 0.0f) {
			if (size.x / size.y > aspect) size.x = size.y * aspect;
			else size.y = size.x / aspect;
		}
		outWidth = std::max(1, static_cast<int>(std::round(size.x)));
		outHeight = std::max(1, static_cast<int>(std::round(size.y)));
		return true;
	}

	void ImGuiEditorLayer::SetPlayModePaused(bool paused) {
		if (!Application::GetIsPlaying() || ApplicationEditorAccess::IsPlaymodePaused() == paused) return;
		ApplicationEditorAccess::SetPlaymodePaused(paused);
		Scene* active = SceneManager::GetActiveScene();
		if (paused) {
			m_EditorPausedAudioEntities.clear();
			if (!active) return;
			for (auto [entity, audio] : active->GetRegistry().view<AudioSourceComponent>().each()) {
				if (audio.IsPlaying()) {
					audio.Pause();
					m_EditorPausedAudioEntities.push_back(entity);
				}
			}
			return;
		}
		if (active) {
			for (EntityHandle entity : m_EditorPausedAudioEntities) {
				if (active->IsValid(entity) && active->HasComponent<AudioSourceComponent>(entity)) {
					auto& audio = active->GetComponent<AudioSourceComponent>(entity);
					if (audio.IsPaused()) audio.Resume();
				}
			}
		}
		m_EditorPausedAudioEntities.clear();
	}

	bool ImGuiEditorLayer::BridgeRequestPlay(std::string& outError) {
		if (Application::GetIsPlaying() || m_PlayModeRecompilePending) return true;
		Scene* active = SceneManager::GetActiveScene();
		if (!active) {
			outError = "no scene is open";
			return false;
		}
		BeginPlayModeRequest(*active);
		if (!Application::GetIsPlaying() && !m_PlayModeRecompilePending) {
			outError = "Play mode did not start (script errors, duplicate entity ids or an unsaved project; see `funke editor logs --level warning`)";
			return false;
		}
		return true;
	}

	bool ImGuiEditorLayer::BridgeOpenScene(const std::string& sceneNameOrPath, bool saveFirst, bool discardChanges, std::string& outError) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			outError = "no project is open";
			return false;
		}
		if (Application::GetIsPlaying()) {
			outError = "stop Play mode first (`funke editor stop`)";
			return false;
		}

		std::filesystem::path path = Path::FromUtf8(sceneNameOrPath);
		if (path.extension() != ".scene") {
			path = Path::FromUtf8(project->GetSceneFilePath(sceneNameOrPath));
		}
		else if (path.is_relative()) {
			path = Path::FromUtf8(project->RootDirectory) / path;
		}
		std::error_code ec;
		if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
			outError = "no scene '" + sceneNameOrPath + "' in this project";
			return false;
		}

		std::vector<std::string> dirtyNames;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			if (std::shared_ptr<Scene> scene = weakScene.lock(); scene && scene->IsDirty()) dirtyNames.push_back(scene->GetName());
		}
		if (!dirtyNames.empty() && !saveFirst && !discardChanges) {
			std::string names;
			for (const std::string& name : dirtyNames) names += (names.empty() ? "" : ", ") + name;
			outError = "unsaved changes in " + names + "; pass --save or --discard";
			return false;
		}
		if (!dirtyNames.empty() && saveFirst) {
			std::vector<std::string> saved;
			if (!BridgeSaveScenes(saved, outError)) return false;
		}
		if (!OpenSceneExclusive(Path::ToUtf8(path))) {
			outError = "the scene could not be opened (see the log)";
			return false;
		}
		return true;
	}

	bool ImGuiEditorLayer::BridgeSaveScenes(std::vector<std::string>& outSaved, std::string& outError) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			outError = "no project is open";
			return false;
		}
		if (Application::GetIsPlaying()) {
			outError = "scenes can't be saved during Play mode";
			return false;
		}
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			std::shared_ptr<Scene> scene = weakScene.lock();
			if (!scene || !scene->IsDirty()) continue;
			const std::string path = project->GetSceneFilePath(scene->GetName());
			if (path.empty() || !SceneSerializer::SaveToFile(*scene, path)) {
				outError = "could not save scene '" + scene->GetName() + "'";
				return false;
			}
			outSaved.push_back(scene->GetName());
		}
		if (!outSaved.empty() && !SaveCurrentProjectMetadata()) {
			outError = "scenes were saved, but project.json could not be updated";
			return false;
		}
		return true;
	}

	bool ImGuiEditorLayer::BridgeRenderGameView(int width, int height, std::vector<uint8_t>& outRgba, std::string& outError) {
		Application* app = Application::GetInstance();
		Scene* scene = SceneManager::GetActiveScene();
		if (!app || !scene) {
			outError = "no scene is open";
			return false;
		}
		Camera2DComponent* camera = Camera2DComponent::GetMain();
		Viewport* cameraViewport = camera && camera->IsValid() ? camera->GetViewport() : nullptr;
		// Same camera choice as the Game View: a Camera3D renders the scene, a Camera2D only the 2D overlay.
		const EntityHandle camera3DEntity = Camera3DComponent::FindMain(*scene);
		const bool hasCamera3D = camera3DEntity != entt::null && app->GetRenderer3D();
		if (!cameraViewport && !hasCamera3D) {
			outError = "the scene has no main camera";
			return false;
		}
		Framebuffer fbo(width, height);
		if (!fbo.IsValid()) {
			outError = "could not create a " + std::to_string(width) + "x" + std::to_string(height) + " render target";
			return false;
		}

		// The game sees a screen of exactly this size while it renders, like a device with that resolution.
		struct SurfaceRestore {
			Window::UIRegion Region;
			Viewport* CameraViewport = nullptr;
			Camera2DComponent* Camera = nullptr;
			int CameraWidth = 0;
			int CameraHeight = 0;
			~SurfaceRestore() {
				if (Region.IsActive()) {
					Window::SetUIRegion(Region.OffsetX, Region.OffsetY, Region.Width, Region.Height, Region.WindowToRegionScale);
				}
				else {
					Window::ClearUIRegion();
				}
				if (CameraViewport) {
					CameraViewport->SetSize(CameraWidth, CameraHeight);
					Camera->UpdateViewport();
				}
				// Hit tests until the next layout pass must use the game's own surface again.
				SceneManager::ForeachLoadedScene([](Scene& loaded) { ComputeUILayout(loaded); });
			}
		} restore{ Window::GetUIRegion(), cameraViewport, camera,
			cameraViewport ? cameraViewport->GetWidth() : 0, cameraViewport ? cameraViewport->GetHeight() : 0 };
		Window::SetUIRegion(0, 0, width, height);

		std::optional<Camera3DView> view3D;
		if (hasCamera3D) {
			Transform3DHierarchySystem::Propagate(*scene);
			const auto& registry = scene->GetRegistry();
			view3D = Renderer3D::MakeView(registry.get<Transform3DComponent>(camera3DEntity).WorldMatrix,
				registry.get<Camera3DComponent>(camera3DEntity), static_cast<float>(width) / static_cast<float>(height));
		}
		if (cameraViewport) {
			cameraViewport->SetSize(width, height);
			camera->UpdateViewport();
		}
		const GameView2DLayer layer2D = ResolveGameView2DLayer(cameraViewport ? camera : nullptr, view3D ? &*view3D : nullptr);
		const Color clearColor = view3D
			? scene->GetRegistry().get<Camera3DComponent>(camera3DEntity).ClearColor
			: camera->GetClearColor();
		RenderSceneIntoFBO(fbo, *scene, layer2D.ViewProjection, layer2D.ViewAABB, /*withGizmos=*/true, /*sharedGizmosOnly=*/true, clearColor,
			false, false, EditorViewDrawMode::Default, GizmoLayerMask::All, view3D ? &*view3D : nullptr);

		if (!fbo.ReadPixelsRGBA8(outRgba)) {
			outError = "reading the rendered frame back from the GPU failed";
			return false;
		}
		for (size_t i = 3; i < outRgba.size(); i += 4) outRgba[i] = 255;
		return true;
	}

} // namespace Funke
