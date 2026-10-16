#pragma once

#include "Events/SceneEvent.hpp"

namespace Funke {

	class ScenePostStartEvent : public SceneEvent {
	public:
		explicit ScenePostStartEvent(const std::string& sceneName)
			: SceneEvent(sceneName) {
		}

		FNK_EVENT_CLASS_TYPE(ScenePostStart)
	};

} // namespace Funke
