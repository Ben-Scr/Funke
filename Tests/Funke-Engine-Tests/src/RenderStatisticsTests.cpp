#include <doctest/doctest.h>

#include "Graphics/RenderStatistics.hpp"

using namespace Funke;

TEST_CASE("Render statistics count instanced meshes, quads and shadow casters separately") {
	RenderGeometryStats stats;
	// A cube (12 triangles over 24 vertices) drawn as 3 instances.
	stats.AddMesh(36, 24, 3);
	CHECK(stats.Triangles == 36u);
	CHECK(stats.Vertices == 72u);

	// Sprites and particles are quads.
	stats.AddQuads(10);
	CHECK(stats.Triangles == 56u);
	CHECK(stats.Vertices == 112u);

	// The shadow pass draws the same casters again without touching the camera counts.
	stats.AddShadowMesh(36, 3);
	CHECK(stats.ShadowTriangles == 36u);
	CHECK(stats.Triangles == 56u);

	// Counts past 32 bits (a million-triangle mesh drawn 5000 times) do not wrap.
	RenderGeometryStats large;
	large.AddMesh(3'000'000, 1'000'000, 5000);
	CHECK(large.Triangles == 5'000'000'000ull);
	CHECK(large.Vertices == 5'000'000'000ull);
}

TEST_CASE("Render statistics turn running totals into per-frame numbers") {
	RenderGeometryStats earlier;
	earlier.AddMesh(300, 200, 1);
	earlier.AddShadowMesh(300, 1);

	RenderGeometryStats now = earlier;
	// Three frames of 1000 triangles, 600 vertices and 400 shadow triangles each.
	for (int frame = 0; frame < 3; ++frame) {
		now.AddMesh(3000, 600, 1);
		now.AddShadowMesh(1200, 1);
	}

	const RenderGeometryStats drawn = now - earlier;
	CHECK(drawn.Triangles == 3000u);
	CHECK(drawn.Vertices == 1800u);
	CHECK(drawn.ShadowTriangles == 1200u);

	const RenderGeometryStats perFrame = drawn.PerFrame(3);
	CHECK(perFrame.Triangles == 1000u);
	CHECK(perFrame.Vertices == 600u);
	CHECK(perFrame.ShadowTriangles == 400u);
	CHECK(drawn.PerFrame(0) == RenderGeometryStats{});

	// A recreated renderer restarts its totals at 0: the difference saturates instead of wrapping.
	CHECK(earlier - now == RenderGeometryStats{});
	CHECK((now + earlier).Triangles == now.Triangles + earlier.Triangles);
}

TEST_CASE("Render statistics are empty without an application") {
	CHECK(CaptureRenderGeometryTotals() == RenderGeometryStats{});
}
