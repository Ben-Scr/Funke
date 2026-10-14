#include <doctest/doctest.h>

#include "Editor/SpriteSlicing.hpp"

#include <cstdint>
#include <vector>

using namespace Index;

TEST_CASE("SpriteSlicing grid size skips partial cells and honours padding and offset") {
	std::vector<SpriteSlice> slices;
	CHECK(SpriteSlicing::ByGridSize(slices, "sheet", 50, 20, 16, 16) == 3);
	REQUIRE(slices.size() == 3);
	CHECK(slices[2].X == 32);
	CHECK(slices[0].Name == "sheet_0");

	slices.clear();
	// Columns start at 4, 14, 24 (34 + 8 would overflow); two rows of 8.
	CHECK(SpriteSlicing::ByGridSize(slices, "sheet", 40, 16, 8, 8, 2, 0, 4, 0) == 6);
	CHECK(slices[1].X == 14);
	CHECK(slices[3].Y == 8);
}

TEST_CASE("SpriteSlicing grid count derives the cell size and rejects grids that do not fit") {
	std::vector<SpriteSlice> slices;
	CHECK(SpriteSlicing::ByGridCount(slices, "s", 64, 32, 4, 2) == 8);
	CHECK(slices.back().W == 16);
	CHECK(slices.back().H == 16);
	CHECK(SpriteSlicing::ByGridCount(slices, "s", 3, 3, 4, 4) == -1);
}

TEST_CASE("SpriteSlicing names stay unique when appending to existing slices") {
	std::vector<SpriteSlice> slices;
	SpriteSlicing::ByGridSize(slices, "tile", 32, 16, 16, 16);
	SpriteSlicing::ByGridSize(slices, "tile", 32, 16, 16, 16);
	REQUIRE(slices.size() == 4);
	CHECK(slices[2].Name == "tile_0_2");
	CHECK(slices[3].Name == "tile_1_2");
}

TEST_CASE("SpriteSlicing alpha finds each opaque island") {
	constexpr int k_W = 8;
	constexpr int k_H = 4;
	std::vector<std::uint8_t> rgba(k_W * k_H * 4, 0);
	const auto opaque = [&](int x, int y) { rgba[(y * k_W + x) * 4 + 3] = 255; };
	opaque(0, 0); opaque(1, 0); opaque(1, 1);   // island A: 2x2 bounds
	opaque(5, 2); opaque(6, 2); opaque(6, 3);   // island B: 2x2 bounds
	opaque(3, 3);                                // single pixel, dropped by minSize 2

	std::vector<SpriteSlice> slices;
	CHECK(SpriteSlicing::ByAlpha(slices, "a", rgba.data(), k_W, k_H, 1, 2) == 2);
	REQUIRE(slices.size() == 2);
	CHECK(slices[0].X == 0);
	CHECK(slices[0].W == 2);
	CHECK(slices[1].X == 5);
	CHECK(slices[1].Y == 2);
	CHECK(slices[1].H == 2);
}
