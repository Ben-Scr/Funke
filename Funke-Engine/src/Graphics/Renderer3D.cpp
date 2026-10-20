#include "pch.hpp"
#include "Graphics/Renderer3D.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/CustomShaderGpu.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Graphics/Lighting3D.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/PostProcessing/PostProcessor.hpp"
#include "Graphics/PrimitiveMesh.hpp"
#include "Graphics/RenderApi.hpp"
#include "Graphics/Shader.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Graphics/SkyAmbient.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/TextureSampling.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"
#include "Systems/SkinnedMeshAnimationSystem.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <webgpu/webgpu_cpp.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>

namespace Funke {
	namespace {
		constexpr wgpu::TextureFormat k_DepthFormat = wgpu::TextureFormat::Depth24PlusStencil8;
		constexpr wgpu::TextureFormat k_ShadowFormat = wgpu::TextureFormat::Depth32Float;
		// Frame mesh slots: the primitives first, then each custom mesh asset drawn this frame.
		constexpr size_t k_MeshTypeCount = k_PrimitiveMeshTypeCount;

		struct FrameUniforms {
			glm::mat4 ViewProjection;
			glm::mat4 LightViewProjection;
			glm::vec4 CameraPosition;   // w = exposure
			glm::vec4 Ambient;          // w = sky ambient strength
			glm::vec4 SkyTop;
			glm::vec4 SkyHorizon;
			glm::vec4 SkyGround;
			glm::vec4 Params;
			glm::vec4 ShadowParams;
			glm::vec4 FogColor;         // linear rgb, w = 1 when fog is on
			glm::vec4 FogParams;        // x = start, y = end
		};
		static_assert(sizeof(FrameUniforms) == 272);

		struct GpuLight {
			glm::vec4 PositionRange;
			glm::vec4 DirectionType;
			glm::vec4 Radiance;
			glm::vec4 Spot;
		};
		using LightUniforms = std::array<GpuLight, Lighting3D::k_MaxLights>;
		static_assert(sizeof(LightUniforms) == 64 * Lighting3D::k_MaxLights);

		struct SkyUniforms {
			glm::mat4 InverseViewProjection;
			glm::vec4 Top;
			glm::vec4 Horizon;
			glm::vec4 Ground;
			glm::vec4 SunDirection;
			glm::vec4 SunColor;
			glm::vec4 Params;
		};
		static_assert(sizeof(SkyUniforms) == 160);

		// Must match the instance attributes (locations 3..13) of the "mesh" WGSL; the first 64 bytes also feed "shadow3d".
		struct MeshInstance {
			glm::vec4 Model[4];
			glm::vec4 Normal[3];
			glm::vec4 Color;
			glm::vec4 Emissive;
			glm::vec4 Surface;
			glm::vec4 UvTransform;
		};
		static_assert(sizeof(MeshInstance) == 176);
		constexpr uint32_t k_InstanceVec4Count = 11;

		struct ParticleFrameUniforms {
			glm::mat4 ViewProjection;
			glm::vec4 CameraRight;
			glm::vec4 CameraUp;
			glm::vec4 Params;
		};
		static_assert(sizeof(ParticleFrameUniforms) == 112);

		// Must match the instance attributes of the "particle3d" WGSL.
		struct ParticleInstance {
			glm::vec4 PositionSize;
			glm::vec4 Color;
			glm::vec4 Params;
		};
		static_assert(sizeof(ParticleInstance) == 48);

		struct ParticleBatch {
			uint32_t First;
			uint32_t Count;
			TextureHandle Texture;
			ParticleBlendMode3D Blend;
			float ViewDistanceSq;
		};

		struct DrawItem {
			uint64_t Key;          // frame mesh slot << 40 | noShadow << 32 | texture slot
			uint32_t Instance;
			float ViewDistanceSq;  // sorts transparent items back to front
			bool Transparent;
			uint32_t Custom = 0;   // frame custom-material slot + 1; 0 = the standard shader
			// World AABB for culling; Cullable is false when the vertex shader may move geometry past it.
			glm::vec3 BoundsCenter{ 0.0f };
			glm::vec3 BoundsExtent{ 0.0f };
			bool Cullable = false;
		};

		// One custom-shaded material drawn this frame.
		struct FrameCustom {
			uint64_t MaterialGuid = 0;
			const Material* Material = nullptr;
			const CustomShaderProgram* Program = nullptr;
			wgpu::RenderPipeline Pipeline;
			wgpu::BindGroup Group;
			// Set when the shader has fn vertex: the shadow pass then draws through its vs_shadow.
			wgpu::RenderPipeline ShadowPipeline;
		};

		uint64_t MakeDrawKey(size_t mesh, bool castsShadow, uint32_t textureSlot) {
			return (static_cast<uint64_t>(mesh) << 40) | (static_cast<uint64_t>(castsShadow ? 0 : 1) << 32) | textureSlot;
		}
		size_t KeyMesh(uint64_t key) { return static_cast<size_t>(key >> 40); }
		bool KeyCastsShadow(uint64_t key) { return ((key >> 32) & 1) == 0; }
		// Bit 33: outside the camera frustum, drawn only into the shadow map.
		constexpr uint64_t k_KeyHiddenBit = uint64_t(1) << 33;
		bool KeyHidden(uint64_t key) { return (key & k_KeyHiddenBit) != 0; }

		// Planes (xyz normal, w distance) of a view-projection's clip volume, inside where dot >= 0. The near
		// plane uses -w <= z, which also holds for 0..1 depth, so the test errs towards drawing.
		struct FrustumPlanes { glm::vec4 Planes[6]; };

		FrustumPlanes ExtractFrustum(const glm::mat4& viewProjection) {
			const glm::mat4 m = glm::transpose(viewProjection);
			FrustumPlanes f;
			f.Planes[0] = m[3] + m[0];
			f.Planes[1] = m[3] - m[0];
			f.Planes[2] = m[3] + m[1];
			f.Planes[3] = m[3] - m[1];
			f.Planes[4] = m[3] + m[2];
			f.Planes[5] = m[3] - m[2];
			return f;
		}

		bool IntersectsFrustum(const FrustumPlanes& frustum, const glm::vec3& center, const glm::vec3& extent) {
			for (const glm::vec4& plane : frustum.Planes) {
				const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
				const float radius = std::abs(plane.x) * extent.x + std::abs(plane.y) * extent.y + std::abs(plane.z) * extent.z;
				if (distance < -radius) return false;
			}
			return true;
		}
		uint32_t KeyTextureSlot(uint64_t key) { return static_cast<uint32_t>(key & 0xFFFFFFFFu); }

		void SetAttribute(wgpu::VertexAttribute& attribute, wgpu::VertexFormat format, uint64_t offset, uint32_t location) {
			attribute.format = format;
			attribute.offset = offset;
			attribute.shaderLocation = location;
		}

		// Vertex streams of the "mesh" WGSL and custom mesh shaders: MeshVertex in slot 0, MeshInstance in slot 1.
		struct MeshVertexStreams {
			wgpu::VertexAttribute VertexAttributes[3] = {};
			wgpu::VertexAttribute InstanceAttributes[k_InstanceVec4Count] = {};
			wgpu::VertexBufferLayout Buffers[2] = {};

			MeshVertexStreams() {
				SetAttribute(VertexAttributes[0], wgpu::VertexFormat::Float32x3, offsetof(MeshVertex, Position), 0);
				SetAttribute(VertexAttributes[1], wgpu::VertexFormat::Float32x3, offsetof(MeshVertex, Normal), 1);
				SetAttribute(VertexAttributes[2], wgpu::VertexFormat::Float32x2, offsetof(MeshVertex, UV), 2);
				for (uint32_t i = 0; i < k_InstanceVec4Count; ++i) {
					SetAttribute(InstanceAttributes[i], wgpu::VertexFormat::Float32x4, static_cast<uint64_t>(i) * 16u, 3u + i);
				}
				Buffers[0].arrayStride = sizeof(MeshVertex);
				Buffers[0].stepMode = wgpu::VertexStepMode::Vertex;
				Buffers[0].attributeCount = 3;
				Buffers[0].attributes = VertexAttributes;
				Buffers[1].arrayStride = sizeof(MeshInstance);
				Buffers[1].stepMode = wgpu::VertexStepMode::Instance;
				Buffers[1].attributeCount = k_InstanceVec4Count;
				Buffers[1].attributes = InstanceAttributes;
			}
			// Buffers point into this object.
			MeshVertexStreams(const MeshVertexStreams&) = delete;
			MeshVertexStreams& operator=(const MeshVertexStreams&) = delete;
		};

		uint32_t PackRgba(const Color& c) {
			auto u8 = [](float v) {
				v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
				return static_cast<uint32_t>(v * 255.0f + 0.5f);
			};
			return u8(c.r) | (u8(c.g) << 8) | (u8(c.b) << 16) | (u8(c.a) << 24);
		}

		glm::vec4 ToVec4(const Color& c) { return { c.r, c.g, c.b, c.a }; }

		bool IsSrgbFormat(wgpu::TextureFormat format) {
			return format == wgpu::TextureFormat::BGRA8UnormSrgb || format == wgpu::TextureFormat::RGBA8UnormSrgb;
		}

		// Cache key of a pipeline that draws into the scene target; variant tells apart pipelines of one kind.
		uint64_t ScenePipelineKey(wgpu::TextureFormat colorFormat, uint32_t sampleCount, uint64_t variant = 0) {
			return static_cast<uint64_t>(static_cast<uint32_t>(colorFormat)) | (static_cast<uint64_t>(sampleCount & 0xFFu) << 32)
				| (variant << 40);
		}

		// Formats a multisampled Framebuffer can take so it resolves straight into the target; others go through RGBA16F.
		std::optional<TextureFormat> ToFramebufferFormat(wgpu::TextureFormat format) {
			switch (format) {
				case wgpu::TextureFormat::RGBA8Unorm: return TextureFormat::RGBA8;
				case wgpu::TextureFormat::BGRA8Unorm: return TextureFormat::BGRA8;
				case wgpu::TextureFormat::RGBA16Float: return TextureFormat::RGBA16F;
				default: return std::nullopt;
			}
		}

		// An empty pass whose store resolves the multisampled colour into destination (same size and format).
		void ResolveMultisampled(const Framebuffer& source, const wgpu::TextureView& destination) {
			const auto look = WebGPUBackend::LookupFramebufferByFboId(source.GetBackendId());
			wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
			if (!look.Valid || look.SampleCount <= 1 || !destination || !encoder) return;
			wgpu::RenderPassColorAttachment attachment{};
			attachment.view = look.ColorView;
			attachment.resolveTarget = destination;
			attachment.loadOp = wgpu::LoadOp::Load;
			attachment.storeOp = wgpu::StoreOp::Discard;
			attachment.depthSlice = wgpu::kDepthSliceUndefined;

			wgpu::RenderPassDescriptor desc{};
			desc.label = "renderer3d-msaa-resolve";
			desc.colorAttachmentCount = 1;
			desc.colorAttachments = &attachment;
			encoder.BeginRenderPass(&desc).End();
		}

		wgpu::Buffer CreateBufferWithData(wgpu::Device device, wgpu::BufferUsage usage, const void* data, size_t size, const char* label) {
			wgpu::BufferDescriptor desc{};
			desc.size = (size + 3) & ~size_t(3);
			desc.usage = usage;
			desc.mappedAtCreation = true;
			desc.label = label;
			wgpu::Buffer buffer = device.CreateBuffer(&desc);
			if (!buffer) return nullptr;
			std::memcpy(buffer.GetMappedRange(), data, size);
			buffer.Unmap();
			return buffer;
		}

		// LineList indices for the editor wireframe: the three edges of every triangle. Shared edges draw twice.
		std::vector<uint32_t> BuildWireIndices(const std::vector<uint32_t>& triangles) {
			std::vector<uint32_t> lines;
			lines.reserve(triangles.size() * 2);
			for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
				const uint32_t a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
				lines.insert(lines.end(), { a, b, b, c, c, a });
			}
			return lines;
		}

		bool EnsureBuffer(wgpu::Device device, wgpu::Buffer& buffer, uint64_t& capacity, uint64_t needed, wgpu::BufferUsage usage, const char* label) {
			if (buffer && capacity >= needed) return true;
			uint64_t newCapacity = capacity > 0 ? capacity : 4096;
			while (newCapacity < needed) newCapacity *= 2;
			newCapacity = std::min<uint64_t>(newCapacity, WebGPUBackend::GetMaxBufferSize());
			if (newCapacity < needed) return false;

			wgpu::BufferDescriptor desc{};
			desc.size = newCapacity;
			desc.usage = usage | wgpu::BufferUsage::CopyDst;
			desc.label = label;
			wgpu::Buffer created = device.CreateBuffer(&desc);
			if (!created) return false;
			buffer = std::move(created);
			capacity = newCapacity;
			return true;
		}

		wgpu::Buffer CreateUniformBuffer(wgpu::Device device, uint64_t size, const char* label) {
			wgpu::Buffer buffer;
			uint64_t capacity = 0;
			EnsureBuffer(device, buffer, capacity, size, wgpu::BufferUsage::Uniform, label);
			return buffer;
		}

		wgpu::BindGroupLayoutEntry UniformEntry(uint32_t binding, uint64_t size, wgpu::ShaderStage visibility) {
			wgpu::BindGroupLayoutEntry entry{};
			entry.binding = binding;
			entry.visibility = visibility;
			entry.buffer.type = wgpu::BufferBindingType::Uniform;
			entry.buffer.minBindingSize = size;
			return entry;
		}

		wgpu::BindGroupLayout CreateLayout(wgpu::Device device, const wgpu::BindGroupLayoutEntry* entries, size_t count, const char* label) {
			wgpu::BindGroupLayoutDescriptor desc{};
			desc.entryCount = count;
			desc.entries = entries;
			desc.label = label;
			return device.CreateBindGroupLayout(&desc);
		}

		wgpu::BindGroup CreateUniformBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout, wgpu::Buffer buffer, uint64_t size, const char* label) {
			wgpu::BindGroupEntry entry{};
			entry.binding = 0;
			entry.buffer = buffer;
			entry.size = size;

			wgpu::BindGroupDescriptor desc{};
			desc.layout = layout;
			desc.entryCount = 1;
			desc.entries = &entry;
			desc.label = label;
			return device.CreateBindGroup(&desc);
		}

		wgpu::PipelineLayout CreatePipelineLayout(wgpu::Device device, const wgpu::BindGroupLayout* layouts, size_t count, const char* label) {
			wgpu::PipelineLayoutDescriptor desc{};
			desc.bindGroupLayoutCount = count;
			desc.bindGroupLayouts = layouts;
			desc.label = label;
			return device.CreatePipelineLayout(&desc);
		}

		// sampling: texture quality and anisotropy; linear filtering only (the point sampler keeps every mip and no anisotropy).
		wgpu::Sampler CreateRepeatSampler(wgpu::Device device, wgpu::FilterMode filter, const char* label,
			const TextureSamplingQuality& sampling = {}) {
			wgpu::SamplerDescriptor desc{};
			desc.addressModeU = wgpu::AddressMode::Repeat;
			desc.addressModeV = wgpu::AddressMode::Repeat;
			desc.addressModeW = wgpu::AddressMode::Repeat;
			desc.magFilter = filter;
			desc.minFilter = filter;
			desc.mipmapFilter = filter == wgpu::FilterMode::Linear ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest;
			const bool linear = filter == wgpu::FilterMode::Linear;
			desc.lodMinClamp = linear ? sampling.LodMinClamp : 0.0f;
			desc.maxAnisotropy = linear ? std::max<uint16_t>(sampling.MaxAnisotropy, 1) : 1;
			desc.label = label;
			return device.CreateSampler(&desc);
		}

		struct SceneLighting {
			std::vector<Lighting3D::LightCandidate> Selected;
			glm::vec3 FlatAmbient{ 0.0f };
			int ShadowedLight = -1;
		};

		// The first enabled DirectionalLight is the main light: its ambient is the flat ambient, it alone casts shadows.
		SceneLighting CollectLights(const entt::registry& registry, const glm::vec3& cameraPosition, int maxLights) {
			std::vector<Lighting3D::LightCandidate> candidates;
			SceneLighting result;
			bool anyLight = false;

			const auto directional = registry.view<const DirectionalLightComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
			for (EntityHandle entity : directional) {
				const auto& light = directional.get<const DirectionalLightComponent>(entity);
				const auto& transform = directional.get<const Transform3DComponent>(entity);
				Lighting3D::LightCandidate candidate;
				candidate.Type = Lighting3D::LightType::Directional;
				const glm::vec3 forward = -glm::vec3(transform.WorldMatrix[2]);
				candidate.Direction = glm::dot(forward, forward) > 1e-8f ? glm::normalize(forward) : glm::vec3(0.0f, -1.0f, 0.0f);
				candidate.Radiance = Lighting3D::SrgbToLinear(light.Color) * std::max(light.Intensity, 0.0f);
				if (!anyLight) {
					result.FlatAmbient = Lighting3D::SrgbToLinear(light.Ambient);
					candidate.CastsShadows = light.CastShadows;
				}
				anyLight = true;
				candidates.push_back(candidate);
			}

			const auto points = registry.view<const PointLightComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
			for (EntityHandle entity : points) {
				const auto& light = points.get<const PointLightComponent>(entity);
				const auto& transform = points.get<const Transform3DComponent>(entity);
				Lighting3D::LightCandidate candidate;
				candidate.Type = Lighting3D::LightType::Point;
				candidate.Position = glm::vec3(transform.WorldMatrix[3]);
				candidate.Radiance = Lighting3D::SrgbToLinear(light.Color) * std::max(light.Intensity, 0.0f);
				candidate.Range = std::max(light.Range, 0.0f);
				anyLight = true;
				candidates.push_back(candidate);
			}

			const auto spots = registry.view<const SpotLightComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
			for (EntityHandle entity : spots) {
				const auto& light = spots.get<const SpotLightComponent>(entity);
				const auto& transform = spots.get<const Transform3DComponent>(entity);
				Lighting3D::LightCandidate candidate;
				candidate.Type = Lighting3D::LightType::Spot;
				candidate.Position = glm::vec3(transform.WorldMatrix[3]);
				const glm::vec3 forward = -glm::vec3(transform.WorldMatrix[2]);
				candidate.Direction = glm::dot(forward, forward) > 1e-8f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
				candidate.Radiance = Lighting3D::SrgbToLinear(light.Color) * std::max(light.Intensity, 0.0f);
				candidate.Range = std::max(light.Range, 0.0f);
				Lighting3D::SpotAnglesToCosines(light.InnerAngle, light.OuterAngle, candidate.CosInner, candidate.CosOuter);
				anyLight = true;
				candidates.push_back(candidate);
			}

			// A scene without any light still shows its meshes shaded instead of black.
			if (!anyLight) {
				Lighting3D::LightCandidate fallback;
				fallback.Direction = glm::normalize(glm::vec3(-0.35f, -1.0f, -0.45f));
				fallback.Radiance = glm::vec3(0.8f);
				result.FlatAmbient = Lighting3D::SrgbToLinear(DirectionalLightComponent{}.Ambient);
				candidates.push_back(fallback);
			}

			result.Selected = Lighting3D::SelectLights(candidates, cameraPosition, maxLights);
			for (size_t i = 0; i < result.Selected.size(); ++i) {
				if (result.Selected[i].CastsShadows) {
					result.ShadowedLight = static_cast<int>(i);
					break;
				}
			}
			return result;
		}

		// Main light of the scene for the sun disc; false when it has none.
		bool FindMainDirectionalLight(const entt::registry& registry, glm::vec3& outDirection, Color& outColor) {
			const auto view = registry.view<const DirectionalLightComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
			for (EntityHandle entity : view) {
				const glm::vec3 forward = -glm::vec3(view.get<const Transform3DComponent>(entity).WorldMatrix[2]);
				if (glm::dot(forward, forward) <= 1e-8f) return false;
				outDirection = glm::normalize(forward);
				outColor = view.get<const DirectionalLightComponent>(entity).Color;
				return true;
			}
			return false;
		}
	}

	Camera3DEnvironment Camera3DEnvironment::FromCamera(const Camera3DComponent& camera) {
		Camera3DEnvironment environment;
		environment.Background = camera.Background;
		environment.ClearColor = camera.ClearColor;
		environment.SkyTop = camera.SkyTopColor;
		environment.SkyHorizon = camera.SkyHorizonColor;
		environment.SkyGround = camera.SkyGroundColor;
		environment.SunSize = camera.SunSize;
		environment.SkyAmbient = camera.SkyAmbient;
		environment.SkyboxMaterial = camera.SkyboxMaterial;
		environment.FogEnabled = camera.FogEnabled;
		environment.FogColor = camera.FogColor;
		environment.FogStart = camera.FogStart;
		environment.FogEnd = camera.FogEnd;
		if (camera.AmbientFromSkybox && camera.SkyboxMaterial != 0) {
			if (const auto derived = SkyAmbient::FromMaterial(camera.SkyboxMaterial)) {
				environment.SkyTop = derived->Top;
				environment.SkyHorizon = derived->Horizon;
				environment.SkyGround = derived->Ground;
			}
		}
		return environment;
	}

	struct Renderer3D::GpuState {
		struct MeshBuffers {
			wgpu::Buffer Vertices;
			wgpu::Buffer Indices;
			uint32_t IndexCount = 0;
			uint32_t VertexCount = 0;  // for the Tris/Verts statistics
			// LineList edges, built on the first wireframe draw.
			wgpu::Buffer WireIndices;
			uint32_t WireIndexCount = 0;
		};

		struct CustomMeshEntry {
			MeshBuffers Buffers;
			uint32_t Revision = 0;
			uint32_t WireRevision = 0;
			uint64_t VertexCapacity = 0;
			uint64_t IndexCapacity = 0;
			uint64_t WireIndexCapacity = 0;
		};

		// One posed copy of an animated model per (scene, entity); re-skinned only when the pose or the model changed.
		struct SkinnedKey {
			const Scene* OwnerScene = nullptr;
			uint32_t Entity = 0;
			bool operator==(const SkinnedKey&) const = default;
		};
		struct SkinnedKeyHash {
			size_t operator()(const SkinnedKey& key) const noexcept {
				return std::hash<const void*>{}(key.OwnerScene) ^ (std::hash<uint32_t>{}(key.Entity) * 0x9e3779b97f4a7c15ULL);
			}
		};
		struct SkinnedEntry {
			MeshBuffers Buffers;
			uint64_t VertexCapacity = 0;
			std::vector<MeshVertex> Vertices;
			SkeletalPoseRequest Pose;
			uint32_t Revision = 0;
			std::chrono::steady_clock::time_point LastUsed{};
		};

		struct CachedMaterialGroup {
			wgpu::BindGroup Group;
			wgpu::TextureView View;
		};

		struct TextureEntry {
			TextureHandle Handle;
			std::chrono::steady_clock::time_point LastAttempt{};
		};

		wgpu::ShaderModule MeshModule;
		wgpu::ShaderModule SkyModule;
		wgpu::ShaderModule ShadowModule;
		wgpu::ShaderModule LineModule;
		wgpu::ShaderModule ParticleModule;

		wgpu::BindGroupLayout FrameLayout;
		wgpu::BindGroupLayout MaterialLayout;
		wgpu::BindGroupLayout SkyLayout;
		wgpu::BindGroupLayout ShadowLayout;
		// Group 0 of custom shadow pipelines: the frame uniform only (the shadow map is the pass's attachment).
		wgpu::BindGroupLayout CustomShadowFrameLayout;
		wgpu::BindGroup CustomShadowFrameGroup;
		wgpu::BindGroupLayout LineLayout;
		wgpu::PipelineLayout MeshPipelineLayout;
		wgpu::PipelineLayout SkyPipelineLayout;
		wgpu::PipelineLayout ShadowPipelineLayout;
		wgpu::PipelineLayout LinePipelineLayout;
		wgpu::PipelineLayout WirePipelineLayout;
		wgpu::BindGroupLayout ParticleLayout;
		wgpu::PipelineLayout ParticlePipelineLayout;
		wgpu::Buffer ParticleUniformBuffer;
		wgpu::BindGroup ParticleBindGroup;
		wgpu::Buffer ParticleInstanceBuffer;
		uint64_t ParticleInstanceCapacity = 0;
		std::unordered_map<uint64_t, wgpu::RenderPipeline> ParticlePipelines;
		std::vector<ParticleInstance> ParticleInstances;
		std::vector<ParticleBatch> ParticleBatches;
		std::vector<std::pair<float, uint32_t>> ParticleOrder;

		wgpu::Buffer FrameUniformBuffer;
		wgpu::Buffer LightBuffer;
		wgpu::Buffer SkyUniformBuffer;
		wgpu::Buffer ShadowUniformBuffer;
		wgpu::Buffer LineUniformBuffer;
		wgpu::BindGroup FrameBindGroup;
		wgpu::TextureView FrameBindGroupShadowView;
		wgpu::BindGroup SkyBindGroup;
		wgpu::BindGroup ShadowBindGroup;
		wgpu::BindGroup LineBindGroup;

		wgpu::Sampler ShadowSampler;
		// Bilinear textures; Trilinear and Anisotropic ones also get the anisotropy setting. Both follow the texture quality.
		wgpu::Sampler LinearRepeatSampler;
		wgpu::Sampler AnisotropicRepeatSampler;
		wgpu::Sampler PointRepeatSampler;
		TextureQualityLevel SamplerTextureQuality = TextureQualityLevel::Full;
		AnisotropicFilteringLevel SamplerAnisotropy = AnisotropicFilteringLevel::Off;
		wgpu::Texture ShadowTexture;
		wgpu::TextureView ShadowView;
		uint32_t ShadowResolution = 0;
		wgpu::Texture DummyShadowTexture;
		wgpu::TextureView DummyShadowView;

		wgpu::Buffer InstanceBuffer;
		uint64_t InstanceCapacity = 0;
		wgpu::Buffer LineVertexBuffer;
		uint64_t LineVertexCapacity = 0;

		std::array<MeshBuffers, k_MeshTypeCount> Meshes;
		std::array<glm::vec3, k_MeshTypeCount> MeshBoundsMin{};
		std::array<glm::vec3, k_MeshTypeCount> MeshBoundsMax{};
		std::unordered_map<uint64_t, CustomMeshEntry> CustomMeshes;
		std::unordered_map<SkinnedKey, SkinnedEntry, SkinnedKeyHash> SkinnedMeshes;
		std::vector<glm::mat4> PoseNodes;
		std::vector<glm::mat4> PoseJoints;
		std::vector<const MeshBuffers*> FrameMeshes;
		std::unordered_map<uint64_t, uint32_t> FrameMeshSlots;
		// Keyed by ScenePipelineKey: the MSAA scene target needs its own variants.
		std::unordered_map<uint64_t, wgpu::RenderPipeline> MeshPipelines;
		std::unordered_map<uint64_t, wgpu::RenderPipeline> SkyPipelines;
		std::unordered_map<uint64_t, wgpu::RenderPipeline> LinePipelines;
		std::unordered_map<uint64_t, wgpu::RenderPipeline> WirePipelines;
		wgpu::RenderPipeline ShadowPipeline;
		std::unordered_map<uint64_t, CachedMaterialGroup> MaterialGroups;

		CustomMaterialBindings CustomBindings;
		std::vector<FrameCustom> FrameCustoms;
		std::unordered_map<uint64_t, uint32_t> FrameCustomSlots;
		// MaterialLibrary + shader lookups resolved once per material per RenderMeshes.
		struct FrameMaterial {
			const Material* Value = nullptr;
			const CustomShaderProgram* Program = nullptr;
		};
		std::unordered_map<uint64_t, FrameMaterial> FrameMaterials;
		// By program id, color format and sample count; failed creations stay cached as null.
		std::unordered_map<uint64_t, wgpu::RenderPipeline> CustomPipelines;
		std::unordered_map<uint32_t, wgpu::PipelineLayout> CustomPipelineLayouts;
		std::unordered_map<uint64_t, wgpu::RenderPipeline> CustomShadowPipelines;
		std::unordered_map<uint32_t, wgpu::PipelineLayout> CustomShadowPipelineLayouts;

		// Swap-chain passes have no depth attachment of their own.
		wgpu::Texture OwnDepthTexture;
		wgpu::TextureView OwnDepthView;
		uint32_t OwnDepthWidth = 0;
		uint32_t OwnDepthHeight = 0;

		// Base-color textures by asset GUID; reported to TextureManager so PurgeUnreferenced keeps them.
		std::unordered_map<uint64_t, TextureEntry> MaterialTextures;
		uint32_t TextureReferenceToken = 0;
		uint32_t TextureDestroyToken = 0;

		std::vector<MeshInstance> Instances;
		std::vector<DrawItem> DrawItems;
		std::vector<MeshInstance> InstanceUpload;
		std::vector<TextureHandle> FrameTextures;
		std::unordered_map<uint64_t, uint32_t> FrameTextureSlots;
		LightUniforms LightUpload{};

		wgpu::TextureView EnsureOwnDepth(wgpu::Device device, uint32_t width, uint32_t height) {
			if (OwnDepthView && OwnDepthWidth == width && OwnDepthHeight == height) {
				return OwnDepthView;
			}
			wgpu::TextureDescriptor desc{};
			desc.size = { width, height, 1 };
			desc.format = k_DepthFormat;
			desc.usage = wgpu::TextureUsage::RenderAttachment;
			desc.label = "renderer3d-depth";
			OwnDepthTexture = device.CreateTexture(&desc);
			OwnDepthView = OwnDepthTexture ? OwnDepthTexture.CreateView() : nullptr;
			OwnDepthWidth = OwnDepthView ? width : 0;
			OwnDepthHeight = OwnDepthView ? height : 0;
			return OwnDepthView;
		}

		wgpu::Texture CreateShadowTexture(wgpu::Device device, uint32_t size, const char* label) {
			wgpu::TextureDescriptor desc{};
			desc.size = { size, size, 1 };
			desc.format = k_ShadowFormat;
			desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
			desc.label = label;
			return device.CreateTexture(&desc);
		}

		wgpu::TextureView EnsureShadowMap(wgpu::Device device, uint32_t resolution) {
			if (ShadowView && ShadowResolution == resolution) return ShadowView;
			ShadowTexture = CreateShadowTexture(device, resolution, "renderer3d-shadow-map");
			ShadowView = ShadowTexture ? ShadowTexture.CreateView() : nullptr;
			ShadowResolution = ShadowView ? resolution : 0;
			return ShadowView;
		}

		void ReleaseShadowMap() {
			ShadowView = nullptr;
			ShadowTexture = nullptr;
			ShadowResolution = 0;
		}

		wgpu::BindGroup GetFrameBindGroup(wgpu::Device device, wgpu::TextureView shadowView) {
			if (FrameBindGroup && FrameBindGroupShadowView.Get() == shadowView.Get()) return FrameBindGroup;
			wgpu::BindGroupEntry entries[4] = {};
			entries[0].binding = 0;
			entries[0].buffer = FrameUniformBuffer;
			entries[0].size = sizeof(FrameUniforms);
			entries[1].binding = 1;
			entries[1].buffer = LightBuffer;
			entries[1].size = sizeof(LightUniforms);
			entries[2].binding = 2;
			entries[2].textureView = shadowView;
			entries[3].binding = 3;
			entries[3].sampler = ShadowSampler;

			wgpu::BindGroupDescriptor desc{};
			desc.layout = FrameLayout;
			desc.entryCount = 4;
			desc.entries = entries;
			desc.label = "renderer3d-frame-bg";
			FrameBindGroup = device.CreateBindGroup(&desc);
			FrameBindGroupShadowView = FrameBindGroup ? shadowView : nullptr;
			return FrameBindGroup;
		}

		// Rebuilds the linear samplers when the texture settings changed; cached material groups still hold the old ones.
		void ApplyTextureSampling(wgpu::Device device, TextureQualityLevel quality, AnisotropicFilteringLevel anisotropy) {
			if (LinearRepeatSampler && quality == SamplerTextureQuality && anisotropy == SamplerAnisotropy) return;
			LinearRepeatSampler = CreateRepeatSampler(device, wgpu::FilterMode::Linear, "renderer3d-linear-repeat",
				ResolveTextureSampling(Filter::Bilinear, quality, anisotropy));
			AnisotropicRepeatSampler = CreateRepeatSampler(device, wgpu::FilterMode::Linear, "renderer3d-anisotropic-repeat",
				ResolveTextureSampling(Filter::Anisotropic, quality, anisotropy));
			SamplerTextureQuality = quality;
			SamplerAnisotropy = anisotropy;
			MaterialGroups.clear();
		}

		wgpu::BindGroup GetMaterialBindGroup(wgpu::Device device, TextureHandle handle) {
			Texture2D* texture = TextureManager::GetTexture(handle);
			if (!texture || !texture->IsValid()) return nullptr;
			const auto lookup = WebGPUBackend::LookupTexture2D(texture->GetHandle());
			if (!lookup.Valid || !lookup.View) return nullptr;
			const Filter filter = texture->GetFilter();
			const uint64_t samplerIndex = filter == Filter::Point ? 1u
				: filter == Filter::Trilinear || filter == Filter::Anisotropic ? 2u : 0u;
			const uint64_t key = (texture->GetHandle() << 2) | samplerIndex;

			if (auto it = MaterialGroups.find(key); it != MaterialGroups.end()) {
				if (it->second.View.Get() == lookup.View.Get()) return it->second.Group;
				MaterialGroups.erase(it);
			}

			wgpu::BindGroupEntry entries[2] = {};
			entries[0].binding = 0;
			entries[0].textureView = lookup.View;
			entries[1].binding = 1;
			entries[1].sampler = samplerIndex == 1 ? PointRepeatSampler
				: samplerIndex == 2 ? AnisotropicRepeatSampler : LinearRepeatSampler;

			wgpu::BindGroupDescriptor desc{};
			desc.layout = MaterialLayout;
			desc.entryCount = 2;
			desc.entries = entries;
			desc.label = "renderer3d-material-bg";
			wgpu::BindGroup group = device.CreateBindGroup(&desc);
			if (group) MaterialGroups.emplace(key, CachedMaterialGroup{ group, lookup.View });
			return group;
		}

		// GPU copy of a mesh asset, re-uploaded when MeshLibrary reloaded it; nullptr when the upload failed.
		const MeshBuffers* ResolveCustomMesh(wgpu::Device device, uint64_t guid, const MeshAsset& asset, bool wireframe) {
			CustomMeshEntry& entry = CustomMeshes[guid];
			if (wireframe && entry.WireRevision != asset.Revision) {
				const std::vector<uint32_t> lines = BuildWireIndices(asset.Data.Indices);
				const uint64_t lineBytes = lines.size() * sizeof(uint32_t);
				bool uploaded = false;
				if (IsRuntimeMeshGuid(guid)) {
					uploaded = EnsureBuffer(device, entry.Buffers.WireIndices, entry.WireIndexCapacity, lineBytes, wgpu::BufferUsage::Index, "renderer3d-runtime-mesh-wire-ib");
					if (uploaded) device.GetQueue().WriteBuffer(entry.Buffers.WireIndices, 0, lines.data(), lineBytes);
				}
				else {
					entry.Buffers.WireIndices = CreateBufferWithData(device, wgpu::BufferUsage::Index, lines.data(), lineBytes, "renderer3d-custom-mesh-wire-ib");
					uploaded = static_cast<bool>(entry.Buffers.WireIndices);
				}
				entry.Buffers.WireIndexCount = uploaded ? static_cast<uint32_t>(lines.size()) : 0;
				entry.WireRevision = asset.Revision;
			}
			if (entry.Revision != asset.Revision) {
				const PrimitiveMeshData& data = asset.Data;
				const uint64_t vertexBytes = data.Vertices.size() * sizeof(MeshVertex);
				const uint64_t indexBytes = data.Indices.size() * sizeof(uint32_t);
				if (IsRuntimeMeshGuid(guid)) {
					// Runtime meshes typically change every frame: grow-only buffers rewritten in place instead of reallocated.
					if (!EnsureBuffer(device, entry.Buffers.Vertices, entry.VertexCapacity, vertexBytes, wgpu::BufferUsage::Vertex, "renderer3d-runtime-mesh-vb")
						|| !EnsureBuffer(device, entry.Buffers.Indices, entry.IndexCapacity, indexBytes, wgpu::BufferUsage::Index, "renderer3d-runtime-mesh-ib")) {
						return nullptr;
					}
					wgpu::Queue queue = device.GetQueue();
					queue.WriteBuffer(entry.Buffers.Vertices, 0, data.Vertices.data(), vertexBytes);
					queue.WriteBuffer(entry.Buffers.Indices, 0, data.Indices.data(), indexBytes);
				}
				else {
					entry.Buffers.Vertices = CreateBufferWithData(device, wgpu::BufferUsage::Vertex, data.Vertices.data(), vertexBytes, "renderer3d-custom-mesh-vb");
					entry.Buffers.Indices = CreateBufferWithData(device, wgpu::BufferUsage::Index, data.Indices.data(), indexBytes, "renderer3d-custom-mesh-ib");
				}
				entry.Buffers.IndexCount = static_cast<uint32_t>(data.Indices.size());
				entry.Buffers.VertexCount = static_cast<uint32_t>(data.Vertices.size());
				entry.Revision = asset.Revision;
			}
			return entry.Buffers.Vertices && entry.Buffers.Indices ? &entry.Buffers : nullptr;
		}

		// Vertices posed on the CPU into a per-entity grow-only buffer; indices are the model's shared ones.
		const MeshBuffers* ResolveSkinnedMesh(wgpu::Device device, const SkinnedKey& key, const SkinnedMeshRendererComponent& renderer,
			const MeshAsset& asset, const MeshBuffers& shared, std::chrono::steady_clock::time_point now) {
			SkinnedEntry& entry = SkinnedMeshes[key];
			entry.LastUsed = now;
			const SkinnedMeshData& data = *asset.Skin;
			const SkeletalPoseRequest pose = SkinnedMeshAnimationSystem::GetDrawnPose(renderer, data);
			if (!entry.Buffers.Vertices || entry.Revision != asset.Revision || !(entry.Pose == pose)) {
				FUNKE_PROFILE_SCOPE("Renderer3D.Skinning");
				SkeletalAnimation::EvaluatePose(data, pose, PoseNodes);
				SkeletalAnimation::ComputeJointMatrices(data, PoseNodes, PoseJoints);
				SkeletalAnimation::SkinVertices(data, PoseJoints, entry.Vertices);
				const uint64_t bytes = entry.Vertices.size() * sizeof(MeshVertex);
				if (bytes == 0 || !EnsureBuffer(device, entry.Buffers.Vertices, entry.VertexCapacity, bytes, wgpu::BufferUsage::Vertex, "renderer3d-skinned-vb")) {
					return nullptr;
				}
				device.GetQueue().WriteBuffer(entry.Buffers.Vertices, 0, entry.Vertices.data(), bytes);
				entry.Revision = asset.Revision;
				entry.Pose = pose;
			}
			entry.Buffers.Indices = shared.Indices;
			entry.Buffers.IndexCount = shared.IndexCount;
			entry.Buffers.VertexCount = static_cast<uint32_t>(entry.Vertices.size());
			entry.Buffers.WireIndices = shared.WireIndices;
			entry.Buffers.WireIndexCount = shared.WireIndexCount;
			return &entry.Buffers;
		}

		TextureHandle ResolveMaterialTexture(uint64_t textureGuid) {
			if (textureGuid == 0) return TextureHandle{};
			TextureEntry& entry = MaterialTextures[textureGuid];
			if (TextureManager::IsValid(entry.Handle)) return entry.Handle;
			const auto now = std::chrono::steady_clock::now();
			// Missing textures are retried once a second instead of hitting the disk every frame.
			if (entry.LastAttempt != std::chrono::steady_clock::time_point{} && now - entry.LastAttempt < std::chrono::seconds(1)) {
				return TextureHandle{};
			}
			entry.LastAttempt = now;
			entry.Handle = TextureManager::LoadTextureByUUID(textureGuid, Filter::Bilinear, Wrap::Repeat, Wrap::Repeat);
			return entry.Handle;
		}

		// Transparent: straight-alpha blending, depth tested but not written so later transparent surfaces still show.
		wgpu::RenderPipeline GetMeshPipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, uint32_t sampleCount, bool transparent) {
			const uint64_t key = ScenePipelineKey(colorFormat, sampleCount, transparent ? 1u : 0u);
			if (auto it = MeshPipelines.find(key); it != MeshPipelines.end()) {
				return it->second;
			}

			const MeshVertexStreams streams;

			wgpu::BlendState blend{};
			blend.color.operation = wgpu::BlendOperation::Add;
			blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
			blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			// Keeps the target's alpha opaque: the editor composites its viewport texture with ImGui.
			blend.alpha.operation = wgpu::BlendOperation::Add;
			blend.alpha.srcFactor = wgpu::BlendFactor::One;
			blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = colorFormat;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;
			if (transparent) colorTarget.blend = &blend;

			wgpu::FragmentState fragment{};
			fragment.module = MeshModule;
			fragment.entryPoint = "fs_main";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::DepthStencilState depth{};
			depth.format = k_DepthFormat;
			depth.depthWriteEnabled = !transparent;
			depth.depthCompare = wgpu::CompareFunction::Less;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = transparent ? "renderer3d-mesh-transparent-pipeline" : "renderer3d-mesh-pipeline";
			desc.layout = MeshPipelineLayout;
			desc.vertex.module = MeshModule;
			desc.vertex.entryPoint = "vs_main";
			desc.vertex.bufferCount = 2;
			desc.vertex.buffers = streams.Buffers;
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.frontFace = wgpu::FrontFace::CCW;
			desc.primitive.cullMode = wgpu::CullMode::Back;
			desc.depthStencil = &depth;
			desc.multisample.count = sampleCount;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (pipeline) MeshPipelines.emplace(key, pipeline);
			return pipeline;
		}

		// Editor wireframe: the mesh streams as black lines over MeshBuffers::WireIndices. Group 0 only.
		wgpu::RenderPipeline GetWirePipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, uint32_t sampleCount) {
			const uint64_t key = ScenePipelineKey(colorFormat, sampleCount);
			if (auto it = WirePipelines.find(key); it != WirePipelines.end()) return it->second;
			if (!WirePipelineLayout) return nullptr;

			const MeshVertexStreams streams;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = colorFormat;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragment{};
			fragment.module = MeshModule;
			fragment.entryPoint = "fs_wire";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::DepthStencilState depth{};
			depth.format = k_DepthFormat;
			depth.depthWriteEnabled = true;
			depth.depthCompare = wgpu::CompareFunction::LessEqual;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "renderer3d-wire-pipeline";
			desc.layout = WirePipelineLayout;
			desc.vertex.module = MeshModule;
			desc.vertex.entryPoint = "vs_wire";
			desc.vertex.bufferCount = 2;
			desc.vertex.buffers = streams.Buffers;
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::LineList;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.depthStencil = &depth;
			desc.multisample.count = sampleCount;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (pipeline) WirePipelines.emplace(key, pipeline);
			return pipeline;
		}

		// User shader: same vertex streams as "mesh", group 0 = frame, group 1 = the shader's globals/properties/textures.
		wgpu::RenderPipeline GetCustomPipeline(wgpu::TextureFormat colorFormat, uint32_t sampleCount, const CustomShaderProgram& program) {
			const uint64_t key = ScenePipelineKey(colorFormat, sampleCount, program.Id);
			if (auto it = CustomPipelines.find(key); it != CustomPipelines.end()) return it->second;
			// Recompiles leave old ids behind; rebuilding the few live ones is cheap.
			if (CustomPipelines.size() > 128) CustomPipelines.clear();

			const ShaderDescription& description = program.Description;
			wgpu::BindGroupLayout customLayout = CustomBindings.GetLayout(description);
			if (!customLayout) return nullptr;
			wgpu::PipelineLayout& layout = CustomPipelineLayouts[description.TextureCount];
			if (!layout) {
				const wgpu::BindGroupLayout layouts[2] = { FrameLayout, customLayout };
				layout = CreatePipelineLayout(WebGPUBackend::GetDevice(), layouts, 2, "renderer3d-custom-layout");
			}

			const MeshVertexStreams streams;

			const wgpu::BlendState blend = CustomShaderGpu::MakeBlendState(description.Blend);
			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = colorFormat;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;
			if (description.Blend != ShaderBlend::Opaque) colorTarget.blend = &blend;

			wgpu::FragmentState fragment{};
			fragment.module = program.Module;
			fragment.entryPoint = "fs_main";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::DepthStencilState depth{};
			depth.format = k_DepthFormat;
			depth.depthWriteEnabled = description.DepthWrite;
			depth.depthCompare = wgpu::CompareFunction::Less;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "renderer3d-custom-pipeline";
			desc.layout = layout;
			desc.vertex.module = program.Module;
			desc.vertex.entryPoint = "vs_main";
			desc.vertex.bufferCount = 2;
			desc.vertex.buffers = streams.Buffers;
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.frontFace = wgpu::FrontFace::CCW;
			desc.primitive.cullMode = CustomShaderGpu::ToCullMode(description.Cull);
			desc.depthStencil = &depth;
			desc.multisample.count = sampleCount;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = layout ? CustomShaderGpu::CreatePipeline(desc, program) : nullptr;
			CustomPipelines.emplace(key, pipeline);
			return pipeline;
		}

		// Same vertex streams as the custom pipeline, depth only: the user's fn vertex runs for the shadow map too.
		wgpu::RenderPipeline GetCustomShadowPipeline(const CustomShaderProgram& program) {
			if (auto it = CustomShadowPipelines.find(program.Id); it != CustomShadowPipelines.end()) return it->second;
			if (CustomShadowPipelines.size() > 128) CustomShadowPipelines.clear();

			const ShaderDescription& description = program.Description;
			wgpu::BindGroupLayout customLayout = CustomBindings.GetLayout(description);
			if (!customLayout || !CustomShadowFrameLayout) return nullptr;
			wgpu::PipelineLayout& layout = CustomShadowPipelineLayouts[description.TextureCount];
			if (!layout) {
				const wgpu::BindGroupLayout layouts[2] = { CustomShadowFrameLayout, customLayout };
				layout = CreatePipelineLayout(WebGPUBackend::GetDevice(), layouts, 2, "renderer3d-custom-shadow-layout");
			}

			wgpu::VertexAttribute vertexAttributes[3] = {};
			SetAttribute(vertexAttributes[0], wgpu::VertexFormat::Float32x3, offsetof(MeshVertex, Position), 0);
			SetAttribute(vertexAttributes[1], wgpu::VertexFormat::Float32x3, offsetof(MeshVertex, Normal), 1);
			SetAttribute(vertexAttributes[2], wgpu::VertexFormat::Float32x2, offsetof(MeshVertex, UV), 2);
			wgpu::VertexAttribute instanceAttributes[k_InstanceVec4Count] = {};
			for (uint32_t i = 0; i < k_InstanceVec4Count; ++i) {
				SetAttribute(instanceAttributes[i], wgpu::VertexFormat::Float32x4, static_cast<uint64_t>(i) * 16u, 3u + i);
			}
			wgpu::VertexBufferLayout buffers[2] = {};
			buffers[0].arrayStride = sizeof(MeshVertex);
			buffers[0].stepMode = wgpu::VertexStepMode::Vertex;
			buffers[0].attributeCount = 3;
			buffers[0].attributes = vertexAttributes;
			buffers[1].arrayStride = sizeof(MeshInstance);
			buffers[1].stepMode = wgpu::VertexStepMode::Instance;
			buffers[1].attributeCount = k_InstanceVec4Count;
			buffers[1].attributes = instanceAttributes;

			// Matches GetShadowPipeline.
			wgpu::DepthStencilState depth{};
			depth.format = k_ShadowFormat;
			depth.depthWriteEnabled = true;
			depth.depthCompare = wgpu::CompareFunction::Less;
			depth.depthBiasSlopeScale = 1.5f;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "renderer3d-custom-shadow-pipeline";
			desc.layout = layout;
			desc.vertex.module = program.Module;
			desc.vertex.entryPoint = "vs_shadow";
			desc.vertex.bufferCount = 2;
			desc.vertex.buffers = buffers;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.depthStencil = &depth;
			desc.multisample.count = 1;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = layout ? CustomShaderGpu::CreatePipeline(desc, program) : nullptr;
			CustomShadowPipelines.emplace(program.Id, pipeline);
			return pipeline;
		}

		// Pipeline and material group for each custom slot; a slot whose shader cannot draw switches to the fallback.
		void ResolveFrameCustoms(wgpu::TextureFormat colorFormat, uint32_t sampleCount) {
			for (FrameCustom& custom : FrameCustoms) {
				custom.ShadowPipeline = nullptr;
				for (int attempt = 0; attempt < 2 && custom.Program; ++attempt) {
					custom.Pipeline = GetCustomPipeline(colorFormat, sampleCount, *custom.Program);
					custom.Group = custom.Pipeline ? CustomBindings.GetGroup(custom.MaterialGuid, custom.Material, *custom.Program) : nullptr;
					if (custom.Pipeline && custom.Group) break;
					custom.Program = custom.Program->IsFallback ? nullptr : CustomShaderGpu::GetFallback(ShaderTarget::Mesh3D);
				}
				if (custom.Program && custom.Pipeline && custom.Group && custom.Program->Description.HasVertexFunction) {
					custom.ShadowPipeline = GetCustomShadowPipeline(*custom.Program);
				}
			}
		}

		// Unlit billboards: depth tested against the meshes but never written, so overlapping particles all show.
		wgpu::RenderPipeline GetParticlePipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, uint32_t sampleCount,
			ParticleBlendMode3D blendMode) {
			const bool additive = blendMode == ParticleBlendMode3D::Additive;
			const uint64_t key = ScenePipelineKey(colorFormat, sampleCount, additive ? 1u : 0u);
			if (auto it = ParticlePipelines.find(key); it != ParticlePipelines.end()) {
				return it->second;
			}

			wgpu::VertexAttribute attributes[3] = {};
			for (uint32_t i = 0; i < 3; ++i) {
				SetAttribute(attributes[i], wgpu::VertexFormat::Float32x4, static_cast<uint64_t>(i) * 16u, i);
			}
			wgpu::VertexBufferLayout buffer{};
			buffer.arrayStride = sizeof(ParticleInstance);
			buffer.stepMode = wgpu::VertexStepMode::Instance;
			buffer.attributeCount = 3;
			buffer.attributes = attributes;

			wgpu::BlendState blend{};
			blend.color.operation = wgpu::BlendOperation::Add;
			blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
			blend.color.dstFactor = additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::OneMinusSrcAlpha;
			// Keeps the target's alpha opaque: the editor composites its viewport texture with ImGui.
			blend.alpha.operation = wgpu::BlendOperation::Add;
			blend.alpha.srcFactor = additive ? wgpu::BlendFactor::Zero : wgpu::BlendFactor::One;
			blend.alpha.dstFactor = additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::OneMinusSrcAlpha;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = colorFormat;
			colorTarget.blend = &blend;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragment{};
			fragment.module = ParticleModule;
			fragment.entryPoint = "fs_main";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::DepthStencilState depth{};
			depth.format = k_DepthFormat;
			depth.depthWriteEnabled = false;
			depth.depthCompare = wgpu::CompareFunction::Less;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = additive ? "renderer3d-particle-additive-pipeline" : "renderer3d-particle-pipeline";
			desc.layout = ParticlePipelineLayout;
			desc.vertex.module = ParticleModule;
			desc.vertex.entryPoint = "vs_main";
			desc.vertex.bufferCount = 1;
			desc.vertex.buffers = &buffer;
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.depthStencil = &depth;
			desc.multisample.count = sampleCount;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (pipeline) ParticlePipelines.emplace(key, pipeline);
			return pipeline;
		}

		wgpu::RenderPipeline GetShadowPipeline(wgpu::Device device) {
			if (ShadowPipeline) return ShadowPipeline;

			wgpu::VertexAttribute vertexAttribute{};
			SetAttribute(vertexAttribute, wgpu::VertexFormat::Float32x3, offsetof(MeshVertex, Position), 0);
			wgpu::VertexAttribute instanceAttributes[4] = {};
			for (uint32_t i = 0; i < 4; ++i) {
				SetAttribute(instanceAttributes[i], wgpu::VertexFormat::Float32x4, static_cast<uint64_t>(i) * 16u, 1u + i);
			}

			wgpu::VertexBufferLayout buffers[2] = {};
			buffers[0].arrayStride = sizeof(MeshVertex);
			buffers[0].stepMode = wgpu::VertexStepMode::Vertex;
			buffers[0].attributeCount = 1;
			buffers[0].attributes = &vertexAttribute;
			buffers[1].arrayStride = sizeof(MeshInstance);
			buffers[1].stepMode = wgpu::VertexStepMode::Instance;
			buffers[1].attributeCount = 4;
			buffers[1].attributes = instanceAttributes;

			wgpu::DepthStencilState depth{};
			depth.format = k_ShadowFormat;
			depth.depthWriteEnabled = true;
			depth.depthCompare = wgpu::CompareFunction::Less;
			depth.depthBiasSlopeScale = 1.5f;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "renderer3d-shadow-pipeline";
			desc.layout = ShadowPipelineLayout;
			desc.vertex.module = ShadowModule;
			desc.vertex.entryPoint = "vs_main";
			desc.vertex.bufferCount = 2;
			desc.vertex.buffers = buffers;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			// Planes are single-sided; culling either face would drop them from the map.
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.depthStencil = &depth;
			desc.multisample.count = 1;
			desc.multisample.mask = 0xFFFFFFFF;

			ShadowPipeline = device.CreateRenderPipeline(&desc);
			return ShadowPipeline;
		}

		wgpu::RenderPipeline GetSkyPipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, uint32_t sampleCount) {
			const uint64_t key = ScenePipelineKey(colorFormat, sampleCount);
			if (auto it = SkyPipelines.find(key); it != SkyPipelines.end()) {
				return it->second;
			}

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = colorFormat;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragment{};
			fragment.module = SkyModule;
			fragment.entryPoint = "fs_main";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "renderer3d-sky-pipeline";
			desc.layout = SkyPipelineLayout;
			desc.vertex.module = SkyModule;
			desc.vertex.entryPoint = "vs_main";
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.multisample.count = sampleCount;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (pipeline) SkyPipelines.emplace(key, pipeline);
			return pipeline;
		}

		// Skybox-target material shaders: group 0 is the gradient sky's block, group 1 the material's.
		std::unordered_map<uint64_t, wgpu::RenderPipeline> CustomSkyPipelines;
		std::unordered_map<uint32_t, wgpu::PipelineLayout> CustomSkyPipelineLayouts;

		wgpu::RenderPipeline GetCustomSkyPipeline(wgpu::TextureFormat colorFormat, uint32_t sampleCount, const CustomShaderProgram& program) {
			const uint64_t key = ScenePipelineKey(colorFormat, sampleCount, program.Id);
			if (auto it = CustomSkyPipelines.find(key); it != CustomSkyPipelines.end()) return it->second;
			if (CustomSkyPipelines.size() > 64) CustomSkyPipelines.clear();

			wgpu::BindGroupLayout customLayout = CustomBindings.GetLayout(program.Description);
			wgpu::PipelineLayout& layout = CustomSkyPipelineLayouts[program.Description.TextureCount];
			if (!layout && customLayout) {
				const wgpu::BindGroupLayout layouts[2] = { SkyLayout, customLayout };
				layout = CreatePipelineLayout(WebGPUBackend::GetDevice(), layouts, 2, "renderer3d-custom-sky-layout");
			}

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = colorFormat;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragment{};
			fragment.module = program.Module;
			fragment.entryPoint = "fs_main";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "renderer3d-custom-sky-pipeline";
			desc.layout = layout;
			desc.vertex.module = program.Module;
			desc.vertex.entryPoint = "vs_main";
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.multisample.count = sampleCount;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = layout ? CustomShaderGpu::CreatePipeline(desc, program) : nullptr;
			CustomSkyPipelines.emplace(key, pipeline);
			return pipeline;
		}

		wgpu::RenderPipeline GetLinePipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, uint32_t sampleCount, bool depthTested) {
			const uint64_t key = ScenePipelineKey(colorFormat, sampleCount, depthTested ? 1u : 0u);
			if (auto it = LinePipelines.find(key); it != LinePipelines.end()) {
				return it->second;
			}

			wgpu::VertexAttribute attributes[2] = {};
			SetAttribute(attributes[0], wgpu::VertexFormat::Float32x3, offsetof(PosColorVertex, x), 0);
			SetAttribute(attributes[1], wgpu::VertexFormat::Unorm8x4, offsetof(PosColorVertex, color), 1);

			wgpu::VertexBufferLayout buffer{};
			buffer.arrayStride = sizeof(PosColorVertex);
			buffer.stepMode = wgpu::VertexStepMode::Vertex;
			buffer.attributeCount = 2;
			buffer.attributes = attributes;

			wgpu::BlendState blend{};
			blend.color.operation = wgpu::BlendOperation::Add;
			blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
			blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
			blend.alpha.operation = wgpu::BlendOperation::Add;
			blend.alpha.srcFactor = wgpu::BlendFactor::One;
			blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

			wgpu::ColorTargetState colorTarget{};
			colorTarget.format = colorFormat;
			colorTarget.blend = &blend;
			colorTarget.writeMask = wgpu::ColorWriteMask::All;

			wgpu::FragmentState fragment{};
			fragment.module = LineModule;
			fragment.entryPoint = "fs_main";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::DepthStencilState depth{};
			depth.format = k_DepthFormat;
			depth.depthWriteEnabled = false;
			depth.depthCompare = depthTested ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Always;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = "renderer3d-line-pipeline";
			desc.layout = LinePipelineLayout;
			desc.vertex.module = LineModule;
			desc.vertex.entryPoint = "vs_main";
			desc.vertex.bufferCount = 1;
			desc.vertex.buffers = &buffer;
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::LineList;
			desc.primitive.cullMode = wgpu::CullMode::None;
			desc.depthStencil = &depth;
			desc.multisample.count = sampleCount;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (pipeline) LinePipelines.emplace(key, pipeline);
			return pipeline;
		}
	};

	struct Renderer3D::PostProcessState {
		struct MsaaTarget {
			Framebuffer Fbo;
			uint64_t LastUse = 0;
		};

		PostProcessor Processor;
		Framebuffer SceneFbo;
		// FXAA result when camera effects still follow it.
		Framebuffer AntiAliasFbo;
		// MSAA4x draws into one of these; the editor's Editor View and Game View each keep theirs instead of resizing one.
		std::array<MsaaTarget, 2> MsaaTargets;
		uint64_t MsaaUseCounter = 0;
		WebGPUBackend::BoundTargetSnapshot Caller;
		// The snapshot has no view for the swap chain (acquired lazily); the effects write here.
		WebGPUBackend::CurrentTargetInfo CallerTarget;
		// Null when only anti-aliasing runs.
		const PostProcessing2DComponent* Settings = nullptr;
		// The multisampled target of this pass, resolved into SceneFbo (Intermediate) or straight into the caller.
		Framebuffer* Msaa = nullptr;
		// SceneFbo holds the scene for FXAA and the effects.
		bool Intermediate = false;
		bool AntiAlias = false;
		bool Active = false;
		bool InitFailed = false;

		Framebuffer* AcquireMsaaTarget(int width, int height, TextureFormat format) {
			MsaaTarget* pick = nullptr;
			for (MsaaTarget& target : MsaaTargets) {
				if (target.Fbo.IsValid() && target.Fbo.GetWidth() == width && target.Fbo.GetHeight() == height
					&& target.Fbo.GetColorFormat() == format) {
					pick = &target;
					break;
				}
			}
			if (!pick) {
				pick = &*std::min_element(MsaaTargets.begin(), MsaaTargets.end(),
					[](const MsaaTarget& a, const MsaaTarget& b) { return a.LastUse < b.LastUse; });
				if (!pick->Fbo.Recreate(width, height, format, TextureFilter::Linear, GetMultisampleCount(AntiAliasingMode::MSAA4x))) {
					return nullptr;
				}
			}
			pick->LastUse = ++MsaaUseCounter;
			return &pick->Fbo;
		}

		void ReleaseMsaaTargets() {
			for (MsaaTarget& target : MsaaTargets) {
				target.Fbo.Destroy();
				target.LastUse = 0;
			}
		}
	};

	Renderer3D::Renderer3D() = default;
	Renderer3D::~Renderer3D() = default;

	void Renderer3D::SetGraphicsSettings(const GraphicsSettings& settings) {
		m_Settings = ClampGraphicsSettings(settings);
		if (m_Gpu) {
			if (wgpu::Device device = WebGPUBackend::GetDevice()) {
				m_Gpu->ApplyTextureSampling(device, m_Settings.TextureQuality, m_Settings.AnisotropicFiltering);
			}
		}
		// A 4x target at 1080p holds about 65 MB of colour and depth.
		if (m_PostProcess && !m_PostProcess->Active && GetMultisampleCount(m_Settings.AntiAliasing) == 1) {
			m_PostProcess->ReleaseMsaaTargets();
		}
	}

	bool Renderer3D::Initialize() {
		if (m_IsInitialized) return true;
		if (!WebGPUBackend::IsInitialized()) {
			FNK_CORE_ERROR_TAG("Renderer3D", "Initialize called before the WebGPU backend");
			return false;
		}

		m_MeshShader = std::make_unique<Shader>(std::string("FunkeAssets/Shaders/MeshShader.vs"), std::string("FunkeAssets/Shaders/MeshShader.fs"));
		m_SkyShader = std::make_unique<Shader>(std::string("FunkeAssets/Shaders/SkyShader.vs"), std::string("FunkeAssets/Shaders/SkyShader.fs"));
		m_ShadowShader = std::make_unique<Shader>(std::string("FunkeAssets/Shaders/Shadow3DShader.vs"), std::string("FunkeAssets/Shaders/Shadow3DShader.fs"));
		m_LineShader = std::make_unique<Shader>(std::string("FunkeAssets/Shaders/GizmoShader.vs"), std::string("FunkeAssets/Shaders/GizmoShader.fs"));
		auto lookup = [](const std::unique_ptr<Shader>& shader) {
			return shader->IsValid() ? WebGPUBackend::LookupShader(shader->GetHandle()) : WebGPUBackend::ShaderLookup{};
		};
		const auto meshLookup = lookup(m_MeshShader);
		const auto skyLookup = lookup(m_SkyShader);
		const auto shadowLookup = lookup(m_ShadowShader);
		const auto lineLookup = lookup(m_LineShader);
		if (!meshLookup.Valid || !skyLookup.Valid || !shadowLookup.Valid || !lineLookup.Valid) {
			FNK_CORE_ERROR_TAG("Renderer3D", "A 3D shader failed to load; 3D rendering disabled");
			m_MeshShader.reset();
			m_SkyShader.reset();
			m_ShadowShader.reset();
			m_LineShader.reset();
			return false;
		}

		wgpu::Device device = WebGPUBackend::GetDevice();
		m_Gpu = std::make_unique<GpuState>();
		GpuState& gpu = *m_Gpu;
		gpu.MeshModule = meshLookup.Module;
		gpu.SkyModule = skyLookup.Module;
		gpu.ShadowModule = shadowLookup.Module;
		gpu.LineModule = lineLookup.Module;

		{
			wgpu::BindGroupLayoutEntry entries[4] = {
				UniformEntry(0, sizeof(FrameUniforms), wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment),
				UniformEntry(1, sizeof(LightUniforms), wgpu::ShaderStage::Fragment),
				{}, {},
			};
			entries[2].binding = 2;
			entries[2].visibility = wgpu::ShaderStage::Fragment;
			entries[2].texture.sampleType = wgpu::TextureSampleType::Depth;
			entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
			entries[3].binding = 3;
			entries[3].visibility = wgpu::ShaderStage::Fragment;
			entries[3].sampler.type = wgpu::SamplerBindingType::Comparison;
			gpu.FrameLayout = CreateLayout(device, entries, 4, "renderer3d-frame-bgl");
		}
		{
			wgpu::BindGroupLayoutEntry entries[2] = {};
			entries[0].binding = 0;
			entries[0].visibility = wgpu::ShaderStage::Fragment;
			entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
			entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
			entries[1].binding = 1;
			entries[1].visibility = wgpu::ShaderStage::Fragment;
			entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
			gpu.MaterialLayout = CreateLayout(device, entries, 2, "renderer3d-material-bgl");
		}
		const wgpu::BindGroupLayoutEntry skyEntry = UniformEntry(0, sizeof(SkyUniforms), wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);
		const wgpu::BindGroupLayoutEntry shadowEntry = UniformEntry(0, sizeof(glm::mat4), wgpu::ShaderStage::Vertex);
		const wgpu::BindGroupLayoutEntry lineEntry = UniformEntry(0, sizeof(glm::mat4), wgpu::ShaderStage::Vertex);
		gpu.SkyLayout = CreateLayout(device, &skyEntry, 1, "renderer3d-sky-bgl");
		gpu.ShadowLayout = CreateLayout(device, &shadowEntry, 1, "renderer3d-shadow-bgl");
		gpu.LineLayout = CreateLayout(device, &lineEntry, 1, "renderer3d-line-bgl");
		const wgpu::BindGroupLayoutEntry customShadowFrameEntry = UniformEntry(0, sizeof(FrameUniforms), wgpu::ShaderStage::Vertex);
		gpu.CustomShadowFrameLayout = CreateLayout(device, &customShadowFrameEntry, 1, "renderer3d-custom-shadow-frame-bgl");

		const wgpu::BindGroupLayout meshLayouts[2] = { gpu.FrameLayout, gpu.MaterialLayout };
		gpu.MeshPipelineLayout = CreatePipelineLayout(device, meshLayouts, 2, "renderer3d-mesh-layout");
		gpu.SkyPipelineLayout = CreatePipelineLayout(device, &gpu.SkyLayout, 1, "renderer3d-sky-layout");
		gpu.ShadowPipelineLayout = CreatePipelineLayout(device, &gpu.ShadowLayout, 1, "renderer3d-shadow-layout");
		gpu.LinePipelineLayout = CreatePipelineLayout(device, &gpu.LineLayout, 1, "renderer3d-line-layout");
		gpu.WirePipelineLayout = CreatePipelineLayout(device, &gpu.FrameLayout, 1, "renderer3d-wire-layout");

		gpu.FrameUniformBuffer = CreateUniformBuffer(device, sizeof(FrameUniforms), "renderer3d-frame-ubo");
		gpu.LightBuffer = CreateUniformBuffer(device, sizeof(LightUniforms), "renderer3d-lights-ubo");
		gpu.SkyUniformBuffer = CreateUniformBuffer(device, sizeof(SkyUniforms), "renderer3d-sky-ubo");
		gpu.ShadowUniformBuffer = CreateUniformBuffer(device, sizeof(glm::mat4), "renderer3d-shadow-ubo");
		gpu.LineUniformBuffer = CreateUniformBuffer(device, sizeof(glm::mat4), "renderer3d-line-ubo");

		{
			wgpu::SamplerDescriptor desc{};
			desc.addressModeU = wgpu::AddressMode::ClampToEdge;
			desc.addressModeV = wgpu::AddressMode::ClampToEdge;
			desc.magFilter = wgpu::FilterMode::Linear;
			desc.minFilter = wgpu::FilterMode::Linear;
			desc.compare = wgpu::CompareFunction::LessEqual;
			desc.maxAnisotropy = 1;
			desc.label = "renderer3d-shadow-sampler";
			gpu.ShadowSampler = device.CreateSampler(&desc);
		}
		gpu.ApplyTextureSampling(device, m_Settings.TextureQuality, m_Settings.AnisotropicFiltering);
		gpu.PointRepeatSampler = CreateRepeatSampler(device, wgpu::FilterMode::Nearest, "renderer3d-point-repeat");
		gpu.DummyShadowTexture = gpu.CreateShadowTexture(device, 1, "renderer3d-shadow-dummy");
		gpu.DummyShadowView = gpu.DummyShadowTexture ? gpu.DummyShadowTexture.CreateView() : nullptr;

		if (!gpu.MeshPipelineLayout || !gpu.SkyPipelineLayout || !gpu.ShadowPipelineLayout || !gpu.LinePipelineLayout
			|| !gpu.FrameUniformBuffer || !gpu.LightBuffer || !gpu.SkyUniformBuffer || !gpu.ShadowUniformBuffer || !gpu.LineUniformBuffer
			|| !gpu.ShadowSampler || !gpu.LinearRepeatSampler || !gpu.AnisotropicRepeatSampler || !gpu.PointRepeatSampler
			|| !gpu.DummyShadowView) {
			FNK_CORE_ERROR_TAG("Renderer3D", "Failed to create GPU layouts, buffers or samplers");
			m_Gpu.reset();
			return false;
		}
		gpu.SkyBindGroup = CreateUniformBindGroup(device, gpu.SkyLayout, gpu.SkyUniformBuffer, sizeof(SkyUniforms), "renderer3d-sky-bg");
		gpu.ShadowBindGroup = CreateUniformBindGroup(device, gpu.ShadowLayout, gpu.ShadowUniformBuffer, sizeof(glm::mat4), "renderer3d-shadow-bg");
		if (gpu.CustomShadowFrameLayout) {
			gpu.CustomShadowFrameGroup = CreateUniformBindGroup(device, gpu.CustomShadowFrameLayout, gpu.FrameUniformBuffer,
				sizeof(FrameUniforms), "renderer3d-custom-shadow-frame-bg");
		}
		gpu.LineBindGroup = CreateUniformBindGroup(device, gpu.LineLayout, gpu.LineUniformBuffer, sizeof(glm::mat4), "renderer3d-line-bg");

		m_ParticleShader = std::make_unique<Shader>(std::string("FunkeAssets/Shaders/Particle3DShader.vs"), std::string("FunkeAssets/Shaders/Particle3DShader.fs"));
		if (const auto particleLookup = lookup(m_ParticleShader); particleLookup.Valid) {
			const wgpu::BindGroupLayoutEntry particleEntry = UniformEntry(0, sizeof(ParticleFrameUniforms), wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);
			gpu.ParticleModule = particleLookup.Module;
			gpu.ParticleLayout = CreateLayout(device, &particleEntry, 1, "renderer3d-particle-bgl");
			const wgpu::BindGroupLayout particleLayouts[2] = { gpu.ParticleLayout, gpu.MaterialLayout };
			gpu.ParticlePipelineLayout = CreatePipelineLayout(device, particleLayouts, 2, "renderer3d-particle-layout");
			gpu.ParticleUniformBuffer = CreateUniformBuffer(device, sizeof(ParticleFrameUniforms), "renderer3d-particle-ubo");
			if (gpu.ParticleLayout && gpu.ParticleUniformBuffer) {
				gpu.ParticleBindGroup = CreateUniformBindGroup(device, gpu.ParticleLayout, gpu.ParticleUniformBuffer, sizeof(ParticleFrameUniforms), "renderer3d-particle-bg");
			}
		}
		if (!gpu.ParticleBindGroup || !gpu.ParticlePipelineLayout) {
			FNK_CORE_WARN_TAG("Renderer3D", "Particle shader or resources unavailable; ParticleSystem3D will not draw");
			gpu.ParticleModule = nullptr;
		}

		for (size_t i = 0; i < k_MeshTypeCount; ++i) {
			const PrimitiveMeshData data = BuildPrimitiveMesh(static_cast<PrimitiveMeshType>(i));
			auto& mesh = gpu.Meshes[i];
			mesh.Vertices = CreateBufferWithData(device, wgpu::BufferUsage::Vertex, data.Vertices.data(), data.Vertices.size() * sizeof(MeshVertex), "renderer3d-mesh-vb");
			mesh.Indices = CreateBufferWithData(device, wgpu::BufferUsage::Index, data.Indices.data(), data.Indices.size() * sizeof(uint32_t), "renderer3d-mesh-ib");
			mesh.IndexCount = static_cast<uint32_t>(data.Indices.size());
			mesh.VertexCount = static_cast<uint32_t>(data.Vertices.size());
			const std::vector<uint32_t> lines = BuildWireIndices(data.Indices);
			mesh.WireIndices = CreateBufferWithData(device, wgpu::BufferUsage::Index, lines.data(), lines.size() * sizeof(uint32_t), "renderer3d-mesh-wire-ib");
			mesh.WireIndexCount = mesh.WireIndices ? static_cast<uint32_t>(lines.size()) : 0;
			glm::vec3 boundsMin(std::numeric_limits<float>::max());
			glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
			for (const MeshVertex& vertex : data.Vertices) {
				const glm::vec3 position(vertex.Position[0], vertex.Position[1], vertex.Position[2]);
				boundsMin = glm::min(boundsMin, position);
				boundsMax = glm::max(boundsMax, position);
			}
			if (data.Vertices.empty()) boundsMin = boundsMax = glm::vec3(0.0f);
			gpu.MeshBoundsMin[i] = boundsMin;
			gpu.MeshBoundsMax[i] = boundsMax;
		}

		if (!gpu.CustomBindings.Initialize("renderer3d-custom")) {
			FNK_CORE_WARN_TAG("Renderer3D", "Custom shader resources unavailable; materials with a shader draw with the standard one");
		}

		GpuState* state = m_Gpu.get();
		gpu.TextureReferenceToken = TextureManager::AddReferenceProvider([state](const TextureManager::ReferenceEmitter& emit) {
			for (const auto& [guid, entry] : state->MaterialTextures) {
				if (TextureManager::IsValid(entry.Handle)) emit(entry.Handle);
			}
		});
		// Bind groups hold texture views; drop them before the view dies.
		gpu.TextureDestroyToken = TextureManager::AddDestroyListener([state](TextureHandle) {
			state->MaterialGroups.clear();
		});

		m_IsInitialized = true;
		return true;
	}

	void Renderer3D::Shutdown() {
		if (m_Gpu) {
			if (m_Gpu->TextureReferenceToken != 0) TextureManager::RemoveReferenceProvider(m_Gpu->TextureReferenceToken);
			if (m_Gpu->TextureDestroyToken != 0) TextureManager::RemoveDestroyListener(m_Gpu->TextureDestroyToken);
		}
		// Dawn objects must die before RenderApi::Shutdown.
		if (m_PostProcess) {
			m_PostProcess->Processor.Shutdown();
			m_PostProcess->SceneFbo.Destroy();
			m_PostProcess->AntiAliasFbo.Destroy();
			m_PostProcess->ReleaseMsaaTargets();
			m_PostProcess.reset();
		}
		m_Gpu.reset();
		m_MeshShader.reset();
		m_SkyShader.reset();
		m_ShadowShader.reset();
		m_LineShader.reset();
		m_ParticleShader.reset();
		m_Lines.clear();
		MaterialLibrary::Clear();
		CustomShaderLibrary::Clear();
		m_IsInitialized = false;
	}

	uint32_t Renderer3D::GetShadowMapResolution(ShadowQualityLevel quality) {
		switch (quality) {
			case ShadowQualityLevel::Low: return 1024;
			case ShadowQualityLevel::Medium: return 2048;
			case ShadowQualityLevel::High: return 4096;
			case ShadowQualityLevel::Off: break;
		}
		return 0;
	}

	Camera3DView Renderer3D::MakeView(const glm::mat4& cameraWorld, const Camera3DComponent& camera, float aspect) {
		Camera3DView view;
		view.View = Camera3DComponent::ViewFromWorld(cameraWorld);
		view.Projection = camera.GetProjectionMatrix(aspect);
		view.Position = glm::vec3(cameraWorld[3]);
		view.Environment = Camera3DEnvironment::FromCamera(camera);
		return view;
	}

	float Renderer3D::GetCurrentTargetAspect() {
		uint32_t x = 0, y = 0, width = 0, height = 0;
		if (WebGPUBackend::GetCachedViewport(x, y, width, height) && width > 0 && height > 0) {
			return static_cast<float>(width) / static_cast<float>(height);
		}
		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (target.Valid && target.Height > 0) {
			return static_cast<float>(target.Width) / static_cast<float>(target.Height);
		}
		return 1.0f;
	}

	bool Renderer3D::BeginPostProcess(const PostProcessing2DComponent* settings, const Color& clearColor) {
		if (!m_IsInitialized || !m_PostProcessingEnabled) return false;
		const bool effects = settings && m_Settings.PostProcessingEnabled && settings->HasEnabledEffect();
		const bool fxaa = m_Settings.AntiAliasing == AntiAliasingMode::FXAA;
		const bool msaa = GetMultisampleCount(m_Settings.AntiAliasing) > 1;
		if (!effects && !fxaa && !msaa) return false;
		if (!m_PostProcess) m_PostProcess = std::make_unique<PostProcessState>();
		PostProcessState& state = *m_PostProcess;
		if (state.Active) return false;
		const auto caller = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!caller.Valid || caller.Width == 0 || caller.Height == 0 || caller.SampleCount > 1) return false;
		const int width = static_cast<int>(caller.Width);
		const int height = static_cast<int>(caller.Height);

		// MSAA alone resolves straight into the caller: going through the RGBA16F intermediate would round pixel-art
		// texels differently than drawing without anti-aliasing.
		const std::optional<TextureFormat> callerFormat = ToFramebufferFormat(caller.ColorFormat);
		const bool intermediate = effects || fxaa || !callerFormat;
		if (intermediate) {
			if (!state.Processor.IsInitialized()) {
				if (state.InitFailed) return false;
				if (!state.Processor.Initialize()) {
					state.InitFailed = true;
					FNK_CORE_WARN_TAG("Renderer3D", "PostProcessor unavailable; 3D cameras render without post-processing");
					return false;
				}
			}
			if (!state.SceneFbo.Recreate(width, height, TextureFormat::RGBA16F)) return false;
		}
		Framebuffer* msaaTarget = msaa
			? state.AcquireMsaaTarget(width, height, intermediate ? TextureFormat::RGBA16F : *callerFormat) : nullptr;
		if (!intermediate && !msaaTarget) return false;

		state.Caller = WebGPUBackend::SaveBoundTarget();
		state.CallerTarget = caller;
		state.Settings = effects ? settings : nullptr;
		state.AntiAlias = fxaa;
		state.Msaa = msaaTarget;
		state.Intermediate = intermediate;
		state.Active = true;
		RenderApi::BindFramebuffer(msaaTarget ? *msaaTarget : state.SceneFbo);
		// A direct resolve keeps the caller's viewport (letterbox) so the image lands where it would without MSAA.
		if (intermediate) RenderApi::SetViewport(0, 0, width, height);
		const Color savedClear = RenderApi::GetClearColor();
		RenderApi::SetClearColor(clearColor);
		RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
		RenderApi::SetClearColor(savedClear);
		m_OwnDepthNeedsClear = true;
		return true;
	}

	void Renderer3D::EndPostProcess() {
		if (!m_PostProcess || !m_PostProcess->Active) return;
		PostProcessState& state = *m_PostProcess;
		state.Active = false;
		const WebGPUBackend::CurrentTargetInfo target = state.CallerTarget;
		if (state.Msaa) {
			const wgpu::TextureView destination = state.Intermediate
				? WebGPUBackend::LookupFramebufferByFboId(state.SceneFbo.GetBackendId()).ColorView : target.ColorView;
			ResolveMultisampled(*state.Msaa, destination);
			state.Msaa = nullptr;
		}
		// Flushes the 3D passes so the effects read the finished intermediate.
		WebGPUBackend::FlushCommands();
		if (state.Intermediate) {
			// FXAA first, on the tone-mapped scene: straight into the target without effects, else into an intermediate.
			const Framebuffer* sceneSource = &state.SceneFbo;
			bool antiAliasedToTarget = false;
			if (state.AntiAlias) {
				const AntiAliasingQualityLevel aaQuality = m_Settings.AntiAliasingQuality;
				if (!state.Settings) {
					antiAliasedToTarget = state.Processor.RunAntiAliasing(state.SceneFbo, target.ColorView, target.ColorFormat,
						aaQuality, true);
				}
				else if (state.AntiAliasFbo.Recreate(state.SceneFbo.GetWidth(), state.SceneFbo.GetHeight(), TextureFormat::RGBA16F)) {
					const auto aaLook = WebGPUBackend::LookupFramebufferByFboId(state.AntiAliasFbo.GetBackendId());
					if (aaLook.Valid && state.Processor.RunAntiAliasing(state.SceneFbo, aaLook.ColorView, aaLook.ColorFormat,
						aaQuality, false)) {
						sceneSource = &state.AntiAliasFbo;
					}
				}
			}
			if (!antiAliasedToTarget) {
				state.Processor.Run(*sceneSource, target.ColorView, target.ColorFormat, target.Width, target.Height,
					state.Settings, m_Settings.PostProcessingQuality, true);
			}
		}
		WebGPUBackend::RestoreBoundTarget(state.Caller);
		state.Settings = nullptr;
		state.CallerTarget = {};
		// The caller's depth no longer matches what was drawn; meshes drawn after this clear their own depth again.
		m_OwnDepthNeedsClear = true;
		if (target.IsSwapChain) WebGPUBackend::MarkSwapChainRendered();
	}

	bool Renderer3D::BeginFrame() {
		m_RenderedThisFrame = false;
		m_DrawCalls = 0;
		m_RenderedMeshes = 0;
		m_LightCount = 0;
		m_RenderedParticles = 0;
		if (!m_IsInitialized || m_SkipBeginFrameRender || !m_SceneProvider) return false;

		Scene* cameraScene = nullptr;
		EntityHandle cameraEntity = entt::null;
		m_SceneProvider([&](Scene& scene) {
			if (cameraScene) return;
			Transform3DHierarchySystem::Propagate(scene);
			const EntityHandle found = Camera3DComponent::FindMain(scene);
			if (found != entt::null) {
				cameraScene = &scene;
				cameraEntity = found;
			}
		});
		if (!cameraScene) return false;

		const auto& registry = cameraScene->GetRegistry();
		const auto& camera = registry.get<Camera3DComponent>(cameraEntity);
		const auto& transform = registry.get<Transform3DComponent>(cameraEntity);

		RenderApi::BindDefaultFramebuffer();
		RenderApi::SetClearColor(camera.ClearColor);
		RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
		m_OwnDepthNeedsClear = true;

		const Camera3DView view = MakeView(transform.WorldMatrix, camera, GetCurrentTargetAspect());
		const auto* postProcessing = registry.try_get<PostProcessing2DComponent>(cameraEntity);
		const bool postProcess = BeginPostProcess(postProcessing, camera.ClearColor);
		RenderSky(view, cameraScene);
		m_SceneProvider([&](Scene& scene) {
			RenderScene(scene, view);
		});
		if (postProcess) EndPostProcess();
		// Runtime debug lines (physics overlay); the editor flushes its queue into the Editor View itself.
		if (!Application::IsEditor()) FlushLines(view, true);
		m_RenderedThisFrame = true;
		return true;
	}

	void Renderer3D::RenderSky(const Camera3DView& view, Scene* lightScene) {
		if (!m_IsInitialized || !m_Gpu || view.Environment.Background != CameraBackground::Skybox) return;
		FUNKE_PROFILE_SCOPE("Renderer3D.Sky");
		GpuState& gpu = *m_Gpu;

		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!target.Valid) return;
		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::Queue queue = WebGPUBackend::GetQueue();
		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!device || !queue || !encoder) return;
		const Camera3DEnvironment& environment = view.Environment;

		// A Skybox material replaces the gradient; one that cannot draw falls back to the magenta sky, then the gradient.
		wgpu::RenderPipeline pipeline;
		wgpu::BindGroup customGroup;
		if (environment.SkyboxMaterial != 0) {
			const Material* material = MaterialLibrary::Get(environment.SkyboxMaterial);
			const CustomShaderProgram* program = material && material->Shader != 0
				? CustomShaderGpu::GetProgram(material->Shader, ShaderTarget::Skybox) : nullptr;
			for (int attempt = 0; attempt < 2 && program && gpu.CustomBindings.IsInitialized(); ++attempt) {
				pipeline = gpu.GetCustomSkyPipeline(target.ColorFormat, target.SampleCount, *program);
				customGroup = pipeline ? gpu.CustomBindings.GetGroup(environment.SkyboxMaterial, material, *program) : nullptr;
				if (pipeline && customGroup) break;
				pipeline = nullptr;
				program = program->IsFallback ? nullptr : CustomShaderGpu::GetFallback(ShaderTarget::Skybox);
			}
			if (pipeline) {
				gpu.CustomBindings.WriteGlobals(CustomMaterialBindings::MakeGlobals(view.View, view.Projection, view.Position,
					target.Width, target.Height));
			}
		}
		if (!pipeline) {
			customGroup = nullptr;
			pipeline = gpu.GetSkyPipeline(device, target.ColorFormat, target.SampleCount);
		}
		if (!pipeline) return;

		SkyUniforms sky{};
		sky.InverseViewProjection = glm::inverse(view.Projection * view.View);
		sky.Top = ToVec4(environment.SkyTop);
		sky.Horizon = ToVec4(environment.SkyHorizon);
		sky.Ground = ToVec4(environment.SkyGround);
		glm::vec3 lightDirection(0.0f, -1.0f, 0.0f);
		Color lightColor(1.0f, 1.0f, 1.0f, 1.0f);
		bool hasSun = false;
		if (lightScene && environment.SunSize > 0.0f) {
			Transform3DHierarchySystem::Propagate(*lightScene);
			hasSun = FindMainDirectionalLight(lightScene->GetRegistry(), lightDirection, lightColor);
		}
		const float sunRadius = glm::radians(std::clamp(environment.SunSize, 0.0f, 45.0f));
		sky.SunDirection = glm::vec4(-lightDirection, std::cos(sunRadius));
		// The disc reads as a light source, so it is pulled towards white.
		const glm::vec3 sunColor = glm::mix(glm::vec3(lightColor.r, lightColor.g, lightColor.b), glm::vec3(1.0f), 0.6f);
		sky.SunColor = glm::vec4(sunColor, hasSun ? 1.0f : 0.0f);
		sky.Params = glm::vec4(IsSrgbFormat(target.ColorFormat) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		queue.WriteBuffer(gpu.SkyUniformBuffer, 0, &sky, sizeof(sky));

		wgpu::RenderPassColorAttachment colorAttachment{};
		colorAttachment.view = target.ColorView;
		colorAttachment.loadOp = wgpu::LoadOp::Load;
		colorAttachment.storeOp = wgpu::StoreOp::Store;
		colorAttachment.depthSlice = wgpu::kDepthSliceUndefined;

		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "renderer3d-sky";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &colorAttachment;

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, gpu.SkyBindGroup);
		if (customGroup) pass.SetBindGroup(k_CustomShaderGroup, customGroup);
		pass.Draw(3);
		pass.End();
		++m_DrawCalls;

		WebGPUBackend::FlushCommands();
		if (target.IsSwapChain) {
			WebGPUBackend::MarkSwapChainRendered();
		}
	}

	void Renderer3D::RenderScene(Scene& scene, const Camera3DView& view) {
		if (!m_IsInitialized || !m_Gpu) return;
		FUNKE_PROFILE_SCOPE("Renderer3D.RenderScene");
		Transform3DHierarchySystem::Propagate(scene);
		RenderMeshes(scene, view);
		// Particle billboards would cover the edges.
		if (RenderApi::GetPolygonMode() != PolygonMode::Wireframe) RenderParticles(scene, view);
	}

	void Renderer3D::RenderMeshes(Scene& scene, const Camera3DView& view) {
		GpuState& gpu = *m_Gpu;
		const entt::registry& registry = scene.GetRegistry();
		wgpu::Device device = WebGPUBackend::GetDevice();
		if (!device) return;
		// Editor Triangle/Mixed draw modes: black edges instead of shaded surfaces, depth tested against what is already drawn.
		const bool wireframe = RenderApi::GetPolygonMode() == PolygonMode::Wireframe;

		gpu.Instances.clear();
		gpu.DrawItems.clear();
		gpu.FrameTextures.clear();
		gpu.FrameTextureSlots.clear();
		gpu.FrameMeshes.clear();
		gpu.FrameMeshSlots.clear();
		gpu.FrameCustoms.clear();
		gpu.FrameCustomSlots.clear();
		gpu.FrameMaterials.clear();
		std::erase_if(gpu.CustomMeshes, [](const auto& pair) { return IsRuntimeMeshGuid(pair.first) && !MeshLibrary::IsLiveRuntime(pair.first); });
		// Posed copies of entities that stopped drawing (destroyed, disabled, scene unloaded).
		std::erase_if(gpu.SkinnedMeshes, [now = std::chrono::steady_clock::now()](const auto& pair) {
			return now - pair.second.LastUsed > std::chrono::seconds(2);
		});
		for (const auto& primitive : gpu.Meshes) gpu.FrameMeshes.push_back(&primitive);
		bool anyCaster = false;
		const TextureHandle whiteTexture = TextureManager::GetDefaultTexture(DefaultTexture::Pixel);
		auto textureSlot = [&](TextureHandle handle) {
			if (!TextureManager::IsValid(handle)) handle = whiteTexture;
			const uint64_t key = (static_cast<uint64_t>(handle.index) << 32) | handle.generation;
			auto [it, inserted] = gpu.FrameTextureSlots.emplace(key, static_cast<uint32_t>(gpu.FrameTextures.size()));
			if (inserted) gpu.FrameTextures.push_back(handle);
			return it->second;
		};

		// Frame slot of a mesh asset's shared GPU copy; nullopt when it cannot draw.
		auto customMeshSlot = [&](uint64_t guid, const MeshAsset& asset) -> std::optional<size_t> {
			auto slot = gpu.FrameMeshSlots.find(guid);
			if (slot == gpu.FrameMeshSlots.end()) {
				const auto* buffers = gpu.ResolveCustomMesh(device, guid, asset, wireframe);
				if (!buffers) return std::nullopt;
				slot = gpu.FrameMeshSlots.emplace(guid, static_cast<uint32_t>(gpu.FrameMeshes.size())).first;
				gpu.FrameMeshes.push_back(buffers);
			}
			return slot->second;
		};

		static const Material k_FallbackMaterials[2] = {
			[] { Material m; m.Lit = false; return m; }(),
			[] { Material m; m.Lit = true; return m; }(),
		};

		auto submit = [&](size_t meshIndex, const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& world,
			uint64_t materialGuid, const Color& color, bool lit, bool castShadows, bool cullable) {
			auto [memo, firstUse] = gpu.FrameMaterials.try_emplace(materialGuid);
			if (firstUse) {
				memo->second.Value = MaterialLibrary::Get(materialGuid);
				const Material* resolved = memo->second.Value ? memo->second.Value : &k_FallbackMaterials[0];
				// A user shader replaces the standard one; its color input is the renderer's Color alone.
				memo->second.Program = resolved->Shader != 0 && gpu.CustomBindings.IsInitialized()
					? CustomShaderGpu::GetProgram(resolved->Shader, ShaderTarget::Mesh3D) : nullptr;
			}
			const Material* material = memo->second.Value ? memo->second.Value : &k_FallbackMaterials[lit ? 1 : 0];
			const CustomShaderProgram* program = memo->second.Program;

			uint32_t custom = 0;
			if (program) {
				auto [it, inserted] = gpu.FrameCustomSlots.emplace(materialGuid, static_cast<uint32_t>(gpu.FrameCustoms.size()));
				if (inserted) gpu.FrameCustoms.push_back({ materialGuid, material, program, nullptr, nullptr });
				custom = it->second + 1;
			}

			MeshInstance instance{};
			for (int i = 0; i < 4; ++i) instance.Model[i] = world[i];
			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
			for (int i = 0; i < 3; ++i) instance.Normal[i] = glm::vec4(normalMatrix[i], 0.0f);
			const bool transparent = program ? program->Description.Blend != ShaderBlend::Opaque : material->Surface == MaterialSurface::Transparent;
			if (program) {
				instance.Color = ToVec4(color);
				instance.Emissive = glm::vec4(0.0f, 0.0f, 0.0f, material->Fog ? 1.0f : 0.0f);
				instance.UvTransform = { 1.0f, 1.0f, 0.0f, 0.0f };
			}
			else {
				instance.Color = ToVec4(material->BaseColor) * ToVec4(color);
				// w = 1 lets the camera's fog reach this mesh.
				instance.Emissive = glm::vec4(Lighting3D::SrgbToLinear(material->EmissiveColor) * std::max(material->EmissiveIntensity, 0.0f),
					material->Fog ? 1.0f : 0.0f);
				instance.Surface = { material->Metallic, material->Roughness, material->Lit ? 1.0f : 0.0f, transparent ? 1.0f : 0.0f };
				instance.UvTransform = { material->Tiling.x, material->Tiling.y, material->Offset.x, material->Offset.y };
			}

			const uint32_t slot = program ? 0u : textureSlot(gpu.ResolveMaterialTexture(material->BaseColorTexture));

			// Arvo: the world AABB of a transformed local box.
			glm::vec3 localExtent = (localMax - localMin) * 0.5f;
			if (program && program->Description.HasVertexFunction) {
				cullable &= program->Description.Displacement > 0.0f;
				localExtent += glm::vec3(program->Description.Displacement);
			}
			const glm::vec3 worldCenter = glm::vec3(world * glm::vec4((localMin + localMax) * 0.5f, 1.0f));
			const glm::mat3 basis(world);
			const glm::vec3 worldExtent = glm::abs(basis[0]) * localExtent.x + glm::abs(basis[1]) * localExtent.y
				+ glm::abs(basis[2]) * localExtent.z;

			const glm::vec3 toCamera = worldCenter - view.Position;
			DrawItem item{ MakeDrawKey(meshIndex, castShadows, slot), static_cast<uint32_t>(gpu.Instances.size()),
				glm::dot(toCamera, toCamera), transparent, custom };
			item.BoundsCenter = worldCenter;
			item.BoundsExtent = worldExtent;
			item.Cullable = cullable;
			gpu.DrawItems.push_back(item);
			gpu.Instances.push_back(instance);
			anyCaster |= castShadows;
		};

		const auto meshes = registry.view<const MeshRendererComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : meshes) {
			const auto& renderer = meshes.get<const MeshRendererComponent>(entity);
			const auto& transform = meshes.get<const Transform3DComponent>(entity);
			size_t meshIndex = static_cast<size_t>(renderer.Mesh);
			glm::vec3 localMin(0.0f);
			glm::vec3 localMax(0.0f);
			if (renderer.CustomMesh != 0) {
				const MeshAsset* asset = MeshLibrary::Get(renderer.CustomMesh);
				const std::optional<size_t> slot = asset ? customMeshSlot(renderer.CustomMesh, *asset) : std::nullopt;
				if (!slot) continue;
				meshIndex = *slot;
				localMin = asset->BoundsMin;
				localMax = asset->BoundsMax;
			}
			else if (meshIndex >= k_MeshTypeCount) {
				continue;
			}
			else {
				localMin = gpu.MeshBoundsMin[meshIndex];
				localMax = gpu.MeshBoundsMax[meshIndex];
			}
			submit(meshIndex, localMin, localMax, transform.WorldMatrix, renderer.Material, renderer.Color, renderer.Lit,
				renderer.CastShadows, true);
		}

		const auto now = std::chrono::steady_clock::now();
		const auto skinned = registry.view<const SkinnedMeshRendererComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : skinned) {
			const auto& renderer = skinned.get<const SkinnedMeshRendererComponent>(entity);
			const MeshAsset* asset = MeshLibrary::Get(renderer.Model);
			const std::optional<size_t> shared = asset ? customMeshSlot(renderer.Model, *asset) : std::nullopt;
			if (!shared) continue;
			size_t meshIndex = *shared;
			if (asset->Skin) {
				const GpuState::SkinnedKey key{ &scene, static_cast<uint32_t>(entity) };
				const auto* buffers = gpu.ResolveSkinnedMesh(device, key, renderer, *asset, *gpu.FrameMeshes[*shared], now);
				if (!buffers) continue;
				meshIndex = gpu.FrameMeshes.size();
				gpu.FrameMeshes.push_back(buffers);
			}
			// Bind-pose bounds don't hold once animated, so skinned meshes always draw.
			submit(meshIndex, asset->BoundsMin, asset->BoundsMax, skinned.get<const Transform3DComponent>(entity).WorldMatrix,
				renderer.Material, renderer.Color, renderer.Lit, renderer.CastShadows, false);
		}
		if (gpu.Instances.empty()) return;

		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!target.Valid) return;

		wgpu::Queue queue = WebGPUBackend::GetQueue();
		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!queue || !encoder) return;

		const SceneLighting lighting = CollectLights(registry, view.Position, m_Settings.MaxLights);
		m_LightCount = std::max(m_LightCount, static_cast<uint32_t>(lighting.Selected.size()));
		gpu.LightUpload = {};
		for (size_t i = 0; i < lighting.Selected.size(); ++i) {
			const Lighting3D::LightCandidate& light = lighting.Selected[i];
			GpuLight& out = gpu.LightUpload[i];
			out.PositionRange = glm::vec4(light.Position, light.Range);
			out.DirectionType = glm::vec4(light.Direction, static_cast<float>(light.Type));
			out.Radiance = glm::vec4(light.Radiance, 0.0f);
			out.Spot = glm::vec4(light.CosInner, light.CosOuter, 0.0f, 0.0f);
		}

		const Camera3DEnvironment& environment = view.Environment;
		const bool skyAmbient = environment.Background == CameraBackground::Skybox && environment.SkyAmbient > 0.0f;
		FrameUniforms frame{};
		frame.ViewProjection = view.Projection * view.View;
		frame.CameraPosition = glm::vec4(view.Position, m_Settings.Exposure);
		frame.Ambient = glm::vec4(lighting.FlatAmbient, skyAmbient ? environment.SkyAmbient : 0.0f);
		frame.SkyTop = glm::vec4(Lighting3D::SrgbToLinear(environment.SkyTop), 1.0f);
		frame.SkyHorizon = glm::vec4(Lighting3D::SrgbToLinear(environment.SkyHorizon), 1.0f);
		frame.SkyGround = glm::vec4(Lighting3D::SrgbToLinear(environment.SkyGround), 1.0f);
		frame.Params = glm::vec4(static_cast<float>(lighting.Selected.size()), static_cast<float>(m_Settings.ToneMapping),
			IsSrgbFormat(target.ColorFormat) ? 0.0f : 1.0f, -1.0f);

		// Shadow pass for the main directional light.
		const uint32_t shadowResolution = std::min(GetShadowMapResolution(m_Settings.ShadowQuality), WebGPUBackend::GetMaxTextureDimension2D());
		wgpu::TextureView shadowView = gpu.DummyShadowView;
		if (!wireframe && shadowResolution > 0 && lighting.ShadowedLight >= 0 && anyCaster) {
			wgpu::TextureView map = gpu.EnsureShadowMap(device, shadowResolution);
			wgpu::RenderPipeline shadowPipeline = gpu.GetShadowPipeline(device);
			if (map && shadowPipeline) {
				float texelWorld = 0.0f;
				float depthRange = 1.0f;
				const glm::mat4 lightViewProjection = Lighting3D::ComputeDirectionalShadowMatrix(view.View, view.Projection,
					lighting.Selected[static_cast<size_t>(lighting.ShadowedLight)].Direction, m_Settings.ShadowDistance,
					shadowResolution, &texelWorld, &depthRange);
				frame.LightViewProjection = lightViewProjection;
				frame.Params.w = static_cast<float>(lighting.ShadowedLight);
				frame.ShadowParams = glm::vec4(1.0f / static_cast<float>(shadowResolution), texelWorld / std::max(depthRange, 1e-4f),
					texelWorld * 1.5f, m_Settings.ShadowDistance);
				queue.WriteBuffer(gpu.ShadowUniformBuffer, 0, glm::value_ptr(lightViewProjection), sizeof(glm::mat4));
				shadowView = map;
			}
		}
		else if (shadowResolution == 0 && gpu.ShadowView) {
			gpu.ReleaseShadowMap();
		}

		{
			FUNKE_PROFILE_SCOPE("Renderer3D.Cull");
			const FrustumPlanes cameraFrustum = ExtractFrustum(frame.ViewProjection);
			const bool shadowPass = frame.Params.w >= 0.0f;
			const FrustumPlanes shadowFrustum = ExtractFrustum(frame.LightViewProjection);
			std::erase_if(gpu.DrawItems, [&](DrawItem& item) {
				if (!item.Cullable) return false;
				const bool visible = IntersectsFrustum(cameraFrustum, item.BoundsCenter, item.BoundsExtent);
				bool casts = KeyCastsShadow(item.Key);
				if (casts && (!shadowPass || !IntersectsFrustum(shadowFrustum, item.BoundsCenter, item.BoundsExtent))) {
					casts = false;
					item.Key |= uint64_t(1) << 32;
				}
				if (!visible) item.Key |= k_KeyHiddenBit;
				return !visible && !casts;
			});
		}
		if (gpu.DrawItems.empty()) return;

		// Opaque items batch by key; transparent ones follow, farthest first, so blending composites correctly.
		const auto firstTransparent = std::partition(gpu.DrawItems.begin(), gpu.DrawItems.end(),
			[](const DrawItem& item) { return !item.Transparent; });
		const size_t transparentBegin = static_cast<size_t>(firstTransparent - gpu.DrawItems.begin());
		std::sort(gpu.DrawItems.begin(), firstTransparent, [](const DrawItem& a, const DrawItem& b) {
			if (a.Custom != b.Custom) return a.Custom < b.Custom;
			return a.Key < b.Key;
		});
		std::sort(firstTransparent, gpu.DrawItems.end(), [](const DrawItem& a, const DrawItem& b) {
			if (a.ViewDistanceSq != b.ViewDistanceSq) return a.ViewDistanceSq > b.ViewDistanceSq;
			if (a.Custom != b.Custom) return a.Custom < b.Custom;
			return a.Key < b.Key;
		});

		wgpu::TextureView depthView = target.HasDepth ? target.DepthView : gpu.EnsureOwnDepth(device, target.Width, target.Height);
		wgpu::RenderPipeline pipeline = wireframe
			? gpu.GetWirePipeline(device, target.ColorFormat, target.SampleCount)
			: gpu.GetMeshPipeline(device, target.ColorFormat, target.SampleCount, false);
		wgpu::RenderPipeline transparentPipeline = !wireframe && transparentBegin < gpu.DrawItems.size()
			? gpu.GetMeshPipeline(device, target.ColorFormat, target.SampleCount, true) : nullptr;
		if (!depthView || !pipeline) return;
		if (!wireframe && !gpu.FrameCustoms.empty()) {
			gpu.ResolveFrameCustoms(target.ColorFormat, target.SampleCount);
			gpu.CustomBindings.WriteGlobals(CustomMaterialBindings::MakeGlobals(view.View, view.Projection, view.Position, target.Width, target.Height));
		}
		gpu.InstanceUpload.clear();
		gpu.InstanceUpload.reserve(gpu.DrawItems.size());
		for (const DrawItem& item : gpu.DrawItems) gpu.InstanceUpload.push_back(gpu.Instances[item.Instance]);

		const uint64_t instanceBytes = gpu.InstanceUpload.size() * sizeof(MeshInstance);
		if (!EnsureBuffer(device, gpu.InstanceBuffer, gpu.InstanceCapacity, instanceBytes, wgpu::BufferUsage::Vertex, "renderer3d-instances")) {
			FNK_CORE_WARN_TAG("Renderer3D", "Instance buffer too large ({} bytes); meshes skipped", instanceBytes);
			return;
		}

		if (environment.FogEnabled) {
			frame.FogColor = glm::vec4(Lighting3D::SrgbToLinear(environment.FogColor), 1.0f);
			frame.FogParams = glm::vec4(environment.FogStart, std::max(environment.FogEnd, environment.FogStart + 0.01f), 0.0f, 0.0f);
		}
		queue.WriteBuffer(gpu.FrameUniformBuffer, 0, &frame, sizeof(frame));
		queue.WriteBuffer(gpu.LightBuffer, 0, gpu.LightUpload.data(), sizeof(LightUniforms));
		queue.WriteBuffer(gpu.InstanceBuffer, 0, gpu.InstanceUpload.data(), instanceBytes);

		if (frame.Params.w >= 0.0f) {
			FUNKE_PROFILE_SCOPE("Renderer3D.Shadows");
			wgpu::RenderPassDepthStencilAttachment shadowDepth{};
			shadowDepth.view = shadowView;
			shadowDepth.depthLoadOp = wgpu::LoadOp::Clear;
			shadowDepth.depthClearValue = 1.0f;
			shadowDepth.depthStoreOp = wgpu::StoreOp::Store;

			wgpu::RenderPassDescriptor shadowPassDesc{};
			shadowPassDesc.label = "renderer3d-shadows";
			shadowPassDesc.depthStencilAttachment = &shadowDepth;

			// Full map, not the cached viewport: the shadow map has its own resolution.
			wgpu::RenderPassEncoder shadowPass = encoder.BeginRenderPass(&shadowPassDesc);
			wgpu::RenderPipeline standardShadow = gpu.GetShadowPipeline(device);
			wgpu::RenderPipeline boundShadow;
			shadowPass.SetVertexBuffer(1, gpu.InstanceBuffer);
			// Custom slot whose fn vertex moves the shadow, or 0 for the standard shadow pipeline.
			auto shadowSlot = [&](const DrawItem& item) -> uint32_t {
				return item.Custom != 0 && gpu.FrameCustoms[item.Custom - 1].ShadowPipeline && gpu.CustomShadowFrameGroup
					? item.Custom : 0u;
			};
			for (size_t begin = 0; begin < gpu.DrawItems.size();) {
				const size_t mesh = KeyMesh(gpu.DrawItems[begin].Key);
				const bool casts = KeyCastsShadow(gpu.DrawItems[begin].Key);
				const uint32_t slot = shadowSlot(gpu.DrawItems[begin]);
				size_t end = begin + 1;
				while (end < gpu.DrawItems.size() && KeyMesh(gpu.DrawItems[end].Key) == mesh
					&& KeyCastsShadow(gpu.DrawItems[end].Key) == casts && shadowSlot(gpu.DrawItems[end]) == slot) {
					++end;
				}
				const auto& buffers = *gpu.FrameMeshes[mesh];
				if (casts && buffers.Vertices && buffers.Indices) {
					const wgpu::RenderPipeline runPipeline = slot != 0 ? gpu.FrameCustoms[slot - 1].ShadowPipeline : standardShadow;
					if (runPipeline.Get() != boundShadow.Get()) {
						shadowPass.SetPipeline(runPipeline);
						boundShadow = runPipeline;
						shadowPass.SetBindGroup(0, slot != 0 ? gpu.CustomShadowFrameGroup : gpu.ShadowBindGroup);
					}
					if (slot != 0) shadowPass.SetBindGroup(1, gpu.FrameCustoms[slot - 1].Group);
					shadowPass.SetVertexBuffer(0, buffers.Vertices);
					shadowPass.SetIndexBuffer(buffers.Indices, wgpu::IndexFormat::Uint32);
					shadowPass.DrawIndexed(buffers.IndexCount, static_cast<uint32_t>(end - begin), 0, 0, static_cast<uint32_t>(begin));
					++m_DrawCalls;
					m_GeometryTotals.AddShadowMesh(buffers.IndexCount, static_cast<uint32_t>(end - begin));
				}
				begin = end;
			}
			shadowPass.End();
		}

		wgpu::BindGroup frameGroup = gpu.GetFrameBindGroup(device, shadowView);
		if (!frameGroup) return;

		const bool clearOwnDepth = !target.HasDepth && m_OwnDepthNeedsClear;
		wgpu::RenderPassColorAttachment colorAttachment{};
		colorAttachment.view = target.ColorView;
		colorAttachment.loadOp = wgpu::LoadOp::Load;
		colorAttachment.storeOp = wgpu::StoreOp::Store;
		colorAttachment.depthSlice = wgpu::kDepthSliceUndefined;

		wgpu::RenderPassDepthStencilAttachment depthAttachment{};
		depthAttachment.view = depthView;
		depthAttachment.depthLoadOp = clearOwnDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		depthAttachment.depthClearValue = 1.0f;
		depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
		depthAttachment.stencilLoadOp = clearOwnDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		depthAttachment.stencilStoreOp = wgpu::StoreOp::Store;

		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "renderer3d-meshes";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &colorAttachment;
		passDesc.depthStencilAttachment = &depthAttachment;

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetBindGroup(0, frameGroup);
		pass.SetVertexBuffer(1, gpu.InstanceBuffer);
		size_t boundMesh = std::numeric_limits<size_t>::max();
		wgpu::RenderPipeline boundPipeline;
		uint32_t drawnInstances = 0;
		if (wireframe) {
			// One pipeline and no material group, so batches only split per mesh.
			pass.SetPipeline(pipeline);
			for (size_t begin = 0; begin < gpu.DrawItems.size();) {
				const size_t mesh = KeyMesh(gpu.DrawItems[begin].Key);
				const bool hidden = KeyHidden(gpu.DrawItems[begin].Key);
				size_t end = begin + 1;
				while (end < gpu.DrawItems.size() && KeyMesh(gpu.DrawItems[end].Key) == mesh && KeyHidden(gpu.DrawItems[end].Key) == hidden) {
					++end;
				}
				const auto& buffers = *gpu.FrameMeshes[mesh];
				if (!hidden && buffers.Vertices && buffers.WireIndices && buffers.WireIndexCount > 0) {
					pass.SetVertexBuffer(0, buffers.Vertices);
					pass.SetIndexBuffer(buffers.WireIndices, wgpu::IndexFormat::Uint32);
					pass.DrawIndexed(buffers.WireIndexCount, static_cast<uint32_t>(end - begin), 0, 0, static_cast<uint32_t>(begin));
					drawnInstances += static_cast<uint32_t>(end - begin);
					++m_DrawCalls;
				}
				begin = end;
			}
		}
		else {
			for (size_t begin = 0; begin < gpu.DrawItems.size();) {
				const uint64_t key = gpu.DrawItems[begin].Key;
				const uint32_t custom = gpu.DrawItems[begin].Custom;
				const size_t mesh = KeyMesh(key);
				const uint32_t slot = KeyTextureSlot(key);
				// Batches never straddle the opaque/transparent boundary: the pipeline switches there.
				const size_t rangeEnd = begin < transparentBegin ? transparentBegin : gpu.DrawItems.size();
				size_t end = begin + 1;
				const bool hidden = KeyHidden(key);
				while (end < rangeEnd && gpu.DrawItems[end].Custom == custom && KeyMesh(gpu.DrawItems[end].Key) == mesh
					&& KeyTextureSlot(gpu.DrawItems[end].Key) == slot && KeyHidden(gpu.DrawItems[end].Key) == hidden) {
					++end;
				}
				if (hidden) {
					begin = end;
					continue;
				}
				drawnInstances += static_cast<uint32_t>(end - begin);
				wgpu::RenderPipeline itemPipeline = begin < transparentBegin ? pipeline : transparentPipeline;
				wgpu::BindGroup materialGroup;
				if (custom != 0) {
					const FrameCustom& frameCustom = gpu.FrameCustoms[custom - 1];
					itemPipeline = frameCustom.Pipeline;
					materialGroup = frameCustom.Group;
				}
				else {
					materialGroup = gpu.GetMaterialBindGroup(device, gpu.FrameTextures[slot]);
					if (!materialGroup) materialGroup = gpu.GetMaterialBindGroup(device, whiteTexture);
				}
				const auto& buffers = *gpu.FrameMeshes[mesh];
				if (itemPipeline && materialGroup && buffers.Vertices && buffers.Indices) {
					if (itemPipeline.Get() != boundPipeline.Get()) {
						pass.SetPipeline(itemPipeline);
						boundPipeline = itemPipeline;
					}
					if (boundMesh != mesh) {
						pass.SetVertexBuffer(0, buffers.Vertices);
						pass.SetIndexBuffer(buffers.Indices, wgpu::IndexFormat::Uint32);
						boundMesh = mesh;
					}
					pass.SetBindGroup(1, materialGroup);
					pass.DrawIndexed(buffers.IndexCount, static_cast<uint32_t>(end - begin), 0, 0, static_cast<uint32_t>(begin));
					++m_DrawCalls;
					m_GeometryTotals.AddMesh(buffers.IndexCount, buffers.VertexCount, static_cast<uint32_t>(end - begin));
				}
				begin = end;
			}
		}
		pass.End();
		m_RenderedMeshes += drawnInstances;
		if (!target.HasDepth) m_OwnDepthNeedsClear = false;

		// The next scene's WriteBuffer would otherwise overwrite this pass's uniforms (Dawn batches writes per submit).
		WebGPUBackend::FlushCommands();
		if (target.IsSwapChain) {
			WebGPUBackend::MarkSwapChainRendered();
		}
	}

	void Renderer3D::RenderParticles(Scene& scene, const Camera3DView& view) {
		GpuState& gpu = *m_Gpu;
		if (!gpu.ParticleModule) return;
		const entt::registry& registry = scene.GetRegistry();
		const auto systems = registry.view<const ParticleSystem3DComponent>(entt::exclude<DisabledTag>);
		if (systems.begin() == systems.end()) return;
		FUNKE_PROFILE_SCOPE("Renderer3D.Particles");

		gpu.ParticleInstances.clear();
		gpu.ParticleBatches.clear();
		for (EntityHandle entity : systems) {
			const auto& system = systems.get<const ParticleSystem3DComponent>(entity);
			const auto particles = system.GetParticles();
			if (particles.empty()) continue;

			const auto* transform = registry.try_get<Transform3DComponent>(entity);
			const glm::mat4 emitterWorld = transform ? transform->WorldMatrix : glm::mat4(1.0f);
			const bool local = system.SimulationSpace == ParticleSpace3D::Local;
			const TextureHandle texture = system.Texture != 0 ? gpu.ResolveMaterialTexture(system.Texture) : TextureHandle{};
			const float softDot = TextureManager::IsValid(texture) ? 0.0f : 1.0f;

			// Alpha blending needs back-to-front order inside the system; additive output is order independent.
			gpu.ParticleOrder.clear();
			for (uint32_t i = 0; i < particles.size(); ++i) {
				const glm::vec3 position = local ? glm::vec3(emitterWorld * glm::vec4(particles[i].Position, 1.0f)) : particles[i].Position;
				const glm::vec3 toCamera = position - view.Position;
				gpu.ParticleOrder.emplace_back(glm::dot(toCamera, toCamera), i);
			}
			if (system.BlendMode == ParticleBlendMode3D::Alpha) {
				std::sort(gpu.ParticleOrder.begin(), gpu.ParticleOrder.end(),
					[](const auto& a, const auto& b) { return a.first > b.first; });
			}

			ParticleBatch batch{ static_cast<uint32_t>(gpu.ParticleInstances.size()), 0, texture, system.BlendMode, 0.0f };
			for (const auto& [distanceSq, index] : gpu.ParticleOrder) {
				const ParticleSystem3DComponent::Particle& particle = particles[index];
				if (particle.Size <= 0.0f || particle.Color.a <= 0.0f) continue;
				const glm::vec3 position = local ? glm::vec3(emitterWorld * glm::vec4(particle.Position, 1.0f)) : particle.Position;
				gpu.ParticleInstances.push_back({ glm::vec4(position, particle.Size), ToVec4(particle.Color),
					glm::vec4(glm::radians(particle.Rotation), softDot, 0.0f, 0.0f) });
				batch.ViewDistanceSq = std::max(batch.ViewDistanceSq, distanceSq);
				++batch.Count;
			}
			if (batch.Count > 0) gpu.ParticleBatches.push_back(batch);
		}
		if (gpu.ParticleBatches.empty()) return;

		// Systems composite farthest first; overlapping systems can still interleave wrongly, as in most engines.
		std::stable_sort(gpu.ParticleBatches.begin(), gpu.ParticleBatches.end(),
			[](const ParticleBatch& a, const ParticleBatch& b) { return a.ViewDistanceSq > b.ViewDistanceSq; });

		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::Queue queue = WebGPUBackend::GetQueue();
		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!target.Valid || !device || !queue || !encoder) return;

		wgpu::TextureView depthView = target.HasDepth ? target.DepthView : gpu.EnsureOwnDepth(device, target.Width, target.Height);
		if (!depthView) return;
		const uint64_t instanceBytes = gpu.ParticleInstances.size() * sizeof(ParticleInstance);
		if (!EnsureBuffer(device, gpu.ParticleInstanceBuffer, gpu.ParticleInstanceCapacity, instanceBytes, wgpu::BufferUsage::Vertex, "renderer3d-particles")) {
			FNK_CORE_WARN_TAG("Renderer3D", "Particle buffer too large ({} bytes); particles skipped", instanceBytes);
			return;
		}

		ParticleFrameUniforms frame{};
		frame.ViewProjection = view.Projection * view.View;
		// Rows of the view matrix are the camera's world axes.
		frame.CameraRight = glm::vec4(view.View[0][0], view.View[1][0], view.View[2][0], 0.0f);
		frame.CameraUp = glm::vec4(view.View[0][1], view.View[1][1], view.View[2][1], 0.0f);
		frame.Params = glm::vec4(IsSrgbFormat(target.ColorFormat) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		queue.WriteBuffer(gpu.ParticleUniformBuffer, 0, &frame, sizeof(frame));
		queue.WriteBuffer(gpu.ParticleInstanceBuffer, 0, gpu.ParticleInstances.data(), instanceBytes);

		const bool clearOwnDepth = !target.HasDepth && m_OwnDepthNeedsClear;
		wgpu::RenderPassColorAttachment colorAttachment{};
		colorAttachment.view = target.ColorView;
		colorAttachment.loadOp = wgpu::LoadOp::Load;
		colorAttachment.storeOp = wgpu::StoreOp::Store;
		colorAttachment.depthSlice = wgpu::kDepthSliceUndefined;

		wgpu::RenderPassDepthStencilAttachment depthAttachment{};
		depthAttachment.view = depthView;
		depthAttachment.depthLoadOp = clearOwnDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		depthAttachment.depthClearValue = 1.0f;
		depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
		depthAttachment.stencilLoadOp = clearOwnDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		depthAttachment.stencilStoreOp = wgpu::StoreOp::Store;

		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "renderer3d-particles";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &colorAttachment;
		passDesc.depthStencilAttachment = &depthAttachment;

		const TextureHandle whiteTexture = TextureManager::GetDefaultTexture(DefaultTexture::Pixel);
		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetBindGroup(0, gpu.ParticleBindGroup);
		pass.SetVertexBuffer(0, gpu.ParticleInstanceBuffer);
		wgpu::RenderPipeline boundPipeline;
		for (const ParticleBatch& batch : gpu.ParticleBatches) {
			wgpu::RenderPipeline pipeline = gpu.GetParticlePipeline(device, target.ColorFormat, target.SampleCount, batch.Blend);
			wgpu::BindGroup textureGroup = TextureManager::IsValid(batch.Texture) ? gpu.GetMaterialBindGroup(device, batch.Texture) : nullptr;
			if (!textureGroup) textureGroup = gpu.GetMaterialBindGroup(device, whiteTexture);
			if (!pipeline || !textureGroup) continue;
			if (pipeline.Get() != boundPipeline.Get()) {
				pass.SetPipeline(pipeline);
				boundPipeline = pipeline;
			}
			pass.SetBindGroup(1, textureGroup);
			pass.Draw(6, batch.Count, 0, batch.First);
			++m_DrawCalls;
			m_RenderedParticles += batch.Count;
			m_GeometryTotals.AddQuads(batch.Count);
		}
		pass.End();
		if (!target.HasDepth) m_OwnDepthNeedsClear = false;

		WebGPUBackend::FlushCommands();
		if (target.IsSwapChain) {
			WebGPUBackend::MarkSwapChainRendered();
		}
	}

	void Renderer3D::DrawLine(const glm::vec3& a, const glm::vec3& b, const Color& color) {
		const uint32_t rgba = PackRgba(color);
		auto& lines = m_LinesSeeThrough ? m_SeeThroughLines : m_Lines;
		lines.push_back(PosColorVertex{ a.x, a.y, a.z, rgba });
		lines.push_back(PosColorVertex{ b.x, b.y, b.z, rgba });
	}

	void Renderer3D::DrawWireBox(const glm::mat4& transform, const glm::vec3& halfExtents, const Color& color) {
		glm::vec3 corners[8];
		for (int i = 0; i < 8; ++i) {
			const glm::vec3 local(
				(i & 1) ? halfExtents.x : -halfExtents.x,
				(i & 2) ? halfExtents.y : -halfExtents.y,
				(i & 4) ? halfExtents.z : -halfExtents.z);
			corners[i] = glm::vec3(transform * glm::vec4(local, 1.0f));
		}
		constexpr int edges[12][2] = {
			{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
			{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
		};
		for (const auto& edge : edges) {
			DrawLine(corners[edge[0]], corners[edge[1]], color);
		}
	}

	void Renderer3D::DrawWireCircle(const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB, float radius, const Color& color, int segments) {
		segments = std::max(segments, 3);
		glm::vec3 previous = center + axisA * radius;
		for (int i = 1; i <= segments; ++i) {
			const float angle = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
			const glm::vec3 point = center + (axisA * std::cos(angle) + axisB * std::sin(angle)) * radius;
			DrawLine(previous, point, color);
			previous = point;
		}
	}

	void Renderer3D::DrawGrid(const glm::vec3& center, int halfLineCount, float spacing, const Color& color) {
		const float extent = static_cast<float>(halfLineCount) * spacing;
		const Color axisX{ 0.86f, 0.31f, 0.31f, color.a };
		const Color axisZ{ 0.31f, 0.47f, 0.90f, color.a };
		for (int i = -halfLineCount; i <= halfLineCount; ++i) {
			const float offset = static_cast<float>(i) * spacing;
			const float x = center.x + offset;
			const float z = center.z + offset;
			const bool onXAxis = std::abs(z) < spacing * 0.5f;
			const bool onZAxis = std::abs(x) < spacing * 0.5f;
			DrawLine({ center.x - extent, 0.0f, z }, { center.x + extent, 0.0f, z }, onXAxis ? axisX : color);
			DrawLine({ x, 0.0f, center.z - extent }, { x, 0.0f, center.z + extent }, onZAxis ? axisZ : color);
		}
	}

	void Renderer3D::FlushLines(const Camera3DView& view, bool depthTested) {
		if (m_Lines.empty() && m_SeeThroughLines.empty()) return;
		auto clearQueues = [this]() {
			m_Lines.clear();
			m_SeeThroughLines.clear();
		};
		if (!m_IsInitialized || !m_Gpu) {
			clearQueues();
			return;
		}
		GpuState& gpu = *m_Gpu;

		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::Queue queue = WebGPUBackend::GetQueue();
		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!target.Valid || !device || !queue || !encoder) {
			clearQueues();
			return;
		}

		// One upload: regular lines, then the see-through ones faded (drawn ignoring depth), then at full color (depth tested).
		m_LineUpload.clear();
		m_LineUpload.insert(m_LineUpload.end(), m_Lines.begin(), m_Lines.end());
		for (PosColorVertex vertex : m_SeeThroughLines) {
			const uint32_t alpha = (vertex.color >> 24) & 0xFFu;
			vertex.color = (vertex.color & 0x00FFFFFFu) | ((alpha * 3u / 10u) << 24);
			m_LineUpload.push_back(vertex);
		}
		m_LineUpload.insert(m_LineUpload.end(), m_SeeThroughLines.begin(), m_SeeThroughLines.end());
		const uint32_t regularCount = static_cast<uint32_t>(m_Lines.size());
		const uint32_t seeThroughCount = static_cast<uint32_t>(m_SeeThroughLines.size());

		wgpu::TextureView depthView = target.HasDepth ? target.DepthView : gpu.EnsureOwnDepth(device, target.Width, target.Height);
		const uint32_t samples = target.SampleCount;
		wgpu::RenderPipeline pipeline = gpu.GetLinePipeline(device, target.ColorFormat, samples, depthTested);
		wgpu::RenderPipeline hiddenPipeline = seeThroughCount > 0 ? gpu.GetLinePipeline(device, target.ColorFormat, samples, false) : nullptr;
		wgpu::RenderPipeline visiblePipeline = seeThroughCount > 0 ? gpu.GetLinePipeline(device, target.ColorFormat, samples, true) : nullptr;
		const uint64_t bytes = m_LineUpload.size() * sizeof(PosColorVertex);
		if (!depthView || !pipeline || (seeThroughCount > 0 && (!hiddenPipeline || !visiblePipeline))
			|| !EnsureBuffer(device, gpu.LineVertexBuffer, gpu.LineVertexCapacity, bytes, wgpu::BufferUsage::Vertex, "renderer3d-lines")) {
			clearQueues();
			return;
		}

		const glm::mat4 viewProjection = view.Projection * view.View;
		queue.WriteBuffer(gpu.LineUniformBuffer, 0, glm::value_ptr(viewProjection), sizeof(glm::mat4));
		queue.WriteBuffer(gpu.LineVertexBuffer, 0, m_LineUpload.data(), bytes);

		const bool clearOwnDepth = !target.HasDepth && m_OwnDepthNeedsClear;
		wgpu::RenderPassColorAttachment colorAttachment{};
		colorAttachment.view = target.ColorView;
		colorAttachment.loadOp = wgpu::LoadOp::Load;
		colorAttachment.storeOp = wgpu::StoreOp::Store;
		colorAttachment.depthSlice = wgpu::kDepthSliceUndefined;

		wgpu::RenderPassDepthStencilAttachment depthAttachment{};
		depthAttachment.view = depthView;
		depthAttachment.depthLoadOp = clearOwnDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		depthAttachment.depthClearValue = 1.0f;
		depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
		depthAttachment.stencilLoadOp = clearOwnDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		depthAttachment.stencilStoreOp = wgpu::StoreOp::Store;

		wgpu::RenderPassDescriptor passDesc{};
		passDesc.label = "renderer3d-lines";
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &colorAttachment;
		passDesc.depthStencilAttachment = &depthAttachment;

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetBindGroup(0, gpu.LineBindGroup);
		pass.SetVertexBuffer(0, gpu.LineVertexBuffer, 0, bytes);
		if (regularCount > 0) {
			pass.SetPipeline(pipeline);
			pass.Draw(regularCount);
		}
		if (seeThroughCount > 0) {
			pass.SetPipeline(hiddenPipeline);
			pass.Draw(seeThroughCount, 1, regularCount);
			pass.SetPipeline(visiblePipeline);
			pass.Draw(seeThroughCount, 1, regularCount + seeThroughCount);
		}
		pass.End();
		clearQueues();
		if (!target.HasDepth) m_OwnDepthNeedsClear = false;

		WebGPUBackend::FlushCommands();
		if (target.IsSwapChain) {
			WebGPUBackend::MarkSwapChainRendered();
		}
	}

}
