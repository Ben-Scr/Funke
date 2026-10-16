#include <doctest/doctest.h>

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Graphics/SpriteQuad.hpp"

#include <cmath>

using namespace Index;

namespace {
	// A 128x64 px slice at 128 px per unit with a bottom-centre pivot.
	SpriteGeometry FeetPivotGeometry() {
		SpriteGeometry geometry;
		geometry.Size = { 1.0f, 0.5f };
		geometry.PivotOffset = { 0.0f, 0.25f };
		return geometry;
	}
}

TEST_CASE("Native-size sprites sit on their pivot and follow the transform scale") {
	Transform2DComponent transform({ 3.0f, 2.0f }, { 2.0f, 2.0f });
	const SpriteQuad quad = PlaceSpriteQuad(transform, FeetPivotGeometry());
	CHECK(quad.Size.x == doctest::Approx(2.0f));
	CHECK(quad.Size.y == doctest::Approx(1.0f));
	CHECK(quad.Center.x == doctest::Approx(3.0f));
	CHECK(quad.Center.y == doctest::Approx(2.5f));
}

TEST_CASE("The pivot offset rotates with the transform") {
	Transform2DComponent transform({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 3.14159265f * 0.5f);
	const SpriteQuad quad = PlaceSpriteQuad(transform, FeetPivotGeometry());
	CHECK(quad.Center.x == doctest::Approx(-0.25f));
	CHECK(quad.Center.y == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("Sprites without pixels per unit fill the transform scale") {
	Transform2DComponent transform({ 1.0f, -1.0f }, { 4.0f, 3.0f });
	SpriteRendererComponent sprite;
	const SpriteQuad quad = ResolveSpriteQuad(transform, sprite);
	CHECK(quad.Center.x == doctest::Approx(1.0f));
	CHECK(quad.Center.y == doctest::Approx(-1.0f));
	CHECK(quad.Size.x == doctest::Approx(4.0f));
	CHECK(quad.Size.y == doctest::Approx(3.0f));
}

TEST_CASE("Flips mirror the UVs") {
	SpriteRendererComponent sprite;
	sprite.FlipX = true;
	SpriteGeometry geometry = ResolveSpriteGeometry(sprite);
	CHECK(geometry.Uv.U0 > geometry.Uv.U1);
	CHECK(geometry.Uv.V0 < geometry.Uv.V1);

	sprite.FlipX = false;
	sprite.FlipY = true;
	geometry = ResolveSpriteGeometry(sprite);
	CHECK(geometry.Uv.U0 < geometry.Uv.U1);
	CHECK(geometry.Uv.V0 > geometry.Uv.V1);
}

TEST_CASE("The geometry key changes with every input that changes the geometry") {
	SpriteRendererComponent sprite;
	const uint64_t base = ComputeSpriteGeometryKey(sprite);
	CHECK(base != 0);

	SpriteRendererComponent named = sprite;
	named.SpriteName = "walk_0";
	CHECK(ComputeSpriteGeometryKey(named) != base);

	SpriteRendererComponent native = sprite;
	native.PixelsPerUnit = 32.0f;
	CHECK(ComputeSpriteGeometryKey(native) != base);

	SpriteRendererComponent flipped = sprite;
	flipped.FlipX = true;
	CHECK(ComputeSpriteGeometryKey(flipped) != base);
	CHECK(ComputeSpriteGeometryKey(flipped) == ComputeSpriteGeometryKey(flipped));
}
