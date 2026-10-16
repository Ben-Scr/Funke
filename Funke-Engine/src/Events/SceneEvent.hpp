#pragma once

#include "Events/FunkeEvent.hpp"

#include <string>

namespace Funke {

	class SceneEvent : public FunkeEvent {
	public:
		const std::string& GetSceneName() const { return m_SceneName; }

		FNK_EVENT_CLASS_CATEGORY(EventCategoryScene)

	protected:
		explicit SceneEvent(const std::string& sceneName)
			: m_SceneName(sceneName) {
		}

		std::string m_SceneName;
	};

} // namespace Funke
