#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class MouseMovedEvent : public FunkeEvent {
	public:
		MouseMovedEvent(float x, float y)
			: m_X(x), m_Y(y) {
		}

		float GetX() const { return m_X; }
		float GetY() const { return m_Y; }

		std::string ToString() const override {
			return std::string("MouseMovedEvent: ") + std::to_string(m_X) + ", " + std::to_string(m_Y);
		}

		FNK_EVENT_CLASS_TYPE(MouseMoved)
		FNK_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse)

	private:
		float m_X;
		float m_Y;
	};

} // namespace Funke
