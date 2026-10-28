#pragma once

#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/UI/CircularSliderComponent.hpp"

#include <algorithm>
#include <cmath>

namespace Funke::CircularSliderGeometry {

	inline constexpr float k_Pi = 3.14159265358979323846f;
	inline constexpr float k_Tau = 2.0f * k_Pi;
	inline constexpr float k_DegreesToRadians = k_Pi / 180.0f;

	struct Shape {
		Vec2 Center{};
		float OuterRadius = 0.0f;
		float Thickness = 0.0f;
		float Radius = 0.0f;
		float StartRadians = 0.0f;
		float SweepRadians = 0.0f;
		float NormalizedValue = 0.0f;
	};

	inline Shape Resolve(const CircularSliderComponent& slider, const RectTransform2DComponent& rect) {
		Shape shape;
		const Vec2 size = rect.GetSize();
		const Vec2 center = rect.GetCenter();
		const Vec2 pivot = rect.ResolvedValid ? rect.ResolvedPivot : rect.AnchoredPosition;
		const float c = std::cos(rect.Rotation);
		const float s = std::sin(rect.Rotation);
		shape.Center = { pivot.x + c * (center.x - pivot.x) - s * (center.y - pivot.y),
			pivot.y + s * (center.x - pivot.x) + c * (center.y - pivot.y) };
		shape.OuterRadius = std::max(0.0f, std::min(size.x, size.y) * 0.5f);
		const Vec2 worldScale = rect.ResolvedValid ? rect.Scale : rect.LocalScale;
		const float scale = std::min(std::abs(worldScale.x), std::abs(worldScale.y));
		shape.Thickness = std::clamp(slider.RingThickness * scale, 0.0f, shape.OuterRadius);
		shape.Radius = shape.OuterRadius - shape.Thickness * 0.5f;
		shape.StartRadians = slider.StartAngleDegrees * k_DegreesToRadians + rect.Rotation;
		shape.SweepRadians = std::clamp(slider.SweepDegrees, 0.0f, 360.0f)
			* k_DegreesToRadians * (slider.Clockwise ? -1.0f : 1.0f);
		const float range = slider.MaxValue - slider.MinValue;
		shape.NormalizedValue = range != 0.0f ? std::clamp((slider.Value - slider.MinValue) / range, 0.0f, 1.0f) : 0.0f;
		return shape;
	}

	inline Vec2 PointAt(const Shape& shape, float normalizedValue) {
		const float angle = shape.StartRadians + shape.SweepRadians * normalizedValue;
		return { shape.Center.x + std::cos(angle) * shape.Radius,
			shape.Center.y + std::sin(angle) * shape.Radius };
	}

	inline Vec2 HandlePosition(const Shape& shape) {
		return PointAt(shape, shape.NormalizedValue);
	}

	inline bool Contains(const Shape& shape, const Vec2& point) {
		const float sweep = std::abs(shape.SweepRadians);
		if (shape.Thickness <= 0.0f || shape.OuterRadius <= 0.0f || sweep <= 0.0f) return false;
		const float dx = point.x - shape.Center.x;
		const float dy = point.y - shape.Center.y;
		const float halfThickness = shape.Thickness * 0.5f;
		const float radialDistance = std::abs(std::hypot(dx, dy) - shape.Radius);
		if (sweep >= k_Tau) return radialDistance <= halfThickness;

		float angle = (std::atan2(dy, dx) - shape.StartRadians) * (shape.SweepRadians < 0.0f ? -1.0f : 1.0f);
		angle = std::fmod(angle, k_Tau);
		if (angle < 0.0f) angle += k_Tau;
		if (angle <= sweep && radialDistance <= halfThickness) return true;
		for (float t : { 0.0f, 1.0f }) {
			const Vec2 end = PointAt(shape, t);
			const float x = point.x - end.x;
			const float y = point.y - end.y;
			if (x * x + y * y <= halfThickness * halfThickness) return true;
		}
		return false;
	}

}
