#pragma once

#include "Core/Export.hpp"

namespace Index {
	class Scene;

	class INDEX_API SceneExecutionScope final {
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

	INDEX_API Scene* GetCurrentExecutionScene() noexcept;
	INDEX_API Scene* ResolveImplicitScene() noexcept;
}
