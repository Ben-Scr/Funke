#include <pch.hpp>
#include "Gui/PrefabThumbnailCache.hpp"

#include "Collections/AABB.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Graphics/RenderApi.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/Renderer3D.hpp"
#include "Graphics/Text/Font.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/Text/TextRenderer.hpp"
#include "Gui/GuiRenderer.hpp"
#include "Gui/PrefabInspector.hpp"
#include "Math/VectorMath.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/EntityPicker3D.hpp"
#include "Scene/Scene.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Systems/UILayoutSystem.hpp"

#include <imgui.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Funke {

	namespace {
		constexpr uint64_t k_StampCheckIntervalFrames = 60;
		// A font that never finishes baking must not hold the queue forever.
		constexpr uint64_t k_MaxSettleFrames = 120;
		constexpr float k_FramePadding = 1.1f;
		constexpr float k_PreviewFill = 0.9f;
		// Narrower than the Editor View so small props aren't distorted; same three-quarter angle.
		constexpr float k_PreviewFieldOfView = 30.0f;
		constexpr float k_PreviewYawDegrees = -30.0f;
		constexpr float k_PreviewPitchDegrees = -25.0f;

		uint64_t CurrentFrame() {
			return static_cast<uint64_t>(ImGui::GetFrameCount());
		}

		struct PreviewFraming {
			bool Is3D = false;
			glm::mat4 ViewProjection{ 1.0f };
			AABB ViewAABB{};
			Camera3DView View3D;
		};

		// Text is not sized by its transform: measure the glyphs (with the fallback font while the real one bakes).
		AABB MeasureWorldText(TextRendererComponent& text, const Transform2DComponent& transform) {
			Font* font = text.Text.empty() ? nullptr : TextRenderer::ResolveFont(text);
			if (!font || !font->IsLoaded()) return AABB::FromTransform(transform);

			const float bakedSize = font->GetPixelSize() > 0.0f ? font->GetPixelSize() : text.FontSize;
			const float scale = std::abs((text.FontSize / bakedSize) * transform.Scale.x / k_TextPixelsPerWorldUnit);
			const Vec2 size = TextRenderer::MeasureNaturalSize(*font, text.Text, text.LetterSpacing, text.LineSpacing);
			const float width = size.x * scale;
			const float height = size.y * scale;
			const float x = transform.Position.x + text.Margin.x * scale;
			const float y = transform.Position.y - text.Margin.y * scale;
			const float left = text.HAlign == TextAlignment::Left ? x
				: text.HAlign == TextAlignment::Center ? x - width * 0.5f
				: x - width;
			// The block sits above, around or below the baseline depending on VAlign; cover all three.
			Vec2 corners[4] = {
				Vec2{ left, y - height }, Vec2{ left + width, y - height },
				Vec2{ left + width, y + height }, Vec2{ left, y + height },
			};
			AABB bounds{ Vec2{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
				Vec2{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() } };
			for (Vec2& corner : corners) {
				if (transform.Rotation != 0.0f) {
					corner = transform.Position + Vector::Rotated(corner - transform.Position, transform.Rotation);
				}
				bounds.Min.x = std::min(bounds.Min.x, corner.x);
				bounds.Min.y = std::min(bounds.Min.y, corner.y);
				bounds.Max.x = std::max(bounds.Max.x, corner.x);
				bounds.Max.y = std::max(bounds.Max.y, corner.y);
			}
			return bounds;
		}

		bool TryBuild2DBounds(Scene& scene, float uiPixelToWorld, AABB& outBounds) {
			bool any = false;
			auto include = [&](const AABB& bounds) {
				if (!any) {
					outBounds = bounds;
					any = true;
					return;
				}
				outBounds.Min.x = std::min(outBounds.Min.x, bounds.Min.x);
				outBounds.Min.y = std::min(outBounds.Min.y, bounds.Min.y);
				outBounds.Max.x = std::max(outBounds.Max.x, bounds.Max.x);
				outBounds.Max.y = std::max(outBounds.Max.y, bounds.Max.y);
			};

			auto& registry = scene.GetRegistry();
			for (auto [entity, transform] : registry.view<const Transform2DComponent>(entt::exclude<DisabledTag>).each()) {
				if (registry.all_of<SpriteRendererComponent>(entity)) {
					include(AABB::FromTransform(transform));
				}
				if (auto* text = registry.try_get<TextRendererComponent>(entity)) {
					include(MeasureWorldText(*text, transform));
				}
			}

			for (auto [entity, rect] : registry.view<const RectTransform2DComponent>(entt::exclude<DisabledTag>).each()) {
				const Vec2 bl = rect.GetBottomLeft();
				const Vec2 tr = rect.GetTopRight();
				const Vec2 pivot = rect.ResolvedValid ? rect.ResolvedPivot
					: Vec2{ (bl.x + tr.x) * 0.5f, (bl.y + tr.y) * 0.5f };
				Vec2 corners[4] = {
					Vec2{ bl.x * uiPixelToWorld, bl.y * uiPixelToWorld },
					Vec2{ tr.x * uiPixelToWorld, bl.y * uiPixelToWorld },
					Vec2{ tr.x * uiPixelToWorld, tr.y * uiPixelToWorld },
					Vec2{ bl.x * uiPixelToWorld, tr.y * uiPixelToWorld },
				};
				if (rect.Rotation != 0.0f) {
					const Vec2 worldPivot{ pivot.x * uiPixelToWorld, pivot.y * uiPixelToWorld };
					for (Vec2& corner : corners) {
						corner = worldPivot + Vector::Rotated(corner - worldPivot, rect.Rotation);
					}
				}
				AABB bounds{ corners[0], corners[0] };
				for (const Vec2& corner : corners) {
					bounds.Min.x = std::min(bounds.Min.x, corner.x);
					bounds.Min.y = std::min(bounds.Min.y, corner.y);
					bounds.Max.x = std::max(bounds.Max.x, corner.x);
					bounds.Max.y = std::max(bounds.Max.y, corner.y);
				}
				include(bounds);
			}
			return any;
		}

		bool TryBuild3DBounds(Scene& scene, glm::vec3& outMin, glm::vec3& outMax) {
			outMin = glm::vec3(std::numeric_limits<float>::max());
			outMax = glm::vec3(std::numeric_limits<float>::lowest());
			bool any = false;
			const auto& registry = scene.GetRegistry();
			for (auto [entity, transform] : registry.view<const Transform3DComponent>(entt::exclude<DisabledTag>).each()) {
				if (!registry.any_of<MeshRendererComponent, SkinnedMeshRendererComponent>(entity)) continue;
				const LocalBounds3D bounds = EntityPicker3D::GetLocalBounds(scene, entity);
				for (int corner = 0; corner < 8; ++corner) {
					const glm::vec3 local = bounds.Center + glm::vec3(
						(corner & 1) ? bounds.HalfExtents.x : -bounds.HalfExtents.x,
						(corner & 2) ? bounds.HalfExtents.y : -bounds.HalfExtents.y,
						(corner & 4) ? bounds.HalfExtents.z : -bounds.HalfExtents.z);
					const glm::vec3 world(transform.WorldMatrix * glm::vec4(local, 1.0f));
					outMin = glm::min(outMin, world);
					outMax = glm::max(outMax, world);
					any = true;
				}
			}
			return any;
		}

		Camera3DView MakeOrbitView(const glm::vec3& focus, float distance, float radius) {
			const glm::quat orientation = glm::angleAxis(glm::radians(k_PreviewYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(glm::radians(k_PreviewPitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
			glm::mat4 world = glm::mat4_cast(orientation);
			world[3] = glm::vec4(focus + orientation * glm::vec3(0.0f, 0.0f, distance), 1.0f);

			Camera3DView view;
			view.View = glm::inverse(world);
			const float nearClip = std::max(distance - radius * 2.0f, distance * 0.01f);
			view.Projection = glm::perspectiveRH_ZO(glm::radians(k_PreviewFieldOfView), 1.0f, nearClip, distance + radius * 2.0f);
			view.Position = glm::vec3(world[3]);
			return view;
		}

		// Moves the camera until the projected box fills k_PreviewFill of the tile, whatever the prefab's scale.
		Camera3DView FitOrbitView(const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
			glm::vec3 focus = (boundsMin + boundsMax) * 0.5f;
			const float radius = std::max(glm::length(boundsMax - boundsMin) * 0.5f, 1.0e-4f);
			const float halfFovTan = std::tan(glm::radians(k_PreviewFieldOfView) * 0.5f);
			float distance = radius / std::sin(glm::radians(k_PreviewFieldOfView) * 0.5f);
			Camera3DView view = MakeOrbitView(focus, distance, radius);

			for (int pass = 0; pass < 3; ++pass) {
				const glm::mat4 viewProjection = view.Projection * view.View;
				glm::vec2 ndcMin(std::numeric_limits<float>::max());
				glm::vec2 ndcMax(std::numeric_limits<float>::lowest());
				for (int corner = 0; corner < 8; ++corner) {
					const glm::vec3 point((corner & 1) ? boundsMax.x : boundsMin.x,
						(corner & 2) ? boundsMax.y : boundsMin.y,
						(corner & 4) ? boundsMax.z : boundsMin.z);
					const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
					if (clip.w <= 1.0e-5f) return view;
					ndcMin = glm::min(ndcMin, glm::vec2(clip) / clip.w);
					ndcMax = glm::max(ndcMax, glm::vec2(clip) / clip.w);
				}
				const float extent = std::max(ndcMax.x - ndcMin.x, ndcMax.y - ndcMin.y) * 0.5f;
				if (extent <= 1.0e-6f) return view;

				const glm::vec2 offset = (ndcMin + ndcMax) * 0.5f * distance * halfFovTan;
				const glm::mat4 world = glm::inverse(view.View);
				focus += glm::vec3(world[0]) * offset.x + glm::vec3(world[1]) * offset.y;
				distance *= extent / k_PreviewFill;
				view = MakeOrbitView(focus, distance, radius);
			}
			return view;
		}

		// False when the prefab draws nothing (logic-only prefabs keep their file icon).
		bool TryFramePrefab(Scene& scene, PreviewFraming& out) {
			TransformHierarchySystem::Propagate(scene);
			Transform3DHierarchySystem::Propagate(scene);
			ComputeUILayout(scene);

			const float uiPixelToWorld = GuiRenderer::ComputeWorldUIPixelScale();
			AABB bounds2D;
			const bool has2D = TryBuild2DBounds(scene, uiPixelToWorld, bounds2D);
			glm::vec3 min3D, max3D;
			// Without the Funke.3D module nothing draws the meshes.
			Application* app = Application::GetInstance();
			const bool has3D = app && app->GetRenderer3D() && TryBuild3DBounds(scene, min3D, max3D);
			if (!has2D && !has3D) return false;

			out.Is3D = has3D;
			if (has3D) {
				// Sprites and UI sit on the z = 0 plane next to the meshes.
				if (has2D) {
					min3D = glm::min(min3D, glm::vec3(bounds2D.Min.x, bounds2D.Min.y, 0.0f));
					max3D = glm::max(max3D, glm::vec3(bounds2D.Max.x, bounds2D.Max.y, 0.0f));
				}
				out.View3D = FitOrbitView(min3D, max3D);
				out.View3D.Environment.Background = CameraBackground::SolidColor;
				out.View3D.Environment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
				out.ViewProjection = out.View3D.Projection * out.View3D.View;
				out.ViewAABB = AABB{ Vec2{ -1.0e9f, -1.0e9f }, Vec2{ 1.0e9f, 1.0e9f } };
				return true;
			}

			const Vec2 center{ (bounds2D.Min.x + bounds2D.Max.x) * 0.5f, (bounds2D.Min.y + bounds2D.Max.y) * 0.5f };
			const Vec2 size = bounds2D.Scale();
			const float halfExtent = std::max(std::max(std::abs(size.x), std::abs(size.y)) * 0.5f * k_FramePadding, 0.01f);
			out.ViewProjection = glm::ortho(center.x - halfExtent, center.x + halfExtent,
				center.y - halfExtent, center.y + halfExtent, -1.0f, 1.0f);
			out.ViewAABB = AABB{ Vec2{ center.x - halfExtent, center.y - halfExtent },
				Vec2{ center.x + halfExtent, center.y + halfExtent } };
			return true;
		}

		void RenderPreview(Framebuffer& fbo, Scene& scene, const PreviewFraming& framing) {
			Application* app = Application::GetInstance();
			if (!app) return;
			Renderer2D* renderer2D = app->GetRenderer2D();
			Renderer3D* renderer3D = framing.Is3D ? app->GetRenderer3D() : nullptr;

			const PolygonMode previousPolygonMode = RenderApi::GetPolygonMode();
			RenderApi::SetPolygonMode(PolygonMode::Filled);
			RenderApi::SetColorMask(true, true, true, true);
			RenderApi::BindFramebuffer(fbo);
			RenderApi::SetViewport(0, 0, fbo.GetWidth(), fbo.GetHeight());
			RenderApi::SetClearColor(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
			RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);

			if (renderer3D) {
				renderer3D->RenderScene(scene, framing.View3D);
			}
			if (renderer2D) {
				// The game camera's post processing belongs to the game, not to an asset tile.
				renderer2D->SetPostProcessingEnabled(false);
				renderer2D->SetComposeOverTarget(renderer3D != nullptr);
				renderer2D->RenderSceneWithVP(scene, framing.ViewProjection, framing.ViewAABB);
				renderer2D->SetComposeOverTarget(false);
				renderer2D->SetPostProcessingEnabled(true);
			}
			if (GuiRenderer* gui = app->GetGuiRenderer()) {
				gui->RenderScene(scene, framing.ViewProjection, GuiRenderer::ComputeWorldUIPixelScale());
			}

			RenderApi::BindDefaultFramebuffer();
			if (Window* window = Application::GetWindow()) {
				RenderApi::SetViewport(0, 0, window->GetWidth(), window->GetHeight());
			}
			RenderApi::SetPolygonMode(previousPolygonMode);
		}

		// The text renderer draws the default font while a custom one still bakes; only the requested font is final.
		bool AreFontsReady(Scene& scene) {
			uint64_t projectDefault = 0;
			if (const FunkeProject* project = ProjectManager::GetCurrentProject()) {
				projectDefault = project->DefaultFontAssetId;
			}
			for (auto [entity, text] : scene.GetRegistry().view<const TextRendererComponent>().each()) {
				if (!FontManager::IsValid(text.ResolvedFont)) return false;
				uint64_t wanted = static_cast<uint64_t>(text.FontAssetId);
				if ((wanted == 0 || wanted == k_DefaultFontAssetId) && projectDefault != 0) {
					wanted = projectDefault;
				}
				if (wanted != 0 && wanted != k_DefaultFontAssetId
					&& FontManager::GetFontAssetUUID(text.ResolvedFont) != wanted) {
					return false;
				}
			}
			return true;
		}
	}

	struct PrefabThumbnailCache::PendingRender {
		std::string Path;
		std::unique_ptr<Scene> PrefabScene;
		Framebuffer Target;
		uint64_t StartFrame = 0;
	};

	PrefabThumbnailCache::PrefabThumbnailCache() = default;
	PrefabThumbnailCache::~PrefabThumbnailCache() = default;

	uint64_t PrefabThumbnailCache::GetThumbnail(const std::string& absolutePath) {
		const uint64_t frame = CurrentFrame();
		auto it = m_Entries.find(absolutePath);
		if (it == m_Entries.end()) {
			const auto lruIt = m_LRU.insert(m_LRU.begin(), absolutePath);
			it = m_Entries.try_emplace(absolutePath).first;
			it->second.LruIt = lruIt;
			it->second.LastUsedFrame = frame;
			it->second.LastStampCheckFrame = frame;
			EnforceCapacity();
		}
		else {
			m_LRU.splice(m_LRU.begin(), m_LRU, it->second.LruIt);
		}

		Entry& entry = it->second;
		entry.LastUsedFrame = frame;
		if (!entry.NeedsRender && frame - entry.LastStampCheckFrame >= k_StampCheckIntervalFrames) {
			entry.LastStampCheckFrame = frame;
			// Saved from Prefab edit mode or changed outside the editor: the old preview stays until the new one is done.
			entry.NeedsRender = FileStamp::Read(absolutePath) != entry.Stamp;
		}
		if (entry.NeedsRender) {
			m_Requests.push_back(absolutePath);
		}
		return entry.HasPreview ? entry.Preview.GetColorTextureBackendId() : 0;
	}

	void PrefabThumbnailCache::Update() {
		if (m_Pending) {
			StepPendingRender();
			m_Requests.clear();
			return;
		}

		for (const std::string& path : m_Requests) {
			auto it = m_Entries.find(path);
			if (it == m_Entries.end() || !it->second.NeedsRender) continue;
			StartRender(path, it->second);
			break;
		}
		m_Requests.clear();
	}

	void PrefabThumbnailCache::StartRender(const std::string& absolutePath, Entry& entry) {
		entry.NeedsRender = false;
		entry.Stamp = FileStamp::Read(absolutePath);
		entry.LastStampCheckFrame = CurrentFrame();

		auto pending = std::make_unique<PendingRender>();
		pending->Path = absolutePath;
		pending->StartFrame = CurrentFrame();
		const EntityHandle root = LoadPrefabSourceIntoDetachedScene(absolutePath, "##PrefabThumbnail",
			"PrefabThumbnail", pending->PrefabScene);
		if (root == entt::null || !pending->Target.Recreate(k_Resolution, k_Resolution)) {
			entry.Preview = Framebuffer{};
			entry.HasPreview = false;
			return;
		}
		m_Pending = std::move(pending);
		StepPendingRender();
	}

	void PrefabThumbnailCache::StepPendingRender() {
		auto it = m_Entries.find(m_Pending->Path);
		if (it == m_Entries.end()) {
			m_Pending.reset();
			return;
		}

		Entry& entry = it->second;
		Scene& scene = *m_Pending->PrefabScene;
		PreviewFraming framing;
		if (!TryFramePrefab(scene, framing)) {
			entry.Preview = Framebuffer{};
			entry.HasPreview = false;
			m_Pending.reset();
			return;
		}

		// Rendering also requests the fonts, so their bakes start on the first pass.
		RenderPreview(m_Pending->Target, scene, framing);
		const bool timedOut = CurrentFrame() - m_Pending->StartFrame >= k_MaxSettleFrames;
		if (!AreFontsReady(scene) && !timedOut) return;

		entry.Preview = std::move(m_Pending->Target);
		entry.HasPreview = true;
		m_Pending.reset();
	}

	void PrefabThumbnailCache::Invalidate(const std::string& absolutePath) {
		auto it = m_Entries.find(absolutePath);
		if (it == m_Entries.end()) return;
		m_LRU.erase(it->second.LruIt);
		m_Entries.erase(it);
	}

	void PrefabThumbnailCache::MarkAllStale() {
		if (m_Pending) {
			if (auto it = m_Entries.find(m_Pending->Path); it != m_Entries.end()) {
				it->second.NeedsRender = true;
			}
			m_Pending.reset();
		}
		for (auto& [path, entry] : m_Entries) {
			// A prefab that failed to load or draws nothing only changes with its own file.
			entry.NeedsRender = entry.NeedsRender || entry.HasPreview;
		}
	}

	void PrefabThumbnailCache::Clear() {
		m_Pending.reset();
		m_Requests.clear();
		m_Entries.clear();
		m_LRU.clear();
	}

	void PrefabThumbnailCache::EnforceCapacity() {
		const uint64_t frame = CurrentFrame();
		auto it = m_LRU.end();
		while (m_Entries.size() > k_MaxEntries && it != m_LRU.begin()) {
			--it;
			auto entryIt = m_Entries.find(*it);
			if (entryIt != m_Entries.end() && entryIt->second.LastUsedFrame == frame) {
				continue;
			}
			if (entryIt != m_Entries.end()) {
				m_Entries.erase(entryIt);
			}
			it = m_LRU.erase(it);
		}
	}

}
