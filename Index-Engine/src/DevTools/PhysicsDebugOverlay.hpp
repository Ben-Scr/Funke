#pragma once
#include "Core/Export.hpp"

namespace Index {

	// Runtime physics overlay (F8 / `physics.debug`): collider wireframes, triggers coloured, contact points and normals.
	class INDEX_API PhysicsDebugOverlay {
	public:
		static bool IsEnabled();
		static void SetEnabled(bool enabled);
		static bool GetShowContacts();
		static void SetShowContacts(bool show);

		// Queues this frame's drawings for every loaded scene; Application calls it after OnPreRender.
		static void QueueFrame();
	};

} // namespace Index
