#pragma once
#include "Undo/EditCommand.hpp"
#include "Collections/Vec2.hpp"

#include <cstdint>

namespace Funke {

	// One undo step for a Transform3D gizmo drag on a single entity.
	class Transform3DEditCommand : public EditCommand {
	public:
		struct State {
			Vec3 Position{ 0.0f };
			Vec3 Rotation{ 0.0f };
			Vec3 Scale{ 1.0f };
			bool operator==(const State& other) const {
				return Position == other.Position && Rotation == other.Rotation && Scale == other.Scale;
			}
		};

		Transform3DEditCommand(uint64_t persistentId, const State& before, const State& after)
			: m_PersistentId(persistentId), m_Before(before), m_After(after) {}

		void Undo(Scene& scene) override { Apply(scene, m_Before); }
		void Redo(Scene& scene) override { Apply(scene, m_After); }
		const char* Name() const override { return "Transform"; }
		std::string Label() const override { return "Transform 3D"; }

	private:
		void Apply(Scene& scene, const State& state) const;

		uint64_t m_PersistentId = 0;
		State m_Before;
		State m_After;
	};

}
