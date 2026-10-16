#pragma once
#include "Scene/Entity.hpp"
#include <cstddef>
#include <functional>
#include <span>

namespace Funke {

	class Scene;

	// Multi-entity: hides components present on ALL entities; conflict checks use the union,
	// so an entry is disabled if adding it would violate conflictsWith on ANY selected entity.
	// outChanged signals callers (e.g. component-intersection cache) to invalidate this frame.
	void RenderAddComponentPopup(
		const char* popupId,
		Scene& scene,
		std::span<const Entity> entities,
		char* searchBuffer,
		std::size_t searchBufferSize,
		bool* outChanged = nullptr,
		// Runs right before a component is added, e.g. to snapshot the entities for undo.
		const std::function<void()>& onBeforeChange = {});

}
