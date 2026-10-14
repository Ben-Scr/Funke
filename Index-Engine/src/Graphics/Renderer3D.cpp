#include "pch.hpp"
#include "Graphics/Renderer3D.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Tags.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/Lighting3D.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/PrimitiveMesh.hpp"
#include "Graphics/RenderApi.hpp"
#include "Graphics/Shader.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"
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
#include <unordered_map>

namespace Index {
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
		};
		static_assert(sizeof(FrameUniforms) == 240);

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
		};

		uint64_t MakeDrawKey(size_t mesh, bool castsShadow, uint32_t textureSlot) {
			return (static_cast<uint64_t>(mesh) << 40) | (static_cast<uint64_t>(castsShadow ? 0 : 1) << 32) | textureSlot;
		}
		size_t KeyMesh(uint64_t key) { return static_cast<size_t>(key >> 40); }
		bool KeyCastsShadow(uint64_t key) { return ((key >> 32) & 0xFF) == 0; }
		uint32_t KeyTextureSlot(uint64_t key) { return static_cast<uint32_t>(key & 0xFFFFFFFFu); }

		void SetAttribute(wgpu::VertexAttribute& attribute, wgpu::VertexFormat format, uint64_t offset, uint32_t location) {
			attribute.format = format;
			attribute.offset = offset;
			attribute.shaderLocation = location;
		}

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

		wgpu::Sampler CreateRepeatSampler(wgpu::Device device, wgpu::FilterMode filter, const char* label) {
			wgpu::SamplerDescriptor desc{};
			desc.addressModeU = wgpu::AddressMode::Repeat;
			desc.addressModeV = wgpu::AddressMode::Repeat;
			desc.addressModeW = wgpu::AddressMode::Repeat;
			desc.magFilter = filter;
			desc.minFilter = filter;
			desc.mipmapFilter = filter == wgpu::FilterMode::Linear ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest;
			desc.maxAnisotropy = 1;
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
		return environment;
	}

	struct Renderer3D::GpuState {
		struct MeshBuffers {
			wgpu::Buffer Vertices;
			wgpu::Buffer Indices;
			uint32_t IndexCount = 0;
		};

		struct CustomMeshEntry {
			MeshBuffers Buffers;
			uint32_t Revision = 0;
			uint64_t VertexCapacity = 0;
			uint64_t IndexCapacity = 0;
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
		wgpu::BindGroupLayout LineLayout;
		wgpu::PipelineLayout MeshPipelineLayout;
		wgpu::PipelineLayout SkyPipelineLayout;
		wgpu::PipelineLayout ShadowPipelineLayout;
		wgpu::PipelineLayout LinePipelineLayout;
		wgpu::BindGroupLayout ParticleLayout;
		wgpu::PipelineLayout ParticlePipelineLayout;
		wgpu::Buffer ParticleUniformBuffer;
		wgpu::BindGroup ParticleBindGroup;
		wgpu::Buffer ParticleInstanceBuffer;
		uint64_t ParticleInstanceCapacity = 0;
		std::unordered_map<uint32_t, wgpu::RenderPipeline> ParticlePipelines;
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
		wgpu::Sampler LinearRepeatSampler;
		wgpu::Sampler PointRepeatSampler;
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
		std::unordered_map<uint64_t, CustomMeshEntry> CustomMeshes;
		std::vector<const MeshBuffers*> FrameMeshes;
		std::unordered_map<uint64_t, uint32_t> FrameMeshSlots;
		std::unordered_map<uint32_t, wgpu::RenderPipeline> MeshPipelines;
		std::unordered_map<uint32_t, wgpu::RenderPipeline> SkyPipelines;
		std::unordered_map<uint32_t, wgpu::RenderPipeline> LinePipelines;
		wgpu::RenderPipeline ShadowPipeline;
		std::unordered_map<uint64_t, CachedMaterialGroup> MaterialGroups;

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

		wgpu::BindGroup GetMaterialBindGroup(wgpu::Device device, TextureHandle handle) {
			Texture2D* texture = TextureManager::GetTexture(handle);
			if (!texture || !texture->IsValid()) return nullptr;
			const auto lookup = WebGPUBackend::LookupTexture2D(texture->GetHandle());
			if (!lookup.Valid || !lookup.View) return nullptr;
			const bool point = texture->GetFilter() == Filter::Point;
			const uint64_t key = (texture->GetHandle() << 1) | (point ? 1u : 0u);

			if (auto it = MaterialGroups.find(key); it != MaterialGroups.end()) {
				if (it->second.View.Get() == lookup.View.Get()) return it->second.Group;
				MaterialGroups.erase(it);
			}

			wgpu::BindGroupEntry entries[2] = {};
			entries[0].binding = 0;
			entries[0].textureView = lookup.View;
			entries[1].binding = 1;
			entries[1].sampler = point ? PointRepeatSampler : LinearRepeatSampler;

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
		const MeshBuffers* ResolveCustomMesh(wgpu::Device device, uint64_t guid, const MeshAsset& asset) {
			CustomMeshEntry& entry = CustomMeshes[guid];
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
				entry.Revision = asset.Revision;
			}
			return entry.Buffers.Vertices && entry.Buffers.Indices ? &entry.Buffers : nullptr;
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
		wgpu::RenderPipeline GetMeshPipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, bool transparent) {
			const uint32_t key = static_cast<uint32_t>(colorFormat) | (transparent ? 0x80000000u : 0u);
			if (auto it = MeshPipelines.find(key); it != MeshPipelines.end()) {
				return it->second;
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
			desc.vertex.buffers = buffers;
			desc.fragment = &fragment;
			desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
			desc.primitive.frontFace = wgpu::FrontFace::CCW;
			desc.primitive.cullMode = wgpu::CullMode::Back;
			desc.depthStencil = &depth;
			desc.multisample.count = 1;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (pipeline) MeshPipelines.emplace(key, pipeline);
			return pipeline;
		}

		// Unlit billboards: depth tested against the meshes but never written, so overlapping particles all show.
		wgpu::RenderPipeline GetParticlePipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, ParticleBlendMode3D blendMode) {
			const bool additive = blendMode == ParticleBlendMode3D::Additive;
			const uint32_t key = static_cast<uint32_t>(colorFormat) | (additive ? 0x80000000u : 0u);
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
			desc.multisample.count = 1;
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

		wgpu::RenderPipeline GetSkyPipeline(wgpu::Device device, wgpu::TextureFormat colorFormat) {
			const uint32_t key = static_cast<uint32_t>(colorFormat);
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
			desc.multisample.count = 1;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (pipeline) SkyPipelines.emplace(key, pipeline);
			return pipeline;
		}

		wgpu::RenderPipeline GetLinePipeline(wgpu::Device device, wgpu::TextureFormat colorFormat, bool depthTested) {
			const uint32_t key = (static_cast<uint32_t>(colorFormat) << 1) | (depthTested ? 1u : 0u);
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
			desc.multisample.count = 1;
			desc.multisample.mask = 0xFFFFFFFF;

			wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
			if (pipeline) LinePipelines.emplace(key, pipeline);
			return pipeline;
		}
	};

	Renderer3D::Renderer3D() = default;
	Renderer3D::~Renderer3D() = default;

	bool Renderer3D::Initialize() {
		if (m_IsInitialized) return true;
		if (!WebGPUBackend::IsInitialized()) {
			IDX_CORE_ERROR_TAG("Renderer3D", "Initialize called before the WebGPU backend");
			return false;
		}

		m_MeshShader = std::make_unique<Shader>(std::string("IndexAssets/Shaders/MeshShader.vs"), std::string("IndexAssets/Shaders/MeshShader.fs"));
		m_SkyShader = std::make_unique<Shader>(std::string("IndexAssets/Shaders/SkyShader.vs"), std::string("IndexAssets/Shaders/SkyShader.fs"));
		m_ShadowShader = std::make_unique<Shader>(std::string("IndexAssets/Shaders/Shadow3DShader.vs"), std::string("IndexAssets/Shaders/Shadow3DShader.fs"));
		m_LineShader = std::make_unique<Shader>(std::string("IndexAssets/Shaders/GizmoShader.vs"), std::string("IndexAssets/Shaders/GizmoShader.fs"));
		auto lookup = [](const std::unique_ptr<Shader>& shader) {
			return shader->IsValid() ? WebGPUBackend::LookupShader(shader->GetHandle()) : WebGPUBackend::ShaderLookup{};
		};
		const auto meshLookup = lookup(m_MeshShader);
		const auto skyLookup = lookup(m_SkyShader);
		const auto shadowLookup = lookup(m_ShadowShader);
		const auto lineLookup = lookup(m_LineShader);
		if (!meshLookup.Valid || !skyLookup.Valid || !shadowLookup.Valid || !lineLookup.Valid) {
			IDX_CORE_ERROR_TAG("Renderer3D", "A 3D shader failed to load; 3D rendering disabled");
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

		const wgpu::BindGroupLayout meshLayouts[2] = { gpu.FrameLayout, gpu.MaterialLayout };
		gpu.MeshPipelineLayout = CreatePipelineLayout(device, meshLayouts, 2, "renderer3d-mesh-layout");
		gpu.SkyPipelineLayout = CreatePipelineLayout(device, &gpu.SkyLayout, 1, "renderer3d-sky-layout");
		gpu.ShadowPipelineLayout = CreatePipelineLayout(device, &gpu.ShadowLayout, 1, "renderer3d-shadow-layout");
		gpu.LinePipelineLayout = CreatePipelineLayout(device, &gpu.LineLayout, 1, "renderer3d-line-layout");

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
		gpu.LinearRepeatSampler = CreateRepeatSampler(device, wgpu::FilterMode::Linear, "renderer3d-linear-repeat");
		gpu.PointRepeatSampler = CreateRepeatSampler(device, wgpu::FilterMode::Nearest, "renderer3d-point-repeat");
		gpu.DummyShadowTexture = gpu.CreateShadowTexture(device, 1, "renderer3d-shadow-dummy");
		gpu.DummyShadowView = gpu.DummyShadowTexture ? gpu.DummyShadowTexture.CreateView() : nullptr;

		if (!gpu.MeshPipelineLayout || !gpu.SkyPipelineLayout || !gpu.ShadowPipelineLayout || !gpu.LinePipelineLayout
			|| !gpu.FrameUniformBuffer || !gpu.LightBuffer || !gpu.SkyUniformBuffer || !gpu.ShadowUniformBuffer || !gpu.LineUniformBuffer
			|| !gpu.ShadowSampler || !gpu.LinearRepeatSampler || !gpu.PointRepeatSampler || !gpu.DummyShadowView) {
			IDX_CORE_ERROR_TAG("Renderer3D", "Failed to create GPU layouts, buffers or samplers");
			m_Gpu.reset();
			return false;
		}
		gpu.SkyBindGroup = CreateUniformBindGroup(device, gpu.SkyLayout, gpu.SkyUniformBuffer, sizeof(SkyUniforms), "renderer3d-sky-bg");
		gpu.ShadowBindGroup = CreateUniformBindGroup(device, gpu.ShadowLayout, gpu.ShadowUniformBuffer, sizeof(glm::mat4), "renderer3d-shadow-bg");
		gpu.LineBindGroup = CreateUniformBindGroup(device, gpu.LineLayout, gpu.LineUniformBuffer, sizeof(glm::mat4), "renderer3d-line-bg");

		m_ParticleShader = std::make_unique<Shader>(std::string("IndexAssets/Shaders/Particle3DShader.vs"), std::string("IndexAssets/Shaders/Particle3DShader.fs"));
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
			IDX_CORE_WARN_TAG("Renderer3D", "Particle shader or resources unavailable; ParticleSystem3D will not draw");
			gpu.ParticleModule = nullptr;
		}

		for (size_t i = 0; i < k_MeshTypeCount; ++i) {
			const PrimitiveMeshData data = BuildPrimitiveMesh(static_cast<PrimitiveMeshType>(i));
			auto& mesh = gpu.Meshes[i];
			mesh.Vertices = CreateBufferWithData(device, wgpu::BufferUsage::Vertex, data.Vertices.data(), data.Vertices.size() * sizeof(MeshVertex), "renderer3d-mesh-vb");
			mesh.Indices = CreateBufferWithData(device, wgpu::BufferUsage::Index, data.Indices.data(), data.Indices.size() * sizeof(uint32_t), "renderer3d-mesh-ib");
			mesh.IndexCount = static_cast<uint32_t>(data.Indices.size());
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
		m_Gpu.reset();
		m_MeshShader.reset();
		m_SkyShader.reset();
		m_ShadowShader.reset();
		m_LineShader.reset();
		m_ParticleShader.reset();
		m_Lines.clear();
		MaterialLibrary::Clear();
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
		RenderSky(view, cameraScene);
		m_SceneProvider([&](Scene& scene) {
			RenderScene(scene, view);
		});
		m_RenderedThisFrame = true;
		return true;
	}

	void Renderer3D::RenderSky(const Camera3DView& view, Scene* lightScene) {
		if (!m_IsInitialized || !m_Gpu || view.Environment.Background != CameraBackground::Skybox) return;
		INDEX_PROFILE_SCOPE("Renderer3D.Sky");
		GpuState& gpu = *m_Gpu;

		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!target.Valid) return;
		wgpu::Device device = WebGPUBackend::GetDevice();
		wgpu::Queue queue = WebGPUBackend::GetQueue();
		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!device || !queue || !encoder) return;
		wgpu::RenderPipeline pipeline = gpu.GetSkyPipeline(device, target.ColorFormat);
		if (!pipeline) return;

		const Camera3DEnvironment& environment = view.Environment;
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
		INDEX_PROFILE_SCOPE("Renderer3D.RenderScene");
		Transform3DHierarchySystem::Propagate(scene);
		RenderMeshes(scene, view);
		RenderParticles(scene, view);
	}

	void Renderer3D::RenderMeshes(Scene& scene, const Camera3DView& view) {
		GpuState& gpu = *m_Gpu;
		const entt::registry& registry = scene.GetRegistry();
		wgpu::Device device = WebGPUBackend::GetDevice();
		if (!device) return;

		gpu.Instances.clear();
		gpu.DrawItems.clear();
		gpu.FrameTextures.clear();
		gpu.FrameTextureSlots.clear();
		gpu.FrameMeshes.clear();
		gpu.FrameMeshSlots.clear();
		std::erase_if(gpu.CustomMeshes, [](const auto& pair) { return IsRuntimeMeshGuid(pair.first) && !MeshLibrary::IsLiveRuntime(pair.first); });
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

		const auto meshes = registry.view<const MeshRendererComponent, const Transform3DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : meshes) {
			const auto& renderer = meshes.get<const MeshRendererComponent>(entity);
			const auto& transform = meshes.get<const Transform3DComponent>(entity);
			size_t meshIndex = static_cast<size_t>(renderer.Mesh);
			glm::vec3 localCenter(0.0f);
			if (renderer.CustomMesh != 0) {
				auto slot = gpu.FrameMeshSlots.find(renderer.CustomMesh);
				const MeshAsset* asset = MeshLibrary::Get(renderer.CustomMesh);
				if (!asset) continue;
				if (slot == gpu.FrameMeshSlots.end()) {
					const auto* buffers = gpu.ResolveCustomMesh(device, renderer.CustomMesh, *asset);
					if (!buffers) continue;
					slot = gpu.FrameMeshSlots.emplace(renderer.CustomMesh, static_cast<uint32_t>(gpu.FrameMeshes.size())).first;
					gpu.FrameMeshes.push_back(buffers);
				}
				meshIndex = slot->second;
				localCenter = (asset->BoundsMin + asset->BoundsMax) * 0.5f;
			}
			else if (meshIndex >= k_MeshTypeCount) {
				continue;
			}

			Material fallback;
			fallback.Lit = renderer.Lit;
			const Material* material = MaterialLibrary::Get(renderer.Material);
			if (!material) material = &fallback;

			MeshInstance instance{};
			for (int i = 0; i < 4; ++i) instance.Model[i] = transform.WorldMatrix[i];
			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform.WorldMatrix)));
			for (int i = 0; i < 3; ++i) instance.Normal[i] = glm::vec4(normalMatrix[i], 0.0f);
			instance.Color = ToVec4(material->BaseColor) * ToVec4(renderer.Color);
			instance.Emissive = glm::vec4(Lighting3D::SrgbToLinear(material->EmissiveColor) * std::max(material->EmissiveIntensity, 0.0f), 0.0f);
			const bool transparent = material->Surface == MaterialSurface::Transparent;
			instance.Surface = { material->Metallic, material->Roughness, material->Lit ? 1.0f : 0.0f, transparent ? 1.0f : 0.0f };
			instance.UvTransform = { material->Tiling.x, material->Tiling.y, material->Offset.x, material->Offset.y };

			const uint32_t slot = textureSlot(gpu.ResolveMaterialTexture(material->BaseColorTexture));
			const glm::vec3 toCamera = glm::vec3(transform.WorldMatrix * glm::vec4(localCenter, 1.0f)) - view.Position;
			gpu.DrawItems.push_back({ MakeDrawKey(meshIndex, renderer.CastShadows, slot), static_cast<uint32_t>(gpu.Instances.size()),
				glm::dot(toCamera, toCamera), transparent });
			gpu.Instances.push_back(instance);
			anyCaster |= renderer.CastShadows;
		}
		if (gpu.Instances.empty()) return;

		const auto target = WebGPUBackend::BeginRenderToCurrentTarget();
		if (!target.Valid) return;

		wgpu::Queue queue = WebGPUBackend::GetQueue();
		wgpu::CommandEncoder encoder = WebGPUBackend::GetFrameEncoder();
		if (!queue || !encoder) return;

		// Opaque items batch by key; transparent ones follow, farthest first, so blending composites correctly.
		const auto firstTransparent = std::partition(gpu.DrawItems.begin(), gpu.DrawItems.end(),
			[](const DrawItem& item) { return !item.Transparent; });
		const size_t transparentBegin = static_cast<size_t>(firstTransparent - gpu.DrawItems.begin());
		std::sort(gpu.DrawItems.begin(), firstTransparent, [](const DrawItem& a, const DrawItem& b) {
			return a.Key < b.Key;
		});
		std::sort(firstTransparent, gpu.DrawItems.end(), [](const DrawItem& a, const DrawItem& b) {
			if (a.ViewDistanceSq != b.ViewDistanceSq) return a.ViewDistanceSq > b.ViewDistanceSq;
			return a.Key < b.Key;
		});

		wgpu::TextureView depthView = target.HasDepth ? target.DepthView : gpu.EnsureOwnDepth(device, target.Width, target.Height);
		wgpu::RenderPipeline pipeline = gpu.GetMeshPipeline(device, target.ColorFormat, false);
		wgpu::RenderPipeline transparentPipeline = transparentBegin < gpu.DrawItems.size()
			? gpu.GetMeshPipeline(device, target.ColorFormat, true) : nullptr;
		if (!depthView || !pipeline) return;
		gpu.InstanceUpload.clear();
		gpu.InstanceUpload.reserve(gpu.DrawItems.size());
		for (const DrawItem& item : gpu.DrawItems) gpu.InstanceUpload.push_back(gpu.Instances[item.Instance]);

		const uint64_t instanceBytes = gpu.InstanceUpload.size() * sizeof(MeshInstance);
		if (!EnsureBuffer(device, gpu.InstanceBuffer, gpu.InstanceCapacity, instanceBytes, wgpu::BufferUsage::Vertex, "renderer3d-instances")) {
			IDX_CORE_WARN_TAG("Renderer3D", "Instance buffer too large ({} bytes); meshes skipped", instanceBytes);
			return;
		}

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
		if (shadowResolution > 0 && lighting.ShadowedLight >= 0 && anyCaster) {
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

		queue.WriteBuffer(gpu.FrameUniformBuffer, 0, &frame, sizeof(frame));
		queue.WriteBuffer(gpu.LightBuffer, 0, gpu.LightUpload.data(), sizeof(LightUniforms));
		queue.WriteBuffer(gpu.InstanceBuffer, 0, gpu.InstanceUpload.data(), instanceBytes);

		if (frame.Params.w >= 0.0f) {
			INDEX_PROFILE_SCOPE("Renderer3D.Shadows");
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
			shadowPass.SetPipeline(gpu.GetShadowPipeline(device));
			shadowPass.SetBindGroup(0, gpu.ShadowBindGroup);
			shadowPass.SetVertexBuffer(1, gpu.InstanceBuffer);
			for (size_t begin = 0; begin < gpu.DrawItems.size();) {
				const size_t mesh = KeyMesh(gpu.DrawItems[begin].Key);
				const bool casts = KeyCastsShadow(gpu.DrawItems[begin].Key);
				size_t end = begin + 1;
				while (end < gpu.DrawItems.size() && KeyMesh(gpu.DrawItems[end].Key) == mesh
					&& KeyCastsShadow(gpu.DrawItems[end].Key) == casts) {
					++end;
				}
				const auto& buffers = *gpu.FrameMeshes[mesh];
				if (casts && buffers.Vertices && buffers.Indices) {
					shadowPass.SetVertexBuffer(0, buffers.Vertices);
					shadowPass.SetIndexBuffer(buffers.Indices, wgpu::IndexFormat::Uint32);
					shadowPass.DrawIndexed(buffers.IndexCount, static_cast<uint32_t>(end - begin), 0, 0, static_cast<uint32_t>(begin));
					++m_DrawCalls;
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
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, frameGroup);
		pass.SetVertexBuffer(1, gpu.InstanceBuffer);
		size_t boundMesh = std::numeric_limits<size_t>::max();
		for (size_t begin = 0; begin < gpu.DrawItems.size();) {
			if (begin == transparentBegin) {
				if (!transparentPipeline) break;
				pass.SetPipeline(transparentPipeline);
			}
			const uint64_t key = gpu.DrawItems[begin].Key;
			const size_t mesh = KeyMesh(key);
			const uint32_t slot = KeyTextureSlot(key);
			// Batches never straddle the opaque/transparent boundary: the pipeline switches there.
			const size_t rangeEnd = begin < transparentBegin ? transparentBegin : gpu.DrawItems.size();
			size_t end = begin + 1;
			while (end < rangeEnd && KeyMesh(gpu.DrawItems[end].Key) == mesh && KeyTextureSlot(gpu.DrawItems[end].Key) == slot) {
				++end;
			}
			const auto& buffers = *gpu.FrameMeshes[mesh];
			wgpu::BindGroup materialGroup = gpu.GetMaterialBindGroup(device, gpu.FrameTextures[slot]);
			if (!materialGroup) materialGroup = gpu.GetMaterialBindGroup(device, whiteTexture);
			if (materialGroup && buffers.Vertices && buffers.Indices) {
				if (boundMesh != mesh) {
					pass.SetVertexBuffer(0, buffers.Vertices);
					pass.SetIndexBuffer(buffers.Indices, wgpu::IndexFormat::Uint32);
					boundMesh = mesh;
				}
				pass.SetBindGroup(1, materialGroup);
				pass.DrawIndexed(buffers.IndexCount, static_cast<uint32_t>(end - begin), 0, 0, static_cast<uint32_t>(begin));
				++m_DrawCalls;
			}
			begin = end;
		}
		pass.End();
		m_RenderedMeshes += static_cast<uint32_t>(gpu.Instances.size());
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
		INDEX_PROFILE_SCOPE("Renderer3D.Particles");

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
			IDX_CORE_WARN_TAG("Renderer3D", "Particle buffer too large ({} bytes); particles skipped", instanceBytes);
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
			wgpu::RenderPipeline pipeline = gpu.GetParticlePipeline(device, target.ColorFormat, batch.Blend);
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
		wgpu::RenderPipeline pipeline = gpu.GetLinePipeline(device, target.ColorFormat, depthTested);
		wgpu::RenderPipeline hiddenPipeline = seeThroughCount > 0 ? gpu.GetLinePipeline(device, target.ColorFormat, false) : nullptr;
		wgpu::RenderPipeline visiblePipeline = seeThroughCount > 0 ? gpu.GetLinePipeline(device, target.ColorFormat, true) : nullptr;
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
