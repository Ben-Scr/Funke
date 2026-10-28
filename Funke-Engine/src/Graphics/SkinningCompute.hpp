#pragma once
#include "Graphics/SkeletalAnimation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Funke::SkinningCompute {

	inline constexpr uint32_t k_WorkgroupSize = 64;

	struct Vertex {
		float Position[4];
		float Normal[4];
		uint32_t Joints[4];
		float Weights[4];
		float UV[2];
		float Padding[2];
	};
	static_assert(sizeof(Vertex) == 80 && offsetof(Vertex, UV) == 64);
	static_assert(sizeof(MeshVertex) == 32 && offsetof(MeshVertex, Normal) == 12 && offsetof(MeshVertex, UV) == 24);

	inline bool FitsLimits(uint64_t vertices, uint64_t joints, uint64_t maxBindingSize, uint64_t maxBufferSize, uint32_t maxWorkgroups) {
		const uint64_t limit = std::min(maxBindingSize, maxBufferSize);
		return vertices > 0 && joints > 0 && vertices <= limit / sizeof(Vertex) && joints <= limit / sizeof(glm::mat4)
			&& (vertices + k_WorkgroupSize - 1) / k_WorkgroupSize <= maxWorkgroups;
	}

	inline bool PackVertices(const SkinnedMeshData& data, std::vector<Vertex>& out) {
		const size_t count = data.BindVertices.size();
		if (data.VertexJoints.size() < count || data.VertexWeights.size() < count) return false;
		out.resize(count);
		for (size_t i = 0; i < count; ++i) {
			const MeshVertex& source = data.BindVertices[i];
			Vertex& vertex = out[i];
			vertex = {};
			std::copy_n(source.Position, 3, vertex.Position);
			vertex.Position[3] = 1.0f;
			std::copy_n(source.Normal, 3, vertex.Normal);
			std::copy_n(source.UV, 2, vertex.UV);
			for (int k = 0; k < 4; ++k) {
				vertex.Joints[k] = data.VertexJoints[i][k];
				vertex.Weights[k] = data.VertexWeights[i][k];
			}
		}
		return true;
	}

	// Scalar output matches MeshVertex's packed vertex layout for every existing draw pipeline.
	inline constexpr const char* k_Shader = R"WGSL(
struct SkinVertex {
    position: vec4<f32>,
    normal: vec4<f32>,
    joints: vec4<u32>,
    weights: vec4<f32>,
    uv: vec2<f32>,
    padding: vec2<f32>,
};
@group(0) @binding(0) var<storage, read> source: array<SkinVertex>;
@group(0) @binding(1) var<storage, read> joints: array<mat4x4<f32>>;
@group(0) @binding(2) var<storage, read_write> output: array<f32>;

@compute @workgroup_size(64)
fn skin(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= arrayLength(&source)) { return; }
    let vertex = source[id.x];
    let identity = mat4x4<f32>(vec4<f32>(1, 0, 0, 0), vec4<f32>(0, 1, 0, 0),
        vec4<f32>(0, 0, 1, 0), vec4<f32>(0, 0, 0, 1));
    var matrix = mat4x4<f32>();
    for (var k = 0u; k < 4u; k++) {
        let weight = vertex.weights[k];
        if (weight == 0.0) { continue; }
        var joint = identity;
        if (vertex.joints[k] < arrayLength(&joints)) { joint = joints[vertex.joints[k]]; }
        matrix += weight * joint;
    }
    let position = (matrix * vertex.position).xyz;
    var normal = (matrix * vertex.normal).xyz;
    let magnitude = length(normal);
    if (magnitude > 1e-12) { normal /= magnitude; }
    else { normal = vec3<f32>(0, 1, 0); }
    let base = id.x * 8u;
    output[base] = position.x;
    output[base + 1u] = position.y;
    output[base + 2u] = position.z;
    output[base + 3u] = normal.x;
    output[base + 4u] = normal.y;
    output[base + 5u] = normal.z;
    output[base + 6u] = vertex.uv.x;
    output[base + 7u] = vertex.uv.y;
}
)WGSL";
}
