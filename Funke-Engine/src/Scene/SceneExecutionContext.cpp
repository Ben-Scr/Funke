#include "pch.hpp"
#include "Scene/SceneExecutionContext.hpp"

#include "Core/Application.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

namespace Funke {
	namespace {
		thread_local Scene* s_CurrentExecutionScene = nullptr;
	}

	SceneExecutionScope::SceneExecutionScope(Scene& scene) noexcept
		: m_PreviousScene(s_CurrentExecutionScene)
	{
		s_CurrentExecutionScene = &scene;
	}

	SceneExecutionScope::~SceneExecutionScope()
	{
		s_CurrentExecutionScene = m_PreviousScene;
	}

	Scene* GetCurrentExecutionScene() noexcept
	{
		return s_CurrentExecutionScene;
	}

	Scene* ResolveImplicitScene() noexcept
	{
		if (Scene* scene = GetCurrentExecutionScene()) {
			return scene;
		}

		Application* app = Application::GetInstance();
		if (!app || !app->GetSceneManager()) {
			return nullptr;
		}

		Scene* scene = SceneManager::GetActiveScene();
		return scene && scene->IsLoaded() ? scene : nullptr;
	}
}
