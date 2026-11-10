#pragma once
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"

#include <algorithm>
#include <cmath>

namespace Funke::BoundsResize {
	inline Vec2 ParentScale(const Vec2& worldScale, const Vec2& localScale) {
		return {
			std::abs(localScale.x) > 1e-6f ? worldScale.x / localScale.x : 0.0f,
			std::abs(localScale.y) > 1e-6f ? worldScale.y / localScale.y : 0.0f
		};
	}

	inline void Apply(Vec2& size, Vec2& position, const Vec2& pivot, const Vec2& sizeScale,
		float rotation, const Vec2& parentScale, float parentRotation, const Vec2& worldDelta,
		int edgeX, int edgeY, float minimumSize) {
		if (std::abs(parentScale.x) < 1e-6f || std::abs(parentScale.y) < 1e-6f) return;
		const Vec2 delta = Rotate(worldDelta, -rotation);
		Vec2 pivotDelta{};
		auto resizeAxis = [&](float& value, float& shift, float movement, float scale, float pivotValue, int edge) {
			if (edge == 0 || std::abs(scale) < 1e-6f) return;
			const float next = std::max(minimumSize, value + static_cast<float>(edge) * movement / scale);
			// Keep the opposite edge fixed, including when the minimum size clamps the drag.
			shift = (next - value) * scale * (edge > 0 ? pivotValue : pivotValue - 1.0f);
			value = next;
		};
		resizeAxis(size.x, pivotDelta.x, delta.x, sizeScale.x, pivot.x, edgeX);
		resizeAxis(size.y, pivotDelta.y, delta.y, sizeScale.y, pivot.y, edgeY);
		const Vec2 parentDelta = Rotate(Rotate(pivotDelta, rotation), -parentRotation);
		position += Vec2{ parentDelta.x / parentScale.x, parentDelta.y / parentScale.y };
	}

	inline void Transform(Transform2DComponent& transform, const Vec2& worldDelta, int edgeX, int edgeY) {
		const Vec2 parentScale = ParentScale(transform.Scale, transform.LocalScale);
		Vec2 size{ std::abs(transform.LocalScale.x), std::abs(transform.LocalScale.y) };
		Apply(size, transform.LocalPosition, { 0.5f, 0.5f }, { std::abs(parentScale.x), std::abs(parentScale.y) },
			transform.Rotation, parentScale, transform.Rotation - transform.LocalRotation, worldDelta, edgeX, edgeY, 0.01f);
		transform.LocalScale = { std::copysign(size.x, transform.LocalScale.x), std::copysign(size.y, transform.LocalScale.y) };
	}

	inline void Rect(RectTransform2DComponent& rect, float worldScale, const Vec2& worldDelta, int edgeX, int edgeY) {
		const Vec2 parentScale = ParentScale(rect.Scale, rect.LocalScale) * worldScale;
		Apply(rect.SizeDelta, rect.AnchoredPosition, rect.Pivot, rect.Scale * worldScale, rect.Rotation,
			parentScale, rect.Rotation - rect.LocalRotation, worldDelta,
			rect.FillWidth ? 0 : edgeX, rect.FillHeight ? 0 : edgeY, 1.0f);
	}
}
