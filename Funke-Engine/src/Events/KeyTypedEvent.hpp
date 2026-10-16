#pragma once

#include "Events/KeyEvent.hpp"

namespace Funke {

	class KeyTypedEvent : public KeyEvent {
	public:
		explicit KeyTypedEvent(int keyCode)
			: KeyEvent(keyCode) {
		}

		FNK_EVENT_CLASS_TYPE(KeyTyped)
	};

} // namespace Funke
