#include "pch.hpp"
#include "Diagnostics/DebugStats.hpp"
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
#include "Core/Time.hpp"
#include "Gui/GuiRenderer.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/CustomShaderGpu.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Graphics/FramebufferCache.hpp"
#include "Graphics/Frustum.hpp"
#include "Graphics/GizmoRenderer.hpp"
#include "Graphics/Lighting3D.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/PostProcessing/PostProcessor.hpp"
#include "Graphics/PrimitiveMesh.hpp"
#include "Graphics/RenderApi.hpp"
#include "Graphics/Shader.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Graphics/SkinningCompute.hpp"
#include "Graphics/SkyAmbient.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/TextureSampling.hpp"
#include "Profiling/Profiler.hpp"
#include "Profiling/GpuTimer.hpp"
#include "Scene/Scene.hpp"
#include "Systems/SkinnedMeshAnimationSystem.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <entt/container/dense_map.hpp>
#include <webgpu/webgpu_cpp.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Funke {
	namespace {
		constexpr wgpu::TextureFormat k_DepthFormat = wgpu::TextureFormat::Depth24PlusStencil8;
		constexpr wgpu::TextureFormat k_ShadowFormat = wgpu::TextureFormat::Depth32Float;
		// Frame mesh slots: the primitives first, then each custom mesh asset drawn this frame.
		constexpr size_t k_MeshTypeCount = k_PrimitiveMeshTypeCount;

		// Dense frame lookups keep their storage across passes. Mix all 64 bits because
		// texture handles pack the index in the high half and often share a generation.
		struct FrameKeyHash {
			size_t operator()(uint64_t value) const noexcept {
				value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
				value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
				return static_cast<size_t>(value ^ (value >> 31));
			}
		};

		template<typename Value>
		using FrameLookup = entt::dense_map<uint64_t, Value, FrameKeyHash>;

		template<typename Value>
		void ResetFrameLookup(FrameLookup<Value>& lookup) {
			const size_t previousSize = lookup.size();
			lookup.clear();
			// clear resets EnTT's bucket count; reserve restores it without reallocating.
			lookup.reserve(previousSize);
		}

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
            glm::vec4 WorldOrigin;
            glm::vec4 RelativeCameraPosition;
		};
		static_assert(sizeof(FrameUniforms) == 304);

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
		SceneLighting CollectLights(const entt::registry& registry, const glm::vec3& cameraPosition,
			const FrustumPlanes& cameraFrustum, int maxLights, const glm::vec3& origin) {
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
				const auto position = glm::vec3(glm::dvec3(Transform3DHierarchySystem::ComputeWorldMatrixPrecise(registry, entity)[3]) - glm::dvec3(origin));
				anyLight = true;
				if (light.Intensity <= 0.0f || light.Range <= 0.0f
					|| !IntersectsFrustumSphere(cameraFrustum, position, light.Range)) continue;
				Lighting3D::LightCandidate candidate;
				candidate.Type = Lighting3D::LightType::Point;
				candidate.Position = position;
				candidate.Radiance = Lighting3D::SrgbToLinear(light.Color) * std::max(light.Intensity, 0.0f);
				candidate.Range = light.Range;
				candidates.push_back(candidate);
			}

			const auto spots = registry.view<const SpotLightComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
			for (EntityHandle entity : spots) {
				const auto& light = spots.get<const SpotLightComponent>(entity);
				const auto& transform = spots.get<const Transform3DComponent>(entity);
				anyLight = true;
				if (light.Intensity <= 0.0f || light.Range <= 0.0f
					|| !IntersectsFrustumSphere(cameraFrustum, glm::vec3(glm::dvec3(Transform3DHierarchySystem::ComputeWorldMatrixPrecise(registry, entity)[3]) - glm::dvec3(origin)), light.Range)) continue;
				Lighting3D::LightCandidate candidate;
				candidate.Type = Lighting3D::LightType::Spot;
				candidate.Position = glm::vec3(glm::dvec3(Transform3DHierarchySystem::ComputeWorldMatrixPrecise(registry, entity)[3]) - glm::dvec3(origin));
				const glm::vec3 forward = -glm::vec3(transform.WorldMatrix[2]);
				candidate.Direction = glm::dot(forward, forward) > 1e-8f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
				candidate.Radiance = Lighting3D::SrgbToLinear(light.Color) * std::max(light.Intensity, 0.0f);
				candidate.Range = light.Range;
				Lighting3D::SpotAnglesToCosines(light.InnerAngle, light.OuterAngle, candidate.CosInner, candidate.CosOuter);
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
				const glm::vec3 forward = -glm::vec3(Transform3DHierarchySystem::ComputeWorldMatrix(registry, entity)[2]);
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

		// Raw handles describe draw semantics without per-draw wrapper AddRef/Release traffic.
		// A finished bundle owns the referenced Dawn resources; its signature is never hash-only.
		struct BundleDraw {
			WGPURenderPipeline Pipeline = nullptr;
			WGPUBindGroup FrameGroup = nullptr;
			WGPUBindGroup MaterialGroup = nullptr;
			WGPUBuffer Vertices = nullptr;
			WGPUBuffer Indices = nullptr;
			uint32_t IndexCount = 0;
			uint32_t InstanceCount = 0;
			uint32_t FirstInstance = 0;
			bool operator==(const BundleDraw&) const = default;
		};

		struct BundlePassKey {
			WGPUBuffer Instances = nullptr;
			wgpu::TextureFormat ColorFormat = wgpu::TextureFormat::Undefined;
			wgpu::TextureFormat DepthFormat = wgpu::TextureFormat::Undefined;
			uint32_t SampleCount = 1;
			bool operator==(const BundlePassKey&) const = default;
		};

		struct DirectDrawEncoder {
			WGPURenderPassEncoder Handle;
			void Pipeline(WGPURenderPipeline value) const { wgpuRenderPassEncoderSetPipeline(Handle, value); }
			void Group(uint32_t slot, WGPUBindGroup value) const { wgpuRenderPassEncoderSetBindGroup(Handle, slot, value, 0, nullptr); }
			void Vertex(uint32_t slot, WGPUBuffer value) const { wgpuRenderPassEncoderSetVertexBuffer(Handle, slot, value, 0, WGPU_WHOLE_SIZE); }
			void Index(WGPUBuffer value) const { wgpuRenderPassEncoderSetIndexBuffer(Handle, value, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE); }
			void Draw(const BundleDraw& value) const { wgpuRenderPassEncoderDrawIndexed(Handle, value.IndexCount, value.InstanceCount, 0, 0, value.FirstInstance); }
		};

		struct RecordedDrawEncoder {
			WGPURenderBundleEncoder Handle;
			void Pipeline(WGPURenderPipeline value) const { wgpuRenderBundleEncoderSetPipeline(Handle, value); }
			void Group(uint32_t slot, WGPUBindGroup value) const { wgpuRenderBundleEncoderSetBindGroup(Handle, slot, value, 0, nullptr); }
			void Vertex(uint32_t slot, WGPUBuffer value) const { wgpuRenderBundleEncoderSetVertexBuffer(Handle, slot, value, 0, WGPU_WHOLE_SIZE); }
			void Index(WGPUBuffer value) const { wgpuRenderBundleEncoderSetIndexBuffer(Handle, value, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE); }
			void Draw(const BundleDraw& value) const { wgpuRenderBundleEncoderDrawIndexed(Handle, value.IndexCount, value.InstanceCount, 0, 0, value.FirstInstance); }
		};

		template<class Encoder>
		static void EncodeBundleDraws(const Encoder& encoder, const BundlePassKey& key, const std::vector<BundleDraw>& draws) {
			WGPURenderPipeline pipeline = nullptr;
			WGPUBindGroup frameGroup = nullptr, materialGroup = nullptr;
			WGPUBuffer vertices = nullptr, indices = nullptr;
			encoder.Vertex(1, key.Instances);
			for (const BundleDraw& draw : draws) {
				if (pipeline != draw.Pipeline) {
					encoder.Pipeline(draw.Pipeline);
					pipeline = draw.Pipeline;
					frameGroup = materialGroup = nullptr;
				}
				if (frameGroup != draw.FrameGroup) {
					encoder.Group(0, draw.FrameGroup);
					frameGroup = draw.FrameGroup;
				}
				// The standard shadow pipeline has no group 1. A later custom pipeline rebinds it.
				if (draw.MaterialGroup && materialGroup != draw.MaterialGroup) {
					encoder.Group(1, draw.MaterialGroup);
					materialGroup = draw.MaterialGroup;
				}
				if (vertices != draw.Vertices) {
					encoder.Vertex(0, draw.Vertices);
					vertices = draw.Vertices;
				}
				if (indices != draw.Indices) {
					encoder.Index(draw.Indices);
					indices = draw.Indices;
				}
				encoder.Draw(draw);
			}
		}

		struct DrawBundleCache {
			static constexpr size_t k_MinDraws = 64;
			wgpu::RenderBundle Bundle;
			BundlePassKey Key;
			std::vector<BundleDraw> Signature;
			bool HaveSignature = false;
			bool Attempted = false;
			int64_t LastFrame = -1;

			void Reset() {
				Bundle = nullptr;
				Signature.clear();
				HaveSignature = false;
				Attempted = false;
				LastFrame = -1;
			}

			void Submit(wgpu::Device device, wgpu::RenderPassEncoder pass, const BundlePassKey& key,
				std::vector<BundleDraw>& draws, int64_t frame) {
				if (draws.size() < k_MinDraws) {
					Reset();
					if (!draws.empty()) EncodeBundleDraws(DirectDrawEncoder{ pass.Get() }, key, draws);
					return;
				}
				// Keep the old bundle alive during comparison: its references prevent handle-address reuse.
				const bool same = HaveSignature && Key == key && Signature == draws;
				if (!same) {
					Bundle = nullptr;
					Attempted = false;
				}
				// Two equal signatures on consecutive frames before recording. Failed attempts wait for a change.
				if (same && !Bundle && !Attempted && frame == LastFrame + 1) {
					Attempted = true;
					wgpu::RenderBundleEncoderDescriptor desc{};
					desc.label = "renderer3d-stable-draws";
					desc.colorFormatCount = key.ColorFormat == wgpu::TextureFormat::Undefined ? 0 : 1;
					desc.colorFormats = desc.colorFormatCount ? &key.ColorFormat : nullptr;
					desc.depthStencilFormat = key.DepthFormat;
					desc.sampleCount = key.SampleCount;
					wgpu::RenderBundleEncoder recorder = device.CreateRenderBundleEncoder(&desc);
					if (recorder) {
						EncodeBundleDraws(RecordedDrawEncoder{ recorder.Get() }, key, draws);
						Bundle = recorder.Finish();
					}
				}
				if (Bundle) pass.ExecuteBundles(1, &Bundle);
				else EncodeBundleDraws(DirectDrawEncoder{ pass.Get() }, key, draws);
				LastFrame = frame;
				if (!same) {
					Key = key;
					Signature.swap(draws);
					HaveSignature = true;
				}
			}
		};

		DrawBundleCache ShadowDrawBundle;
		DrawBundleCache ColorDrawBundle;
		std::vector<BundleDraw> ShadowBundleDraws;
		std::vector<BundleDraw> ColorBundleDraws;
		std::vector<BundleDraw> TransparentDirectDraws;

		struct CustomMeshEntry {
			MeshBuffers Buffers;
			wgpu::Buffer SkinSource;
			uint32_t SkinRevision = 0;
			uint32_t Revision = 0;
			uint32_t WireRevision = 0;
			uint64_t VertexCapacity = 0;
			uint64_t IndexCapacity = 0;
			uint64_t WireIndexCapacity = 0;
			std::chrono::steady_clock::time_point LastUsed{};
			bool UsedSincePurge = false;
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
			wgpu::Buffer JointBuffer;
			wgpu::Buffer SkinSource;
			wgpu::BindGroup SkinGroup;
			uint64_t JointCapacity = 0;
			uint64_t GroupVertexBytes = 0;
			uint64_t GroupJointBytes = 0;
			std::vector<glm::mat4> Joints;
			std::vector<MeshVertex> Vertices;
			SkeletalPoseRequest Pose;
			uint64_t Model = 0;
			uint32_t Revision = 0;
			glm::vec3 BoundsMin{ 0.0f };
			glm::vec3 BoundsMax{ 0.0f };
			bool Cullable = false;
			bool PoseValid = false;
			bool VerticesDirty = true;
			bool StorageOutput = false;
			std::chrono::steady_clock::time_point LastUsed{};
			bool UsedSincePurge = false;
		};
		struct SkinnedWork {
			const MeshAsset* Asset = nullptr;
			SkinnedEntry* Entry = nullptr;
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
		std::unique_ptr<Shader> GridShader;
		wgpu::ShaderModule GridModule;
		struct GridInstance {
			glm::vec4 CenterSpacing;
			glm::vec4 Color;
			glm::vec4 OriginExtent;
		};
		std::vector<GridInstance> Grids;
		wgpu::Buffer GridInstanceBuffer;
		uint64_t GridInstanceCapacity = 0;
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
		std::vector<NodePose> PoseScratch;
		std::vector<NodePose> PreviousPoseScratch;
		std::vector<SkinnedWork> FrameSkinning;
		std::vector<SkinningCompute::Vertex> SkinSourceUpload;
		wgpu::BindGroupLayout SkinLayout;
		wgpu::ComputePipeline SkinPipeline;
		wgpu::Limits SkinLimits{};
		bool SkinPipelineAttempted = false;
		bool SkinDispatchLogged = false;
		std::optional<GpuPassTrace> SkinTrace;
		std::vector<const MeshBuffers*> FrameMeshes;
		FrameLookup<uint32_t> FrameMeshSlots;
		// Keyed by ScenePipelineKey: the MSAA scene target needs its own variants.
		std::unordered_map<uint64_t, wgpu::RenderPipeline> MeshPipelines;
		std::unordered_map<uint64_t, wgpu::RenderPipeline> SkyPipelines;
		std::unordered_map<uint64_t, wgpu::RenderPipeline> LinePipelines;
		std::unordered_map<uint64_t, wgpu::RenderPipeline> WirePipelines;
		wgpu::RenderPipeline ShadowPipeline;
		std::unordered_map<uint64_t, CachedMaterialGroup> MaterialGroups;

		CustomMaterialBindings CustomBindings;
		std::vector<FrameCustom> FrameCustoms;
		// Material inputs are shared by all instances in this pass and refreshed on the next one.
		struct FrameMaterial {
			const Material* Value = nullptr;
			const CustomShaderProgram* Program = nullptr;
			glm::vec4 Color{ 1.0f };
			glm::vec4 Emissive{ 0.0f };
			glm::vec4 Surface{ 0.0f };
			glm::vec4 UvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
			uint32_t TextureSlot = 0;
			uint32_t Custom = 0;
			bool Transparent = false;
			bool ResourcesResolved = false;
		};
		FrameLookup<FrameMaterial> FrameMaterials;
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

		// Loaded scene materials own these textures; cached handles can expire on a purge.
		std::unordered_map<uint64_t, TextureEntry> MaterialTextures;
		uint32_t TextureDestroyToken = 0;

		std::vector<MeshInstance> Instances;
		std::vector<DrawItem> DrawItems;
		uint64_t MeshSubmitSerial = UINT64_MAX;
		uint64_t SkySubmitSerial = UINT64_MAX;
		uint64_t ParticleSubmitSerial = UINT64_MAX;
		uint64_t LineSubmitSerial = UINT64_MAX;
		std::vector<MeshInstance> InstanceUpload;
		std::vector<TextureHandle> FrameTextures;
		FrameLookup<uint32_t> FrameTextureSlots;
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
			entry.LastUsed = std::chrono::steady_clock::now();
			entry.UsedSincePurge = true;
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

		SkinnedEntry& ResolveSkinnedPose(const SkinnedKey& key, const SkinnedMeshRendererComponent& renderer,
			const MeshAsset& asset, std::chrono::steady_clock::time_point now) {
			SkinnedEntry& entry = SkinnedMeshes[key];
			entry.LastUsed = now;
			entry.UsedSincePurge = true;
			const SkinnedMeshData& data = *asset.Skin;
			const SkeletalPoseRequest pose = SkinnedMeshAnimationSystem::GetDrawnPose(renderer, data);
			if (!entry.PoseValid || entry.Model != renderer.Model || entry.Revision != asset.Revision || !(entry.Pose == pose)) {
				FUNKE_PROFILE_SCOPE("Renderer3D.SkinningPose");
				SkeletalAnimation::EvaluatePose(data, pose, PoseNodes, PoseScratch, PreviousPoseScratch);
				SkeletalAnimation::ComputeJointMatrices(data, PoseNodes, entry.Joints);
				entry.Cullable = SkeletalAnimation::PosedBounds(data, entry.Joints, entry.BoundsMin, entry.BoundsMax);
				if (!entry.Cullable) {
					entry.BoundsMin = asset.BoundsMin;
					entry.BoundsMax = asset.BoundsMax;
				}
				entry.Model = renderer.Model;
				entry.Revision = asset.Revision;
				entry.Pose = pose;
				entry.PoseValid = true;
				entry.VerticesDirty = true;
			}
			entry.Buffers.VertexCount = static_cast<uint32_t>(data.BindVertices.size());
			return entry;
		}

		bool EnsureSkinPipeline(wgpu::Device device) {
			if (SkinPipelineAttempted) return static_cast<bool>(SkinPipeline);
			SkinPipelineAttempted = true;
			if (const char* fallback = std::getenv("FUNKE_REFERENCE_CPU_SKINNING"); fallback && std::strcmp(fallback, "1") == 0)
				return false;
			if (device.GetLimits(&SkinLimits) != wgpu::Status::Success || SkinLimits.maxStorageBuffersPerShaderStage < 3
				|| SkinLimits.maxComputeInvocationsPerWorkgroup < SkinningCompute::k_WorkgroupSize
				|| SkinLimits.maxComputeWorkgroupSizeX < SkinningCompute::k_WorkgroupSize) return false;
			wgpu::ShaderSourceWGSL source{};
			source.code = SkinningCompute::k_Shader;
			wgpu::ShaderModuleDescriptor moduleDesc{};
			moduleDesc.nextInChain = &source;
			moduleDesc.label = "renderer3d-skinning";
			const wgpu::ShaderModule module = device.CreateShaderModule(&moduleDesc);
			if (!WebGPUBackend::ShaderModuleCompiled(module)) return false;
			wgpu::BindGroupLayoutEntry entries[3]{};
			for (uint32_t i = 0; i < 3; ++i) {
				entries[i].binding = i;
				entries[i].visibility = wgpu::ShaderStage::Compute;
				entries[i].buffer.type = i == 2 ? wgpu::BufferBindingType::Storage : wgpu::BufferBindingType::ReadOnlyStorage;
			}
			SkinLayout = CreateLayout(device, entries, 3, "renderer3d-skinning-layout");
			wgpu::PipelineLayoutDescriptor layoutDesc{};
			layoutDesc.bindGroupLayoutCount = 1;
			layoutDesc.bindGroupLayouts = &SkinLayout;
			wgpu::ComputePipelineDescriptor desc{};
			desc.layout = device.CreatePipelineLayout(&layoutDesc);
			desc.compute.module = module;
			desc.compute.entryPoint = "skin";
			desc.label = "renderer3d-skinning";
			SkinPipeline = device.CreateComputePipeline(&desc);
			return static_cast<bool>(SkinPipeline);
		}

		bool SkinOnGpu(wgpu::Device device, wgpu::CommandEncoder encoder, wgpu::ComputePassEncoder& pass,
			SkinnedEntry& entry, const SkinnedMeshData& data) {
			if (!EnsureSkinPipeline(device) || !SkinningCompute::FitsLimits(data.BindVertices.size(), entry.Joints.size(),
				SkinLimits.maxStorageBufferBindingSize, SkinLimits.maxBufferSize, SkinLimits.maxComputeWorkgroupsPerDimension)) return false;
			CustomMeshEntry& model = CustomMeshes[entry.Model];
			if (!model.SkinSource || model.SkinRevision != entry.Revision) {
				if (!SkinningCompute::PackVertices(data, SkinSourceUpload)) return false;
				model.SkinSource = CreateBufferWithData(device, wgpu::BufferUsage::Storage, SkinSourceUpload.data(),
					SkinSourceUpload.size() * sizeof(SkinningCompute::Vertex), "renderer3d-skinning-source");
				model.SkinRevision = entry.Revision;
			}
			if (!model.SkinSource) return false;
			const uint64_t vertexBytes = data.BindVertices.size() * sizeof(MeshVertex);
			const uint64_t jointBytes = entry.Joints.size() * sizeof(glm::mat4);
			if (!entry.StorageOutput) {
				entry.Buffers.Vertices = nullptr;
				entry.VertexCapacity = 0;
				entry.SkinGroup = nullptr;
			}
			const auto oldVertices = entry.Buffers.Vertices.Get();
			const auto oldJoints = entry.JointBuffer.Get();
			if (!EnsureBuffer(device, entry.Buffers.Vertices, entry.VertexCapacity, vertexBytes,
				wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Storage, "renderer3d-skinned-vb")
				|| !EnsureBuffer(device, entry.JointBuffer, entry.JointCapacity, jointBytes, wgpu::BufferUsage::Storage, "renderer3d-skinning-joints")) return false;
			entry.StorageOutput = true;
			if (!entry.SkinGroup || entry.SkinSource.Get() != model.SkinSource.Get() || oldVertices != entry.Buffers.Vertices.Get()
				|| oldJoints != entry.JointBuffer.Get() || entry.GroupVertexBytes != vertexBytes || entry.GroupJointBytes != jointBytes) {
				wgpu::BindGroupEntry bindings[3]{};
				bindings[0].buffer = model.SkinSource;
				bindings[0].size = data.BindVertices.size() * sizeof(SkinningCompute::Vertex);
				bindings[1].buffer = entry.JointBuffer;
				bindings[1].size = jointBytes;
				bindings[2].buffer = entry.Buffers.Vertices;
				bindings[2].size = vertexBytes;
				for (uint32_t i = 0; i < 3; ++i) bindings[i].binding = i;
				wgpu::BindGroupDescriptor desc{};
				desc.layout = SkinLayout;
				desc.entries = bindings;
				desc.entryCount = 3;
				desc.label = "renderer3d-skinning";
				entry.SkinGroup = device.CreateBindGroup(&desc);
				entry.SkinSource = model.SkinSource;
				entry.GroupVertexBytes = vertexBytes;
				entry.GroupJointBytes = jointBytes;
			}
			if (!entry.SkinGroup) return false;
			// Palettes belong to entities: queued uploads cannot overwrite another dispatch's pose.
			device.GetQueue().WriteBuffer(entry.JointBuffer, 0, entry.Joints.data(), jointBytes);
			FUNKE_PROFILE_VALUE("Renderer3D.JointUploadBytes", float(jointBytes));
			if (!pass) {
				wgpu::ComputePassDescriptor desc{};
				desc.label = "renderer3d-skinning";
				SkinTrace.emplace(encoder, desc);
				pass = encoder.BeginComputePass(&desc);
				pass.SetPipeline(SkinPipeline);
			}
			pass.SetBindGroup(0, entry.SkinGroup);
			pass.DispatchWorkgroups(static_cast<uint32_t>((data.BindVertices.size() + SkinningCompute::k_WorkgroupSize - 1) / SkinningCompute::k_WorkgroupSize));
			if (!SkinDispatchLogged) {
				FNK_CORE_INFO_TAG("Renderer3D", "GPU skinning active (compute, {} vertices in first dispatch)", data.BindVertices.size());
				SkinDispatchLogged = true;
			}
			return true;
		}

		void UpdateSkinnedVertices(wgpu::Device device, wgpu::CommandEncoder encoder, wgpu::ComputePassEncoder& pass, const SkinnedWork& work) {
			SkinnedEntry& entry = *work.Entry;
			if (!entry.VerticesDirty && entry.Buffers.Vertices) return;
			const SkinnedMeshData& data = *work.Asset->Skin;
			if (!SkinOnGpu(device, encoder, pass, entry, data)) {
				FUNKE_PROFILE_SCOPE("Renderer3D.SkinningFallback");
				SkeletalAnimation::SkinVertices(data, entry.Joints, entry.Vertices);
				const uint64_t bytes = entry.Vertices.size() * sizeof(MeshVertex);
				const auto oldVertices = entry.Buffers.Vertices.Get();
				if (bytes == 0 || !EnsureBuffer(device, entry.Buffers.Vertices, entry.VertexCapacity, bytes, wgpu::BufferUsage::Vertex, "renderer3d-skinned-vb")) {
					entry.Buffers.Vertices = nullptr;
					entry.VertexCapacity = 0;
					entry.StorageOutput = false;
					return;
				}
				if (entry.Buffers.Vertices.Get() != oldVertices) {
					entry.StorageOutput = false;
					entry.SkinGroup = nullptr;
				}
				device.GetQueue().WriteBuffer(entry.Buffers.Vertices, 0, entry.Vertices.data(), bytes);
			}
			entry.VerticesDirty = false;
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

		wgpu::RenderPipeline GetLinePipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, uint32_t sampleCount, bool depthTested, bool grid = false) {
			const uint64_t key = ScenePipelineKey(colorFormat, sampleCount, (depthTested ? 1u : 0u) | (grid ? 2u : 0u));
			if (auto it = LinePipelines.find(key); it != LinePipelines.end()) {
				return it->second;
			}

			wgpu::VertexAttribute attributes[3] = {};
			SetAttribute(attributes[0], wgpu::VertexFormat::Float32x3, offsetof(PosColorVertex, x), 0);
			SetAttribute(attributes[1], wgpu::VertexFormat::Unorm8x4, offsetof(PosColorVertex, color), 1);
			if (grid) {
				SetAttribute(attributes[0], wgpu::VertexFormat::Float32x4, offsetof(GridInstance, CenterSpacing), 0);
				SetAttribute(attributes[1], wgpu::VertexFormat::Float32x4, offsetof(GridInstance, Color), 1);
				SetAttribute(attributes[2], wgpu::VertexFormat::Float32x4, offsetof(GridInstance, OriginExtent), 2);
			}

			wgpu::VertexBufferLayout buffer{};
			buffer.arrayStride = grid ? sizeof(GridInstance) : sizeof(PosColorVertex);
			buffer.stepMode = grid ? wgpu::VertexStepMode::Instance : wgpu::VertexStepMode::Vertex;
			buffer.attributeCount = grid ? 3 : 2;
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
			fragment.module = grid ? GridModule : LineModule;
			fragment.entryPoint = "fs_main";
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;

			wgpu::DepthStencilState depth{};
			depth.format = k_DepthFormat;
			depth.depthWriteEnabled = false;
			depth.depthCompare = depthTested ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Always;

			wgpu::RenderPipelineDescriptor desc{};
			desc.label = grid ? "renderer3d-grid-pipeline" : "renderer3d-line-pipeline";
			desc.layout = LinePipelineLayout;
			desc.vertex.module = grid ? GridModule : LineModule;
			desc.vertex.entryPoint = "vs_main";
			desc.vertex.bufferCount = 1;
			desc.vertex.buffers = &buffer;
			desc.fragment = &fragment;
			desc.primitive.topology = grid ? wgpu::PrimitiveTopology::TriangleList : wgpu::PrimitiveTopology::LineList;
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
		PostProcessScene SceneInputs;
		FramebufferCache<> SceneTargets;
		Framebuffer* SceneFbo = nullptr;
		// FXAA result when camera effects still follow it.
		FramebufferCache<> AntiAliasTargets;
		FramebufferCache<> EffectTargets;
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
		uint32_t ViewportX = 0, ViewportY = 0, ViewportWidth = 0, ViewportHeight = 0;

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
		// Bind groups hold texture views; drop them before the view dies.
		gpu.TextureDestroyToken = TextureManager::AddDestroyListener([state](TextureHandle handle) {
			state->MaterialGroups.clear();
			std::erase_if(state->MaterialTextures, [handle](const auto& pair) { return pair.second.Handle == handle; });
		});

		m_IsInitialized = true;
		return true;
	}

	void Renderer3D::Shutdown() {
		if (m_Gpu) {
			if (m_Gpu->TextureDestroyToken != 0) TextureManager::RemoveDestroyListener(m_Gpu->TextureDestroyToken);
		}
		// Dawn objects must die before RenderApi::Shutdown.
		if (m_PostProcess) {
			m_PostProcess->Processor.Shutdown();
			m_PostProcess->SceneTargets.Clear();
			m_PostProcess->AntiAliasTargets.Clear();
			m_PostProcess->EffectTargets.Clear();
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

	size_t Renderer3D::PurgeUnusedMeshes(std::chrono::milliseconds maxIdle) {
		if (!m_Gpu) return 0;
		GpuState& gpu = *m_Gpu;
		gpu.FrameMeshes.clear();
		gpu.FrameSkinning.clear();
		const auto now = std::chrono::steady_clock::now();
		const size_t before = gpu.CustomMeshes.size() + gpu.SkinnedMeshes.size();
		for (auto it = gpu.SkinnedMeshes.begin(); it != gpu.SkinnedMeshes.end();) {
			auto& entry = it->second;
			const bool used = std::exchange(entry.UsedSincePurge, false);
			if (!used && now - entry.LastUsed >= maxIdle) it = gpu.SkinnedMeshes.erase(it);
			else ++it;
		}
		for (auto it = gpu.CustomMeshes.begin(); it != gpu.CustomMeshes.end();) {
			auto& entry = it->second;
			const bool used = std::exchange(entry.UsedSincePurge, false);
			const bool expired = IsRuntimeMeshGuid(it->first)
				? !MeshLibrary::IsLiveRuntime(it->first) : now - entry.LastUsed >= maxIdle;
			if (!used && expired) it = gpu.CustomMeshes.erase(it);
			else ++it;
		}
		const size_t removed = before - gpu.CustomMeshes.size() - gpu.SkinnedMeshes.size();
		if (removed != 0) {
			gpu.ShadowDrawBundle.Reset();
			gpu.ColorDrawBundle.Reset();
		}
		return removed;
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

    Camera3DView Renderer3D::MakePreciseView(const glm::dmat4& cameraWorld, const Camera3DComponent& camera, float aspect) {
        Camera3DView view = MakeView(glm::mat4(cameraWorld), camera, aspect);
        view.PositionRemainder = glm::dvec3(cameraWorld[3]) - glm::dvec3(view.Position);
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
		if (!m_IsInitialized) return false;
		const bool effects = m_PostProcessingEnabled && settings && m_Settings.PostProcessingEnabled && settings->HasEnabledEffect();
		const bool fxaa = m_PostProcessingEnabled && m_Settings.AntiAliasing == AntiAliasingMode::FXAA;
		const bool msaa = m_PostProcessingEnabled && GetMultisampleCount(m_Settings.AntiAliasing) > 1;
		const bool scaled = m_Settings.RenderScale < 1.0f;
		if (!effects && !fxaa && !msaa && !scaled) return false;
		if (!m_PostProcess) m_PostProcess = std::make_unique<PostProcessState>();
		PostProcessState& state = *m_PostProcess;
		if (state.Active) return false;
		const auto caller = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!caller.Valid || caller.Width == 0 || caller.Height == 0 || caller.SampleCount > 1) return false;
		state.ViewportX = state.ViewportY = 0;
		state.ViewportWidth = caller.Width;
		state.ViewportHeight = caller.Height;
		WebGPUBackend::GetCachedViewport(state.ViewportX, state.ViewportY, state.ViewportWidth, state.ViewportHeight);
		state.ViewportX = std::min(state.ViewportX, caller.Width - 1);
		state.ViewportY = std::min(state.ViewportY, caller.Height - 1);
		state.ViewportWidth = std::clamp(state.ViewportWidth, 1u, caller.Width - state.ViewportX);
		state.ViewportHeight = std::clamp(state.ViewportHeight, 1u, caller.Height - state.ViewportY);

		// MSAA alone resolves straight into the caller: going through the RGBA16F intermediate would round pixel-art
		// texels differently than drawing without anti-aliasing.
		const std::optional<TextureFormat> callerFormat = ToFramebufferFormat(caller.ColorFormat);
		const bool intermediate = effects || fxaa || scaled || !callerFormat;
		const int width = intermediate ? std::max(1, static_cast<int>(std::lround(state.ViewportWidth * m_Settings.RenderScale)))
			: static_cast<int>(caller.Width);
		const int height = intermediate ? std::max(1, static_cast<int>(std::lround(state.ViewportHeight * m_Settings.RenderScale)))
			: static_cast<int>(caller.Height);
		if (intermediate) {
			if (!state.Processor.IsInitialized()) {
				if (state.InitFailed) return false;
				if (!state.Processor.Initialize()) {
					state.InitFailed = true;
					FNK_CORE_WARN_TAG("Renderer3D", "PostProcessor unavailable; 3D cameras render without post-processing");
					return false;
				}
			}
			state.SceneFbo = state.SceneTargets.Acquire({ width, height, TextureFormat::RGBA16F, 1, !msaa });
			if (!state.SceneFbo) return false;
		}
		Framebuffer* msaaTarget = msaa
			? state.AcquireMsaaTarget(width, height, intermediate ? TextureFormat::RGBA16F : *callerFormat) : nullptr;
		if (msaa && !msaaTarget) return false;
		if (!intermediate && !msaaTarget) return false;

		state.Caller = WebGPUBackend::SaveBoundTarget();
		state.CallerTarget = caller;
		state.Settings = effects ? settings : nullptr;
		state.SceneInputs = {};
		state.AntiAlias = fxaa;
		state.Msaa = msaaTarget;
		state.Intermediate = intermediate;
		state.Active = true;
		RenderApi::BindFramebuffer(msaaTarget ? *msaaTarget : *state.SceneFbo);
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
        Diagnostics::DebugStats::Scope debugTiming(Diagnostics::DebugStats::Timing::RenderCpu);
		if (!m_PostProcess || !m_PostProcess->Active) return;
		PostProcessState& state = *m_PostProcess;
		state.Active = false;
		const WebGPUBackend::CurrentTargetInfo target = state.CallerTarget;
		if (state.Intermediate) {
			const Framebuffer& depthSource = state.Msaa ? *state.Msaa : *state.SceneFbo;
			state.SceneInputs.Depth = WebGPUBackend::LookupFramebufferDepthSampleView(depthSource.GetBackendId());
			state.SceneInputs.Samples = depthSource.GetSampleCount();
		}
		if (state.Msaa) {
			const wgpu::TextureView destination = state.Intermediate
				? WebGPUBackend::LookupFramebufferByFboId(state.SceneFbo->GetBackendId()).ColorView : target.ColorView;
			ResolveMultisampled(*state.Msaa, destination);
			state.Msaa = nullptr;
		}
		if (state.Intermediate) {
			const Framebuffer* sceneSource = state.SceneFbo;
			const int width = sceneSource->GetWidth(), height = sceneSource->GetHeight();
			const bool upscale = width != state.ViewportWidth || height != state.ViewportHeight;
			const bool subrect = state.ViewportX != 0 || state.ViewportY != 0
				|| state.ViewportWidth != target.Width || state.ViewportHeight != target.Height;
			RenderApi::SetViewport(state.ViewportX, state.ViewportY, state.ViewportWidth, state.ViewportHeight);
			bool antiAliasedToTarget = false;
			if (state.AntiAlias) {
				const AntiAliasingQualityLevel aaQuality = m_Settings.AntiAliasingQuality;
				if (!state.Settings && !upscale) {
					antiAliasedToTarget = state.Processor.RunAntiAliasing(*state.SceneFbo, target.ColorView, target.ColorFormat,
						aaQuality, true);
				}
				else if (const Framebuffer* antiAlias = state.AntiAliasTargets.Acquire({ width, height })) {
					const auto aaLook = WebGPUBackend::LookupFramebufferByFboId(antiAlias->GetBackendId());
					if (aaLook.Valid && state.Processor.RunAntiAliasing(*state.SceneFbo, aaLook.ColorView, aaLook.ColorFormat,
						aaQuality, false)) {
						sceneSource = antiAlias;
					}
				}
			}
			if (!antiAliasedToTarget) {
				if (state.Settings && (upscale || subrect)) {
					if (const Framebuffer* effect = state.EffectTargets.Acquire({ width, height })) {
						const auto output = WebGPUBackend::LookupFramebufferByFboId(effect->GetBackendId());
						RenderApi::SetViewport(0, 0, width, height);
						state.Processor.Run(*sceneSource, output.ColorView, output.ColorFormat, width, height,
							state.Settings, m_Settings.PostProcessingQuality, true, &state.SceneInputs);
						RenderApi::SetViewport(state.ViewportX, state.ViewportY, state.ViewportWidth, state.ViewportHeight);
						sceneSource = effect;
					}
				}
				else if (state.Settings) {
					state.Processor.Run(*sceneSource, target.ColorView, target.ColorFormat, target.Width, target.Height,
						state.Settings, m_Settings.PostProcessingQuality, true, &state.SceneInputs);
					sceneSource = nullptr;
				}
				if (sceneSource) state.Processor.Blit(*sceneSource, target.ColorView, target.ColorFormat,
					target.Width, target.Height, m_Settings.UpscaleFilter);
			}
		}
		WebGPUBackend::RestoreBoundTarget(state.Caller);
		RenderApi::SetViewport(state.ViewportX, state.ViewportY, state.ViewportWidth, state.ViewportHeight);
		state.Settings = nullptr;
		state.CallerTarget = {};
		// The caller's depth no longer matches what was drawn; meshes drawn after this clear their own depth again.
		m_OwnDepthNeedsClear = true;
		if (target.IsSwapChain) WebGPUBackend::MarkSwapChainRendered();
	}

	void Renderer3D::RenderWorldUI(Scene& scene, const Camera3DView& view) {
        Diagnostics::DebugStats::Scope debugTiming(Diagnostics::DebugStats::Timing::RenderCpu);
		if (!m_IsInitialized || !m_Gpu) return;
		auto* gui = Application::GetInstance()->GetGuiRenderer();
		if (!gui) return;
		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!target.Valid) return;
		const auto saved = WebGPUBackend::SaveBoundTarget();
		if (!target.HasDepth) {
			auto withDepth = saved;
			withDepth.ColorView = target.ColorView;
			withDepth.ColorFormat = target.ColorFormat;
			withDepth.Width = target.Width;
			withDepth.Height = target.Height;
			withDepth.SampleCount = target.SampleCount;
			withDepth.IsSwapChain = false;
			withDepth.DepthView = m_Gpu->EnsureOwnDepth(WebGPUBackend::GetDevice(), target.Width, target.Height);
			if (!withDepth.DepthView) return;
			if (m_OwnDepthNeedsClear) {
				wgpu::RenderPassDepthStencilAttachment depth{};
				depth.view = withDepth.DepthView;
				depth.depthLoadOp = wgpu::LoadOp::Clear;
				depth.depthStoreOp = wgpu::StoreOp::Store;
				depth.depthClearValue = 1.0f;
				depth.stencilLoadOp = wgpu::LoadOp::Clear;
				depth.stencilStoreOp = wgpu::StoreOp::Store;
				wgpu::RenderPassDescriptor desc{};
				desc.depthStencilAttachment = &depth;
				auto pass = WebGPUBackend::GetFrameEncoder().BeginRenderPass(&desc);
				pass.End();
				m_OwnDepthNeedsClear = false;
			}
			WebGPUBackend::RestoreBoundTarget(withDepth);
		}
		gui->RenderWorldScene(scene, view.Projection * view.View);
		if (!target.HasDepth) {
			WebGPUBackend::RestoreBoundTarget(saved);
			if (target.IsSwapChain) WebGPUBackend::MarkSwapChainRendered();
		}
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
			const EntityHandle found = Camera3DComponent::FindMain(scene);
			if (found != entt::null) {
				cameraScene = &scene;
				cameraEntity = found;
			}
		});
		if (!cameraScene) return false;

		const auto& registry = cameraScene->GetRegistry();
		const auto& camera = registry.get<Camera3DComponent>(cameraEntity);
		const glm::dmat4 cameraWorld = Transform3DHierarchySystem::ComputeWorldMatrixPrecise(registry, cameraEntity);

		RenderApi::BindDefaultFramebuffer();
		RenderApi::SetClearColor(camera.ClearColor);
		RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
		m_OwnDepthNeedsClear = true;

		const Camera3DView view = MakePreciseView(cameraWorld, camera, GetCurrentTargetAspect());
		const auto* postProcessing = camera.PostProcessingEnabled
			? registry.try_get<PostProcessing2DComponent>(cameraEntity) : nullptr;
		const bool postProcess = BeginPostProcess(postProcessing, camera.ClearColor);
		RenderSky(view, cameraScene);
		m_SceneProvider([&](Scene& scene) {
			RenderScene(scene, view);
		});
		if (!Application::IsEditor()) {
			if (Gizmo::GetShowInRuntime()) QueueGizmos(view, GizmoLayerMask::Shared | GizmoLayerMask::Debug);
			FlushLines(view, true);
		}
		m_SceneProvider([&](Scene& scene) { RenderWorldUI(scene, view); });
		if (postProcess) EndPostProcess();
		m_RenderedThisFrame = true;
		return true;
	}

	void Renderer3D::RenderSky(const Camera3DView& view, Scene* lightScene) {
        Diagnostics::DebugStats::Scope debugTiming(Diagnostics::DebugStats::Timing::RenderCpu);
		if (!m_IsInitialized || !m_Gpu || !m_ViewOptions.Skybox || view.Environment.Background != CameraBackground::Skybox) return;
		FUNKE_PROFILE_SCOPE("Renderer3D.Sky");
		GpuState& gpu = *m_Gpu;
		if (gpu.SkySubmitSerial == WebGPUBackend::GetFrameSubmitSerial()) WebGPUBackend::FlushCommands();

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
			const CustomShaderProgram* program = material && material->Shader != 0 && m_ViewOptions.AllowsShader(material->Shader)
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
		sky.InverseViewProjection = glm::inverse(view.Projection * glm::mat4(glm::mat3(view.View)));
		sky.Top = ToVec4(environment.SkyTop);
		sky.Horizon = ToVec4(environment.SkyHorizon);
		sky.Ground = ToVec4(environment.SkyGround);
		glm::vec3 lightDirection(0.0f, -1.0f, 0.0f);
		Color lightColor(1.0f, 1.0f, 1.0f, 1.0f);
		bool hasSun = false;
		if (lightScene && environment.SunSize > 0.0f) {
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
		GpuPassTrace passTrace(encoder, passDesc);

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, gpu.SkyBindGroup);
		if (customGroup) pass.SetBindGroup(k_CustomShaderGroup, customGroup);
		pass.Draw(3);
		pass.End();
		passTrace.Resolve();
		++m_DrawCalls;

		gpu.SkySubmitSerial = WebGPUBackend::GetFrameSubmitSerial();
		if (customGroup) WebGPUBackend::FlushCommands();
		if (target.IsSwapChain) {
			WebGPUBackend::MarkSwapChainRendered();
		}
	}

	void Renderer3D::RenderScene(Scene& scene, const Camera3DView& view) {
        Diagnostics::DebugStats::Scope debugTiming(Diagnostics::DebugStats::Timing::RenderCpu);
		if (!m_IsInitialized || !m_Gpu) return;
		if (m_PostProcess && m_PostProcess->Active) {
			// Empty/sky-only scenes still need the real camera for depth reconstruction.
			m_PostProcess->SceneInputs.View = view.RelativeView(view.RenderOrigin());
			m_PostProcess->SceneInputs.Projection = view.Projection;
			m_PostProcess->SceneInputs.Position = view.RelativePosition(view.RenderOrigin());
            m_PostProcess->SceneInputs.WorldOrigin = view.RenderOrigin();
		}
		FUNKE_PROFILE_SCOPE("Renderer3D.RenderScene");
		Transform3DHierarchySystem::Propagate(scene);
		RenderMeshes(scene, view);
		// Particle billboards would cover the edges.
		if (m_ViewOptions.Particles && RenderApi::GetPolygonMode() != PolygonMode::Wireframe) RenderParticles(scene, view);
	}

	void Renderer3D::RenderMeshes(Scene& scene, const Camera3DView& view) {
		GpuState& gpu = *m_Gpu;
		const entt::registry& registry = scene.GetRegistry();
		const auto meshes = registry.view<const MeshRendererComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
		const auto skinned = registry.view<const SkinnedMeshRendererComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
		if (meshes.begin() == meshes.end() && skinned.begin() == skinned.end()) return;
		if (gpu.MeshSubmitSerial == WebGPUBackend::GetFrameSubmitSerial()) WebGPUBackend::FlushCommands();
		wgpu::Device device = WebGPUBackend::GetDevice();
		if (!device) return;
		// Editor Triangle/Mixed draw modes: black edges instead of shaded surfaces, depth tested against what is already drawn.
		const bool wireframe = RenderApi::GetPolygonMode() == PolygonMode::Wireframe;
		const glm::vec3 origin = view.RenderOrigin();
        const glm::mat4 relativeView = view.RelativeView(origin);
        const glm::vec3 cameraPosition = view.RelativePosition(origin);
        const FrustumPlanes cameraFrustum = ExtractFrustum(view.Projection * relativeView);
		const SceneLighting lighting = m_ViewOptions.Lighting
			? CollectLights(registry, cameraPosition, cameraFrustum, m_Settings.MaxLights, origin) : SceneLighting{};
		const uint32_t shadowResolution = m_ViewOptions.Shadows && m_ViewOptions.Lighting
			? std::min(GetShadowMapResolution(m_Settings.ShadowQuality), WebGPUBackend::GetMaxTextureDimension2D()) : 0;
		const bool shadowsEnabled = !wireframe && shadowResolution > 0 && lighting.ShadowedLight >= 0;
		float shadowTexelWorld = 0.0f, shadowDepthRange = 1.0f;
		const glm::mat4 lightViewProjection = shadowsEnabled
			? Lighting3D::ComputeDirectionalShadowMatrix(relativeView, view.Projection,
				lighting.Selected[static_cast<size_t>(lighting.ShadowedLight)].Direction, m_Settings.ShadowDistance,
				shadowResolution, &shadowTexelWorld, &shadowDepthRange) : glm::mat4(1.0f);
		const FrustumPlanes candidateShadowFrustum = ExtractFrustum(lightViewProjection);

		gpu.Instances.clear();
		gpu.DrawItems.clear();
		gpu.FrameTextures.clear();
		ResetFrameLookup(gpu.FrameTextureSlots);
		gpu.FrameMeshes.clear();
		gpu.FrameSkinning.clear();
		ResetFrameLookup(gpu.FrameMeshSlots);
		gpu.FrameCustoms.clear();
		ResetFrameLookup(gpu.FrameMaterials);
		for (const auto& primitive : gpu.Meshes) gpu.FrameMeshes.push_back(&primitive);
		bool anyCaster = false;
		bool hadCandidates = false;
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

        auto relativeWorld = [&](EntityHandle entity) {
            glm::dmat4 world = Transform3DHierarchySystem::ComputeWorldMatrixPrecise(registry, entity);
            world[3] -= glm::dvec4(origin, 0.0);
            return glm::mat4(world);
        };
        static const Material k_FallbackMaterial;

		auto submit = [&](const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& world,
			uint64_t materialGuid, const Color& color, bool lit, bool castShadows, bool cullable, auto&& resolveMesh) {
			anyCaster |= castShadows;
			hadCandidates = true;
			auto [memo, firstUse] = gpu.FrameMaterials.try_emplace(materialGuid);
			auto& prepared = memo->second;
			if (firstUse) {
				prepared.Value = MaterialLibrary::Get(materialGuid);
				const Material* material = prepared.Value ? prepared.Value : &k_FallbackMaterial;
				// A user shader replaces the standard one; its color input is the renderer's Color alone.
				prepared.Program = material->Shader != 0 && m_ViewOptions.Lighting && m_ViewOptions.AllowsShader(material->Shader) && gpu.CustomBindings.IsInitialized()
					? CustomShaderGpu::GetProgram(material->Shader, ShaderTarget::Mesh3D) : nullptr;
				prepared.Transparent = prepared.Program ? prepared.Program->Description.Blend != ShaderBlend::Opaque
					: material->Surface == MaterialSurface::Transparent;
				if (prepared.Program) {
					prepared.Emissive.w = material->Fog ? 1.0f : 0.0f;
				}
				else {
					prepared.Color = ToVec4(material->BaseColor);
					prepared.Emissive = glm::vec4(Lighting3D::SrgbToLinear(material->EmissiveColor) * std::max(material->EmissiveIntensity, 0.0f),
						material->Fog ? 1.0f : 0.0f);
					prepared.Surface = { material->Metallic, material->Roughness, material->Lit ? 1.0f : 0.0f, prepared.Transparent ? 1.0f : 0.0f };
					prepared.UvTransform = { material->Tiling.x, material->Tiling.y, material->Offset.x, material->Offset.y };
				}
			}
			const CustomShaderProgram* program = prepared.Program;

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
			const MeshVisibility visibility = ClassifyMeshVisibility(cameraFrustum,
				shadowsEnabled ? &candidateShadowFrustum : nullptr, worldCenter, worldExtent, cullable, castShadows);
			if (!visibility.IsNeeded()) return;
			const std::optional<size_t> meshIndex = resolveMesh();
			if (!meshIndex) return;
			if (!prepared.ResourcesResolved) {
				const Material* material = prepared.Value ? prepared.Value : &k_FallbackMaterial;
				if (program) {
					prepared.Custom = static_cast<uint32_t>(gpu.FrameCustoms.size()) + 1;
					gpu.FrameCustoms.push_back({ materialGuid, material, program, nullptr, nullptr });
				}
				else prepared.TextureSlot = textureSlot(gpu.ResolveMaterialTexture(material->BaseColorTexture));
				prepared.ResourcesResolved = true;
			}
			MeshInstance instance{};
			for (int i = 0; i < 4; ++i) instance.Model[i] = world[i];
			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
			for (int i = 0; i < 3; ++i) instance.Normal[i] = glm::vec4(normalMatrix[i], 0.0f);
			instance.Color = prepared.Color * ToVec4(color);
			instance.Emissive = prepared.Emissive;
			instance.Surface = prepared.Surface;
			instance.UvTransform = prepared.UvTransform;
			if (!prepared.Value) instance.Surface.z = lit ? 1.0f : 0.0f;
			if (!m_ViewOptions.Lighting) instance.Surface.z = 0.0f;

			const glm::vec3 toCamera = worldCenter - cameraPosition;
			DrawItem item{ MakeDrawKey(*meshIndex, visibility.CastsShadow, prepared.TextureSlot), static_cast<uint32_t>(gpu.Instances.size()),
				glm::dot(toCamera, toCamera), prepared.Transparent, prepared.Custom };
			// Preserve the early camera test; the shadow map can still fail to initialize below.
			if (!visibility.Visible) item.Key |= k_KeyHiddenBit;
			gpu.DrawItems.push_back(item);
			gpu.Instances.push_back(instance);
			anyCaster |= visibility.CastsShadow;
		};

		for (EntityHandle entity : meshes) {
			const auto& renderer = meshes.get<const MeshRendererComponent>(entity);
			size_t meshIndex = static_cast<size_t>(renderer.Mesh);
			glm::vec3 localMin(0.0f);
			glm::vec3 localMax(0.0f);
			const MeshAsset* asset = nullptr;
			if (renderer.CustomMesh != 0) {
				asset = MeshLibrary::Get(renderer.CustomMesh);
				if (!asset) continue;
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
			submit(localMin, localMax, relativeWorld(entity), renderer.Material, renderer.Color, renderer.Lit,
				renderer.CastShadows, true, [&]() -> std::optional<size_t> {
					return asset ? customMeshSlot(renderer.CustomMesh, *asset) : std::optional<size_t>(meshIndex);
				});
		}

		const auto now = std::chrono::steady_clock::now();
		for (EntityHandle entity : skinned) {
			const auto& renderer = skinned.get<const SkinnedMeshRendererComponent>(entity);
			const MeshAsset* asset = MeshLibrary::Get(renderer.Model);
			if (!asset) continue;
			GpuState::SkinnedEntry* skinnedEntry = nullptr;
			glm::vec3 localMin = asset->BoundsMin;
			glm::vec3 localMax = asset->BoundsMax;
			bool cullable = true;
			if (asset->Skin) {
				const GpuState::SkinnedKey key{ &scene, static_cast<uint32_t>(entity) };
				auto& entry = gpu.ResolveSkinnedPose(key, renderer, *asset, now);
				skinnedEntry = &entry;
				localMin = entry.BoundsMin;
				localMax = entry.BoundsMax;
				cullable = entry.Cullable;
			}
			submit(localMin, localMax, relativeWorld(entity),
				renderer.Material, renderer.Color, renderer.Lit, renderer.CastShadows, cullable, [&]() -> std::optional<size_t> {
					const auto shared = customMeshSlot(renderer.Model, *asset);
					if (!shared || !skinnedEntry) return shared;
					const auto& sharedBuffers = *gpu.FrameMeshes[*shared];
					skinnedEntry->Buffers.Indices = sharedBuffers.Indices;
					skinnedEntry->Buffers.IndexCount = sharedBuffers.IndexCount;
					skinnedEntry->Buffers.WireIndices = sharedBuffers.WireIndices;
					skinnedEntry->Buffers.WireIndexCount = sharedBuffers.WireIndexCount;
					const size_t meshIndex = gpu.FrameMeshes.size();
					gpu.FrameMeshes.push_back(&skinnedEntry->Buffers);
					gpu.FrameSkinning.resize(gpu.FrameMeshes.size());
					gpu.FrameSkinning[meshIndex] = { asset, skinnedEntry };
					return meshIndex;
				});
		}
		if (gpu.Instances.empty() && (!hadCandidates || !(m_PostProcess && m_PostProcess->Active))) return;

		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!target.Valid) return;

		wgpu::Queue queue = WebGPUBackend::GetQueue();
		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!queue || !encoder) return;

		m_LightCount = std::max(m_LightCount, static_cast<uint32_t>(lighting.Selected.size()));
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
		frame.ViewProjection = view.Projection * relativeView;
		frame.CameraPosition = glm::vec4(view.Position, m_Settings.Exposure);
        frame.WorldOrigin = glm::vec4(origin, 0.0f);
        frame.RelativeCameraPosition = glm::vec4(cameraPosition, 0.0f);
		frame.Ambient = glm::vec4(lighting.FlatAmbient, skyAmbient ? environment.SkyAmbient : 0.0f);
		frame.SkyTop = glm::vec4(Lighting3D::SrgbToLinear(environment.SkyTop), 1.0f);
		frame.SkyHorizon = glm::vec4(Lighting3D::SrgbToLinear(environment.SkyHorizon), 1.0f);
		frame.SkyGround = glm::vec4(Lighting3D::SrgbToLinear(environment.SkyGround), 1.0f);
		frame.Params = glm::vec4(static_cast<float>(lighting.Selected.size()), static_cast<float>(m_Settings.ToneMapping),
			IsSrgbFormat(target.ColorFormat) ? 0.0f : 1.0f, -1.0f);

		// Shadow pass for the main directional light.
		wgpu::TextureView shadowView = gpu.DummyShadowView;
		if (!wireframe && shadowResolution > 0 && lighting.ShadowedLight >= 0 && anyCaster) {
			wgpu::TextureView map = gpu.EnsureShadowMap(device, shadowResolution);
			wgpu::RenderPipeline shadowPipeline = gpu.GetShadowPipeline(device);
			if (map && shadowPipeline) {
				frame.LightViewProjection = lightViewProjection;
				frame.Params.w = static_cast<float>(lighting.ShadowedLight);
				frame.ShadowParams = glm::vec4(1.0f / static_cast<float>(shadowResolution), shadowTexelWorld / std::max(shadowDepthRange, 1e-4f),
					shadowTexelWorld * 1.5f, m_Settings.ShadowDistance);
				shadowView = map;
			}
		}
		else if (m_Settings.ShadowQuality == ShadowQualityLevel::Off && gpu.ShadowView) {
			gpu.ReleaseShadowMap();
		}
		{
			FUNKE_PROFILE_SCOPE("Renderer3D.Cull");
			const bool shadowPass = frame.Params.w >= 0.0f;
			anyCaster = false;
			std::erase_if(gpu.DrawItems, [&](DrawItem& item) {
				// The early tests used these same camera/light matrices and displacement bounds.
				// Only shadow-map/pipeline availability can invalidate an accepted caster now.
				const MeshVisibility visibility{ !KeyHidden(item.Key), shadowPass && KeyCastsShadow(item.Key) };
				if (!visibility.CastsShadow) item.Key |= uint64_t(1) << 32;
				anyCaster |= visibility.CastsShadow;
				return !visibility.IsNeeded();
			});
			// Post effects can sample sun visibility even when the map contains no casters.
			if (!anyCaster && !(m_PostProcess && m_PostProcess->Active)) {
				frame.Params.w = -1.0f;
				frame.ShadowParams = glm::vec4(0.0f);
				shadowView = gpu.DummyShadowView;
			}
		}
		if (m_PostProcess && m_PostProcess->Active) {
			auto& inputs = m_PostProcess->SceneInputs;
			inputs.View = relativeView;
			inputs.Projection = view.Projection;
			inputs.Position = cameraPosition;
            inputs.WorldOrigin = origin;
			inputs.LightViewProjection = frame.LightViewProjection;
			inputs.ShadowParams = frame.ShadowParams;
			inputs.Shadow = shadowView;
			if (frame.Params.w >= 0.0f) {
				const auto& sun = lighting.Selected[static_cast<size_t>(lighting.ShadowedLight)];
				inputs.SunDirection = glm::vec4(-sun.Direction, 1.0f);
				inputs.SunRadiance = glm::vec4(sun.Radiance, 1.0f);
			}
		}

		if (gpu.DrawItems.empty()) {
			if (frame.Params.w >= 0.0f) {
				wgpu::RenderPassDepthStencilAttachment depth{};
				depth.view = shadowView;
				depth.depthLoadOp = wgpu::LoadOp::Clear;
				depth.depthClearValue = 1.0f;
				depth.depthStoreOp = wgpu::StoreOp::Store;
				wgpu::RenderPassDescriptor desc{};
				desc.label = "renderer3d-empty-shadow-map";
				desc.depthStencilAttachment = &depth;
				encoder.BeginRenderPass(&desc).End();
			}
			return;
		}

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
		FUNKE_PROFILE_VALUE("Renderer3D.InstanceUploadBytes", float(instanceBytes));
		FUNKE_PROFILE_VALUE("Renderer3D.Lights", float(lighting.Selected.size()));
		if (Profiler::IsFrameCaptureEnabled()) {
			FUNKE_PROFILE_VALUE("Renderer3D.OffscreenCasters", float(std::count_if(gpu.DrawItems.begin(), gpu.DrawItems.end(),
				[](const DrawItem& item) { return KeyHidden(item.Key) && KeyCastsShadow(item.Key); })));
		}
		if (!EnsureBuffer(device, gpu.InstanceBuffer, gpu.InstanceCapacity, instanceBytes, wgpu::BufferUsage::Vertex, "renderer3d-instances")) {
			FNK_CORE_WARN_TAG("Renderer3D", "Instance buffer too large ({} bytes); meshes skipped", instanceBytes);
			return;
		}

		wgpu::BindGroup frameGroup = gpu.GetFrameBindGroup(device, shadowView);
		if (!frameGroup) return;
		{
			FUNKE_PROFILE_SCOPE("Renderer3D.SkinningDispatch");
			wgpu::ComputePassEncoder skinPass;
			for (const DrawItem& item : gpu.DrawItems) {
				const size_t mesh = KeyMesh(item.Key);
				if (mesh < gpu.FrameSkinning.size() && gpu.FrameSkinning[mesh].Entry) {
					gpu.UpdateSkinnedVertices(device, encoder, skinPass, gpu.FrameSkinning[mesh]);
				}
			}
			if (skinPass) {
				skinPass.End();
				if (gpu.SkinTrace) gpu.SkinTrace->Resolve();
				gpu.SkinTrace.reset();
			}
		}

		if (environment.FogEnabled && m_ViewOptions.Fog) {
			frame.FogColor = glm::vec4(Lighting3D::SrgbToLinear(environment.FogColor), 1.0f);
			frame.FogParams = glm::vec4(environment.FogStart, std::max(environment.FogEnd, environment.FogStart + 0.01f), 0.0f, 0.0f);
		}
		queue.WriteBuffer(gpu.FrameUniformBuffer, 0, &frame, sizeof(frame));
		if (!lighting.Selected.empty()) {
			queue.WriteBuffer(gpu.LightBuffer, 0, gpu.LightUpload.data(), lighting.Selected.size() * sizeof(GpuLight));
		}
		queue.WriteBuffer(gpu.InstanceBuffer, 0, gpu.InstanceUpload.data(), instanceBytes);

		if (frame.Params.w >= 0.0f) {
			FUNKE_PROFILE_SCOPE("Renderer3D.Shadows");
			queue.WriteBuffer(gpu.ShadowUniformBuffer, 0, glm::value_ptr(frame.LightViewProjection), sizeof(glm::mat4));
			wgpu::RenderPassDepthStencilAttachment shadowDepth{};
			shadowDepth.view = shadowView;
			shadowDepth.depthLoadOp = wgpu::LoadOp::Clear;
			shadowDepth.depthClearValue = 1.0f;
			shadowDepth.depthStoreOp = wgpu::StoreOp::Store;

			wgpu::RenderPassDescriptor shadowPassDesc{};
			shadowPassDesc.label = "renderer3d-shadows";
			shadowPassDesc.depthStencilAttachment = &shadowDepth;

			// Full map, not the cached viewport: the shadow map has its own resolution.
			GpuPassTrace passTrace(encoder, shadowPassDesc);
			wgpu::RenderPassEncoder shadowPass = encoder.BeginRenderPass(&shadowPassDesc);
			wgpu::RenderPipeline standardShadow = gpu.GetShadowPipeline(device);
			gpu.ShadowBundleDraws.clear();
			gpu.ShadowBundleDraws.reserve(gpu.DrawItems.size());
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
					gpu.ShadowBundleDraws.push_back({
						slot != 0 ? gpu.FrameCustoms[slot - 1].ShadowPipeline.Get() : standardShadow.Get(),
						slot != 0 ? gpu.CustomShadowFrameGroup.Get() : gpu.ShadowBindGroup.Get(),
						slot != 0 ? gpu.FrameCustoms[slot - 1].Group.Get() : nullptr,
						buffers.Vertices.Get(), buffers.Indices.Get(), buffers.IndexCount,
						static_cast<uint32_t>(end - begin), static_cast<uint32_t>(begin) });
					++m_DrawCalls;
					m_GeometryTotals.AddShadowMesh(buffers.IndexCount, static_cast<uint32_t>(end - begin));
				}
				begin = end;
			}
			gpu.ShadowDrawBundle.Submit(device, shadowPass,
				{ gpu.InstanceBuffer.Get(), wgpu::TextureFormat::Undefined, k_ShadowFormat, 1 },
				gpu.ShadowBundleDraws, Time::GetFrameCount());
			shadowPass.End();
			passTrace.Resolve();
		}

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
		GpuPassTrace passTrace(encoder, passDesc);

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		uint32_t drawnInstances = 0;
		if (wireframe) {
			pass.SetBindGroup(0, frameGroup);
			pass.SetVertexBuffer(1, gpu.InstanceBuffer);
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
			gpu.ColorBundleDraws.clear();
			gpu.TransparentDirectDraws.clear();
			gpu.ColorBundleDraws.reserve(transparentBegin);
			gpu.TransparentDirectDraws.reserve(gpu.DrawItems.size() - transparentBegin);
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
					auto& commands = begin >= transparentBegin ? gpu.TransparentDirectDraws : gpu.ColorBundleDraws;
					commands.push_back({ itemPipeline.Get(), frameGroup.Get(), materialGroup.Get(),
						buffers.Vertices.Get(), buffers.Indices.Get(), buffers.IndexCount,
						static_cast<uint32_t>(end - begin), static_cast<uint32_t>(begin) });
					++m_DrawCalls;
					m_GeometryTotals.AddMesh(buffers.IndexCount, buffers.VertexCount, static_cast<uint32_t>(end - begin));
				}
				begin = end;
			}
			const GpuState::BundlePassKey colorKey{ gpu.InstanceBuffer.Get(), target.ColorFormat, k_DepthFormat, target.SampleCount };
			gpu.ColorDrawBundle.Submit(device, pass, colorKey,
				gpu.ColorBundleDraws, Time::GetFrameCount());
			// ExecuteBundles resets bindings; a fresh encoder state binds every required resource again.
			// Transparent draws keep their existing far-to-near sequence after the opaque prefix.
			if (!gpu.TransparentDirectDraws.empty()) {
				GpuState::EncodeBundleDraws(GpuState::DirectDrawEncoder{ pass.Get() }, colorKey, gpu.TransparentDirectDraws);
			}
		}
		pass.End();
		passTrace.Resolve();
		m_RenderedMeshes += drawnInstances;
		if (!target.HasDepth) m_OwnDepthNeedsClear = false;

		gpu.MeshSubmitSerial = WebGPUBackend::GetFrameSubmitSerial();
		if (!gpu.FrameCustoms.empty()) WebGPUBackend::FlushCommands();
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
		if (gpu.ParticleSubmitSerial == WebGPUBackend::GetFrameSubmitSerial()) WebGPUBackend::FlushCommands();
		FUNKE_PROFILE_SCOPE("Renderer3D.Particles");

		const glm::vec3 origin = view.RenderOrigin();
        const glm::vec3 cameraPosition = view.RelativePosition(origin);
        const glm::mat4 viewProjection = view.Projection * view.RelativeView(origin);
		const FrustumPlanes frustum = ExtractFrustum(viewProjection);
		const glm::vec3 cameraRight(view.View[0][0], view.View[1][0], view.View[2][0]);
		const glm::vec3 cameraUp(view.View[0][1], view.View[1][1], view.View[2][1]);
		const glm::vec3 unitExtent = BillboardBoundsExtent(cameraRight, cameraUp);
		gpu.ParticleInstances.clear();
		gpu.ParticleBatches.clear();
		for (EntityHandle entity : systems) {
			const auto& system = systems.get<const ParticleSystem3DComponent>(entity);
			const auto particles = system.GetParticles();
			if (particles.empty()) continue;

			const auto* transform = registry.try_get<Transform3DComponent>(entity);
			glm::dmat4 preciseEmitter = transform ? Transform3DHierarchySystem::ComputeWorldMatrixPrecise(registry, entity) : glm::dmat4(1.0);
            preciseEmitter[3] -= glm::dvec4(origin, 0.0);
            const glm::mat4 emitterWorld(preciseEmitter);
			const bool local = system.SimulationSpace == ParticleSpace3D::Local;
			// Alpha blending needs back-to-front order inside the system; additive output is order independent.
			gpu.ParticleOrder.clear();
			for (uint32_t i = 0; i < particles.size(); ++i) {
				const auto& particle = particles[i];
				if (particle.Size <= 0.0f || particle.Color.a <= 0.0f) continue;
				const glm::vec3 position = local ? glm::vec3(emitterWorld * glm::vec4(particle.Position, 1.0f)) : particle.Position - origin;
				if (!IntersectsFrustum(frustum, position, unitExtent * particle.Size)) continue;
				const glm::vec3 toCamera = position - cameraPosition;
				gpu.ParticleOrder.emplace_back(glm::dot(toCamera, toCamera), i);
			}
			if (gpu.ParticleOrder.empty()) continue;
			if (system.BlendMode == ParticleBlendMode3D::Alpha) {
				std::sort(gpu.ParticleOrder.begin(), gpu.ParticleOrder.end(),
					[](const auto& a, const auto& b) { return a.first > b.first; });
			}

			const TextureHandle texture = system.Texture != 0 ? gpu.ResolveMaterialTexture(system.Texture) : TextureHandle{};
			const float softDot = TextureManager::IsValid(texture) ? 0.0f : 1.0f;
			ParticleBatch batch{ static_cast<uint32_t>(gpu.ParticleInstances.size()), 0, texture, system.BlendMode, 0.0f };
			for (const auto& [distanceSq, index] : gpu.ParticleOrder) {
				const ParticleSystem3DComponent::Particle& particle = particles[index];
				const glm::vec3 position = local ? glm::vec3(emitterWorld * glm::vec4(particle.Position, 1.0f)) : particle.Position - origin;
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
		frame.ViewProjection = viewProjection;
		// Rows of the view matrix are the camera's world axes.
		frame.CameraRight = glm::vec4(cameraRight, 0.0f);
		frame.CameraUp = glm::vec4(cameraUp, 0.0f);
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
		GpuPassTrace passTrace(encoder, passDesc);
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
		passTrace.Resolve();
		if (!target.HasDepth) m_OwnDepthNeedsClear = false;

		gpu.ParticleSubmitSerial = WebGPUBackend::GetFrameSubmitSerial();
		if (target.IsSwapChain) {
			WebGPUBackend::MarkSwapChainRendered();
		}
	}

	void Renderer3D::QueueGizmos(const Camera3DView& view, GizmoLayerMask layerMask) {
		const Vec3 origin = view.RenderOrigin();
		const bool seeThrough = m_LinesSeeThrough;
		m_LinesSeeThrough = false;
		for (const Line3D& line : GizmoRenderer2D::BuildLines3D(
			view.Projection * view.RelativeView(origin), layerMask, origin))
			DrawLine(line.Start, line.End, line.Color);
		m_LinesSeeThrough = seeThrough;
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
		if (!m_IsInitialized || !m_Gpu || halfLineCount <= 0 || !std::isfinite(spacing) || spacing <= 0.0f) return;
		GpuState& gpu = *m_Gpu;
		if (!gpu.GridShader) {
			gpu.GridShader = std::make_unique<Shader>(std::string("grid3d"), std::string("grid3d"));
			if (gpu.GridShader->IsValid()) gpu.GridModule = WebGPUBackend::LookupShader(gpu.GridShader->GetHandle()).Module;
		}
		if (!gpu.GridModule) return;
		const float extent = static_cast<float>(halfLineCount) * spacing;
		gpu.Grids.push_back({ { center.x, 0.0f, center.z, spacing }, { color.r, color.g, color.b, color.a }, { 0.0f, 0.0f, 0.0f, extent } });
	}

	void Renderer3D::FlushLines(const Camera3DView& view, bool depthTested) {
		if (m_Lines.empty() && m_SeeThroughLines.empty() && (!m_Gpu || m_Gpu->Grids.empty())) return;
		auto clearQueues = [this]() {
			m_Lines.clear();
			m_SeeThroughLines.clear();
			if (m_Gpu) m_Gpu->Grids.clear();
		};
		if (!m_IsInitialized || !m_Gpu) {
			clearQueues();
			return;
		}
		GpuState& gpu = *m_Gpu;
		if (gpu.LineSubmitSerial == WebGPUBackend::GetFrameSubmitSerial()) WebGPUBackend::FlushCommands();

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
		wgpu::RenderPipeline gridPipeline = !gpu.Grids.empty() ? gpu.GetLinePipeline(device, target.ColorFormat, samples, depthTested, true) : nullptr;
		const uint64_t bytes = m_LineUpload.size() * sizeof(PosColorVertex);
		const uint64_t gridBytes = gpu.Grids.size() * sizeof(GpuState::GridInstance);
		if (!depthView || !pipeline || (seeThroughCount > 0 && (!hiddenPipeline || !visiblePipeline))
			|| (bytes > 0 && !EnsureBuffer(device, gpu.LineVertexBuffer, gpu.LineVertexCapacity, bytes, wgpu::BufferUsage::Vertex, "renderer3d-lines"))
			|| (gridBytes > 0 && (!gridPipeline || !EnsureBuffer(device, gpu.GridInstanceBuffer, gpu.GridInstanceCapacity, gridBytes,
				wgpu::BufferUsage::Vertex, "renderer3d-grids")))) {
			clearQueues();
			return;
		}

		const glm::vec3 origin = view.RenderOrigin();
        for (auto& vertex : m_LineUpload) { vertex.x -= origin.x; vertex.y -= origin.y; vertex.z -= origin.z; }
        const glm::mat4 viewProjection = view.Projection * view.RelativeView(origin);
		queue.WriteBuffer(gpu.LineUniformBuffer, 0, glm::value_ptr(viewProjection), sizeof(glm::mat4));
		if (bytes > 0) queue.WriteBuffer(gpu.LineVertexBuffer, 0, m_LineUpload.data(), bytes);
		if (gridBytes > 0) {
			for (auto& grid : gpu.Grids) grid.OriginExtent = glm::vec4(origin, grid.OriginExtent.w);
			queue.WriteBuffer(gpu.GridInstanceBuffer, 0, gpu.Grids.data(), gridBytes);
		}

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
		GpuPassTrace passTrace(encoder, passDesc);

		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
		WebGPUBackend::ApplyCachedViewportToPass(pass);
		pass.SetBindGroup(0, gpu.LineBindGroup);
		if (gridBytes > 0) {
			pass.SetPipeline(gridPipeline);
			pass.SetVertexBuffer(0, gpu.GridInstanceBuffer, 0, gridBytes);
			pass.Draw(6, static_cast<uint32_t>(gpu.Grids.size()));
		}
		if (bytes > 0) pass.SetVertexBuffer(0, gpu.LineVertexBuffer, 0, bytes);
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
		passTrace.Resolve();
		clearQueues();
		if (!target.HasDepth) m_OwnDepthNeedsClear = false;

		gpu.LineSubmitSerial = WebGPUBackend::GetFrameSubmitSerial();
		if (target.IsSwapChain) {
			WebGPUBackend::MarkSwapChainRendered();
		}
	}

}
