#pragma once

#include "Core/Export.hpp"

namespace Funke {
	class Scene;

	class FUNKE_API SceneExecutionScope final {
	public:
		explicit SceneExecutionScope(Scene& scene) noexcept;
		~SceneExecutionScope();

		SceneExecutionScope(const SceneExecutionScope&) = delete;
		SceneExecutionScope& operator=(const SceneExecutionScope&) = delete;
		SceneExecutionScope(SceneExecutionScope&&) = delete;
		SceneExecutionScope& operator=(SceneExecutionScope&&) = delete;

	private:
		Scene* m_PreviousScene = nullptr;
	};

	FUNKE_API Scene* GetCurrentExecutionScene() noexcept;
	FUNKE_API Scene* ResolveImplicitScene() noexcept;
}
