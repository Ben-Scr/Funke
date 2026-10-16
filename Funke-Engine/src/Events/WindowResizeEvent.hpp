#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class WindowResizeEvent : public FunkeEvent {
	public:
		WindowResizeEvent(int width, int height)
			: m_Width(width), m_Height(height) {
		}

		int GetWidth() const { return m_Width; }
		int GetHeight() const { return m_Height; }

		std::string ToString() const override {
			return std::string("WindowResizeEvent: ") + std::to_string(m_Width) + ", " + std::to_string(m_Height);
		}

		FNK_EVENT_CLASS_TYPE(WindowResize)
		FNK_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		int m_Width;
		int m_Height;
	};

} // namespace Funke
