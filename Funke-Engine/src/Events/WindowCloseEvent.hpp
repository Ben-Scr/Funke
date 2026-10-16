#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class WindowCloseEvent : public FunkeEvent {
	public:
		WindowCloseEvent() = default;

		FNK_EVENT_CLASS_TYPE(WindowClose)
		FNK_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

} // namespace Funke
