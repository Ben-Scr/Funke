#include "pch.hpp"
#include "DevTools/PhysicsDebugOverlay.hpp"

#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#if FUNKE_WITH_PHYSICS && FUNKE_WITH_2D
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Physics/PhysicsDebugDraw2D.hpp"
#endif
#if FUNKE_WITH_PHYSICS && FUNKE_WITH_3D
#include "Graphics/Renderer3D.hpp"
#include "Physics3D/PhysicsDebugDraw3D.hpp"
#endif

#include <algorithm>

namespace Funke {

	namespace {
		bool g_Enabled = false;
		bool g_ShowContacts = true;
	}

	bool PhysicsDebugOverlay::IsEnabled() { return g_Enabled; }
	void PhysicsDebugOverlay::SetEnabled(bool enabled) { g_Enabled = enabled; }
	bool PhysicsDebugOverlay::GetShowContacts() { return g_ShowContacts; }
	void PhysicsDebugOverlay::SetShowContacts(bool show) { g_ShowContacts = show; }

	void PhysicsDebugOverlay::QueueFrame() {
		if (!g_Enabled) return;
		Application* app = Application::GetInstance();
		if (!app) return;

#if FUNKE_WITH_PHYSICS && FUNKE_WITH_2D
		float worldPerPixel = 0.01f;
		if (const Camera2DComponent* camera = Camera2DComponent::GetMain()) {
			const int height = app->GetWindow() ? std::max(1, app->GetWindow()->GetHeight()) : 1;
			worldPerPixel = camera->GetViewportAABB().Scale().y / static_cast<float>(height);
		}
#endif
#if FUNKE_WITH_PHYSICS && FUNKE_WITH_3D
		Renderer3D* renderer3D = app->GetRenderer3D();
#endif

		SceneManager::ForeachLoadedScene([&](Scene& scene) {
#if FUNKE_WITH_PHYSICS && FUNKE_WITH_2D
			PhysicsDebugDraw2D::DrawScene(scene, g_ShowContacts, worldPerPixel);
#endif
#if FUNKE_WITH_PHYSICS && FUNKE_WITH_3D
			if (renderer3D) PhysicsDebugDraw3D::DrawScene(*renderer3D, scene, g_ShowContacts);
#endif
			(void)scene;
		});
	}

} // namespace Funke
