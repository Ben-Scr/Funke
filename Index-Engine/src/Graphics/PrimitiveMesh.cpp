#include "pch.hpp"
#include "Graphics/PrimitiveMesh.hpp"

#include <cmath>
#include <numbers>

namespace Index {
	namespace {
		constexpr int k_SphereSegments = 32;
		constexpr int k_SphereRings = 16;
		constexpr int k_CylinderSegments = 32;

		void PushVertex(PrimitiveMeshData& mesh, float px, float py, float pz, float nx, float ny, float nz, float u, float v) {
			mesh.Vertices.push_back(MeshVertex{ { px, py, pz }, { nx, ny, nz }, { u, v } });
		}

		// Quad with corners origin, origin+a, origin+a+b, origin+b; CCW seen from the side `normal` points to.
		void PushQuad(PrimitiveMeshData& mesh, const float origin[3], const float a[3], const float b[3], const float normal[3]) {
			const uint32_t base = static_cast<uint32_t>(mesh.Vertices.size());
			const float uv[4][2] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
			for (int corner = 0; corner < 4; ++corner) {
				const float sa = (corner == 1 || corner == 2) ? 1.0f : 0.0f;
				const float sb = (corner >= 2) ? 1.0f : 0.0f;
				PushVertex(mesh,
					origin[0] + a[0] * sa + b[0] * sb,
					origin[1] + a[1] * sa + b[1] * sb,
					origin[2] + a[2] * sa + b[2] * sb,
					normal[0], normal[1], normal[2], uv[corner][0], uv[corner][1]);
			}
			mesh.Indices.insert(mesh.Indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
		}

		PrimitiveMeshData BuildCube() {
			PrimitiveMeshData mesh;
			mesh.Vertices.reserve(24);
			mesh.Indices.reserve(36);
			struct Face { float Origin[3]; float A[3]; float B[3]; float Normal[3]; };
			const Face faces[6] = {
				{ {  0.5f, -0.5f,  0.5f }, {  0, 0, -1 }, { 0, 1, 0 }, {  1, 0, 0 } },
				{ { -0.5f, -0.5f, -0.5f }, {  0, 0,  1 }, { 0, 1, 0 }, { -1, 0, 0 } },
				{ { -0.5f,  0.5f,  0.5f }, {  1, 0,  0 }, { 0, 0, -1 }, { 0, 1, 0 } },
				{ { -0.5f, -0.5f, -0.5f }, {  1, 0,  0 }, { 0, 0,  1 }, { 0, -1, 0 } },
				{ { -0.5f, -0.5f,  0.5f }, {  1, 0,  0 }, { 0, 1, 0 }, {  0, 0, 1 } },
				{ {  0.5f, -0.5f, -0.5f }, { -1, 0,  0 }, { 0, 1, 0 }, {  0, 0, -1 } },
			};
			for (const Face& face : faces) {
				PushQuad(mesh, face.Origin, face.A, face.B, face.Normal);
			}
			return mesh;
		}

		PrimitiveMeshData BuildPlane() {
			PrimitiveMeshData mesh;
			const float origin[3] = { -0.5f, 0.0f, 0.5f };
			const float a[3] = { 1.0f, 0.0f, 0.0f };
			const float b[3] = { 0.0f, 0.0f, -1.0f };
			const float normal[3] = { 0.0f, 1.0f, 0.0f };
			PushQuad(mesh, origin, a, b, normal);
			return mesh;
		}

		PrimitiveMeshData BuildSphere() {
			PrimitiveMeshData mesh;
			constexpr float radius = 0.5f;
			constexpr float pi = std::numbers::pi_v<float>;
			for (int ring = 0; ring <= k_SphereRings; ++ring) {
				const float v = static_cast<float>(ring) / k_SphereRings;
				const float phi = v * pi;
				for (int segment = 0; segment <= k_SphereSegments; ++segment) {
					const float u = static_cast<float>(segment) / k_SphereSegments;
					const float theta = u * 2.0f * pi;
					const float nx = std::sin(phi) * std::sin(theta);
					const float ny = std::cos(phi);
					const float nz = std::sin(phi) * std::cos(theta);
					PushVertex(mesh, nx * radius, ny * radius, nz * radius, nx, ny, nz, u, v);
				}
			}
			const uint32_t stride = k_SphereSegments + 1;
			for (uint32_t ring = 0; ring < static_cast<uint32_t>(k_SphereRings); ++ring) {
				for (uint32_t segment = 0; segment < static_cast<uint32_t>(k_SphereSegments); ++segment) {
					const uint32_t top = ring * stride + segment;
					const uint32_t bottom = top + stride;
					mesh.Indices.insert(mesh.Indices.end(), { top, bottom, top + 1, top + 1, bottom, bottom + 1 });
				}
			}
			return mesh;
		}

		PrimitiveMeshData BuildCylinder() {
			PrimitiveMeshData mesh;
			constexpr float radius = 0.5f;
			constexpr float halfHeight = 0.5f;
			constexpr float pi = std::numbers::pi_v<float>;

			for (int segment = 0; segment <= k_CylinderSegments; ++segment) {
				const float u = static_cast<float>(segment) / k_CylinderSegments;
				const float theta = u * 2.0f * pi;
				const float nx = std::sin(theta);
				const float nz = std::cos(theta);
				PushVertex(mesh, nx * radius, halfHeight, nz * radius, nx, 0.0f, nz, u, 0.0f);
				PushVertex(mesh, nx * radius, -halfHeight, nz * radius, nx, 0.0f, nz, u, 1.0f);
			}
			for (uint32_t segment = 0; segment < static_cast<uint32_t>(k_CylinderSegments); ++segment) {
				const uint32_t top = segment * 2;
				mesh.Indices.insert(mesh.Indices.end(), { top, top + 1, top + 2, top + 2, top + 1, top + 3 });
			}

			for (int cap = 0; cap < 2; ++cap) {
				const float ny = cap == 0 ? 1.0f : -1.0f;
				const float y = ny * halfHeight;
				const uint32_t center = static_cast<uint32_t>(mesh.Vertices.size());
				PushVertex(mesh, 0.0f, y, 0.0f, 0.0f, ny, 0.0f, 0.5f, 0.5f);
				for (int segment = 0; segment <= k_CylinderSegments; ++segment) {
					const float theta = static_cast<float>(segment) / k_CylinderSegments * 2.0f * pi;
					const float x = std::sin(theta);
					const float z = std::cos(theta);
					PushVertex(mesh, x * radius, y, z * radius, 0.0f, ny, 0.0f, 0.5f + x * 0.5f, 0.5f - z * 0.5f);
				}
				for (uint32_t segment = 0; segment < static_cast<uint32_t>(k_CylinderSegments); ++segment) {
					const uint32_t a = center + 1 + segment;
					if (cap == 0) {
						mesh.Indices.insert(mesh.Indices.end(), { center, a, a + 1 });
					}
					else {
						mesh.Indices.insert(mesh.Indices.end(), { center, a + 1, a });
					}
				}
			}
			return mesh;
		}
	}

	PrimitiveMeshData BuildPrimitiveMesh(PrimitiveMeshType type) {
		switch (type) {
		case PrimitiveMeshType::Cube: return BuildCube();
		case PrimitiveMeshType::Sphere: return BuildSphere();
		case PrimitiveMeshType::Plane: return BuildPlane();
		case PrimitiveMeshType::Cylinder: return BuildCylinder();
		default: return BuildCube();
		}
	}

	std::string_view PrimitiveMeshTypeName(PrimitiveMeshType type) {
		switch (type) {
		case PrimitiveMeshType::Cube: return "Cube";
		case PrimitiveMeshType::Sphere: return "Sphere";
		case PrimitiveMeshType::Plane: return "Plane";
		case PrimitiveMeshType::Cylinder: return "Cylinder";
		default: return "Unknown";
		}
	}

}
