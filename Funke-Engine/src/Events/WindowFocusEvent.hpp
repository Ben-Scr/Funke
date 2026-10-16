#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class WindowFocusEvent : public FunkeEvent {
	public:
		WindowFocusEvent() = default;

		FNK_EVENT_CLASS_TYPE(WindowFocus)
		FNK_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

} // namespace Funke
