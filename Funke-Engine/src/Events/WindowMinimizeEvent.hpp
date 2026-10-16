#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class WindowMinimizeEvent : public FunkeEvent {
	public:
		explicit WindowMinimizeEvent(bool minimized)
			: m_Minimized(minimized) {
		}

		bool IsMinimized() const { return m_Minimized; }

		FNK_EVENT_CLASS_TYPE(WindowMinimize)
		FNK_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		bool m_Minimized;
	};

} // namespace Funke
