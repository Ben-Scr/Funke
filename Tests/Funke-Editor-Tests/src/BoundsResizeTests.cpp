#include <doctest/doctest.h>
#include "Editor/BoundsResize.hpp"

using namespace Funke;

namespace {
	void CheckPoint(const Vec2& actual, const Vec2& expected) {
		CHECK(actual.x == doctest::Approx(expected.x).epsilon(0.0001));
		CHECK(actual.y == doctest::Approx(expected.y).epsilon(0.0001));
	}

	Vec2 TransformHandle(const Transform2DComponent& transform, const Vec2& parentScale, float parentRotation, int x, int y) {
		return Rotate(Hadamard(transform.LocalPosition, parentScale), parentRotation)
			+ Rotate({ x * std::abs(transform.LocalScale.x * parentScale.x) * 0.5f,
				y * std::abs(transform.LocalScale.y * parentScale.y) * 0.5f }, transform.Rotation);
	}

	Vec2 RectHandle(const RectTransform2DComponent& rect, const Vec2& parentScale, float parentRotation, float worldScale, int x, int y) {
		const Vec2 corner{ (static_cast<float>(x + 1) * 0.5f - rect.Pivot.x) * rect.SizeDelta.x * rect.Scale.x,
			(static_cast<float>(y + 1) * 0.5f - rect.Pivot.y) * rect.SizeDelta.y * rect.Scale.y };
		return (Rotate(Hadamard(rect.AnchoredPosition, parentScale), parentRotation) + Rotate(corner, rect.Rotation)) * worldScale;
	}
}

TEST_CASE("Bounds resize follows rotated handles and fixes the opposite edge under scaled parents") {
	for (float rotation : { 0.0f, 0.3f, 1.5707963f, -2.2f }) {
		for (const Vec2 parentScale : { Vec2{1, 1}, Vec2{2, 0.5f}, Vec2{-2, 0.5f} }) {
			for (float sign : { -1.0f, 1.0f }) {
				for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) {
					if (x == 0 && y == 0) continue;
					INFO("rotation=", rotation, " parent=", parentScale.x, ",", parentScale.y, " sign=", sign, " handle=", x, ",", y);
					constexpr float parentRotation = 0.7f;
					Transform2DComponent transform({3, -2}, {4 * sign, 2}, rotation - parentRotation);
					transform.Rotation = rotation;
					transform.Scale = Hadamard(transform.LocalScale, parentScale);
					const Vec2 fixed = TransformHandle(transform, parentScale, parentRotation, -x, -y);
					const Vec2 dragged = TransformHandle(transform, parentScale, parentRotation, x, y);
					const Vec2 delta = Rotate({ x * 0.4f, y * 0.3f }, rotation);
					BoundsResize::Transform(transform, delta, x, y);
					CheckPoint(TransformHandle(transform, parentScale, parentRotation, -x, -y), fixed);
					CheckPoint(TransformHandle(transform, parentScale, parentRotation, x, y), dragged + delta);
					CHECK(std::signbit(transform.LocalScale.x) == std::signbit(sign));
				}
			}
		}
	}
}

TEST_CASE("Bounds resize respects rotated UI pivots and local and inherited scale") {
	for (float rotation : { 0.0f, 0.3f, 1.5707963f, -2.2f }) {
		for (const Vec2 pivot : { Vec2{0, 0}, Vec2{0.5f, 0.5f}, Vec2{0.2f, 0.8f}, Vec2{1, 1} }) {
			for (float worldScale : { 0.01f, 0.05f }) {
				for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) {
					if (x == 0 && y == 0) continue;
					INFO("rotation=", rotation, " pivot=", pivot.x, ",", pivot.y, " scale=", worldScale, " handle=", x, ",", y);
					const Vec2 parentScale{2.0f, 0.5f};
					constexpr float parentRotation = -0.6f;
					RectTransform2DComponent rect;
					rect.AnchoredPosition = {30, -20};
					rect.Pivot = pivot;
					rect.SizeDelta = {100, 60};
					rect.LocalScale = {-1.5f, 0.7f};
					rect.Scale = Hadamard(parentScale, rect.LocalScale);
					rect.Rotation = rotation;
					rect.LocalRotation = rotation - parentRotation;
					const Vec2 fixed = RectHandle(rect, parentScale, parentRotation, worldScale, -x, -y);
					const Vec2 dragged = RectHandle(rect, parentScale, parentRotation, worldScale, x, y);
					const Vec2 delta = Rotate({ x * -0.2f, y * 0.1f }, rotation);
					BoundsResize::Rect(rect, worldScale, delta, x, y);
					CheckPoint(RectHandle(rect, parentScale, parentRotation, worldScale, -x, -y), fixed);
					CheckPoint(RectHandle(rect, parentScale, parentRotation, worldScale, x, y), dragged + delta);
				}
			}
		}
	}
}

TEST_CASE("Bounds resize ignores perpendicular motion and clamps without moving the opposite edge") {
	Transform2DComponent transform({3, -2}, {4, 2}, 1.5707963f);
	const Vec2 fixed = TransformHandle(transform, {1, 1}, 0, -1, 0);
	BoundsResize::Transform(transform, {-5, 0}, 1, 0);
	CHECK(transform.LocalScale.x == doctest::Approx(4));
	CheckPoint(transform.LocalPosition, {3, -2});
	BoundsResize::Transform(transform, {0, -20}, 1, 0);
	CHECK(transform.LocalScale.x == doctest::Approx(0.01f));
	CHECK(transform.LocalScale.y == doctest::Approx(2));
	CheckPoint(TransformHandle(transform, {1, 1}, 0, -1, 0), fixed);

	RectTransform2DComponent rect;
	rect.Pivot = {0.2f, 0.8f};
	rect.Rotation = rect.LocalRotation = 0.3f;
	const Vec2 fixedCorner = RectHandle(rect, {1, 1}, 0, 0.01f, 1, -1);
	BoundsResize::Rect(rect, 0.01f, Rotate({5, -5}, rect.Rotation), -1, 1);
	CheckPoint(rect.SizeDelta, {1, 1});
	CheckPoint(RectHandle(rect, {1, 1}, 0, 0.01f, 1, -1), fixedCorner);
}
