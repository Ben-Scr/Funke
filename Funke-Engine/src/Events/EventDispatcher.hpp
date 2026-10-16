#pragma once

#include "Events/FunkeEvent.hpp"

namespace Funke {

	class EventDispatcher {
	public:
		explicit EventDispatcher(FunkeEvent& event)
			: m_Event(event) {
		}

		template<typename T, typename F>
		bool Dispatch(F&& func) {
			if (m_Event.GetEventType() == T::GetStaticType() && !m_Event.Handled) {
				m_Event.Handled = func(static_cast<T&>(m_Event));
				return true;
			}
			return false;
		}

	private:
		FunkeEvent& m_Event;
	};

} // namespace Funke
