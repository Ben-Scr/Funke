#include "pch.hpp"
#include "Graphics/SkeletalAnimation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Funke {
	namespace {
		glm::quat ToQuat(const glm::vec4& v) { return glm::quat(v.w, v.x, v.y, v.z); }

		glm::quat ShortestSlerp(const glm::quat& a, glm::quat b, float t) {
			if (glm::dot(a, b) < 0.0f) b = -b;
			return glm::normalize(glm::slerp(a, b, t));
		}

		glm::vec4 Hermite(const glm::vec4& v0, const glm::vec4& out0, const glm::vec4& in1, const glm::vec4& v1, float t, float dt) {
			const float t2 = t * t;
			const float t3 = t2 * t;
			return (2.0f * t3 - 3.0f * t2 + 1.0f) * v0 + (t3 - 2.0f * t2 + t) * dt * out0
				+ (-2.0f * t3 + 3.0f * t2) * v1 + (t3 - t2) * dt * in1;
		}

		glm::vec4 SampleChannel(const SkeletalChannel& channel, float time) {
			const std::vector<float>& times = channel.Times;
			const bool cubic = channel.Interpolation == KeyframeInterpolation::CubicSpline;
			auto value = [&](size_t key) { return channel.Values[cubic ? key * 3 + 1 : key]; };
			if (time <= times.front()) return value(0);
			if (time >= times.back()) return value(times.size() - 1);

			const size_t next = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), time) - times.begin());
			const size_t prev = next - 1;
			const float span = times[next] - times[prev];
			const float t = span > 0.0f ? (time - times[prev]) / span : 0.0f;
			switch (channel.Interpolation) {
			case KeyframeInterpolation::Step:
				return value(prev);
			case KeyframeInterpolation::CubicSpline:
				return Hermite(value(prev), channel.Values[prev * 3 + 2], channel.Values[next * 3], value(next), t, span);
			case KeyframeInterpolation::Linear:
				break;
			}
			if (channel.Property == AnimatedProperty::Rotation) {
				const glm::quat q = ShortestSlerp(ToQuat(value(prev)), ToQuat(value(next)), t);
				return { q.x, q.y, q.z, q.w };
			}
			return glm::mix(value(prev), value(next), t);
		}

		glm::mat4 LocalMatrix(const NodePose& pose) {
			glm::mat4 m = glm::mat4_cast(pose.Rotation);
			m[0] *= pose.Scale.x;
			m[1] *= pose.Scale.y;
			m[2] *= pose.Scale.z;
			m[3] = glm::vec4(pose.Translation, 1.0f);
			return m;
		}
	}

	int SkinnedMeshData::FindClip(std::string_view name) const {
		for (size_t i = 0; i < Clips.size(); ++i) {
			if (Clips[i].Name == name) return static_cast<int>(i);
		}
		return -1;
	}

	int SkinnedMeshData::FindNode(std::string_view name) const {
		for (size_t i = 0; i < Nodes.size(); ++i) {
			if (Nodes[i].Name == name) return static_cast<int>(i);
		}
		return -1;
	}

	namespace SkeletalAnimation {
		void RestPose(const SkinnedMeshData& data, std::vector<NodePose>& outPose) {
			outPose.resize(data.Nodes.size());
			for (size_t i = 0; i < data.Nodes.size(); ++i) {
				const SkeletonNode& node = data.Nodes[i];
				outPose[i] = { node.Translation, node.Rotation, node.Scale };
			}
		}

		void SampleClip(const SkeletalClip& clip, float time, std::vector<NodePose>& pose) {
			time = std::clamp(time, 0.0f, std::max(clip.Duration, 0.0f));
			for (const SkeletalChannel& channel : clip.Channels) {
				if (channel.Node >= pose.size() || channel.Times.empty()) continue;
				const glm::vec4 v = SampleChannel(channel, time);
				NodePose& target = pose[channel.Node];
				switch (channel.Property) {
				case AnimatedProperty::Translation: target.Translation = glm::vec3(v); break;
				case AnimatedProperty::Rotation: target.Rotation = glm::normalize(ToQuat(v)); break;
				case AnimatedProperty::Scale: target.Scale = glm::vec3(v); break;
				}
			}
		}

		void Blend(std::vector<NodePose>& pose, const std::vector<NodePose>& other, float weight) {
			weight = std::clamp(weight, 0.0f, 1.0f);
			const size_t count = std::min(pose.size(), other.size());
			for (size_t i = 0; i < count; ++i) {
				pose[i].Translation = glm::mix(pose[i].Translation, other[i].Translation, weight);
				pose[i].Rotation = ShortestSlerp(pose[i].Rotation, other[i].Rotation, weight);
				pose[i].Scale = glm::mix(pose[i].Scale, other[i].Scale, weight);
			}
		}

		void ComputeNodeMatrices(const SkinnedMeshData& data, const std::vector<NodePose>& pose, std::vector<glm::mat4>& outModel) {
			outModel.assign(data.Nodes.size(), glm::mat4(1.0f));
			for (uint32_t index : data.NodeOrder) {
				if (index >= pose.size()) continue;
				const glm::mat4 local = LocalMatrix(pose[index]);
				const int32_t parent = data.Nodes[index].Parent;
				outModel[index] = parent >= 0 ? outModel[static_cast<size_t>(parent)] * local : local;
			}
		}

		void ComputeJointMatrices(const SkinnedMeshData& data, const std::vector<glm::mat4>& nodeModel, std::vector<glm::mat4>& outJoints) {
			outJoints.resize(data.Joints.size());
			for (size_t i = 0; i < data.Joints.size(); ++i) {
				const SkinJoint& joint = data.Joints[i];
				outJoints[i] = (joint.Node < nodeModel.size() ? nodeModel[joint.Node] : glm::mat4(1.0f)) * joint.InverseBind;
			}
		}

		void SkinVertices(const SkinnedMeshData& data, const std::vector<glm::mat4>& joints, std::vector<MeshVertex>& outVertices) {
			const size_t count = data.BindVertices.size();
			outVertices.resize(count);
			const glm::mat4 identity(1.0f);
			for (size_t v = 0; v < count; ++v) {
				const MeshVertex& source = data.BindVertices[v];
				if (v >= data.VertexJoints.size() || v >= data.VertexWeights.size()) {
					outVertices[v] = source;
					continue;
				}
				const std::array<uint16_t, 4>& influence = data.VertexJoints[v];
				const glm::vec4& weight = data.VertexWeights[v];
				glm::mat4 m(0.0f);
				for (int k = 0; k < 4; ++k) {
					if (weight[k] == 0.0f) continue;
					m += weight[k] * (influence[k] < joints.size() ? joints[influence[k]] : identity);
				}
				const glm::vec3 p = glm::vec3(m * glm::vec4(source.Position[0], source.Position[1], source.Position[2], 1.0f));
				// Joint matrices are mostly rigid; the inverse transpose isn't worth it per vertex.
				glm::vec3 n = glm::mat3(m) * glm::vec3(source.Normal[0], source.Normal[1], source.Normal[2]);
				const float length = glm::length(n);
				n = length > 1e-12f ? n / length : glm::vec3(0.0f, 1.0f, 0.0f);
				MeshVertex& out = outVertices[v];
				out.Position[0] = p.x;
				out.Position[1] = p.y;
				out.Position[2] = p.z;
				out.Normal[0] = n.x;
				out.Normal[1] = n.y;
				out.Normal[2] = n.z;
				out.UV[0] = source.UV[0];
				out.UV[1] = source.UV[1];
			}
		}

		void ComputeJointBounds(SkinnedMeshData& data) {
			data.JointBoundsValid = !data.BindVertices.empty() && data.VertexJoints.size() >= data.BindVertices.size()
				&& data.VertexWeights.size() >= data.BindVertices.size();
			data.BoundsWeightSumMin = 1.0f;
			data.BoundsWeightSumMax = 1.0f;
			for (SkinJoint& joint : data.Joints) {
				joint.BoundsMin = glm::vec3(std::numeric_limits<float>::max());
				joint.BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());
			}
			const size_t count = std::min({ data.BindVertices.size(), data.VertexJoints.size(), data.VertexWeights.size() });
			for (size_t v = 0; v < count; ++v) {
				const MeshVertex& vertex = data.BindVertices[v];
				const glm::vec3 p(vertex.Position[0], vertex.Position[1], vertex.Position[2]);
				float sum = 0.0f;
				if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) data.JointBoundsValid = false;
				for (int k = 0; k < 4; ++k) {
					const uint16_t index = data.VertexJoints[v][k];
					const float weight = data.VertexWeights[v][k];
					sum += weight;
					if (weight == 0.0f) continue;
					if (!std::isfinite(weight) || weight < 0.0f || index >= data.Joints.size()) {
						data.JointBoundsValid = false;
						continue;
					}
					SkinJoint& joint = data.Joints[index];
					joint.BoundsMin = glm::min(joint.BoundsMin, p);
					joint.BoundsMax = glm::max(joint.BoundsMax, p);
				}
				if (!std::isfinite(sum) || sum <= 0.0f) data.JointBoundsValid = false;
				data.BoundsWeightSumMin = std::min(data.BoundsWeightSumMin, sum);
				data.BoundsWeightSumMax = std::max(data.BoundsWeightSumMax, sum);
			}
		}

		bool PosedBounds(const SkinnedMeshData& data, const std::vector<glm::mat4>& joints, glm::vec3& outMin, glm::vec3& outMax) {
			if (!data.JointBoundsValid || joints.size() < data.Joints.size()) return false;
			outMin = glm::vec3(std::numeric_limits<float>::max());
			outMax = glm::vec3(std::numeric_limits<float>::lowest());
			bool any = false;
			const size_t count = std::min(data.Joints.size(), joints.size());
			for (size_t i = 0; i < count; ++i) {
				const SkinJoint& joint = data.Joints[i];
				if (joint.BoundsMin.x > joint.BoundsMax.x) continue;
				// Box of the transformed box: centre moves with the matrix, extent grows by |M| per axis.
				const glm::vec3 center = (joint.BoundsMin + joint.BoundsMax) * 0.5f;
				const glm::vec3 half = (joint.BoundsMax - joint.BoundsMin) * 0.5f;
				const glm::mat4& m = joints[i];
				const glm::vec3 posedCenter(m * glm::vec4(center, 1.0f));
				const glm::vec3 posedHalf = glm::abs(glm::vec3(m[0])) * half.x + glm::abs(glm::vec3(m[1])) * half.y + glm::abs(glm::vec3(m[2])) * half.z;
				for (int axis = 0; axis < 3; ++axis) {
					if (!std::isfinite(posedCenter[axis]) || !std::isfinite(posedHalf[axis])) return false;
				}
				outMin = glm::min(outMin, posedCenter - posedHalf);
				outMax = glm::max(outMax, posedCenter + posedHalf);
				any = true;
			}
			if (any) {
				const glm::vec3 minScaled = glm::min(outMin * data.BoundsWeightSumMin, outMin * data.BoundsWeightSumMax);
				const glm::vec3 maxScaled = glm::max(outMax * data.BoundsWeightSumMin, outMax * data.BoundsWeightSumMax);
				const glm::vec3 rounding = glm::max(glm::abs(minScaled), glm::abs(maxScaled)) * 1e-6f + glm::vec3(1e-6f);
				outMin = minScaled - rounding;
				outMax = maxScaled + rounding;
				for (int axis = 0; axis < 3; ++axis) {
					if (!std::isfinite(outMin[axis]) || !std::isfinite(outMax[axis])) return false;
				}
			}
			return any;
		}

		void EvaluatePose(const SkinnedMeshData& data, const SkeletalPoseRequest& request, std::vector<glm::mat4>& outNodeModel) {
			std::vector<NodePose> pose, previous;
			EvaluatePose(data, request, outNodeModel, pose, previous);
		}

		void EvaluatePose(const SkinnedMeshData& data, const SkeletalPoseRequest& request, std::vector<glm::mat4>& outNodeModel,
			std::vector<NodePose>& pose, std::vector<NodePose>& previous) {
			auto validClip = [&](int clip) { return clip >= 0 && static_cast<size_t>(clip) < data.Clips.size(); };
			RestPose(data, pose);
			if (validClip(request.Clip)) SampleClip(data.Clips[static_cast<size_t>(request.Clip)], request.Time, pose);
			if (validClip(request.PreviousClip) && request.Fade < 1.0f) {
				RestPose(data, previous);
				SampleClip(data.Clips[static_cast<size_t>(request.PreviousClip)], request.PreviousTime, previous);
				Blend(previous, pose, request.Fade);
				pose.swap(previous);
			}
			ComputeNodeMatrices(data, pose, outNodeModel);
		}

		bool AdvanceTime(float& time, float delta, float duration, bool loop) {
			if (duration <= 0.0f) {
				time = 0.0f;
				return loop;
			}
			time += delta;
			if (loop) {
				time = std::fmod(time, duration);
				if (time < 0.0f) time += duration;
				return true;
			}
			if (time >= duration) {
				time = duration;
				return delta <= 0.0f;
			}
			if (time <= 0.0f) {
				time = 0.0f;
				return delta >= 0.0f;
			}
			return true;
		}
	}

}
