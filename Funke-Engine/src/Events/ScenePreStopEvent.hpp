#pragma once

#include "Events/SceneEvent.hpp"

namespace Funke {

	class ScenePreStopEvent : public SceneEvent {
	public:
		explicit ScenePreStopEvent(const std::string& sceneName)
			: SceneEvent(sceneName) {
		}

		FNK_EVENT_CLASS_TYPE(ScenePreStop)
	};

} // namespace Funke
