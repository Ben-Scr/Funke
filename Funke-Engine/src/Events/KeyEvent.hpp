#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class KeyEvent : public FunkeEvent {
	public:
		int GetKeyCode() const { return m_KeyCode; }

		FNK_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryKeyboard)

	protected:
		explicit KeyEvent(int keyCode)
			: m_KeyCode(keyCode) {
		}

		int m_KeyCode;
	};

} // namespace Funke
