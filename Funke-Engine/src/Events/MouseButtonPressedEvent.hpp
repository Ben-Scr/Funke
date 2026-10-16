#pragma once

#include "Events/MouseButtonEvent.hpp"

namespace Funke {

	class MouseButtonPressedEvent : public MouseButtonEvent {
	public:
		explicit MouseButtonPressedEvent(int button)
			: MouseButtonEvent(button) {
		}

		std::string ToString() const override {
			return std::string("MouseButtonPressedEvent: ") + std::to_string(m_Button);
		}

		FNK_EVENT_CLASS_TYPE(MouseButtonPressed)
	};

} // namespace Funke
