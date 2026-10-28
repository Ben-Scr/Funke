#include <pch.hpp>
#include "Systems/ImGuiEditorLayer.hpp"
#include "Core/EngineModules.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/PrimitiveMesh.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/Renderer3D.hpp"
#include "Physics3D/PhysicsDebugDraw3D.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/EntityPicker.hpp"
#include "Scene/EntityPicker3D.hpp"
#include "Scene/Scene.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Systems/UILayoutSystem.hpp"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

// Editor View in 3D mode: orbit camera, mesh rendering through Renderer3D, ray picking and a full 3-axis gizmo.
namespace Funke {
	namespace {
		const Color k_GridColor{ 0.55f, 0.55f, 0.60f, 0.35f };
		const Color k_SelectionColor{ 1.0f, 0.62f, 0.15f, 1.0f };
		const Color k_CameraColor{ 0.85f, 0.85f, 0.85f, 0.9f };
		const Color k_LightColor{ 1.0f, 0.86f, 0.35f, 0.9f };

		bool s_OrbitActive = false;
		bool s_PanActive = false;

		// Children inherit the nearest ancestor that has a Transform3D (see Transform3DHierarchySystem).
		glm::mat4 FindParentWorld(const Scene& scene, EntityHandle entity) {
			const auto& registry = scene.GetRegistry();
			const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
			EntityHandle parent = hierarchy ? hierarchy->Parent : entt::null;
			for (int depth = 0; parent != entt::null && registry.valid(parent) && depth < 1024; ++depth) {
				if (const auto* transform = registry.try_get<Transform3DComponent>(parent)) {
					return transform->WorldMatrix;
				}
				const auto* parentHierarchy = registry.try_get<HierarchyComponent>(parent);
				parent = parentHierarchy ? parentHierarchy->Parent : entt::null;
			}
			return glm::mat4(1.0f);
		}

		// Selected: the real view volume, near to far plane, at the Game View's aspect (what GetProjectionMatrix
		// covers). Otherwise a short section of it near the camera, with a triangle on top marking its up side.
		void DrawCameraMarker(Renderer3D& renderer, const glm::mat4& world, const Camera3DComponent& camera,
			float aspect, bool selected) {
			const glm::vec3 origin(world[3]);
			const glm::vec3 right = glm::normalize(glm::vec3(world[0]));
			const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
			const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
			const bool perspective = camera.Projection == CameraProjection::Perspective;
			const float nearClip = std::max(camera.NearClip, 0.0001f);
			const float farClip = std::max(camera.FarClip, nearClip + 0.001f);
			const float farDepth = selected ? farClip : std::min(farClip, nearClip + 2.0f);
			const float tanHalfFov = std::tan(glm::radians(std::clamp(camera.FieldOfView, 1.0f, 179.0f)) * 0.5f);
			const auto planeCorners = [&](float depth, glm::vec3 (&out)[4]) {
				const float halfHeight = perspective ? tanHalfFov * depth : std::max(camera.OrthographicSize, 0.001f);
				const float halfWidth = halfHeight * aspect;
				const glm::vec3 center = origin + forward * depth;
				out[0] = center + right * halfWidth + up * halfHeight;
				out[1] = center - right * halfWidth + up * halfHeight;
				out[2] = center - right * halfWidth - up * halfHeight;
				out[3] = center + right * halfWidth - up * halfHeight;
			};
			glm::vec3 nearCorners[4];
			glm::vec3 farCorners[4];
			planeCorners(nearClip, nearCorners);
			planeCorners(farDepth, farCorners);
			for (int i = 0; i < 4; ++i) {
				renderer.DrawLine(nearCorners[i], nearCorners[(i + 1) % 4], k_CameraColor);
				renderer.DrawLine(farCorners[i], farCorners[(i + 1) % 4], k_CameraColor);
				renderer.DrawLine(perspective && !selected ? origin : nearCorners[i], farCorners[i], k_CameraColor);
			}
			if (!selected) {
				const glm::vec3 topLeft = farCorners[1];
				const glm::vec3 topRight = farCorners[0];
				const float width = glm::length(topRight - topLeft);
				const glm::vec3 baseLeft = glm::mix(topLeft, topRight, 0.3f);
				const glm::vec3 baseRight = glm::mix(topLeft, topRight, 0.7f);
				const glm::vec3 apex = glm::mix(topLeft, topRight, 0.5f) + up * (width * 0.2f);
				renderer.DrawLine(baseLeft, apex, k_CameraColor);
				renderer.DrawLine(apex, baseRight, k_CameraColor);
				renderer.DrawLine(baseRight, baseLeft, k_CameraColor);
			}
		}

		void DrawLightMarker(Renderer3D& renderer, const glm::mat4& world) {
			const glm::vec3 origin(world[3]);
			const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
			const glm::vec3 right = glm::normalize(glm::vec3(world[0]));
			const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
			for (int i = 0; i < 4; ++i) {
				const float angle = static_cast<float>(i) * 1.5707963f;
				const glm::vec3 offset = (right * std::cos(angle) + up * std::sin(angle)) * 0.2f;
				renderer.DrawLine(origin + offset, origin + offset + forward * 1.5f, k_LightColor);
			}
			renderer.DrawWireBox(world, glm::vec3(0.15f), k_LightColor);
		}

		void DrawWireSphere(Renderer3D& renderer, const glm::vec3& center, float radius, const Color& color) {
			renderer.DrawWireCircle(center, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, radius, color);
			renderer.DrawWireCircle(center, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, radius, color);
			renderer.DrawWireCircle(center, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, radius, color);
		}

		// Small bulb for every point light; the range sphere only while selected (it would cover the view otherwise).
		void DrawPointLightMarker(Renderer3D& renderer, const glm::mat4& world, const PointLightComponent& light, bool selected) {
			const glm::vec3 origin(world[3]);
			DrawWireSphere(renderer, origin, 0.2f, k_LightColor);
			if (selected) {
				DrawWireSphere(renderer, origin, std::max(light.Range, 0.0f), Color(k_LightColor.r, k_LightColor.g, k_LightColor.b, 0.45f));
			}
		}

		const Color k_ParticleShapeColor{ 0.45f, 0.75f, 1.0f, 0.9f };

		// Circle in the emitter's local space, so the outline follows the transform's scale like the emission does.
		void DrawLocalArc(Renderer3D& renderer, const glm::mat4& world, const glm::vec3& center, const glm::vec3& axisA,
			const glm::vec3& axisB, float radius, float sweep, const Color& color) {
			constexpr int k_Segments = 48;
			const int segments = std::max(4, static_cast<int>(k_Segments * sweep / glm::two_pi<float>()));
			glm::vec3 previous(world * glm::vec4(center + axisA * radius, 1.0f));
			for (int i = 1; i <= segments; ++i) {
				const float angle = sweep * static_cast<float>(i) / static_cast<float>(segments);
				const glm::vec3 point(world * glm::vec4(center + (axisA * std::cos(angle) + axisB * std::sin(angle)) * radius, 1.0f));
				renderer.DrawLine(previous, point, color);
				previous = point;
			}
		}

		void DrawParticleShapeGizmo(Renderer3D& renderer, const glm::mat4& world, const ParticleSystem3DComponent& ps) {
			const glm::vec3 x(1.0f, 0.0f, 0.0f), y(0.0f, 1.0f, 0.0f), z(0.0f, 0.0f, 1.0f), origin(0.0f);
			const float radius = std::max(ps.Radius, 0.0f);
			constexpr float k_Full = glm::two_pi<float>();
			switch (ps.Shape) {
				case ParticleShape3D::Sphere:
					DrawLocalArc(renderer, world, origin, x, y, radius, k_Full, k_ParticleShapeColor);
					DrawLocalArc(renderer, world, origin, x, z, radius, k_Full, k_ParticleShapeColor);
					DrawLocalArc(renderer, world, origin, y, z, radius, k_Full, k_ParticleShapeColor);
					break;
				case ParticleShape3D::Hemisphere:
					DrawLocalArc(renderer, world, origin, x, z, radius, k_Full, k_ParticleShapeColor);
					DrawLocalArc(renderer, world, origin, x, y, radius, glm::pi<float>(), k_ParticleShapeColor);
					DrawLocalArc(renderer, world, origin, z, y, radius, glm::pi<float>(), k_ParticleShapeColor);
					break;
				case ParticleShape3D::Cone: {
					// One unit tall: shows the spread, not the travel distance.
					constexpr float k_Height = 1.0f;
					const float topRadius = radius + std::tan(glm::radians(std::clamp(ps.ConeAngle, 0.0f, 89.9f))) * k_Height;
					DrawLocalArc(renderer, world, origin, x, z, radius, k_Full, k_ParticleShapeColor);
					DrawLocalArc(renderer, world, y * k_Height, x, z, topRadius, k_Full, k_ParticleShapeColor);
					for (const glm::vec3& side : { x, -x, z, -z }) {
						renderer.DrawLine(glm::vec3(world * glm::vec4(side * radius, 1.0f)),
							glm::vec3(world * glm::vec4(y * k_Height + side * topRadius, 1.0f)), k_ParticleShapeColor);
					}
					break;
				}
				case ParticleShape3D::Box:
					renderer.DrawWireBox(world, glm::abs(glm::vec3(ps.BoxSize)) * 0.5f, k_ParticleShapeColor);
					break;
			}
		}

		void DrawSpotLightMarker(Renderer3D& renderer, const glm::mat4& world, const SpotLightComponent& light, bool selected) {
			const glm::vec3 origin(world[3]);
			const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
			const glm::vec3 right = glm::normalize(glm::vec3(world[0]));
			const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
			const float length = selected ? std::max(light.Range, 0.0f) : 1.0f;
			const float outer = glm::radians(std::clamp(light.OuterAngle, 0.1f, 89.9f));
			const float radius = std::tan(outer) * length;
			const glm::vec3 end = origin + forward * length;
			for (int i = 0; i < 4; ++i) {
				const float angle = static_cast<float>(i) * 1.5707963f;
				renderer.DrawLine(origin, end + (right * std::cos(angle) + up * std::sin(angle)) * radius, k_LightColor);
			}
			renderer.DrawWireCircle(end, right, up, radius, k_LightColor);
			if (selected) {
				const float inner = glm::radians(std::clamp(light.InnerAngle, 0.0f, 89.9f));
				renderer.DrawWireCircle(end, right, up, std::tan(inner) * length, Color(k_LightColor.r, k_LightColor.g, k_LightColor.b, 0.45f));
			}
			DrawWireSphere(renderer, origin, 0.15f, k_LightColor);
		}
	}

	void ImGuiEditorLayer::ResolveEditorViewMode() {
		if (m_EditorViewModeResolved) return;
		m_EditorViewModeResolved = true;
		if (const FunkeProject* project = ProjectManager::GetCurrentProject()) {
			m_EditorView3D = project->EditorView3D.value_or(project->PrefersThreeD());
		}
		if (!EngineModules::IsActive(EngineModule::ThreeD)) {
			m_EditorView3D = false;
		}
		else if (!EngineModules::IsActive(EngineModule::TwoD)) {
			m_EditorView3D = true;
		}
	}

	void ImGuiEditorLayer::SetEditorView3D(bool enabled) {
		if (!EngineModules::IsActive(EngineModule::TwoD) || !EngineModules::IsActive(EngineModule::ThreeD)) return;
		if (m_EditorView3D == enabled) return;
		m_EditorView3D = enabled;
		if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
			project->EditorView3D = m_EditorView3D;
			project->Save();
		}
	}

	Entity ImGuiEditorLayer::CreateDefaultEntity(Scene& scene, const std::string& name) {
		if (!m_EditorView3D) {
			return scene.CreateEntity(name);
		}
		Entity entity = scene.CreateEmptyEntity();
		if (!name.empty()) {
			entity.AddComponent<NameComponent>(name);
		}
		entity.AddComponent<Transform3DComponent>();
		return entity;
	}

	void ImGuiEditorLayer::FrameEditorCamera3D(Scene& scene) {
		Transform3DHierarchySystem::Propagate(scene);
		glm::vec3 boundsMin(std::numeric_limits<float>::max());
		glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
		bool any = false;
		const auto& registry = scene.GetRegistry();
		for (auto [entity, transform] : registry.view<const Transform3DComponent>().each()) {
			if (!registry.any_of<MeshRendererComponent, SkinnedMeshRendererComponent>(entity)) continue;
			const LocalBounds3D bounds = EntityPicker3D::GetLocalBounds(scene, entity);
			const glm::vec3& halfExtents = bounds.HalfExtents;
			for (int corner = 0; corner < 8; ++corner) {
				const glm::vec3 local = bounds.Center + glm::vec3((corner & 1) ? halfExtents.x : -halfExtents.x,
					(corner & 2) ? halfExtents.y : -halfExtents.y,
					(corner & 4) ? halfExtents.z : -halfExtents.z);
				const glm::vec3 world(transform.WorldMatrix * glm::vec4(local, 1.0f));
				boundsMin = glm::min(boundsMin, world);
				boundsMax = glm::max(boundsMax, world);
				any = true;
			}
		}
		if (any) {
			m_EditorCamera3D.Focus((boundsMin + boundsMax) * 0.5f, glm::length(boundsMax - boundsMin) * 0.5f);
		}
	}

	void ImGuiEditorLayer::FocusSelectedEntity3D(Scene& scene) {
		// F is a fly key's neighbour; ignore it while RMB-flying.
		if (s_OrbitActive || m_SelectedEntity == entt::null || !scene.IsValid(m_SelectedEntity)) return;
		Transform3DHierarchySystem::Propagate(scene);
		const auto* transform = scene.GetRegistry().try_get<Transform3DComponent>(m_SelectedEntity);
		if (!transform) {
			// Sprites and UI sit on the z = 0 plane in the 3D view.
			AABB bounds;
			if (!TryBuildEntityAABB(scene, m_SelectedEntity, true, bounds)) return;
			const Vec2 size = bounds.Scale();
			m_EditorCamera3D.Focus({ (bounds.Min.x + bounds.Max.x) * 0.5f, (bounds.Min.y + bounds.Max.y) * 0.5f, 0.0f },
				std::max(0.5f, glm::length(glm::vec2(size.x, size.y)) * 0.5f));
			return;
		}
		const LocalBounds3D bounds = EntityPicker3D::GetHierarchyBounds(scene, m_SelectedEntity);
		const glm::vec3& halfExtents = bounds.HalfExtents;
		const glm::vec3 worldExtents(
			glm::length(glm::vec3(transform->WorldMatrix[0])) * halfExtents.x,
			glm::length(glm::vec3(transform->WorldMatrix[1])) * halfExtents.y,
			glm::length(glm::vec3(transform->WorldMatrix[2])) * halfExtents.z);
		m_EditorCamera3D.Focus(glm::vec3(transform->WorldMatrix * glm::vec4(bounds.Center, 1.0f)), glm::length(worldExtents));
	}

	void ImGuiEditorLayer::QueueEditorView3DOverlays(Scene& scene, Renderer3D& renderer) {
		const glm::vec3 focus = m_EditorCamera3D.GetFocus();
		// Grid follows the camera in whole cells so it never visibly slides.
		const float spacing = m_EditorCamera3D.GetDistance() > 60.0f ? 10.0f : 1.0f;
		renderer.DrawGrid({ std::round(focus.x / spacing) * spacing, 0.0f, std::round(focus.z / spacing) * spacing }, 40, spacing, k_GridColor);

		const auto& registry = scene.GetRegistry();
		if (m_ShowGizmos) {
			float cameraAspect = GetGameViewAspect();
			if (cameraAspect <= 0.0f && m_GameViewFBO.IsValid() && m_GameViewFBO.GetHeight() > 0) {
				cameraAspect = static_cast<float>(m_GameViewFBO.GetWidth()) / static_cast<float>(m_GameViewFBO.GetHeight());
			}
			if (cameraAspect <= 0.0f) cameraAspect = 16.0f / 9.0f;
			for (auto [entity, camera, transform] : registry.view<const Camera3DComponent, const Transform3DComponent>().each()) {
				DrawCameraMarker(renderer, transform.WorldMatrix, camera, cameraAspect, IsEntitySelected(entity));
			}
			for (auto [entity, light, transform] : registry.view<const DirectionalLightComponent, const Transform3DComponent>().each()) {
				(void)light;
				DrawLightMarker(renderer, transform.WorldMatrix);
			}
			for (auto [entity, light, transform] : registry.view<const PointLightComponent, const Transform3DComponent>().each()) {
				DrawPointLightMarker(renderer, transform.WorldMatrix, light, IsEntitySelected(entity));
			}
			for (auto [entity, light, transform] : registry.view<const SpotLightComponent, const Transform3DComponent>().each()) {
				DrawSpotLightMarker(renderer, transform.WorldMatrix, light, IsEntitySelected(entity));
			}
			// Idle emitters draw nothing in edit mode; a small marker keeps them findable.
			for (auto [entity, particles, transform] : registry.view<const ParticleSystem3DComponent, const Transform3DComponent>().each()) {
				(void)particles;
				DrawWireSphere(renderer, glm::vec3(transform.WorldMatrix[3]), 0.15f, k_ParticleShapeColor);
			}
		}

		for (EntityHandle selected : GetSelectedEntities(scene)) {
			if (const auto* transform = registry.try_get<Transform3DComponent>(selected)) {
				const LocalBounds3D bounds = EntityPicker3D::GetHierarchyBounds(scene, selected);
				renderer.DrawWireBox(transform->WorldMatrix * glm::translate(glm::mat4(1.0f), bounds.Center), bounds.HalfExtents * 1.02f, k_SelectionColor);
				PhysicsDebugDraw3D::DrawColliders(renderer, scene, selected, transform->WorldMatrix);
				if (const auto* particles = registry.try_get<ParticleSystem3DComponent>(selected)) {
					DrawParticleShapeGizmo(renderer, transform->WorldMatrix, *particles);
				}
			}
			else if (AABB bounds; TryBuildEntityAABB(scene, selected, false, bounds)) {
				// 2D entity: outline its bounds on the z = 0 plane it is drawn on.
				const Vec2 size = bounds.Scale();
				const glm::vec3 center{ (bounds.Min.x + bounds.Max.x) * 0.5f, (bounds.Min.y + bounds.Max.y) * 0.5f, 0.0f };
				renderer.DrawWireBox(glm::translate(glm::mat4(1.0f), center),
					{ std::abs(size.x) * 0.5f, std::abs(size.y) * 0.5f, 0.001f }, k_SelectionColor);
			}
		}
	}

	Vec2 ImGuiEditorLayer::RenderEditorView3D(Scene& scene, const Vec2& viewportSize, bool overlayHovered) {
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		Application* app = Application::GetInstance();
		Renderer3D* renderer3D = app ? app->GetRenderer3D() : nullptr;
		const int width = static_cast<int>(viewportSize.x);
		const int height = static_cast<int>(viewportSize.y);
		m_EditorViewFBO.Recreate(width, height);
		if (!renderer3D || !m_EditorViewFBO.IsValid()) {
			ImGui::TextDisabled("3D rendering is unavailable");
			return { cursor.x, cursor.y };
		}

		// Camera input: a drag begun inside the view keeps steering even when the cursor leaves it.
		const ImGuiIO& io = ImGui::GetIO();
		const bool hovered = m_IsEditorViewHovered && !overlayHovered;
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered) s_OrbitActive = true;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) s_OrbitActive = false;
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && hovered) s_PanActive = true;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) s_PanActive = false;

		EditorCamera3D::Input cameraInput;
		cameraInput.Orbit = s_OrbitActive;
		cameraInput.Pan = s_PanActive && !s_OrbitActive;
		cameraInput.MouseDelta = (cameraInput.Orbit || cameraInput.Pan) ? glm::vec2(io.MouseDelta.x, io.MouseDelta.y) : glm::vec2(0.0f);
		cameraInput.Scroll = hovered ? io.MouseWheel : 0.0f;
		if (s_OrbitActive) {
			cameraInput.Fly = {
				(ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f),
				(ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f),
				(ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f),
			};
			cameraInput.Fast = io.KeyShift;
		}
		m_EditorCamera3D.Update(Time::GetDeltaTimeUnscaled(), cameraInput);

		// First look at a scene frames its meshes, like a 2D scene opening on its camera.
		if (m_EditorCamera3DFramedScene != scene.GetName()) {
			m_EditorCamera3DFramedScene = scene.GetName();
			FrameEditorCamera3D(scene);
		}
		if (m_EditorCamera3DFocusSelectionPending) {
			m_EditorCamera3DFocusSelectionPending = false;
			FocusSelectedEntity3D(scene);
		}

		Camera3DView view = m_EditorCamera3D.GetView(viewportSize.x / viewportSize.y);
		Transform3DHierarchySystem::Propagate(scene);
		// The scene's main camera decides sky and sky ambient, so the Editor View lights meshes like the game does.
		const EntityHandle mainCamera = Camera3DComponent::FindMain(scene);
		if (mainCamera != entt::null && !IsInPrefabEditMode()) {
			view.Environment = Camera3DEnvironment::FromCamera(scene.GetRegistry().get<Camera3DComponent>(mainCamera));
		}
		else {
			view.Environment.Background = CameraBackground::SolidColor;
		}
		QueueEditorView3DOverlays(scene, *renderer3D);

		static const Color k_EditorClearColor(0.18f, 0.18f, 0.20f, 1.0f);
		const Color prefabClearColor(k_PrefabEditClearR, k_PrefabEditClearG, k_PrefabEditClearB, 1.0f);
		const AABB everything{ Vec2{ -1.0e9f, -1.0e9f }, Vec2{ 1.0e9f, 1.0e9f } };
		auto* ppRenderer = app->GetRenderer2D();
		if (ppRenderer) ppRenderer->SetPostProcessingEnabled(m_ShowPostProcessing);
		renderer3D->SetPostProcessingEnabled(m_ShowPostProcessing);
		RenderSceneIntoFBO(m_EditorViewFBO, scene, view.Projection * view.View, everything,
			false, false, IsInPrefabEditMode() ? prefabClearColor : k_EditorClearColor, IsInPrefabEditMode(), true,
			m_EditorViewDrawMode, GizmoLayerMask::EditorOnly, &view);
		if (ppRenderer) ppRenderer->SetPostProcessingEnabled(true);
		renderer3D->SetPostProcessingEnabled(true);

		ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_EditorViewFBO.GetColorTextureBackendId())),
			ImVec2(viewportSize.x, viewportSize.y));
		const bool imageHovered = ImGui::IsItemHovered();
		const ImVec2 imageTopLeft = ImGui::GetItemRectMin();
		m_EditorViewImageX = imageTopLeft.x;
		m_EditorViewImageY = imageTopLeft.y;
		m_EditorViewImageW = viewportSize.x;
		m_EditorViewImageH = viewportSize.y;

		const Vec2 topLeft{ imageTopLeft.x, imageTopLeft.y };
		const bool gizmoActive = DrawTransform3DGizmo(scene, view, topLeft, viewportSize);

		if (imageHovered && !overlayHovered && !gizmoActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			const ImVec2 mouse = ImGui::GetMousePos();
			const float ndcX = (mouse.x - imageTopLeft.x) / viewportSize.x * 2.0f - 1.0f;
			const float ndcY = 1.0f - (mouse.y - imageTopLeft.y) / viewportSize.y * 2.0f;
			const glm::mat4 inverseViewProjection = glm::inverse(view.Projection * view.View);
			glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
			glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
			nearPoint /= nearPoint.w;
			farPoint /= farPoint.w;

			const glm::vec3 rayOrigin(nearPoint);
			const glm::vec3 rayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));
			// Sprites and UI are drawn over the meshes on the z = 0 plane, so their hits come first.
			std::vector<EntityHandle> stack;
			if (EngineModules::IsActive(EngineModule::TwoD) && std::abs(rayDirection.z) > 1e-6f) {
				const float t = -rayOrigin.z / rayDirection.z;
				if (t > 0.0f) {
					const glm::vec3 onPlane = rayOrigin + rayDirection * t;
					TransformHierarchySystem::Propagate(scene);
					ComputeUILayout(scene);
					EntityPicker::PickEntitiesAtPoint(scene, Vec2{ onPlane.x, onPlane.y }, stack, /*pixelPrecise*/ true);
				}
			}
			std::vector<EntityHandle> meshHits;
			EntityPicker3D::PickAll(scene, rayOrigin, rayDirection, meshHits);
			stack.insert(stack.end(), meshHits.begin(), meshHits.end());
			const bool hasModifier = io.KeyCtrl || io.KeyShift;
			if (stack.empty()) {
				if (!hasModifier) ClearEntitySelection();
				m_PickCycleStack.clear();
			}
			else if (hasModifier) {
				ToggleEntitySelection(stack.front(), -1);
				m_PickCycleStack.clear();
			}
			else {
				// Same cycling as the 2D view: a repeat click on the same spot steps to the next entity behind.
				EntityHandle pick = stack.front();
				const float dx = mouse.x - m_PickCycleScreenPos.x;
				const float dy = mouse.y - m_PickCycleScreenPos.y;
				constexpr float k_PickCycleTolPx = 3.0f;
				if (dx * dx + dy * dy <= k_PickCycleTolPx * k_PickCycleTolPx && stack == m_PickCycleStack) {
					const auto it = std::find(stack.begin(), stack.end(), m_SelectedEntity);
					if (it != stack.end()) {
						pick = stack[(static_cast<size_t>(it - stack.begin()) + 1) % stack.size()];
					}
				}
				SetSingleEntitySelection(pick, -1);
				m_PickCycleScreenPos = Vec2{ mouse.x, mouse.y };
				m_PickCycleStack = std::move(stack);
			}
		}

		return topLeft;
	}

	bool ImGuiEditorLayer::DrawTransform3DGizmo(Scene& scene, const Camera3DView& view, const Vec2& imageTopLeft, const Vec2& viewportSize) {
		auto commitDrag = [&]() {
			if (!m_Gizmo3DDragActive) return;
			m_Gizmo3DDragActive = false;
			EntityHandle handle = entt::null;
			if (!scene.TryResolveEntityRef(m_Gizmo3DDragEntityId, handle) || !scene.IsValid(handle)) return;
			const auto* transform = scene.GetRegistry().try_get<Transform3DComponent>(handle);
			if (!transform) return;
			const Transform3DEditCommand::State after{ transform->Position, transform->Rotation, transform->Scale };
			if (!(after == m_Gizmo3DDragBefore)) {
				GetUndoStack(scene).Push(std::make_unique<Transform3DEditCommand>(m_Gizmo3DDragEntityId, m_Gizmo3DDragBefore, after));
			}
		};

		Transform3DComponent* transform = nullptr;
		const bool hasTarget = m_GizmoMode != EditorGizmoMode::None
			&& m_GizmoMode != EditorGizmoMode::Bounds
			&& m_SelectedEntity != entt::null
			&& scene.IsValid(m_SelectedEntity)
			&& scene.TryGetComponent<Transform3DComponent>(m_SelectedEntity, transform) && transform;
		if (!hasTarget) {
			commitDrag();
			return false;
		}

		ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
		ImGuizmo::MODE space = ImGuizmo::WORLD;
		const bool snapOverride = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
		float snapValues[3] = { 0.0f, 0.0f, 0.0f };
		const float* snap = nullptr;
		if (m_GizmoMode == EditorGizmoMode::Rotate) {
			operation = ImGuizmo::ROTATE;
			space = ImGuizmo::LOCAL;
			if (EditorPreferences::GetRotationSnapEnabled() || snapOverride) {
				snapValues[0] = snapValues[1] = snapValues[2] = EditorPreferences::GetRotationSnapDegrees();
				snap = snapValues;
			}
		}
		else if (m_GizmoMode == EditorGizmoMode::Scale) {
			operation = ImGuizmo::SCALE;
			space = ImGuizmo::LOCAL;
			if (EditorPreferences::GetScaleSnapEnabled() || snapOverride) {
				snapValues[0] = snapValues[1] = snapValues[2] = EditorPreferences::GetScaleSnap();
				snap = snapValues;
			}
		}
		else if (EditorPreferences::GetGridSnapEnabled() || snapOverride) {
			snapValues[0] = snapValues[1] = snapValues[2] = EditorPreferences::GetGridSizeX();
			snap = snapValues;
		}

		ImGuizmo::BeginFrame();
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(imageTopLeft.x, imageTopLeft.y, viewportSize.x, viewportSize.y);
		ImGuizmo::AllowAxisFlip(true);
		// The 2D view narrows these; they are global ImGuizmo state, so restore the defaults every frame.
		ImGuizmo::SetAxisMask(false, false, false);
		ImGuizmo::SetAxisLimit(0.0025f);
		ImGuizmo::SetPlaneLimit(0.02f);

		glm::mat4 world = transform->WorldMatrix;
		ImGuizmo::Manipulate(&view.View[0][0], &view.Projection[0][0], operation, space, &world[0][0], nullptr, snap);

		if (ImGuizmo::IsUsing()) {
			if (!m_Gizmo3DDragActive) {
				m_Gizmo3DDragActive = true;
				m_Gizmo3DDragEntityId = scene.GetEntityPersistentID(m_SelectedEntity);
				m_Gizmo3DDragBefore = { transform->Position, transform->Rotation, transform->Scale };
			}

			Vec3 position, rotation, scale;
			Transform3DComponent::Decompose(glm::inverse(FindParentWorld(scene, m_SelectedEntity)) * world, position, rotation, scale);
			// Write only the manipulated channel so decomposition noise never leaks into the others.
			if (operation == ImGuizmo::TRANSLATE) transform->Position = position;
			else if (operation == ImGuizmo::ROTATE) transform->Rotation = rotation;
			else transform->Scale = scale;
			Transform3DHierarchySystem::Propagate(scene);
			scene.MarkDirty();
		}
		else {
			commitDrag();
		}

		return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
	}

}
