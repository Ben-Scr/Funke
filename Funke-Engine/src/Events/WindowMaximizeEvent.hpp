#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class WindowMaximizeEvent : public FunkeEvent {
	public:
		explicit WindowMaximizeEvent(bool maximized)
			: m_Maximized(maximized) {
		}

		bool IsMaximized() const { return m_Maximized; }

		FNK_EVENT_CLASS_TYPE(WindowMaximize)
		FNK_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		bool m_Maximized;
	};

} // namespace Funke
