#pragma once

#include "Inspector/PropertyValue.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Funke {

	class Scene;

	struct StructureReferenceSnapshot {
		uint64_t OwnerId = 0;
		std::string ComponentName;
		std::vector<std::string> PropertyPath;
		PropertyValue Value;
	};

	using StructureReferenceState = std::vector<StructureReferenceSnapshot>;

	StructureReferenceState CaptureStructureReferences(Scene& scene);
	void RetainAffectedStructureReferences(StructureReferenceState& before, StructureReferenceState& after,
		std::span<const uint64_t> affectedIds);
	void RestoreStructureReferences(Scene& scene, const StructureReferenceState& state);

}
