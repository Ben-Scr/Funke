#pragma once

#include "Events/SceneEvent.hpp"

namespace Funke {

	class ScenePostStopEvent : public SceneEvent {
	public:
		explicit ScenePostStopEvent(const std::string& sceneName)
			: SceneEvent(sceneName) {
		}

		FNK_EVENT_CLASS_TYPE(ScenePostStop)
	};

} // namespace Funke
