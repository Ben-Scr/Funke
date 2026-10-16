#pragma once
#include "Core/Export.hpp"
#include "Graphics/PrimitiveMesh.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	struct SkeletonNode {
		std::string Name;
		int32_t Parent = -1;
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
	};

	// What a vertex weight refers to: the node's posed model-space matrix times InverseBind.
	// Rigid (unskinned) parts use their own node with an identity InverseBind.
	struct SkinJoint {
		uint32_t Node = 0;
		glm::mat4 InverseBind{ 1.0f };
	};

	enum class KeyframeInterpolation : uint8_t { Linear, Step, CubicSpline };
	enum class AnimatedProperty : uint8_t { Translation, Rotation, Scale };

	struct SkeletalChannel {
		uint32_t Node = 0;
		AnimatedProperty Property = AnimatedProperty::Translation;
		KeyframeInterpolation Interpolation = KeyframeInterpolation::Linear;
		std::vector<float> Times;
		// xyz (w unused) or a quaternion as xyzw; CubicSpline stores in-tangent, value, out-tangent per key.
		std::vector<glm::vec4> Values;
	};

	struct SkeletalClip {
		std::string Name;
		float Duration = 0.0f;
		std::vector<SkeletalChannel> Channels;
	};

	struct NodePose {
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
	};

	// Skeleton, bind-space vertices and clips of an imported model; vertices and indices line up with MeshAsset::Data.
	struct FUNKE_API SkinnedMeshData {
		std::vector<SkeletonNode> Nodes;
		// Parents before children.
		std::vector<uint32_t> NodeOrder;
		std::vector<SkinJoint> Joints;
		std::vector<MeshVertex> BindVertices;
		std::vector<std::array<uint16_t, 4>> VertexJoints;
		std::vector<glm::vec4> VertexWeights;
		std::vector<SkeletalClip> Clips;

		int FindClip(std::string_view name) const;
		int FindNode(std::string_view name) const;
	};

	// Which clips a pose blends: Clip at Time, cross-faded from PreviousClip at PreviousTime while Fade < 1.
	struct SkeletalPoseRequest {
		int Clip = -1;
		float Time = 0.0f;
		int PreviousClip = -1;
		float PreviousTime = 0.0f;
		float Fade = 1.0f;

		bool operator==(const SkeletalPoseRequest&) const = default;
	};

	namespace SkeletalAnimation {
		FUNKE_API void RestPose(const SkinnedMeshData& data, std::vector<NodePose>& outPose);
		// Overwrites the properties the clip animates; time is clamped to [0, Duration].
		FUNKE_API void SampleClip(const SkeletalClip& clip, float time, std::vector<NodePose>& pose);
		// pose = mix(pose, other, weight); rotations take the shortest arc.
		FUNKE_API void Blend(std::vector<NodePose>& pose, const std::vector<NodePose>& other, float weight);
		FUNKE_API void ComputeNodeMatrices(const SkinnedMeshData& data, const std::vector<NodePose>& pose, std::vector<glm::mat4>& outModel);
		FUNKE_API void ComputeJointMatrices(const SkinnedMeshData& data, const std::vector<glm::mat4>& nodeModel, std::vector<glm::mat4>& outJoints);
		FUNKE_API void SkinVertices(const SkinnedMeshData& data, const std::vector<glm::mat4>& joints, std::vector<MeshVertex>& outVertices);

		// Rest pose, clip sampling and cross-fade in one call; out-of-range clips sample as the rest pose.
		FUNKE_API void EvaluatePose(const SkinnedMeshData& data, const SkeletalPoseRequest& request, std::vector<glm::mat4>& outNodeModel);

		// Advances a looping or clamped play head; returns false once a non-looping clip has reached its end.
		FUNKE_API bool AdvanceTime(float& time, float delta, float duration, bool loop);
	}

}
