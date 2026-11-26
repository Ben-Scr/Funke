#include "Gui/AssetPreview3DCache.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Graphics/CustomShaderLibrary.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/RenderApi.hpp"
#include "Graphics/Renderer3D.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Path.hpp"
#include "Gui/ThumbnailCache.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <imgui.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Funke {

	namespace {
		constexpr int k_Resolution = 256;
		constexpr size_t k_MaxEntries = 64;
		constexpr uint64_t k_StampCheckIntervalFrames = 60;

		uint64_t CurrentFrame() { return static_cast<uint64_t>(ImGui::GetFrameCount()); }

		bool FrameModel(const MeshAsset& asset, Transform3DComponent& transform, Camera3DView& view) {
			const glm::dvec3 boundsMin(asset.BoundsMin), boundsMax(asset.BoundsMax);
			const glm::dvec3 size = boundsMax - boundsMin;
			for (int axis = 0; axis < 3; ++axis) {
				if (!std::isfinite(boundsMin[axis]) || !std::isfinite(boundsMax[axis]) || size[axis] < 0.0) return false;
			}
			const double extent = std::max({ size.x, size.y, size.z });
			if (extent <= 0.0) return false;
			const float scale = static_cast<float>(1.0 / extent);
			if (!std::isfinite(scale) || scale <= 0.0f) return false;
			transform.Scale = Vec3(scale);
			transform.Position = glm::vec3(-(boundsMin + size * 0.5) / extent);

			const glm::vec3 direction = glm::normalize(glm::vec3(1.4f, 1.0f, 2.0f));
			const glm::mat3 rotation(glm::lookAtRH(direction, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
			const glm::vec3 halfSize(size / extent * 0.5);
			const float slope = std::tan(glm::radians(30.0f) * 0.5f) * 0.88f;
			float distance = 0.0f;
			for (int corner = 0; corner < 8; ++corner) {
				const glm::vec3 point = rotation * (halfSize * glm::vec3(
					(corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f, (corner & 4) ? 1.0f : -1.0f));
				// Fit every corner in perspective, including its depth relative to the model centre.
				distance = std::max(distance, point.z + std::max(std::abs(point.x), std::abs(point.y)) / slope);
			}
			view.Position = direction * distance;
			view.View = glm::lookAtRH(view.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			view.Projection = glm::perspectiveRH_ZO(glm::radians(30.0f), 1.0f, 0.01f, distance + 2.0f);
			return true;
		}
	}

	AssetPreview3DCache::AssetPreview3DCache() = default;
	AssetPreview3DCache::~AssetPreview3DCache() { Clear(); }

	uint64_t AssetPreview3DCache::GetThumbnail(const std::string& absolutePath) {
		Entry& entry = m_Entries[absolutePath];
		const uint64_t frame = CurrentFrame();
		entry.LastUsedFrame = frame;
		if (!entry.NeedsRender && frame - entry.LastStampCheckFrame >= k_StampCheckIntervalFrames) {
			entry.LastStampCheckFrame = frame;
			if (entry.Shader != 0) CustomShaderLibrary::GetDescription(entry.Shader);
			entry.NeedsRender = FileStamp::Read(absolutePath) != entry.Stamp;
			for (const auto& [path, stamp] : entry.Dependencies) {
				entry.NeedsRender |= FileStamp::Read(path) != stamp;
			}
			m_ReloadModels |= entry.NeedsRender;
		}
		if (entry.NeedsRender) m_Requests.push_back(absolutePath);
		return entry.Preview.GetColorTextureBackendId();
	}

	void AssetPreview3DCache::Update() {
		const uint32_t generation = CustomShaderLibrary::GetGeneration();
		if (generation != m_ShaderGeneration) {
			// A file change can reach the thumbnail before the shader hot-reload publishes its compiled version.
			m_ShaderGeneration = generation;
			MarkAllStale();
		}
		EnforceCapacity();
		if (m_ReloadModels) {
			// Imported images and external glTF buffers are owned by MeshLibrary, not TextureManager.
			MeshLibrary::ReloadChangedFromDisk();
			m_ReloadModels = false;
		}
		if (m_Renderer && CurrentFrame() % k_StampCheckIntervalFrames == 0) m_Renderer->PurgeUnusedMeshes();
		// Render before tiles submit their texture views to ImGui, at most one requested asset per frame.
		for (const std::string& path : m_Requests) {
			auto it = m_Entries.find(path);
			if (it == m_Entries.end() || !it->second.NeedsRender) continue;
			Render(path, it->second);
			break;
		}
		m_Requests.clear();
	}

	void AssetPreview3DCache::Render(const std::string& path, Entry& entry) {
		entry.NeedsRender = false;
		entry.Stamp = FileStamp::Read(path);
		entry.LastStampCheckFrame = CurrentFrame();
		const uint64_t guid = AssetRegistry::GetGUID(path);
		const bool isModel = ThumbnailCache::GetAssetType(Path::ToUtf8(Path::FromUtf8(path).extension())) == AssetType::Mesh;
		const MeshAsset* model = isModel ? MeshLibrary::Get(guid) : nullptr;
		const Material* material = isModel ? nullptr : MaterialLibrary::Get(guid);
		entry.Shader = material ? material->Shader : 0;
		entry.Dependencies.clear();
		Application* app = Application::GetInstance();
		if ((!material && !model) || !app || !app->GetRenderer3D()) {
			entry.Preview = Framebuffer{};
			return;
		}
		auto addDependency = [&](uint64_t asset) {
			if (asset == 0) return;
			const std::string dependency = AssetRegistry::ResolvePath(asset);
			if (dependency.empty()) return;
			entry.Dependencies.emplace_back(dependency, FileStamp::Read(dependency));
			entry.Dependencies.emplace_back(dependency + ".meta", FileStamp::Read(dependency + ".meta"));
		};
		entry.Dependencies.emplace_back(path + ".meta", FileStamp::Read(path + ".meta"));
		if (material) {
			addDependency(material->Shader);
			addDependency(material->BaseColorTexture);
			addDependency(material->NormalTexture);
			addDependency(material->MetallicRoughnessTexture);
			addDependency(material->EmissiveTexture);
			addDependency(material->OcclusionTexture);
			for (const auto& [name, value] : material->Properties) {
				if (value.Type == ShaderPropertyType::Texture) addDependency(value.Texture);
			}
			if (material->Shader != 0) {
				const ShaderDescription* shader = CustomShaderLibrary::GetDescription(material->Shader);
				if (shader && shader->Target != ShaderTarget::Mesh3D) {
					entry.Preview = Framebuffer{};
					return;
				}
			}
		}
		// A private renderer keeps scene exposure, debug overrides and statistics out of the thumbnails.
		if (!m_Renderer) {
			m_Renderer = std::make_unique<Renderer3D>();
			if (!m_Renderer->Initialize()) {
				m_Renderer->Shutdown();
				m_Renderer.reset();
				return;
			}
			RenderViewOptions options;
			options.Shadows = false;
			options.Fog = false;
			m_Renderer->SetViewOptions(options);
		}

		auto scene = Scene::CreateDetachedScene("##AssetPreview3D");
		auto& registry = scene->GetRegistry();
		const EntityHandle entity = scene->CreateEntityHandle();
		auto& transform = registry.emplace<Transform3DComponent>(entity);
		auto& mesh = registry.emplace<MeshRendererComponent>(entity);
		mesh.Mesh = PrimitiveMeshType::Sphere;
		mesh.Material = isModel ? 0 : guid;
		mesh.CustomMesh = isModel ? guid : 0;
		mesh.CastShadows = false;

		Camera3DView view;
		view.Position = glm::vec3(0.0f, 0.0f, 2.2f);
		view.View = glm::lookAtRH(view.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		view.Projection = glm::perspectiveRH_ZO(glm::radians(30.0f), 1.0f, 0.1f, 10.0f);
		if (model && !FrameModel(*model, transform, view)) {
			entry.Preview = Framebuffer{};
			return;
		}

		auto addLight = [&](const Vec3& rotation, const Color& color, float intensity) {
			const EntityHandle light = scene->CreateEntityHandle();
			registry.emplace<Transform3DComponent>(light).Rotation = rotation;
			auto& lighting = registry.emplace<DirectionalLightComponent>(light);
			lighting.Color = color;
			lighting.Intensity = intensity;
			lighting.Ambient = Color{ 0.0f, 0.0f, 0.0f, 1.0f };
			lighting.CastShadows = false;
		};
		addLight(Vec3{ -35.0f, -35.0f, 0.0f }, Color{ 1.0f, 0.95f, 0.88f, 1.0f }, 3.0f);
		addLight(Vec3{ 15.0f, 65.0f, 0.0f }, Color{ 0.75f, 0.85f, 1.0f, 1.0f }, 1.2f);
		Transform3DHierarchySystem::Propagate(*scene);

		// Keep the sky ambient/reflections while leaving the background transparent.
		view.Environment.Background = CameraBackground::Skybox;
		view.Environment.SkyTop = Color{ 0.85f, 0.88f, 0.95f, 1.0f };
		view.Environment.SkyHorizon = Color{ 0.65f, 0.65f, 0.65f, 1.0f };
		view.Environment.SkyGround = Color{ 0.12f, 0.13f, 0.16f, 1.0f };
		view.Environment.SkyAmbient = 0.8f;

		Framebuffer target;
		if (!target.Recreate(k_Resolution, k_Resolution)) return;
		const PolygonMode previousMode = RenderApi::GetPolygonMode();
		const Color previousClear = RenderApi::GetClearColor();
		RenderApi::SetPolygonMode(PolygonMode::Filled);
		RenderApi::BindFramebuffer(target);
		RenderApi::SetViewport(0, 0, k_Resolution, k_Resolution);
		RenderApi::SetClearColor(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
		m_Renderer->RenderScene(*scene, view);
		RenderApi::BindDefaultFramebuffer();
		if (Window* window = Application::GetWindow()) {
			RenderApi::SetViewport(0, 0, window->GetWidth(), window->GetHeight());
		}
		RenderApi::SetPolygonMode(previousMode);
		RenderApi::SetClearColor(previousClear);
		entry.Preview = std::move(target);
	}

	void AssetPreview3DCache::Invalidate(const std::string& absolutePath) {
		if (auto it = m_Entries.find(absolutePath); it != m_Entries.end()) it->second.NeedsRender = true;
		m_ReloadModels = true;
	}

	void AssetPreview3DCache::MarkAllStale() {
		for (auto& [path, entry] : m_Entries) entry.NeedsRender = true;
		m_ReloadModels = true;
	}

	void AssetPreview3DCache::Clear() {
		m_Requests.clear();
		m_Entries.clear();
		if (m_Renderer) m_Renderer->Shutdown();
		m_Renderer.reset();
	}

	void AssetPreview3DCache::EnforceCapacity() {
		const uint64_t frame = CurrentFrame();
		while (m_Entries.size() > k_MaxEntries) {
			auto oldest = m_Entries.end();
			for (auto it = m_Entries.begin(); it != m_Entries.end(); ++it) {
				// Last frame's visible tiles must survive until their queued previews render.
				if (it->second.LastUsedFrame + 1 >= frame) continue;
				if (oldest == m_Entries.end() || it->second.LastUsedFrame < oldest->second.LastUsedFrame) oldest = it;
			}
			if (oldest == m_Entries.end()) break;
			m_Entries.erase(oldest);
		}
	}
}
