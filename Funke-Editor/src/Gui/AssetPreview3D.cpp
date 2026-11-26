#include "Gui/AssetPreview3D.hpp"
#include "Assets/AssetLoader.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/RenderApi.hpp"
#include "Gui/EditorUI.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Path.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace Funke {
	AssetPreview3D::AssetPreview3D() = default;
	AssetPreview3D::~AssetPreview3D() { Close(); }

	void AssetPreview3D::Open(const std::string& path) {
		if (path == m_Path && m_Error.empty()) {
			m_FocusPending = m_FramePending = true;
			return;
		}
		Close();
		m_Path = path;
		m_FocusPending = m_FramePending = true;
		m_Camera = EditorCamera3D{};
		m_Asset = AssetRegistry::GetGUID(path);
		m_IsMaterial = AssetRegistry::GetKind(m_Asset) == AssetKind::Material;
		m_Radius = m_IsMaterial ? 0.5f : 1.0f;
		AssetLoader::RetainAsset(m_Asset);
		Application* app = Application::GetInstance();
		if (!app || !app->GetRenderer3D()) {
			m_Error = "Enable the Funke.3D module to preview models and materials.";
			return;
		}
		if (!m_IsMaterial) {
			m_Load = AssetLoader::LoadAsync(m_Asset);
			if (!m_Load) m_Error = "Could not load this model.";
		}
	}

	void AssetPreview3D::Close() {
		if (m_Load) AssetLoader::Release(std::exchange(m_Load, 0));
		if (m_Asset) AssetLoader::ReleaseAsset(std::exchange(m_Asset, 0));
		m_Scene.reset();
		m_Target.Destroy();
		m_Path.clear();
		m_Error.clear();
		m_Revision = 0;
		m_Entity = entt::null;
		m_IsMaterial = false;
		m_Focused = m_Look = m_Pan = m_FramePending = m_FocusPending = false;
	}

	void AssetPreview3D::EnsureScene() {
		if (m_Scene) return;
		m_Scene = Scene::CreateDetachedScene("##AssetPreview3DView");
		m_Entity = m_Scene->CreateEntityHandle();
		auto& registry = m_Scene->GetRegistry();
		registry.emplace<Transform3DComponent>(m_Entity);
		auto& mesh = registry.emplace<MeshRendererComponent>(m_Entity);
		mesh.Mesh = PrimitiveMeshType::Sphere;
		mesh.Material = m_IsMaterial ? m_Asset : 0;
		mesh.CustomMesh = m_IsMaterial ? 0 : m_Asset;
		mesh.CastShadows = false;
		auto light = [&](Vec3 rotation, Color color, float intensity) {
			const auto entity = m_Scene->CreateEntityHandle();
			registry.emplace<Transform3DComponent>(entity).Rotation = rotation;
			auto& component = registry.emplace<DirectionalLightComponent>(entity);
			component.Color = color;
			component.Intensity = intensity;
			component.Ambient = Color{ 0.0f, 0.0f, 0.0f, 1.0f };
			component.CastShadows = false;
		};
		light(Vec3{ -35.0f, -35.0f, 0.0f }, Color{ 1.0f, 0.95f, 0.88f, 1.0f }, 3.0f);
		light(Vec3{ 15.0f, 65.0f, 0.0f }, Color{ 0.75f, 0.85f, 1.0f, 1.0f }, 1.2f);
		Transform3DHierarchySystem::Propagate(*m_Scene);
	}

	bool AssetPreview3D::UpdateModel(const MeshAsset& asset) {
		const glm::dvec3 minimum(asset.BoundsMin), maximum(asset.BoundsMax);
		const glm::dvec3 size = maximum - minimum;
		for (int axis = 0; axis < 3; ++axis) {
			if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || size[axis] < 0.0) return false;
		}
		const double extent = std::max({ size.x, size.y, size.z });
		if (extent <= 0.0 || !std::isfinite(static_cast<float>(1.0 / extent))) return false;
		EnsureScene();
		auto& transform = m_Scene->GetRegistry().get<Transform3DComponent>(m_Entity);
		transform.Scale = Vec3(static_cast<float>(1.0 / extent));
		transform.Position = glm::vec3(-(minimum + size * 0.5) / extent);
		m_Radius = static_cast<float>(glm::length(size / extent) * 0.5);
		m_Revision = asset.Revision;
		Transform3DHierarchySystem::Propagate(*m_Scene);
		return true;
	}

	void AssetPreview3D::Frame(float aspect) {
		m_Camera.Focus(glm::vec3(0.0f), m_Radius / std::min(aspect, 1.0f));
		m_FramePending = false;
	}

	void AssetPreview3D::DrawPreview(float width, float height) {
		Application* app = Application::GetInstance();
		Renderer3D* renderer = app ? app->GetRenderer3D() : nullptr;
		if (!renderer || !m_Target.Recreate(static_cast<int>(width), static_cast<int>(height))) return;
		auto settings = EditorPreferences::GetCamera3DSettings();
		settings.NavigationMode = EditorCamera3DNavigationMode::Orbit;
		m_Camera.SetSettings(settings);
		const float aspect = width / height;
		if (m_FramePending) Frame(aspect);
		Camera3DView view = m_Camera.GetView(aspect);
		view.Environment.Background = CameraBackground::Skybox;
		view.Environment.SkyTop = Color{ 0.85f, 0.88f, 0.95f, 1.0f };
		view.Environment.SkyHorizon = Color{ 0.65f, 0.65f, 0.65f, 1.0f };
		view.Environment.SkyGround = Color{ 0.12f, 0.13f, 0.16f, 1.0f };
		view.Environment.SkyAmbient = 0.8f;
		const RenderViewOptions previousOptions = renderer->GetViewOptions();
		const PolygonMode previousMode = RenderApi::GetPolygonMode();
		const Color previousClear = RenderApi::GetClearColor();
		RenderViewOptions options;
		options.Shadows = options.Fog = false;
		renderer->SetViewOptions(options);
		RenderApi::SetPolygonMode(PolygonMode::Filled);
		RenderApi::BindFramebuffer(m_Target);
		RenderApi::SetViewport(0, 0, static_cast<int>(width), static_cast<int>(height));
		const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		RenderApi::SetClearColor(Color{ background.x, background.y, background.z, 1.0f });
		RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
		renderer->RenderScene(*m_Scene, view);
		RenderApi::BindDefaultFramebuffer();
		if (Window* window = Application::GetWindow()) RenderApi::SetViewport(0, 0, window->GetWidth(), window->GetHeight());
		RenderApi::SetPolygonMode(previousMode);
		RenderApi::SetClearColor(previousClear);
		renderer->SetViewOptions(previousOptions);

		ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_Target.GetColorTextureBackendId())), ImVec2(width, height));
		const bool hovered = ImGui::IsItemHovered();
		const ImGuiIO& io = ImGui::GetIO();
		if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) m_Look = true;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) m_Look = false;
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) m_Pan = true;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_Pan = false;
		EditorCamera3D::Input input;
		input.Look = m_Look;
		input.Pan = m_Pan && !m_Look;
		input.MouseDelta = (m_Look || m_Pan) ? glm::vec2(io.MouseDelta.x, io.MouseDelta.y) : glm::vec2(0.0f);
		input.Scroll = hovered ? io.MouseWheel : 0.0f;
		m_Camera.Update(Time::GetDeltaTimeUnscaled(), input);
	}

	void AssetPreview3D::Render() {
		if (!IsOpen()) return;
		m_Focused = false;
		if (std::exchange(m_FocusPending, false)) ImGui::SetNextWindowFocus();
		const bool visible = ImGui::Begin("Editor View", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (!visible) {
			m_Look = m_Pan = false;
			ImGui::End();
			return;
		}
		m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		EditorUI::BeginToolbar("##AssetPreview3DToolbar");
		const bool back = EditorUI::Button("Back");
		EditorUI::Tooltip("Return to the scene or prefab (Esc)");
		ImGui::SameLine();
		ImGui::TextUnformatted(Path::ToUtf8(Path::FromUtf8(m_Path).filename()).c_str());
		ImGui::SameLine();
		ImGui::TextDisabled(m_IsMaterial ? "Material preview" : "Model preview");
		ImGui::SameLine();
		if (EditorUI::Button("Frame")) m_FramePending = true;
		EditorUI::Tooltip("Frame preview (F). Drag to orbit, middle-drag to pan, scroll to zoom.");
		EditorUI::EndToolbar();
		if (back || (m_Focused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
			Close();
			ImGui::End();
			return;
		}
		if (m_Focused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) m_FramePending = true;
		if (m_Load) {
			const auto result = AssetLoader::GetResult(m_Load);
			if (result.Status != AssetLoadStatus::Pending) {
				AssetLoader::Release(std::exchange(m_Load, 0));
				if (!result.Succeeded()) m_Error = result.Message.empty() ? "Could not load this model." : result.Message;
			}
		}
		if (m_Load) EditorUI::EmptyState("Loading model...");
		else if (!m_Error.empty()) EditorUI::EmptyState(m_Error.c_str());
		else {
			const char* unavailable = nullptr;
			if (m_IsMaterial) {
				const Material* material = MaterialLibrary::Get(m_Asset);
				if (!material) unavailable = "Could not load this material.";
				else {
					const ShaderDescription* shader = material->Shader ? CustomShaderLibrary::GetDescription(material->Shader) : nullptr;
					if (shader && shader->Target != ShaderTarget::Mesh3D)
						unavailable = "Sphere previews require a Standard or Mesh3D material.";
					else EnsureScene();
				}
			}
			else {
				const MeshAsset* asset = MeshLibrary::Get(m_Asset);
				if (!asset || ((m_Revision != asset->Revision || !m_Scene) && !UpdateModel(*asset)))
					unavailable = "This model has no renderable geometry.";
			}
			if (unavailable) EditorUI::EmptyState(unavailable);
			else {
				const ImVec2 size = ImGui::GetContentRegionAvail();
				if (size.x >= 1.0f && size.y >= 1.0f) DrawPreview(size.x, size.y);
			}
		}
		ImGui::End();
	}
}
