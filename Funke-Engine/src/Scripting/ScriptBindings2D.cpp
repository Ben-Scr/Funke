#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/ScriptGlue.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Collections/Viewport.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TerrainRenderer2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/Window.hpp"
#include "Graphics/TextureManager.hpp"
#include "Scene/EntityPicker.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Systems/TransformHierarchySystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace Funke {
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);
	bool TryResolveEntityByUUID(const Scene& scene, uint64_t entityID, EntityHandle& outHandle);
	EntityHandle ToEntityHandle(uint64_t id);
	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle);
	Scene* GetScene();

	uint64_t FromEntityHandle(EntityHandle handle);

	// MUST swallow exceptions here: bindings are called via [UnmanagedCallersOnly] and unwinding
	// through CoreCLR frames is UB. Allocations (std::string, JSON parse, etc.) can throw std::bad_alloc.
	#define FNK_BINDING_TRY try
	#define FNK_BINDING_CATCH(default_return)                                                  \
		catch (const std::exception& _fnk_ex) {                                                \
			FNK_CORE_ERROR_TAG("ScriptBinding", "exception in {}: {}", __func__, _fnk_ex.what()); \
			return default_return;                                                             \
		}                                                                                      \
		catch (...) {                                                                          \
			FNK_CORE_ERROR_TAG("ScriptBinding", "unknown exception in {}", __func__);          \
			return default_return;                                                             \
		}
	// Void-return variant (no return statement in catch).
	#define FNK_BINDING_CATCH_VOID                                                             \
		catch (const std::exception& _fnk_ex) {                                                \
			FNK_CORE_ERROR_TAG("ScriptBinding", "exception in {}: {}", __func__, _fnk_ex.what()); \
		}                                                                                      \
		catch (...) {                                                                          \
			FNK_CORE_ERROR_TAG("ScriptBinding", "unknown exception in {}", __func__);          \
		}

	static int CopyStringToBuffer(std::string_view value, char* outBuffer, int capacity)
	{
		const int requiredBytes = static_cast<int>(std::min(
			value.size(),
			static_cast<size_t>(std::numeric_limits<int>::max())));
		if (outBuffer && capacity > 0) {
			const int bytesToCopy = std::min(requiredBytes, capacity - 1);
			if (bytesToCopy > 0) {
				std::memcpy(outBuffer, value.data(), static_cast<size_t>(bytesToCopy));
			}
			outBuffer[bytesToCopy] = '\0';
		}
		return requiredBytes;
	}

	static int CopyCStringToBuffer(const char* value, char* outBuffer, int capacity)
	{
		return CopyStringToBuffer(value ? std::string_view(value) : std::string_view(), outBuffer, capacity);
	}

	#define GET_COMPONENT(Type, entityID, failReturn) \
		Scene* scene = nullptr; \
		EntityHandle handle = entt::null; \
		if (!ResolveEntityReference(entityID, scene, handle)) return failReturn; \
		if (!scene->IsValid(handle)) return failReturn; \
		if (!scene->HasComponent<Type>(handle)) return failReturn; \
		auto& comp = scene->GetComponent<Type>(handle)
	// ── Transform2D ─────────────────────────────────────────────────────

	static const Transform2DComponent* GetParentTransform(Scene& scene, EntityHandle handle)
	{
		if (!scene.HasComponent<HierarchyComponent>(handle)) return nullptr;
		const EntityHandle parent = scene.GetComponent<HierarchyComponent>(handle).Parent;
		if (parent == entt::null || !scene.IsValid(parent) || !scene.HasComponent<Transform2DComponent>(parent)) {
			return nullptr;
		}
		return &scene.GetComponent<Transform2DComponent>(parent);
	}

	static Vec2 WorldPositionToLocal(const Transform2DComponent* parent, const Vec2& worldPosition)
	{
		if (!parent) return worldPosition;
		Vec2 local = Rotate(worldPosition - parent->Position, -parent->Rotation);
		if (std::abs(parent->Scale.x) > 0.00001f) local.x /= parent->Scale.x;
		if (std::abs(parent->Scale.y) > 0.00001f) local.y /= parent->Scale.y;
		return local;
	}

	static Vec2 WorldScaleToLocal(const Transform2DComponent* parent, const Vec2& worldScale)
	{
		if (!parent) return worldScale;
		Vec2 local = worldScale;
		if (std::abs(parent->Scale.x) > 0.00001f) local.x /= parent->Scale.x;
		if (std::abs(parent->Scale.y) > 0.00001f) local.y /= parent->Scale.y;
		return local;
	}

	static Transform2DComponent ComputeWorldTransform2D(const Scene& scene, EntityHandle entity)
	{
		const auto& registry = scene.GetRegistry();
		const auto* adapter = registry.ctx().find<Transform2DExternalAdapter>();
		std::vector<const Transform2DComponent*> ancestors;
		Transform2DComponent world;
		for (int depth = 0; depth < 1024 && registry.valid(entity); ++depth) {
			const auto* transform = registry.try_get<Transform2DComponent>(entity);
			if (!transform) break;
			// A clean physics body owns its world pose independently of its ancestors.
			if (!transform->IsDirty() && adapter && adapter->GetState
				&& adapter->GetState(registry, entity).OwnsWorldTransform) {
				world.Position = transform->Position;
				world.Rotation = transform->Rotation;
				world.Scale = transform->Scale;
				break;
			}
			ancestors.push_back(transform);
			const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
			entity = hierarchy ? hierarchy->Parent : entt::null;
		}
		for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
			world.Position = world.TransformPoint((*it)->LocalPosition);
			world.Rotation += (*it)->LocalRotation;
			world.Scale = Hadamard(world.Scale, (*it)->LocalScale);
		}
		return world;
	}

	// Entity.SetParent(worldPositionStays): keeps the Transform2D world pose across the reparent.
	bool SetParentKeepingWorld2D(Scene& scene, EntityHandle handle, Entity newParent)
	{
		if (!scene.HasComponent<Transform2DComponent>(handle)) return false;
		const Transform2DComponent before = ComputeWorldTransform2D(scene, handle);
		const Vec2 worldPosition = before.Position;
		const Vec2 worldScale = before.Scale;
		const float worldRotation = before.Rotation;

		scene.GetEntity(handle).SetParent(newParent);

		// Re-fetch: SetParent may toggle DisabledTag, which reorders Transform2D storage through the renderer's owning group.
		Transform2DComponent& childTransform = scene.GetComponent<Transform2DComponent>(handle);
		Transform2DComponent parentWorld;
		const Transform2DComponent* parentTransform = nullptr;
		const Entity parent = scene.GetEntity(handle).GetParent();
		if (parent.IsValid() && parent.HasComponent<Transform2DComponent>()) {
			parentWorld = ComputeWorldTransform2D(scene, parent.GetHandle());
			parentTransform = &parentWorld;
		}
		childTransform.LocalPosition = WorldPositionToLocal(parentTransform, worldPosition);
		childTransform.LocalScale = WorldScaleToLocal(parentTransform, worldScale);
		childTransform.LocalRotation = parentTransform ? worldRotation - parentTransform->Rotation : worldRotation;
		childTransform.Position = worldPosition;
		childTransform.Rotation = worldRotation;
		childTransform.Scale = worldScale;
		childTransform.MarkDirty();
		return true;
	}

	static void Funke_Transform2D_GetPosition(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Transform2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		*outX = comp.Position.x; *outY = comp.Position.y;
	}

	static void Funke_Transform2D_SetPosition(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		const Vec2 worldPosition{ x, y };
		comp.LocalPosition = WorldPositionToLocal(GetParentTransform(*scene, handle), worldPosition);
		comp.Position = worldPosition;
		comp.MarkDirty();
	}

	static float Funke_Transform2D_GetRotation(uint64_t entityID)
	{
		GET_COMPONENT(Transform2DComponent, entityID, 0.0f);
		return comp.Rotation;
	}

	static void Funke_Transform2D_SetRotation(uint64_t entityID, float rotation)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		if (const Transform2DComponent* parent = GetParentTransform(*scene, handle)) {
			comp.LocalRotation = rotation - parent->Rotation;
		}
		else {
			comp.LocalRotation = rotation;
		}
		comp.Rotation = rotation;
		comp.MarkDirty();
	}

	static void Funke_Transform2D_GetScale(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Transform2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		*outX = comp.Scale.x; *outY = comp.Scale.y;
	}

	static void Funke_Transform2D_SetScale(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		const Vec2 worldScale{ x, y };
		comp.LocalScale = WorldScaleToLocal(GetParentTransform(*scene, handle), worldScale);
		comp.Scale = worldScale;
		comp.MarkDirty();
	}

	static uint64_t Funke_Transform2D_GetEntity(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		if (!scene->HasComponent<Transform2DComponent>(handle)) return 0;
		return GetEntityScriptId(*scene, handle);
	}

	static void Funke_Transform2D_GetLocalPosition(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Transform2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		*outX = comp.LocalPosition.x; *outY = comp.LocalPosition.y;
	}

	static void Funke_Transform2D_SetLocalPosition(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		const Vec2 localPosition{ x, y };
		comp.LocalPosition = localPosition;
		if (const Transform2DComponent* parent = GetParentTransform(*scene, handle)) {
			comp.Position = parent->TransformPoint(localPosition);
		}
		else {
			comp.Position = localPosition;
		}
		comp.MarkDirty();
	}

	static float Funke_Transform2D_GetLocalRotation(uint64_t entityID)
	{
		GET_COMPONENT(Transform2DComponent, entityID, 0.0f);
		return comp.LocalRotation;
	}

	static void Funke_Transform2D_SetLocalRotation(uint64_t entityID, float rotation)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		comp.LocalRotation = rotation;
		comp.Rotation = rotation;
		if (const Transform2DComponent* parent = GetParentTransform(*scene, handle)) {
			comp.Rotation += parent->Rotation;
		}
		comp.MarkDirty();
	}

	static void Funke_Transform2D_GetLocalScale(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Transform2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		*outX = comp.LocalScale.x; *outY = comp.LocalScale.y;
	}

	static void Funke_Transform2D_SetLocalScale(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Transform2DComponent, entityID, );
		const Vec2 localScale{ x, y };
		comp.LocalScale = localScale;
		if (const Transform2DComponent* parent = GetParentTransform(*scene, handle)) {
			comp.Scale = Hadamard(parent->Scale, localScale);
		}
		else {
			comp.Scale = localScale;
		}
		comp.MarkDirty();
	}


	// ── SpriteRenderer ──────────────────────────────────────────────────

	static void Funke_SpriteRenderer_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1));
		*r = comp.Color.r; *g = comp.Color.g; *b = comp.Color.b; *a = comp.Color.a;
	}

	static void Funke_SpriteRenderer_SetColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.Color = { r, g, b, a };
	}

	static uint64_t Funke_SpriteRenderer_GetTexture(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);

		uint64_t assetId = static_cast<uint64_t>(comp.TextureAssetId);
		if (assetId == 0 && TextureManager::IsValid(comp.TextureHandle)) {
			assetId = TextureManager::GetTextureAssetUUID(comp.TextureHandle);
			if (assetId != 0) {
				comp.TextureAssetId = UUID(assetId);
			}
		}

		return assetId;
	}

	static void Funke_SpriteRenderer_SetTexture(uint64_t entityID, uint64_t assetId)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );

		if (assetId == 0) {
			comp.TextureHandle = TextureHandle::Invalid();
			comp.TextureAssetId = UUID(0);
			return;
		}

		comp.TextureAssetId = UUID(assetId);
		comp.TextureHandle = TextureManager::LoadTextureByUUID(assetId);
		// Apply an explicit FilterMode to the freshly bound texture. Default keeps the texture's own (.meta) filter, like
		// the Inspector setter; re-applying it rebuilt the shared sampler on every call (slow for many sprites).
		if (comp.FilterMode != Filter::Default) {
			if (auto* tex = TextureManager::GetTexture(comp.TextureHandle); tex && tex->GetFilter() != comp.FilterMode) {
				tex->SetFilter(comp.FilterMode);
			}
		}
	}

	static int Funke_SpriteRenderer_GetSortingOrder(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);
		return comp.SortingOrder;
	}

	static void Funke_SpriteRenderer_SetSortingOrder(uint64_t entityID, int order)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.SortingOrder = static_cast<short>(order);
	}

	static int Funke_SpriteRenderer_GetSortingLayer(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);
		return comp.SortingLayer;
	}

	static void Funke_SpriteRenderer_SetSortingLayer(uint64_t entityID, int layer)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.SortingLayer = static_cast<uint8_t>(layer);
	}

	static int Funke_SpriteRenderer_GetFilter(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, static_cast<int>(Filter::Bilinear));
		return static_cast<int>(comp.FilterMode);
	}

	static void Funke_SpriteRenderer_SetFilter(uint64_t entityID, int filter)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.FilterMode = static_cast<Filter>(filter);
		if (comp.FilterMode == Filter::Default) {
			// Revert to the texture's own (.meta/import) filter, as the Inspector does.
			TextureManager::ApplyMetaSamplerToLoaded(TextureManager::GetTextureName(comp.TextureHandle));
		}
		else if (auto* tex = TextureManager::GetTexture(comp.TextureHandle); tex) {
			tex->SetFilter(comp.FilterMode);
		}
	}

	static uint64_t Funke_SpriteRenderer_GetMaterial(uint64_t entityID)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, 0);
		return comp.Material;
	}

	static void Funke_SpriteRenderer_SetMaterial(uint64_t entityID, uint64_t materialGuid)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.Material = materialGuid;
	}

	static int Funke_SpriteRenderer_GetSpriteNameBuffer(uint64_t entityID, char* outBuffer, int capacity)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, CopyCStringToBuffer("", outBuffer, capacity));
		return CopyStringToBuffer(comp.SpriteName, outBuffer, capacity);
	}

	static void Funke_SpriteRenderer_SetSpriteName(uint64_t entityID, const char* name)
	{
		GET_COMPONENT(SpriteRendererComponent, entityID, );
		comp.SpriteName = name ? std::string(name) : std::string{};
	}

	// ── Camera2D ────────────────────────────────────────────────────────

	static float Funke_Camera2D_GetOrthographicSize(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 5.0f);
		return comp.GetOrthographicSize();
	}

	static void Funke_Camera2D_SetOrthographicSize(uint64_t entityID, float size)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetOrthographicSize(size);
	}

	static int Funke_Camera2D_GetPostProcessingEnabled(uint64_t entityID) {
		GET_COMPONENT(Camera2DComponent, entityID, 0);
		return comp.IsPostProcessingEnabled() ? 1 : 0;
	}

	static void Funke_Camera2D_SetPostProcessingEnabled(uint64_t entityID, int enabled) {
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetPostProcessingEnabled(enabled != 0);
	}

	static float Funke_Camera2D_GetZoom(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 1.0f);
		return comp.GetZoom();
	}

	static void Funke_Camera2D_SetZoom(uint64_t entityID, float zoom)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetZoom(zoom);
	}

	static void Funke_Camera2D_GetClearColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(Camera2DComponent, entityID, (void)(*r = 0.1f, *g = 0.1f, *b = 0.1f, *a = 1.0f));
		const auto& cc = comp.GetClearColor();
		*r = cc.r; *g = cc.g; *b = cc.b; *a = cc.a;
	}

	static void Funke_Camera2D_SetClearColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(Camera2DComponent, entityID, );
		comp.SetClearColor(Color(r, g, b, a));
	}

	static void Funke_Camera2D_ScreenToWorld(uint64_t entityID, float sx, float sy, float* outX, float* outY)
	{
		GET_COMPONENT(Camera2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 world = comp.ScreenToWorld({ sx, sy });
		*outX = world.x; *outY = world.y;
	}

	// While scripts run, the editor's camera viewport is the whole editor window; screen points (Input.MousePosition)
	// refer to the Game View, like ScreenToWorld.
	static float Funke_Camera2D_GetViewportWidth(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 0.0f);
		const Window::UIRegion region = Window::GetUIRegion();
		return region.IsActive() ? static_cast<float>(region.Width) : comp.ViewportWidth();
	}

	static float Funke_Camera2D_GetViewportHeight(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 0.0f);
		const Window::UIRegion region = Window::GetUIRegion();
		return region.IsActive() ? static_cast<float>(region.Height) : comp.ViewportHeight();
	}

	static uint64_t Funke_Camera2D_GetMainEntity()
	{
		Scene* scene = GetScene();
		if (!scene) return 0;

		const EntityHandle handle = scene->GetMainCameraEntity();
		if (handle == entt::null) return 0;
		return GetEntityScriptId(*scene, handle);
	}

	static void Funke_Camera2D_GetFrustum(uint64_t entityID, float* minX, float* minY, float* maxX, float* maxY)
	{
		GET_COMPONENT(Camera2DComponent, entityID, (void)(*minX = 0, *minY = 0, *maxX = 0, *maxY = 0));
		// Rebuild from the live transform first: the renderer refreshes this cache after scripts run, so a camera moved this frame would otherwise report last frame's bounds.
		comp.UpdateViewport();
		const AABB box = comp.GetViewportAABB();
		*minX = box.Min.x; *minY = box.Min.y; *maxX = box.Max.x; *maxY = box.Max.y;
	}

	// ── ParticleSystem2D ────────────────────────────────────────────────

	static void Funke_ParticleSystem2D_Play(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Play();
	}
	static void Funke_ParticleSystem2D_Pause(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Pause();
	}
	static void Funke_ParticleSystem2D_Stop(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.Stop();
	}
	static int Funke_ParticleSystem2D_IsPlaying(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.IsPlaying() ? 1 : 0;
	}
	static int Funke_ParticleSystem2D_GetPlayOnAwake(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.PlayOnAwake ? 1 : 0;
	}
	static void Funke_ParticleSystem2D_SetPlayOnAwake(uint64_t entityID, int enabled) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.PlayOnAwake = (enabled != 0);
	}
	static uint64_t Funke_ParticleSystem2D_GetTexture(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		uint64_t assetId = static_cast<uint64_t>(comp.GetTextureAssetId());
		const TextureHandle texHandle = comp.GetTextureHandle();
		if (assetId == 0 && TextureManager::IsValid(texHandle)) {
			assetId = TextureManager::GetTextureAssetUUID(texHandle);
			if (assetId != 0) comp.SetTexture(texHandle, UUID(assetId));
		}
		return assetId;
	}
	static void Funke_ParticleSystem2D_SetTexture(uint64_t entityID, uint64_t assetId) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		if (assetId == 0) {
			comp.SetTexture(TextureHandle::Invalid(), UUID(0));
			return;
		}
		const TextureHandle texHandle = TextureManager::LoadTextureByUUID(assetId);
		comp.SetTexture(texHandle, UUID(assetId));
	}
	static void Funke_ParticleSystem2D_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		*r = comp.RenderingSettings.Color.r; *g = comp.RenderingSettings.Color.g;
		*b = comp.RenderingSettings.Color.b; *a = comp.RenderingSettings.Color.a;
	}
	static void Funke_ParticleSystem2D_SetColor(uint64_t entityID, float r, float g, float b, float a) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.RenderingSettings.Color = { r, g, b, a };
	}
	static float Funke_ParticleSystem2D_GetLifeTime(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0.0f);
		return comp.ParticleSettings.LifeTime;
	}
	static void Funke_ParticleSystem2D_SetLifeTime(uint64_t entityID, float lifetime) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.LifeTime = lifetime;
	}
	static float Funke_ParticleSystem2D_GetSpeed(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0.0f);
		return comp.ParticleSettings.Speed;
	}
	static void Funke_ParticleSystem2D_SetSpeed(uint64_t entityID, float speed) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.Speed = speed;
	}
	static void Funke_ParticleSystem2D_GetScale(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		*outX = comp.ParticleSettings.Scale.x; *outY = comp.ParticleSettings.Scale.y;
	}
	static void Funke_ParticleSystem2D_SetScale(uint64_t entityID, float x, float y) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.ParticleSettings.Scale = { x, y };
	}
	static int Funke_ParticleSystem2D_GetEmitOverTime(uint64_t entityID) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, 0);
		return comp.EmissionSettings.EmitOverTime;
	}
	static void Funke_ParticleSystem2D_SetEmitOverTime(uint64_t entityID, int rate) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		comp.EmissionSettings.EmitOverTime = static_cast<uint16_t>(std::clamp(rate, 0, 65535));
	}
	static void Funke_ParticleSystem2D_Emit(uint64_t entityID, int count) {
		GET_COMPONENT(ParticleSystem2DComponent, entityID, );
		if (count <= 0) return;  // a negative int cast to size_t would fill the system to MaxParticles
		comp.Emit(static_cast<size_t>(count));
	}

	// ── EntityPicker ────────────────────────────────────────────────────

	static int Funke_EntityPicker_TryPickEntity(float worldX, float worldY, uint64_t* entityID) {
		if (!entityID) return 0;
		*entityID = 0;

		Scene* scene = GetScene();
		if (!scene) return 0;

		EntityHandle picked = entt::null;
		if (!EntityPicker::TryPickEntity(*scene, { worldX, worldY }, picked)) {
			return 0;
		}
		*entityID = GetEntityScriptId(*scene, picked);
		return *entityID != 0 ? 1 : 0;
	}


	// ── TerrainRenderer2D ───────────────────────────────────────────────

	constexpr int k_TerrainValues = 19;

	static void Funke_TerrainRenderer2D_Get(uint64_t entityID, float* values, int count)
	{
		if (!values || count <= 0) return;
		GET_COMPONENT(TerrainRenderer2DComponent, entityID, );
		const float v[k_TerrainValues] = {
			comp.FillColor.r, comp.FillColor.g, comp.FillColor.b, comp.FillColor.a,
			comp.FillTileSize.x, comp.FillTileSize.y, static_cast<float>(comp.FillMapping), comp.FillDepth,
			comp.EdgeColor.r, comp.EdgeColor.g, comp.EdgeColor.b, comp.EdgeColor.a,
			comp.EdgeThickness, comp.EdgeOffset, comp.EdgeTileLength,
			static_cast<float>(comp.SortingLayer), static_cast<float>(comp.SortingOrder), static_cast<float>(comp.EdgeSortingOrder),
			comp.EdgeTextureOffset };
		std::copy_n(v, std::min(count, k_TerrainValues), values);
	}

	static void Funke_TerrainRenderer2D_Set(uint64_t entityID, const float* values, int count)
	{
		if (!values || count < k_TerrainValues) return;
		GET_COMPONENT(TerrainRenderer2DComponent, entityID, );
		comp.FillColor = Color{ values[0], values[1], values[2], values[3] };
		comp.FillTileSize = { values[4], values[5] };
		comp.FillMapping = values[6] >= 0.5f ? TerrainFillMapping::Draped : TerrainFillMapping::World;
		comp.FillDepth = std::max(values[7], 0.0f);
		comp.EdgeColor = Color{ values[8], values[9], values[10], values[11] };
		comp.EdgeThickness = std::max(values[12], 0.0f);
		comp.EdgeOffset = values[13];
		comp.EdgeTileLength = values[14];
		comp.SortingLayer = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(values[15])), 0, 255));
		comp.SortingOrder = static_cast<short>(std::clamp(static_cast<int>(std::lround(values[16])), -32768, 32767));
		comp.EdgeSortingOrder = static_cast<short>(std::clamp(static_cast<int>(std::lround(values[17])), -32768, 32767));
		comp.EdgeTextureOffset = values[18];
	}

	static int Funke_TerrainRenderer2D_GetPoints(uint64_t entityID, float* outPoints, int maxPoints)
	{
		GET_COMPONENT(TerrainRenderer2DComponent, entityID, 0);
		const int n = outPoints ? std::min(static_cast<int>(comp.Points.size()), std::max(maxPoints, 0)) : 0;
		for (int i = 0; i < n; ++i) {
			outPoints[i * 2] = comp.Points[static_cast<size_t>(i)].x;
			outPoints[i * 2 + 1] = comp.Points[static_cast<size_t>(i)].y;
		}
		return static_cast<int>(comp.Points.size());
	}

	static void Funke_TerrainRenderer2D_SetPoints(uint64_t entityID, const float* points, int pointCount)
	{
		GET_COMPONENT(TerrainRenderer2DComponent, entityID, );
		comp.Points.clear();
		if (!points || pointCount <= 0) return;
		comp.Points.reserve(static_cast<size_t>(pointCount));
		for (int i = 0; i < pointCount; ++i) comp.Points.push_back(Vec2{ points[i * 2], points[i * 2 + 1] });
	}

	static uint64_t Funke_TerrainRenderer2D_GetTexture(uint64_t entityID, int which)
	{
		GET_COMPONENT(TerrainRenderer2DComponent, entityID, 0);
		return which == 0 ? comp.FillTexture : comp.EdgeTexture;
	}

	static void Funke_TerrainRenderer2D_SetTexture(uint64_t entityID, int which, uint64_t assetId)
	{
		GET_COMPONENT(TerrainRenderer2DComponent, entityID, );
		(which == 0 ? comp.FillTexture : comp.EdgeTexture) = assetId;
	}

	#undef GET_COMPONENT
	#undef FNK_BINDING_CATCH_VOID
	#undef FNK_BINDING_CATCH
	#undef FNK_BINDING_TRY

	void Populate2DBindings(NativeBindings& b)
	{
		b.Transform2D_GetPosition = FNK_GUARDED_BINDING(Funke_Transform2D_GetPosition);
		b.Transform2D_SetPosition = FNK_GUARDED_BINDING(Funke_Transform2D_SetPosition);
		b.Transform2D_GetRotation = FNK_GUARDED_BINDING(Funke_Transform2D_GetRotation);
		b.Transform2D_SetRotation = FNK_GUARDED_BINDING(Funke_Transform2D_SetRotation);
		b.Transform2D_GetScale = FNK_GUARDED_BINDING(Funke_Transform2D_GetScale);
		b.Transform2D_SetScale = FNK_GUARDED_BINDING(Funke_Transform2D_SetScale);
		b.Transform2D_GetEntity = FNK_GUARDED_BINDING(Funke_Transform2D_GetEntity);
		b.Transform2D_GetLocalPosition = FNK_GUARDED_BINDING(Funke_Transform2D_GetLocalPosition);
		b.Transform2D_SetLocalPosition = FNK_GUARDED_BINDING(Funke_Transform2D_SetLocalPosition);
		b.Transform2D_GetLocalRotation = FNK_GUARDED_BINDING(Funke_Transform2D_GetLocalRotation);
		b.Transform2D_SetLocalRotation = FNK_GUARDED_BINDING(Funke_Transform2D_SetLocalRotation);
		b.Transform2D_GetLocalScale = FNK_GUARDED_BINDING(Funke_Transform2D_GetLocalScale);
		b.Transform2D_SetLocalScale = FNK_GUARDED_BINDING(Funke_Transform2D_SetLocalScale);
		b.SpriteRenderer_GetColor = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetColor);
		b.SpriteRenderer_SetColor = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetColor);
		b.SpriteRenderer_GetTexture = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetTexture);
		b.SpriteRenderer_SetTexture = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetTexture);
		b.SpriteRenderer_GetSortingOrder = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetSortingOrder);
		b.SpriteRenderer_SetSortingOrder = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetSortingOrder);
		b.SpriteRenderer_GetSortingLayer = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetSortingLayer);
		b.SpriteRenderer_SetSortingLayer = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetSortingLayer);
		b.Camera2D_GetOrthographicSize = FNK_GUARDED_BINDING(Funke_Camera2D_GetOrthographicSize);
		b.Camera2D_SetOrthographicSize = FNK_GUARDED_BINDING(Funke_Camera2D_SetOrthographicSize);
		b.Camera2D_GetPostProcessingEnabled = FNK_GUARDED_BINDING(Funke_Camera2D_GetPostProcessingEnabled);
		b.Camera2D_SetPostProcessingEnabled = FNK_GUARDED_BINDING(Funke_Camera2D_SetPostProcessingEnabled);
		b.Camera2D_GetZoom = FNK_GUARDED_BINDING(Funke_Camera2D_GetZoom);
		b.Camera2D_SetZoom = FNK_GUARDED_BINDING(Funke_Camera2D_SetZoom);
		b.Camera2D_GetClearColor = FNK_GUARDED_BINDING(Funke_Camera2D_GetClearColor);
		b.Camera2D_SetClearColor = FNK_GUARDED_BINDING(Funke_Camera2D_SetClearColor);
		b.Camera2D_ScreenToWorld = FNK_GUARDED_BINDING(Funke_Camera2D_ScreenToWorld);
		b.Camera2D_GetViewportWidth = FNK_GUARDED_BINDING(Funke_Camera2D_GetViewportWidth);
		b.Camera2D_GetViewportHeight = FNK_GUARDED_BINDING(Funke_Camera2D_GetViewportHeight);
		b.Camera2D_GetMainEntity = FNK_GUARDED_BINDING(Funke_Camera2D_GetMainEntity);
		b.Camera2D_GetFrustum = FNK_GUARDED_BINDING(Funke_Camera2D_GetFrustum);
		b.ParticleSystem2D_Play = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_Play);
		b.ParticleSystem2D_Pause = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_Pause);
		b.ParticleSystem2D_Stop = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_Stop);
		b.ParticleSystem2D_IsPlaying = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_IsPlaying);
		b.ParticleSystem2D_GetPlayOnAwake = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_GetPlayOnAwake);
		b.ParticleSystem2D_SetPlayOnAwake = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_SetPlayOnAwake);
		b.ParticleSystem2D_GetColor = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_GetColor);
		b.ParticleSystem2D_SetColor = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_SetColor);
		b.ParticleSystem2D_GetLifeTime = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_GetLifeTime);
		b.ParticleSystem2D_SetLifeTime = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_SetLifeTime);
		b.ParticleSystem2D_GetSpeed = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_GetSpeed);
		b.ParticleSystem2D_SetSpeed = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_SetSpeed);
		b.ParticleSystem2D_GetScale = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_GetScale);
		b.ParticleSystem2D_SetScale = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_SetScale);
		b.ParticleSystem2D_GetEmitOverTime = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_GetEmitOverTime);
		b.ParticleSystem2D_SetEmitOverTime = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_SetEmitOverTime);
		b.ParticleSystem2D_Emit = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_Emit);
		b.EntityPicker_TryPickEntity = FNK_GUARDED_BINDING(Funke_EntityPicker_TryPickEntity);
		b.SpriteRenderer_GetFilter = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetFilter);
		b.SpriteRenderer_SetFilter = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetFilter);
		b.SpriteRenderer_GetSpriteNameBuffer = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetSpriteNameBuffer);
		b.SpriteRenderer_SetSpriteName       = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetSpriteName);
		b.SpriteRenderer_GetMaterial = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetMaterial);
		b.SpriteRenderer_SetMaterial = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetMaterial);
		b.ParticleSystem2D_GetTexture = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_GetTexture);
		b.ParticleSystem2D_SetTexture = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_SetTexture);
		b.TerrainRenderer2D_Get = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_Get);
		b.TerrainRenderer2D_Set = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_Set);
		b.TerrainRenderer2D_GetPoints = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_GetPoints);
		b.TerrainRenderer2D_SetPoints = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_SetPoints);
		b.TerrainRenderer2D_GetTexture = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_GetTexture);
		b.TerrainRenderer2D_SetTexture = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_SetTexture);
	}

}
