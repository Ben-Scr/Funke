#include "pch.hpp"
#include "Graphics/GltfImporter.hpp"

#include "Graphics/MeshAsset.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Serialization/Json.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <unordered_map>

namespace Funke {
	namespace {
		using Json::Value;

		constexpr uint32_t k_GlbMagic = 0x46546C67;
		constexpr uint32_t k_GlbJsonChunk = 0x4E4F534A;
		constexpr uint32_t k_GlbBinChunk = 0x004E4942;

		const Value* Member(const Value& v, std::string_view key) {
			return v.IsObject() ? v.FindMember(key) : nullptr;
		}

		const Value::Array& ArrayMember(const Value& v, std::string_view key) {
			static const Value::Array s_Empty;
			const Value* m = Member(v, key);
			return m && m->IsArray() ? m->GetArray() : s_Empty;
		}

		int64_t IntMember(const Value& v, std::string_view key, int64_t fallback) {
			const Value* m = Member(v, key);
			return m && m->IsNumber() ? m->AsInt64Or(fallback) : fallback;
		}

		std::string StringMember(const Value& v, std::string_view key) {
			const Value* m = Member(v, key);
			return m && m->IsString() ? m->AsStringOr() : std::string{};
		}

		bool ReadFloats(const Value& v, std::string_view key, float* out, size_t count) {
			const Value::Array& values = ArrayMember(v, key);
			if (values.size() != count) return false;
			for (size_t i = 0; i < count; ++i) out[i] = static_cast<float>(values[i].AsDoubleOr(out[i]));
			return true;
		}

		uint32_t ReadU32(const uint8_t* p) {
			uint32_t value = 0;
			std::memcpy(&value, p, sizeof(value));
			return value;
		}

		std::string PercentDecode(std::string_view uri) {
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			std::string out;
			out.reserve(uri.size());
			for (size_t i = 0; i < uri.size(); ++i) {
				if (uri[i] == '%' && i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0) {
					out.push_back(static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2])));
					i += 2;
				}
				else {
					out.push_back(uri[i]);
				}
			}
			return out;
		}

		class Importer {
		public:
			Importer(const GltfExternalReader& readExternal, std::string& error) : m_ReadExternal(readExternal), m_Error(error) {}

			bool Run(std::span<const uint8_t> bytes, MeshAsset& out) {
				std::string_view jsonText;
				std::span<const uint8_t> binChunk;
				if (bytes.size() >= 12 && ReadU32(bytes.data()) == k_GlbMagic) {
					if (!SplitGlb(bytes, jsonText, binChunk)) return false;
				}
				else {
					jsonText = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				}
				std::string parseError;
				if (!Json::TryParse(jsonText, m_Root, &parseError) || !m_Root.IsObject()) {
					return Fail("invalid JSON: " + parseError);
				}
				if (const Value* asset = Member(m_Root, "asset"); !asset || !StringMember(*asset, "version").starts_with("2")) {
					return Fail("only glTF 2.0 is supported");
				}
				if (!LoadBuffers(binChunk) || !LoadNodes() || !LoadSkins() || !LoadMeshes() || !LoadAnimations()) return false;
				if (m_Indices.empty()) return Fail("the file contains no triangle meshes");
				return Finish(out);
			}

		private:
			bool Fail(std::string message) {
				m_Error = std::move(message);
				return false;
			}

			bool SplitGlb(std::span<const uint8_t> bytes, std::string_view& outJson, std::span<const uint8_t>& outBin) {
				if (ReadU32(bytes.data() + 4) != 2) return Fail("only glTF 2.0 .glb files are supported");
				const size_t length = std::min<size_t>(ReadU32(bytes.data() + 8), bytes.size());
				size_t offset = 12;
				while (offset + 8 <= length) {
					const uint32_t chunkLength = ReadU32(bytes.data() + offset);
					const uint32_t chunkType = ReadU32(bytes.data() + offset + 4);
					offset += 8;
					if (chunkLength > length - offset) return Fail("truncated .glb chunk");
					if (chunkType == k_GlbJsonChunk && outJson.empty()) {
						outJson = std::string_view(reinterpret_cast<const char*>(bytes.data() + offset), chunkLength);
					}
					else if (chunkType == k_GlbBinChunk && outBin.empty()) {
						outBin = bytes.subspan(offset, chunkLength);
					}
					offset += (chunkLength + 3u) & ~size_t(3);
				}
				return outJson.empty() ? Fail(".glb has no JSON chunk") : true;
			}

			bool LoadBuffers(std::span<const uint8_t> binChunk) {
				const Value::Array& buffers = ArrayMember(m_Root, "buffers");
				m_Buffers.resize(buffers.size());
				for (size_t i = 0; i < buffers.size(); ++i) {
					const int64_t byteLength = IntMember(buffers[i], "byteLength", 0);
					const std::string uri = StringMember(buffers[i], "uri");
					std::vector<uint8_t>& data = m_Buffers[i];
					if (uri.empty()) {
						if (i != 0 || binChunk.empty()) return Fail("buffer " + std::to_string(i) + " has no data");
						data.assign(binChunk.begin(), binChunk.end());
					}
					else if (uri.starts_with("data:")) {
						const size_t comma = uri.find(',');
						if (comma == std::string::npos || uri.substr(0, comma).find(";base64") == std::string::npos
							|| !DecodeBase64(std::string_view(uri).substr(comma + 1), data)) {
							return Fail("buffer " + std::to_string(i) + " has an unsupported data uri");
						}
					}
					else if (!m_ReadExternal || !m_ReadExternal(PercentDecode(uri), data)) {
						return Fail("could not read buffer '" + uri + "'");
					}
					if (byteLength < 0 || data.size() < static_cast<size_t>(byteLength)) {
						return Fail("buffer " + std::to_string(i) + " is shorter than its byteLength");
					}
				}
				return true;
			}

			// Accessor values as doubles (exact for every glTF component type); normalized integers map to [0,1] / [-1,1].
			bool ReadAccessor(int64_t index, bool rawIntegers, std::vector<double>& out, int& outComponents) {
				const Value::Array& accessors = ArrayMember(m_Root, "accessors");
				if (index < 0 || static_cast<size_t>(index) >= accessors.size()) return Fail("accessor " + std::to_string(index) + " does not exist");
				const Value& accessor = accessors[static_cast<size_t>(index)];
				const std::string type = StringMember(accessor, "type");
				int components = 0;
				if (type == "SCALAR") components = 1;
				else if (type == "VEC2") components = 2;
				else if (type == "VEC3") components = 3;
				else if (type == "VEC4") components = 4;
				else if (type == "MAT4") components = 16;
				else return Fail("accessor " + std::to_string(index) + " has unsupported type '" + type + "'");

				const int64_t componentType = IntMember(accessor, "componentType", 0);
				size_t componentSize = 0;
				switch (componentType) {
				case 5120: case 5121: componentSize = 1; break;
				case 5122: case 5123: componentSize = 2; break;
				case 5125: case 5126: componentSize = 4; break;
				default: return Fail("accessor " + std::to_string(index) + " has unsupported componentType");
				}
				const int64_t count = IntMember(accessor, "count", 0);
				if (count < 0 || count > (int64_t(1) << 28)) return Fail("accessor " + std::to_string(index) + " has an invalid count");
				const bool normalized = !rawIntegers && Member(accessor, "normalized") && Member(accessor, "normalized")->AsBoolOr(false);
				outComponents = components;
				out.assign(static_cast<size_t>(count) * static_cast<size_t>(components), 0.0);
				if (Member(accessor, "sparse")) FNK_CORE_WARN_TAG("Mesh", "glTF sparse accessors are not supported; accessor {} reads its base values", index);

				const Value* viewIndex = Member(accessor, "bufferView");
				if (!viewIndex) return true;
				const Value::Array& views = ArrayMember(m_Root, "bufferViews");
				const int64_t viewId = viewIndex->AsInt64Or(-1);
				if (viewId < 0 || static_cast<size_t>(viewId) >= views.size()) return Fail("bufferView " + std::to_string(viewId) + " does not exist");
				const Value& view = views[static_cast<size_t>(viewId)];
				const int64_t bufferId = IntMember(view, "buffer", -1);
				if (bufferId < 0 || static_cast<size_t>(bufferId) >= m_Buffers.size()) return Fail("bufferView " + std::to_string(viewId) + " has no buffer");
				const std::vector<uint8_t>& buffer = m_Buffers[static_cast<size_t>(bufferId)];

				const size_t elementSize = componentSize * static_cast<size_t>(components);
				const size_t viewOffset = static_cast<size_t>(std::max<int64_t>(IntMember(view, "byteOffset", 0), 0));
				const size_t viewLength = static_cast<size_t>(std::max<int64_t>(IntMember(view, "byteLength", 0), 0));
				const size_t stride = std::max<size_t>(static_cast<size_t>(std::max<int64_t>(IntMember(view, "byteStride", 0), 0)), elementSize);
				const size_t offset = static_cast<size_t>(std::max<int64_t>(IntMember(accessor, "byteOffset", 0), 0));
				if (viewOffset + viewLength > buffer.size()) return Fail("bufferView " + std::to_string(viewId) + " exceeds its buffer");
				if (count > 0 && offset + stride * static_cast<size_t>(count - 1) + elementSize > viewLength) {
					return Fail("accessor " + std::to_string(index) + " exceeds its bufferView");
				}

				const uint8_t* base = buffer.data() + viewOffset + offset;
				for (size_t e = 0; e < static_cast<size_t>(count); ++e) {
					const uint8_t* element = base + e * stride;
					for (int c = 0; c < components; ++c) {
						const uint8_t* p = element + static_cast<size_t>(c) * componentSize;
						double value = 0.0;
						switch (componentType) {
						case 5120: { int8_t x; std::memcpy(&x, p, 1); value = normalized ? std::max(x / 127.0, -1.0) : x; break; }
						case 5121: { uint8_t x; std::memcpy(&x, p, 1); value = normalized ? x / 255.0 : x; break; }
						case 5122: { int16_t x; std::memcpy(&x, p, 2); value = normalized ? std::max(x / 32767.0, -1.0) : x; break; }
						case 5123: { uint16_t x; std::memcpy(&x, p, 2); value = normalized ? x / 65535.0 : x; break; }
						case 5125: { uint32_t x; std::memcpy(&x, p, 4); value = x; break; }
						case 5126: { float x; std::memcpy(&x, p, 4); value = x; break; }
						default: break;
						}
						out[e * static_cast<size_t>(components) + static_cast<size_t>(c)] = value;
					}
				}
				return true;
			}

			bool LoadNodes() {
				const Value::Array& nodes = ArrayMember(m_Root, "nodes");
				m_Data->Nodes.resize(nodes.size());
				for (size_t i = 0; i < nodes.size(); ++i) {
					SkeletonNode& node = m_Data->Nodes[i];
					node.Name = StringMember(nodes[i], "name");
					if (node.Name.empty()) node.Name = "Node " + std::to_string(i);
					float matrix[16];
					if (ReadFloats(nodes[i], "matrix", matrix, 16)) {
						const glm::mat4 m = glm::make_mat4(matrix);
						node.Translation = glm::vec3(m[3]);
						node.Scale = { glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) };
						glm::mat3 rotation(glm::vec3(m[0]) / std::max(node.Scale.x, 1e-12f), glm::vec3(m[1]) / std::max(node.Scale.y, 1e-12f),
							glm::vec3(m[2]) / std::max(node.Scale.z, 1e-12f));
						if (glm::determinant(rotation) < 0.0f) {
							node.Scale.x = -node.Scale.x;
							rotation[0] = -rotation[0];
						}
						node.Rotation = glm::normalize(glm::quat_cast(rotation));
					}
					else {
						float t[3] = { 0.0f, 0.0f, 0.0f };
						float r[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
						float s[3] = { 1.0f, 1.0f, 1.0f };
						ReadFloats(nodes[i], "translation", t, 3);
						ReadFloats(nodes[i], "rotation", r, 4);
						ReadFloats(nodes[i], "scale", s, 3);
						node.Translation = { t[0], t[1], t[2] };
						node.Rotation = glm::normalize(glm::quat(r[3], r[0], r[1], r[2]));
						node.Scale = { s[0], s[1], s[2] };
					}
				}
				for (size_t i = 0; i < nodes.size(); ++i) {
					for (const Value& child : ArrayMember(nodes[i], "children")) {
						const int64_t c = child.AsInt64Or(-1);
						if (c < 0 || static_cast<size_t>(c) >= nodes.size() || m_Data->Nodes[static_cast<size_t>(c)].Parent >= 0 || static_cast<size_t>(c) == i) {
							return Fail("node " + std::to_string(i) + " has an invalid child");
						}
						m_Data->Nodes[static_cast<size_t>(c)].Parent = static_cast<int32_t>(i);
					}
				}
				std::vector<std::vector<uint32_t>> children(nodes.size());
				std::deque<uint32_t> queue;
				for (size_t i = 0; i < nodes.size(); ++i) {
					const int32_t parent = m_Data->Nodes[i].Parent;
					if (parent < 0) queue.push_back(static_cast<uint32_t>(i));
					else children[static_cast<size_t>(parent)].push_back(static_cast<uint32_t>(i));
				}
				while (!queue.empty()) {
					const uint32_t index = queue.front();
					queue.pop_front();
					m_Data->NodeOrder.push_back(index);
					for (uint32_t child : children[index]) queue.push_back(child);
				}
				if (m_Data->NodeOrder.size() != nodes.size()) return Fail("the node hierarchy contains a cycle");
				return true;
			}

			bool LoadSkins() {
				for (const Value& skin : ArrayMember(m_Root, "skins")) {
					m_SkinBase.push_back(static_cast<uint32_t>(m_Data->Joints.size()));
					const Value::Array& joints = ArrayMember(skin, "joints");
					std::vector<double> inverseBind;
					int components = 0;
					if (const Value* accessor = Member(skin, "inverseBindMatrices")) {
						if (!ReadAccessor(accessor->AsInt64Or(-1), false, inverseBind, components)) return false;
						if (components != 16 || inverseBind.size() < joints.size() * 16) return Fail("a skin's inverseBindMatrices do not match its joints");
					}
					for (size_t j = 0; j < joints.size(); ++j) {
						const int64_t node = joints[j].AsInt64Or(-1);
						if (node < 0 || static_cast<size_t>(node) >= m_Data->Nodes.size()) return Fail("a skin references a missing joint node");
						SkinJoint joint;
						joint.Node = static_cast<uint32_t>(node);
						if (!inverseBind.empty()) {
							float m[16];
							for (int k = 0; k < 16; ++k) m[k] = static_cast<float>(inverseBind[j * 16 + static_cast<size_t>(k)]);
							joint.InverseBind = glm::make_mat4(m);
						}
						m_Data->Joints.push_back(joint);
					}
					m_SkinJointCount.push_back(static_cast<uint32_t>(joints.size()));
				}
				return true;
			}

			uint32_t RigidJoint(uint32_t node) {
				auto [it, inserted] = m_RigidJoints.try_emplace(node, static_cast<uint32_t>(m_Data->Joints.size()));
				if (inserted) m_Data->Joints.push_back({ node, glm::mat4(1.0f) });
				return it->second;
			}

			bool LoadMeshes() {
				const Value::Array& nodes = ArrayMember(m_Root, "nodes");
				const Value::Array& meshes = ArrayMember(m_Root, "meshes");
				for (uint32_t nodeIndex : m_Data->NodeOrder) {
					const Value& node = nodes[nodeIndex];
					const int64_t meshIndex = IntMember(node, "mesh", -1);
					if (meshIndex < 0) continue;
					if (static_cast<size_t>(meshIndex) >= meshes.size()) return Fail("node " + std::to_string(nodeIndex) + " references a missing mesh");
					const int64_t skin = IntMember(node, "skin", -1);
					if (skin >= static_cast<int64_t>(m_SkinBase.size())) return Fail("node " + std::to_string(nodeIndex) + " references a missing skin");
					for (const Value& primitive : ArrayMember(meshes[static_cast<size_t>(meshIndex)], "primitives")) {
						if (!LoadPrimitive(primitive, nodeIndex, skin)) return false;
					}
				}
				return true;
			}

			bool LoadPrimitive(const Value& primitive, uint32_t node, int64_t skin) {
				const int64_t mode = IntMember(primitive, "mode", 4);
				if (mode != 4 && mode != 5 && mode != 6) return true;
				const Value* attributes = Member(primitive, "attributes");
				const Value* positionAccessor = attributes ? Member(*attributes, "POSITION") : nullptr;
				if (!positionAccessor) return true;

				std::vector<double> positions, normals, uvs, joints, weights, indices;
				int components = 0;
				if (!ReadAccessor(positionAccessor->AsInt64Or(-1), false, positions, components)) return false;
				if (components != 3) return Fail("POSITION must be VEC3");
				const size_t vertexCount = positions.size() / 3;
				auto readOptional = [&](const char* name, bool raw, std::vector<double>& out, int expected) {
					const Value* accessor = Member(*attributes, name);
					if (!accessor) return true;
					int c = 0;
					if (!ReadAccessor(accessor->AsInt64Or(-1), raw, out, c)) return false;
					if (c != expected || out.size() != vertexCount * static_cast<size_t>(expected)) {
						out.clear();
						FNK_CORE_WARN_TAG("Mesh", "glTF attribute {} has an unexpected layout and is ignored", name);
					}
					return true;
				};
				if (!readOptional("NORMAL", false, normals, 3) || !readOptional("TEXCOORD_0", false, uvs, 2)
					|| !readOptional("JOINTS_0", true, joints, 4) || !readOptional("WEIGHTS_0", false, weights, 4)) {
					return false;
				}

				std::vector<uint32_t> source;
				if (const Value* accessor = Member(primitive, "indices")) {
					if (!ReadAccessor(accessor->AsInt64Or(-1), true, indices, components)) return false;
					source.reserve(indices.size());
					for (double i : indices) {
						if (i < 0.0 || i >= static_cast<double>(vertexCount)) return Fail("a primitive index is out of range");
						source.push_back(static_cast<uint32_t>(i));
					}
				}
				else {
					for (size_t i = 0; i < vertexCount; ++i) source.push_back(static_cast<uint32_t>(i));
				}

				PrimitiveMeshData local;
				local.Vertices.resize(vertexCount);
				for (size_t v = 0; v < vertexCount; ++v) {
					MeshVertex& vertex = local.Vertices[v];
					for (int k = 0; k < 3; ++k) {
						vertex.Position[k] = static_cast<float>(positions[v * 3 + static_cast<size_t>(k)]);
						vertex.Normal[k] = normals.empty() ? 0.0f : static_cast<float>(normals[v * 3 + static_cast<size_t>(k)]);
					}
					// glTF and the engine both put the UV origin top-left.
					vertex.UV[0] = uvs.empty() ? 0.0f : static_cast<float>(uvs[v * 2]);
					vertex.UV[1] = uvs.empty() ? 0.0f : static_cast<float>(uvs[v * 2 + 1]);
				}
				if (mode == 4) {
					for (size_t i = 0; i + 2 < source.size(); i += 3) local.Indices.insert(local.Indices.end(), { source[i], source[i + 1], source[i + 2] });
				}
				else if (mode == 5) {
					for (size_t i = 0; i + 2 < source.size(); ++i) {
						if (i % 2 == 0) local.Indices.insert(local.Indices.end(), { source[i], source[i + 1], source[i + 2] });
						else local.Indices.insert(local.Indices.end(), { source[i + 1], source[i], source[i + 2] });
					}
				}
				else {
					for (size_t i = 1; i + 1 < source.size(); ++i) local.Indices.insert(local.Indices.end(), { source[0], source[i], source[i + 1] });
				}
				if (local.Indices.empty()) return true;
				if (normals.empty()) RecalculateNormals(local);

				const size_t base = m_Data->BindVertices.size();
				if (base + vertexCount > std::numeric_limits<uint32_t>::max()) return Fail("too many vertices");
				const bool skinned = skin >= 0 && !joints.empty() && !weights.empty();
				const uint32_t rigid = skinned ? 0u : RigidJoint(node);
				for (size_t v = 0; v < vertexCount; ++v) {
					std::array<uint16_t, 4> influence{ static_cast<uint16_t>(rigid), 0, 0, 0 };
					glm::vec4 weight(1.0f, 0.0f, 0.0f, 0.0f);
					if (skinned) {
						const uint32_t skinBase = m_SkinBase[static_cast<size_t>(skin)];
						const uint32_t skinCount = m_SkinJointCount[static_cast<size_t>(skin)];
						float sum = 0.0f;
						for (int k = 0; k < 4; ++k) {
							const double j = joints[v * 4 + static_cast<size_t>(k)];
							const float w = std::max(static_cast<float>(weights[v * 4 + static_cast<size_t>(k)]), 0.0f);
							if (j < 0.0 || j >= skinCount) {
								weight[k] = 0.0f;
								continue;
							}
							influence[k] = static_cast<uint16_t>(skinBase + static_cast<uint32_t>(j));
							weight[k] = w;
							sum += w;
						}
						if (sum > 1e-6f) {
							weight /= sum;
						}
						else {
							influence = { static_cast<uint16_t>(RigidJoint(node)), 0, 0, 0 };
							weight = { 1.0f, 0.0f, 0.0f, 0.0f };
						}
					}
					m_Data->VertexJoints.push_back(influence);
					m_Data->VertexWeights.push_back(weight);
				}
				m_Data->BindVertices.insert(m_Data->BindVertices.end(), local.Vertices.begin(), local.Vertices.end());
				for (uint32_t index : local.Indices) m_Indices.push_back(static_cast<uint32_t>(base) + index);
				if (m_Data->Joints.size() > std::numeric_limits<uint16_t>::max()) return Fail("too many joints");
				return true;
			}

			bool LoadAnimations() {
				const Value::Array& animations = ArrayMember(m_Root, "animations");
				for (size_t a = 0; a < animations.size(); ++a) {
					SkeletalClip clip;
					clip.Name = StringMember(animations[a], "name");
					if (clip.Name.empty()) clip.Name = "Animation " + std::to_string(a);
					const Value::Array& samplers = ArrayMember(animations[a], "samplers");
					for (const Value& channelJson : ArrayMember(animations[a], "channels")) {
						const Value* target = Member(channelJson, "target");
						if (!target) continue;
						const int64_t node = IntMember(*target, "node", -1);
						const std::string path = StringMember(*target, "path");
						if (node < 0 || static_cast<size_t>(node) >= m_Data->Nodes.size()) continue;
						SkeletalChannel channel;
						channel.Node = static_cast<uint32_t>(node);
						int expected = 3;
						if (path == "translation") channel.Property = AnimatedProperty::Translation;
						else if (path == "rotation") { channel.Property = AnimatedProperty::Rotation; expected = 4; }
						else if (path == "scale") channel.Property = AnimatedProperty::Scale;
						else continue;

						const int64_t samplerIndex = IntMember(channelJson, "sampler", -1);
						if (samplerIndex < 0 || static_cast<size_t>(samplerIndex) >= samplers.size()) return Fail("an animation channel has no sampler");
						const Value& sampler = samplers[static_cast<size_t>(samplerIndex)];
						const std::string interpolation = StringMember(sampler, "interpolation");
						if (interpolation == "STEP") channel.Interpolation = KeyframeInterpolation::Step;
						else if (interpolation == "CUBICSPLINE") channel.Interpolation = KeyframeInterpolation::CubicSpline;

						std::vector<double> times, values;
						int components = 0;
						if (!ReadAccessor(IntMember(sampler, "input", -1), false, times, components)) return false;
						if (components != 1 || times.empty()) continue;
						if (!ReadAccessor(IntMember(sampler, "output", -1), false, values, components)) return false;
						const size_t perKey = channel.Interpolation == KeyframeInterpolation::CubicSpline ? 3 : 1;
						if (components != expected || values.size() != times.size() * perKey * static_cast<size_t>(expected)) {
							FNK_CORE_WARN_TAG("Mesh", "glTF animation '{}' has a channel whose keys don't match; skipped", clip.Name);
							continue;
						}
						channel.Times.reserve(times.size());
						for (double t : times) channel.Times.push_back(static_cast<float>(t));
						std::sort(channel.Times.begin(), channel.Times.end());
						channel.Values.reserve(values.size() / static_cast<size_t>(expected));
						for (size_t i = 0; i < values.size(); i += static_cast<size_t>(expected)) {
							channel.Values.emplace_back(static_cast<float>(values[i]), static_cast<float>(values[i + 1]), static_cast<float>(values[i + 2]),
								expected == 4 ? static_cast<float>(values[i + 3]) : 0.0f);
						}
						clip.Duration = std::max(clip.Duration, channel.Times.back());
						clip.Channels.push_back(std::move(channel));
					}
					if (!clip.Channels.empty()) m_Data->Clips.push_back(std::move(clip));
				}
				return true;
			}

			bool Finish(MeshAsset& out) {
				std::vector<glm::mat4> nodeModel;
				std::vector<glm::mat4> joints;
				SkeletalAnimation::EvaluatePose(*m_Data, {}, nodeModel);
				SkeletalAnimation::ComputeJointMatrices(*m_Data, nodeModel, joints);
				SkeletalAnimation::SkinVertices(*m_Data, joints, out.Data.Vertices);
				out.Data.Indices = std::move(m_Indices);
				// Static models need no skeleton at runtime.
				if (!m_SkinBase.empty() || !m_Data->Clips.empty()) out.Skin = std::move(m_Data);
				return true;
			}

			const GltfExternalReader& m_ReadExternal;
			std::string& m_Error;
			Value m_Root;
			std::vector<std::vector<uint8_t>> m_Buffers;
			std::shared_ptr<SkinnedMeshData> m_Data = std::make_shared<SkinnedMeshData>();
			std::vector<uint32_t> m_SkinBase;
			std::vector<uint32_t> m_SkinJointCount;
			std::unordered_map<uint32_t, uint32_t> m_RigidJoints;
			std::vector<uint32_t> m_Indices;
		};
	}

	bool DecodeBase64(std::string_view text, std::vector<uint8_t>& outBytes) {
		auto decode = [](char c) -> int {
			if (c >= 'A' && c <= 'Z') return c - 'A';
			if (c >= 'a' && c <= 'z') return c - 'a' + 26;
			if (c >= '0' && c <= '9') return c - '0' + 52;
			if (c == '+' || c == '-') return 62;
			if (c == '/' || c == '_') return 63;
			return -1;
		};
		outBytes.clear();
		outBytes.reserve(text.size() / 4 * 3);
		uint32_t accumulator = 0;
		int bits = 0;
		for (char c : text) {
			if (c == '=') break;
			if (c == '\r' || c == '\n' || c == ' ') continue;
			const int value = decode(c);
			if (value < 0) return false;
			accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				outBytes.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
			}
		}
		return true;
	}

	bool ParseGltf(std::span<const uint8_t> bytes, const GltfExternalReader& readExternal, MeshAsset& outAsset, std::string* outError) {
		outAsset = MeshAsset{};
		std::string error;
		Importer importer(readExternal, error);
		if (!importer.Run(bytes, outAsset)) {
			if (outError) *outError = error;
			outAsset = MeshAsset{};
			return false;
		}
		return true;
	}

}
