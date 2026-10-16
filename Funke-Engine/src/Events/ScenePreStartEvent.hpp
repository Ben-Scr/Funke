#pragma once

#include "Events/SceneEvent.hpp"

namespace Funke {

	class ScenePreStartEvent : public SceneEvent {
	public:
		explicit ScenePreStartEvent(const std::string& sceneName)
			: SceneEvent(sceneName) {
		}

		FNK_EVENT_CLASS_TYPE(ScenePreStart)
	};

} // namespace Funke
