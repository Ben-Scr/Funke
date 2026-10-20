#pragma once

#include "Core/Export.hpp"

#include <cstdint>

namespace Funke {

	// Geometry the 2D and 3D renderers drew. Renderers keep running totals and readers diff two captures,
	// because the editor's views and the game render at different points of a frame.
	struct RenderGeometryStats {
		uint64_t Triangles = 0;
		uint64_t Vertices = 0;
		// Shadow-map passes draw the casters again; kept apart so Triangles matches what the cameras show.
		uint64_t ShadowTriangles = 0;

		void AddMesh(uint32_t indexCount, uint32_t vertexCount, uint32_t instances) noexcept {
			Triangles += static_cast<uint64_t>(indexCount / 3) * instances;
			Vertices += static_cast<uint64_t>(vertexCount) * instances;
		}
		// Sprites and billboard particles: two triangles over four corners.
		void AddQuads(uint64_t quads) noexcept {
			Triangles += quads * 2;
			Vertices += quads * 4;
		}
		void AddShadowMesh(uint32_t indexCount, uint32_t instances) noexcept {
			ShadowTriangles += static_cast<uint64_t>(indexCount / 3) * instances;
		}

		RenderGeometryStats operator+(const RenderGeometryStats& other) const noexcept {
			return { Triangles + other.Triangles, Vertices + other.Vertices, ShadowTriangles + other.ShadowTriangles };
		}
		// Drawn since `earlier`; saturates at 0 when a renderer was recreated in between.
		RenderGeometryStats operator-(const RenderGeometryStats& earlier) const noexcept {
			auto since = [](uint64_t now, uint64_t then) { return now > then ? now - then : 0; };
			return { since(Triangles, earlier.Triangles), since(Vertices, earlier.Vertices),
				since(ShadowTriangles, earlier.ShadowTriangles) };
		}
		// Average of counts gathered over `frames` frames.
		RenderGeometryStats PerFrame(uint64_t frames) const noexcept {
			if (frames == 0) return {};
			return { Triangles / frames, Vertices / frames, ShadowTriangles / frames };
		}

		bool operator==(const RenderGeometryStats&) const = default;
	};

	// Running totals of the application's renderers: 2D sprites and particles, 3D meshes, particles and shadows.
	FUNKE_API RenderGeometryStats CaptureRenderGeometryTotals();

} // namespace Funke
