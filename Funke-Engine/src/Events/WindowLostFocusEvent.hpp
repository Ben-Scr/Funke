#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class WindowLostFocusEvent : public FunkeEvent {
	public:
		WindowLostFocusEvent() = default;

		FNK_EVENT_CLASS_TYPE(WindowLostFocus)
		FNK_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

} // namespace Funke
