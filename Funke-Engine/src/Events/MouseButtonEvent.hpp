#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class MouseButtonEvent : public FunkeEvent {
	public:
		int GetButton() const { return m_Button; }

		FNK_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton)

	protected:
		explicit MouseButtonEvent(int button)
			: m_Button(button) {
		}

		int m_Button;
	};

} // namespace Funke
