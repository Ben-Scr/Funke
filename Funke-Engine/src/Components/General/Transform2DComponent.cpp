#include "pch.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Scene/Scene.hpp"
#if FUNKE_WITH_PHYSICS2D
#include <box2d/types.h>
#endif

namespace Funke {

	Transform2DComponent Transform2DComponent::FromPosition(const Vec2& pos) {
		Transform2DComponent tr;
		tr.Position = pos;
		tr.LocalPosition = pos;
		return tr;
	}
	Transform2DComponent Transform2DComponent::FromScale(const Vec2& scale) {
		Transform2DComponent tr;
		tr.Scale = scale;
		tr.LocalScale = scale;
		return tr;
	}


	float Transform2DComponent::GetRotationDegrees() const { return Degrees(Rotation); }

	Vec2 Transform2DComponent::Up() const {
		return Rotate(Vec2{ 0.0f, 1.0f }, Rotation);
	}
	Vec2 Transform2DComponent::Down() const {
		return -Up();
	}

	Vec2 Transform2DComponent::Right() const {
		return Rotate(Vec2{ 1.0f, 0.0f }, Rotation);
	}
	Vec2 Transform2DComponent::Left() const {
		return -Right();
	}

	void Transform2DComponent::MarkDirty() {
		m_Dirty = true;
		if (m_OwnerScene && m_OwnerEntity != entt::null) {
			m_OwnerScene->MarkTransformDirty(m_OwnerEntity);
		}
	}

	Vec2 Transform2DComponent::GetForwardDirection() const
	{
		return Up();
	}

	glm::mat3 Transform2DComponent::GetModelMatrix() const {
		const float s = glm::sin(Rotation);
		const float c = glm::cos(Rotation);

		// T * R * S directly: translation only occupies the last column.
		return glm::mat3{
			{ c * Scale.x, s * Scale.x, 0.0f },
			{ -s * Scale.y, c * Scale.y, 0.0f },
			{ Position.x, Position.y, 1.0f }
		};
	}

#if FUNKE_WITH_PHYSICS2D
	b2Rot Transform2DComponent::GetB2Rotation() const {
		// No sign flip: both Funke renderer and Box2D use CCW-positive. Previous -Sin variant inverted handedness and caused physics bodies to render backwards.
		return b2Rot(Cos(Rotation), Sin(Rotation));
	}
#endif
}
