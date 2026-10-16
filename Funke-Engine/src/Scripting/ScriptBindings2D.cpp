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
#include "Components/Graphics/SpriteAnimator2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/Window.hpp"
#include "Graphics/TextureManager.hpp"
#include "Physics/FastPhysics2D.hpp"
#include "Physics/Physics2D.hpp"
#include "Scene/EntityPicker.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

// C# bindings for the Funke.2D module (transforms, sprites, cameras, particles, post-processing, 2D physics,
// picking). Split from ScriptBindings.cpp so a build without the module compiles none of it; the slots
// are then filled with stubs (ScriptBindingsModuleStubs.cpp).
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

	// Entity.SetParent(worldPositionStays): keeps the Transform2D world pose across the reparent.
	bool SetParentKeepingWorld2D(Scene& scene, EntityHandle handle, Entity newParent)
	{
		if (!scene.HasComponent<Transform2DComponent>(handle)) return false;
		// SetParent only swaps the parent, so the local transform — and with it the world pose — would
		// shift into the new parent's frame; snapshot the world pose and re-derive the locals.
		const Transform2DComponent& before = scene.GetComponent<Transform2DComponent>(handle);
		const Vec2 worldPosition = before.Position;
		const Vec2 worldScale = before.Scale;
		const float worldRotation = before.Rotation;

		scene.GetEntity(handle).SetParent(newParent);

		// Re-fetch: SetParent may toggle DisabledTag, which reorders Transform2D storage through the renderer's owning group.
		Transform2DComponent& childTransform = scene.GetComponent<Transform2DComponent>(handle);
		const Transform2DComponent* parentTransform = GetParentTransform(scene, handle);
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

	static float Funke_Camera2D_GetViewportWidth(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 0.0f);
		return comp.ViewportWidth();
	}

	static float Funke_Camera2D_GetViewportHeight(uint64_t entityID)
	{
		GET_COMPONENT(Camera2DComponent, entityID, 0.0f);
		return comp.ViewportHeight();
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

	// ── Rigidbody2D ─────────────────────────────────────────────────────

	static void Funke_Rigidbody2D_ApplyForce(uint64_t entityID, float forceX, float forceY, int wake)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		b2BodyId bodyId = comp.GetBodyHandle();
		if (b2Body_IsValid(bodyId)) b2Body_ApplyForceToCenter(bodyId, { forceX, forceY }, wake != 0);
	}

	static void Funke_Rigidbody2D_ApplyImpulse(uint64_t entityID, float impulseX, float impulseY, int wake)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		b2BodyId bodyId = comp.GetBodyHandle();
		if (b2Body_IsValid(bodyId)) b2Body_ApplyLinearImpulseToCenter(bodyId, { impulseX, impulseY }, wake != 0);
	}

	static void Funke_Rigidbody2D_GetLinearVelocity(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 vel = comp.GetVelocity(); *outX = vel.x; *outY = vel.y;
	}

	static void Funke_Rigidbody2D_SetLinearVelocity(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetVelocity({ x, y });
	}

	static float Funke_Rigidbody2D_GetAngularVelocity(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0.0f);
		return comp.GetAngularVelocity();
	}

	static void Funke_Rigidbody2D_SetAngularVelocity(uint64_t entityID, float velocity)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetAngularVelocity(velocity);
	}

	static int Funke_Rigidbody2D_GetBodyType(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0);
		return static_cast<int>(comp.GetBodyType());
	}

	static void Funke_Rigidbody2D_SetBodyType(uint64_t entityID, int type)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetBodyType(static_cast<BodyType>(type));
	}

	static float Funke_Rigidbody2D_GetGravityScale(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 1.0f);
		return comp.GetGravityScale();
	}

	static void Funke_Rigidbody2D_SetGravityScale(uint64_t entityID, float scale)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetGravityScale(scale);
	}

	static float Funke_Rigidbody2D_GetMass(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 1.0f);
		return comp.GetMass();
	}

	static void Funke_Rigidbody2D_SetMass(uint64_t entityID, float mass)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetMass(mass);
	}

	// ── Rigidbody2D constraints (motion locks) ─────────────────────────
	static int Funke_Rigidbody2D_GetFreezePositionX(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0);
		return comp.GetFreezePositionX() ? 1 : 0;
	}
	static void Funke_Rigidbody2D_SetFreezePositionX(uint64_t entityID, int freeze)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetFreezePositionX(freeze != 0);
	}
	static int Funke_Rigidbody2D_GetFreezePositionY(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0);
		return comp.GetFreezePositionY() ? 1 : 0;
	}
	static void Funke_Rigidbody2D_SetFreezePositionY(uint64_t entityID, int freeze)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetFreezePositionY(freeze != 0);
	}
	static int Funke_Rigidbody2D_GetFreezeRotation(uint64_t entityID)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, 0);
		return comp.GetFreezeRotation() ? 1 : 0;
	}
	static void Funke_Rigidbody2D_SetFreezeRotation(uint64_t entityID, int freeze)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetFreezeRotation(freeze != 0);
	}

	// ── BoxCollider2D ───────────────────────────────────────────────────

	static void Funke_BoxCollider2D_GetScale(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		Vec2 s = comp.GetScale(); *outX = s.x; *outY = s.y;
	}

	static void Funke_BoxCollider2D_GetCenter(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 c = comp.GetCenter(); *outX = c.x; *outY = c.y;
	}

	static void Funke_BoxCollider2D_SetEnabled(uint64_t entityID, int enabled)
	{
		GET_COMPONENT(BoxCollider2DComponent, entityID, );
		comp.SetEnabled(enabled != 0);
	}

	// ── Collider2D (shared by Box/Circle/Polygon) ──────────────────────
	// colliderType: 0 = Box, 1 = Circle, 2 = Polygon (matches C# ColliderKind).

	static Collider2D* FindCollider(uint64_t entityID, int colliderType, Scene*& outScene)
	{
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, outScene, handle) || !outScene->IsValid(handle)) return nullptr;
		switch (colliderType) {
		case 0: return outScene->HasComponent<BoxCollider2DComponent>(handle) ? &outScene->GetComponent<BoxCollider2DComponent>(handle) : nullptr;
		case 1: return outScene->HasComponent<CircleCollider2DComponent>(handle) ? &outScene->GetComponent<CircleCollider2DComponent>(handle) : nullptr;
		case 2: return outScene->HasComponent<PolygonCollider2DComponent>(handle) ? &outScene->GetComponent<PolygonCollider2DComponent>(handle) : nullptr;
		default: return nullptr;
		}
	}

	static int Funke_Collider2D_GetSensor(uint64_t entityID, int colliderType)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		return collider && collider->IsSensor() ? 1 : 0;
	}
	static void Funke_Collider2D_SetSensor(uint64_t entityID, int colliderType, int sensor)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		if (!collider) return;
		// SetSensor recreates the shape and is not virtual on Collider2D.
		switch (colliderType) {
		case 0: static_cast<BoxCollider2DComponent*>(collider)->SetSensor(sensor != 0, *scene); break;
		case 1: static_cast<CircleCollider2DComponent*>(collider)->SetSensor(sensor != 0, *scene); break;
		case 2: static_cast<PolygonCollider2DComponent*>(collider)->SetSensor(sensor != 0, *scene); break;
		}
	}
	static uint64_t Funke_Collider2D_GetLayer(uint64_t entityID, int colliderType)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		return collider ? collider->GetLayer() : 0;
	}
	static void Funke_Collider2D_SetLayer(uint64_t entityID, int colliderType, uint64_t layer)
	{
		Scene* scene = nullptr;
		if (Collider2D* collider = FindCollider(entityID, colliderType, scene)) collider->SetLayer(layer);
	}
	static uint64_t Funke_Collider2D_GetCollisionMask(uint64_t entityID, int colliderType)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		return collider ? collider->GetCollisionMask() : 0;
	}
	static void Funke_Collider2D_SetCollisionMask(uint64_t entityID, int colliderType, uint64_t mask)
	{
		Scene* scene = nullptr;
		if (Collider2D* collider = FindCollider(entityID, colliderType, scene)) collider->SetCollisionMask(mask);
	}
	static float Funke_Collider2D_GetFriction(uint64_t entityID, int colliderType)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		return collider ? collider->GetFriction() : 0.0f;
	}
	static void Funke_Collider2D_SetFriction(uint64_t entityID, int colliderType, float friction)
	{
		Scene* scene = nullptr;
		if (Collider2D* collider = FindCollider(entityID, colliderType, scene)) collider->SetFriction(friction);
	}
	static float Funke_Collider2D_GetBounciness(uint64_t entityID, int colliderType)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		return collider ? collider->GetBounciness() : 0.0f;
	}
	static void Funke_Collider2D_SetBounciness(uint64_t entityID, int colliderType, float bounciness)
	{
		Scene* scene = nullptr;
		if (Collider2D* collider = FindCollider(entityID, colliderType, scene)) collider->SetBounciness(bounciness);
	}
	static void Funke_Physics2D_SetQueryLayerMask(uint64_t layerMask)
	{
		Physics2D::SetQueryLayerMask(layerMask);
	}

	// ── SpriteAnimator2D ────────────────────────────────────────────────

	static void Funke_SpriteAnimator2D_Command(uint64_t entityID, int command)
	{
		GET_COMPONENT(SpriteAnimator2DComponent, entityID, );
		switch (command) {
		case 0: comp.Play(); break;
		case 1: comp.Pause(); break;
		case 2: comp.Stop(); break;
		default: break;
		}
	}
	static int Funke_SpriteAnimator2D_IsPlaying(uint64_t entityID)
	{
		GET_COMPONENT(SpriteAnimator2DComponent, entityID, 0);
		return comp.Playing ? 1 : 0;
	}
	static int Funke_SpriteAnimator2D_GetFrame(uint64_t entityID)
	{
		GET_COMPONENT(SpriteAnimator2DComponent, entityID, 0);
		return comp.CurrentFrame;
	}
	static void Funke_SpriteAnimator2D_SetFrame(uint64_t entityID, int frame)
	{
		GET_COMPONENT(SpriteAnimator2DComponent, entityID, );
		const int last = std::max(0, static_cast<int>(comp.Frames.size()) - 1);
		comp.CurrentFrame = std::clamp(frame, 0, last);
		comp.FrameTime = 0.0f;
	}
	static float Funke_SpriteAnimator2D_GetFramesPerSecond(uint64_t entityID)
	{
		GET_COMPONENT(SpriteAnimator2DComponent, entityID, 0.0f);
		return comp.FramesPerSecond;
	}
	static void Funke_SpriteAnimator2D_SetFramesPerSecond(uint64_t entityID, float fps)
	{
		GET_COMPONENT(SpriteAnimator2DComponent, entityID, );
		comp.FramesPerSecond = std::max(0.0f, fps);
	}

	// ── CircleCollider2D ────────────────────────────────────────────────

	static float Funke_CircleCollider2D_GetRadius(uint64_t entityID)
	{
		GET_COMPONENT(CircleCollider2DComponent, entityID, 0.0f);
		return comp.GetLocalRadius(*scene);
	}

	static void Funke_CircleCollider2D_SetRadius(uint64_t entityID, float radius)
	{
		GET_COMPONENT(CircleCollider2DComponent, entityID, );
		comp.SetRadius(radius, *scene);
	}

	static void Funke_CircleCollider2D_GetCenter(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(CircleCollider2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 c = comp.GetCenter(); *outX = c.x; *outY = c.y;
	}

	static void Funke_CircleCollider2D_SetCenter(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(CircleCollider2DComponent, entityID, );
		comp.SetCenter(Vec2{ x, y }, *scene);
	}

	static void Funke_CircleCollider2D_SetEnabled(uint64_t entityID, int enabled)
	{
		GET_COMPONENT(CircleCollider2DComponent, entityID, );
		comp.SetEnabled(enabled != 0);
	}

	// ── PolygonCollider2D ───────────────────────────────────────────────

	static int Funke_PolygonCollider2D_GetVertexCount(uint64_t entityID)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, 0);
		return comp.GetVertexCount();
	}

	static int Funke_PolygonCollider2D_GetWorldPoints(uint64_t entityID, float* outPoints, int maxOut)
	{
		if (!outPoints || maxOut <= 0) return 0;
		GET_COMPONENT(PolygonCollider2DComponent, entityID, 0);

		std::vector<Vec2> world = comp.GetWorldPoints();
		const int writable = std::min(static_cast<int>(world.size()) * 2, maxOut);
		// Pair pack: index i -> (outPoints[2i], outPoints[2i+1]) so the C# side
		// can read it as a Span<Vector2> without an extra reshape pass.
		for (int i = 0; i < writable / 2; ++i) {
			outPoints[i * 2 + 0] = world[i].x;
			outPoints[i * 2 + 1] = world[i].y;
		}
		return writable / 2;
	}

	static void Funke_PolygonCollider2D_SetPoints(uint64_t entityID, const float* points, int pointCount)
	{
		if (!points) return;
		GET_COMPONENT(PolygonCollider2DComponent, entityID, );
		if (pointCount < PolygonCollider2DComponent::k_MinVertices ||
			pointCount > PolygonCollider2DComponent::k_MaxVertices) {
			return;
		}

		std::vector<Vec2> localPoints;
		localPoints.reserve(static_cast<size_t>(pointCount));
		for (int i = 0; i < pointCount; ++i) {
			localPoints.push_back(Vec2{ points[i * 2], points[i * 2 + 1] });
		}
		comp.SetPoints(localPoints, *scene);
	}

	static void Funke_PolygonCollider2D_SetSides(uint64_t entityID, int sides)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, );
		comp.SetSides(sides, *scene);
	}

	static void Funke_PolygonCollider2D_GetCenter(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, (void)(*outX = 0, *outY = 0));
		Vec2 c = comp.GetCenter(); *outX = c.x; *outY = c.y;
	}

	static void Funke_PolygonCollider2D_SetCenter(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, );
		comp.SetCenter(Vec2{ x, y }, *scene);
	}

	static void Funke_PolygonCollider2D_GetSize(uint64_t entityID, float* outX, float* outY)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, (void)(*outX = 1, *outY = 1));
		Vec2 s = comp.GetSize(); *outX = s.x; *outY = s.y;
	}

	static void Funke_PolygonCollider2D_SetSize(uint64_t entityID, float x, float y)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, );
		comp.SetSize(Vec2{ x, y }, *scene);
	}

	static void Funke_PolygonCollider2D_SetEnabled(uint64_t entityID, int enabled)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, );
		comp.SetEnabled(enabled != 0);
	}

	// ── Index-Physics ────────────────────────────────────────────────────

	static int Funke_FastBody2D_GetBodyType(uint64_t entityID) { GET_COMPONENT(FastBody2DComponent, entityID, 1); return static_cast<int>(comp.Type); }
	static void Funke_FastBody2D_SetBodyType(uint64_t entityID, int type) { GET_COMPONENT(FastBody2DComponent, entityID, ); comp.Type = static_cast<IndexPhys::BodyType>(type); if (comp.m_Body) comp.m_Body->SetBodyType(comp.Type); }
	static float Funke_FastBody2D_GetMass(uint64_t entityID) { GET_COMPONENT(FastBody2DComponent, entityID, 1.0f); return comp.Mass; }
	static void Funke_FastBody2D_SetMass(uint64_t entityID, float mass) { GET_COMPONENT(FastBody2DComponent, entityID, ); comp.Mass = mass; if (comp.m_Body) comp.m_Body->SetMass(mass); }
	static int Funke_FastBody2D_GetUseGravity(uint64_t entityID) { GET_COMPONENT(FastBody2DComponent, entityID, 1); return comp.UseGravity ? 1 : 0; }
	static void Funke_FastBody2D_SetUseGravity(uint64_t entityID, int enabled) { GET_COMPONENT(FastBody2DComponent, entityID, ); comp.UseGravity = enabled != 0; if (comp.m_Body) comp.m_Body->SetGravityEnabled(comp.UseGravity); }
	static void Funke_FastBody2D_GetVelocity(uint64_t entityID, float* outX, float* outY) { GET_COMPONENT(FastBody2DComponent, entityID, (void)(*outX = 0, *outY = 0)); Vec2 v = comp.GetVelocity(); *outX = v.x; *outY = v.y; }
	static void Funke_FastBody2D_SetVelocity(uint64_t entityID, float x, float y) { GET_COMPONENT(FastBody2DComponent, entityID, ); comp.SetVelocity({ x, y }); }

	static void Funke_FastBoxCollider2D_GetHalfExtents(uint64_t entityID, float* outX, float* outY) { GET_COMPONENT(FastBoxCollider2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f)); *outX = comp.HalfExtents.x; *outY = comp.HalfExtents.y; }
	static void Funke_FastBoxCollider2D_SetHalfExtents(uint64_t entityID, float x, float y) { GET_COMPONENT(FastBoxCollider2DComponent, entityID, ); comp.SetHalfExtents({ x, y }); }

	static float Funke_FastCircleCollider2D_GetRadius(uint64_t entityID) { GET_COMPONENT(FastCircleCollider2DComponent, entityID, 0.5f); return comp.Radius; }
	static void Funke_FastCircleCollider2D_SetRadius(uint64_t entityID, float radius) { GET_COMPONENT(FastCircleCollider2DComponent, entityID, ); comp.SetRadius(radius); }

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

	// ── Physics2D ───────────────────────────────────────────────────────

	static int Funke_Physics2D_Raycast(
		float originX, float originY, float dirX, float dirY, float distance,
		uint64_t* hitEntityID, float* hitX, float* hitY, float* hitNormalX, float* hitNormalY, float* hitDistance)
	{
		if (!hitEntityID || !hitX || !hitY || !hitNormalX || !hitNormalY || !hitDistance) return 0;
		*hitEntityID = 0; *hitX = 0; *hitY = 0; *hitNormalX = 0; *hitNormalY = 0; *hitDistance = 0;

		auto result = Physics2D::Raycast({ originX, originY }, { dirX, dirY }, distance);
		if (result.has_value()) {
			if (result->scene && result->scene->IsValid(result->entity)) {
				*hitEntityID = GetEntityScriptId(*result->scene, result->entity);
			}
			*hitX = result->point.x; *hitY = result->point.y;
			*hitNormalX = result->normal.x; *hitNormalY = result->normal.y;
			*hitDistance = result->distance;
			return 1;
		}
		return 0;
	}

	static uint64_t ToScriptEntityId(const PhysicsBodyRef2D& ref) {
		if (!ref.scene || ref.entity == entt::null || !ref.scene->IsValid(ref.entity)) {
			return 0;
		}
		return GetEntityScriptId(*ref.scene, ref.entity);
	}

	static OverlapMode ToOverlapMode(int mode) {
		return mode == 1 ? OverlapMode::Nearest : OverlapMode::First;
	}

	static std::vector<Vec2> ReadPolygonPoints(const float* points, int pointCount) {
		std::vector<Vec2> result;
		constexpr int maxPolygonVertices = 8;
		if (!points || pointCount < 3 || pointCount > maxPolygonVertices) {
			return result;
		}

		result.reserve(static_cast<size_t>(pointCount));
		for (int i = 0; i < pointCount; ++i) {
			result.push_back({ points[i * 2], points[i * 2 + 1] });
		}
		return result;
	}

	static int WriteOverlapResult(std::optional<PhysicsBodyRef2D> result, uint64_t* entityID) {
		if (!entityID) return 0;
		*entityID = result.has_value() ? ToScriptEntityId(*result) : 0;
		return *entityID != 0 ? 1 : 0;
	}

	static int WriteOverlapResults(const std::vector<PhysicsBodyRef2D>& refs, uint64_t* outEntityIDs, int maxOut) {
		if (!outEntityIDs || maxOut <= 0) return 0;

		int count = 0;
		for (const PhysicsBodyRef2D& ref : refs) {
			uint64_t id = ToScriptEntityId(ref);
			if (id == 0) continue;

			if (count < maxOut) {
				outEntityIDs[count] = id;
			}
			++count;
		}
		return count;
	}

	static int Funke_Physics2D_OverlapCircle(float originX, float originY, float radius, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			Physics2D::OverlapCircleRef({ originX, originY }, radius, ToOverlapMode(mode)),
			entityID);
	}

	static int Funke_Physics2D_OverlapBox(float originX, float originY, float halfX, float halfY, float degrees, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			Physics2D::OverlapBoxRef({ originX, originY }, { halfX, halfY }, degrees, ToOverlapMode(mode)),
			entityID);
	}

	static int Funke_Physics2D_OverlapPolygon(float originX, float originY, const float* points, int pointCount, int mode, uint64_t* entityID) {
		std::vector<Vec2> polygon = ReadPolygonPoints(points, pointCount);
		if (polygon.empty()) {
			if (entityID) *entityID = 0;
			return 0;
		}

		return WriteOverlapResult(
			Physics2D::OverlapPolygonRef({ originX, originY }, polygon, ToOverlapMode(mode)),
			entityID);
	}

	static int Funke_Physics2D_OverlapCircleAll(float originX, float originY, float radius, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			Physics2D::OverlapCircleAllRefs({ originX, originY }, radius),
			outEntityIDs, maxOut);
	}

	static int Funke_Physics2D_OverlapBoxAll(float originX, float originY, float halfX, float halfY, float degrees, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			Physics2D::OverlapBoxAllRefs({ originX, originY }, { halfX, halfY }, degrees),
			outEntityIDs, maxOut);
	}

	static int Funke_Physics2D_OverlapPolygonAll(float originX, float originY, const float* points, int pointCount, uint64_t* outEntityIDs, int maxOut) {
		std::vector<Vec2> polygon = ReadPolygonPoints(points, pointCount);
		if (polygon.empty()) {
			return 0;
		}

		return WriteOverlapResults(
			Physics2D::OverlapPolygonAllRefs({ originX, originY }, polygon),
			outEntityIDs, maxOut);
	}

	static int Funke_Physics2D_ContainsPoint(float originX, float originY, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			Physics2D::ContainsPointRef({ originX, originY }, ToOverlapMode(mode)),
			entityID);
	}

	static int Funke_Physics2D_ContainsPointAll(float originX, float originY, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			Physics2D::ContainsPointAllRefs({ originX, originY }),
			outEntityIDs, maxOut);
	}

	// ── FastPhysics2D (Index-Physics) ────────────────────────────────────
	// Same script ABI as Physics2D_*, but routed to the Index-Physics world via
	// FastPhysics2D. Reuses the shared PhysicsBodyRef2D helpers below.

	static int Funke_FastPhysics2D_Raycast(
		float originX, float originY, float dirX, float dirY, float distance,
		uint64_t* hitEntityID, float* hitX, float* hitY, float* hitNormalX, float* hitNormalY, float* hitDistance)
	{
		if (!hitEntityID || !hitX || !hitY || !hitNormalX || !hitNormalY || !hitDistance) return 0;
		*hitEntityID = 0; *hitX = 0; *hitY = 0; *hitNormalX = 0; *hitNormalY = 0; *hitDistance = 0;

		auto result = FastPhysics2D::Raycast({ originX, originY }, { dirX, dirY }, distance);
		if (result.has_value()) {
			if (result->scene && result->scene->IsValid(result->entity)) {
				*hitEntityID = GetEntityScriptId(*result->scene, result->entity);
			}
			*hitX = result->point.x; *hitY = result->point.y;
			*hitNormalX = result->normal.x; *hitNormalY = result->normal.y;
			*hitDistance = result->distance;
			return 1;
		}
		return 0;
	}

	static int Funke_FastPhysics2D_OverlapCircle(float originX, float originY, float radius, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			FastPhysics2D::OverlapCircleRef({ originX, originY }, radius, ToOverlapMode(mode)),
			entityID);
	}

	static int Funke_FastPhysics2D_OverlapBox(float originX, float originY, float halfX, float halfY, float degrees, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			FastPhysics2D::OverlapBoxRef({ originX, originY }, { halfX, halfY }, degrees, ToOverlapMode(mode)),
			entityID);
	}

	static int Funke_FastPhysics2D_OverlapPolygon(float originX, float originY, const float* points, int pointCount, int mode, uint64_t* entityID) {
		std::vector<Vec2> polygon = ReadPolygonPoints(points, pointCount);
		if (polygon.empty()) {
			if (entityID) *entityID = 0;
			return 0;
		}

		return WriteOverlapResult(
			FastPhysics2D::OverlapPolygonRef({ originX, originY }, polygon, ToOverlapMode(mode)),
			entityID);
	}

	static int Funke_FastPhysics2D_OverlapCircleAll(float originX, float originY, float radius, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			FastPhysics2D::OverlapCircleAllRefs({ originX, originY }, radius),
			outEntityIDs, maxOut);
	}

	static int Funke_FastPhysics2D_OverlapBoxAll(float originX, float originY, float halfX, float halfY, float degrees, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			FastPhysics2D::OverlapBoxAllRefs({ originX, originY }, { halfX, halfY }, degrees),
			outEntityIDs, maxOut);
	}

	static int Funke_FastPhysics2D_OverlapPolygonAll(float originX, float originY, const float* points, int pointCount, uint64_t* outEntityIDs, int maxOut) {
		std::vector<Vec2> polygon = ReadPolygonPoints(points, pointCount);
		if (polygon.empty()) {
			return 0;
		}

		return WriteOverlapResults(
			FastPhysics2D::OverlapPolygonAllRefs({ originX, originY }, polygon),
			outEntityIDs, maxOut);
	}

	static int Funke_FastPhysics2D_ContainsPoint(float originX, float originY, int mode, uint64_t* entityID) {
		return WriteOverlapResult(
			FastPhysics2D::ContainsPointRef({ originX, originY }, ToOverlapMode(mode)),
			entityID);
	}

	static int Funke_FastPhysics2D_ContainsPointAll(float originX, float originY, uint64_t* outEntityIDs, int maxOut) {
		return WriteOverlapResults(
			FastPhysics2D::ContainsPointAllRefs({ originX, originY }),
			outEntityIDs, maxOut);
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
		b.Camera2D_GetZoom = FNK_GUARDED_BINDING(Funke_Camera2D_GetZoom);
		b.Camera2D_SetZoom = FNK_GUARDED_BINDING(Funke_Camera2D_SetZoom);
		b.Camera2D_GetClearColor = FNK_GUARDED_BINDING(Funke_Camera2D_GetClearColor);
		b.Camera2D_SetClearColor = FNK_GUARDED_BINDING(Funke_Camera2D_SetClearColor);
		b.Camera2D_ScreenToWorld = FNK_GUARDED_BINDING(Funke_Camera2D_ScreenToWorld);
		b.Camera2D_GetViewportWidth = FNK_GUARDED_BINDING(Funke_Camera2D_GetViewportWidth);
		b.Camera2D_GetViewportHeight = FNK_GUARDED_BINDING(Funke_Camera2D_GetViewportHeight);
		b.Camera2D_GetMainEntity = FNK_GUARDED_BINDING(Funke_Camera2D_GetMainEntity);
		b.Camera2D_GetFrustum = FNK_GUARDED_BINDING(Funke_Camera2D_GetFrustum);
		b.Rigidbody2D_ApplyForce = FNK_GUARDED_BINDING(Funke_Rigidbody2D_ApplyForce);
		b.Rigidbody2D_ApplyImpulse = FNK_GUARDED_BINDING(Funke_Rigidbody2D_ApplyImpulse);
		b.Rigidbody2D_GetLinearVelocity = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetLinearVelocity);
		b.Rigidbody2D_SetLinearVelocity = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetLinearVelocity);
		b.Rigidbody2D_GetAngularVelocity = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetAngularVelocity);
		b.Rigidbody2D_SetAngularVelocity = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetAngularVelocity);
		b.Rigidbody2D_GetBodyType = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetBodyType);
		b.Rigidbody2D_SetBodyType = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetBodyType);
		b.Rigidbody2D_GetGravityScale = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetGravityScale);
		b.Rigidbody2D_SetGravityScale = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetGravityScale);
		b.Rigidbody2D_GetMass = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetMass);
		b.Rigidbody2D_SetMass = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetMass);
		b.BoxCollider2D_GetScale = FNK_GUARDED_BINDING(Funke_BoxCollider2D_GetScale);
		b.BoxCollider2D_GetCenter = FNK_GUARDED_BINDING(Funke_BoxCollider2D_GetCenter);
		b.BoxCollider2D_SetEnabled = FNK_GUARDED_BINDING(Funke_BoxCollider2D_SetEnabled);
		b.CircleCollider2D_GetRadius = FNK_GUARDED_BINDING(Funke_CircleCollider2D_GetRadius);
		b.CircleCollider2D_SetRadius = FNK_GUARDED_BINDING(Funke_CircleCollider2D_SetRadius);
		b.CircleCollider2D_GetCenter = FNK_GUARDED_BINDING(Funke_CircleCollider2D_GetCenter);
		b.CircleCollider2D_SetCenter = FNK_GUARDED_BINDING(Funke_CircleCollider2D_SetCenter);
		b.CircleCollider2D_SetEnabled = FNK_GUARDED_BINDING(Funke_CircleCollider2D_SetEnabled);
		b.PolygonCollider2D_GetVertexCount = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_GetVertexCount);
		b.PolygonCollider2D_GetWorldPoints = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_GetWorldPoints);
		b.PolygonCollider2D_SetPoints = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_SetPoints);
		b.PolygonCollider2D_SetSides = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_SetSides);
		b.PolygonCollider2D_GetCenter = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_GetCenter);
		b.PolygonCollider2D_SetCenter = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_SetCenter);
		b.PolygonCollider2D_GetSize = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_GetSize);
		b.PolygonCollider2D_SetSize = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_SetSize);
		b.PolygonCollider2D_SetEnabled = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_SetEnabled);
		b.FastBody2D_GetBodyType = FNK_GUARDED_BINDING(Funke_FastBody2D_GetBodyType);
		b.FastBody2D_SetBodyType = FNK_GUARDED_BINDING(Funke_FastBody2D_SetBodyType);
		b.FastBody2D_GetMass = FNK_GUARDED_BINDING(Funke_FastBody2D_GetMass);
		b.FastBody2D_SetMass = FNK_GUARDED_BINDING(Funke_FastBody2D_SetMass);
		b.FastBody2D_GetUseGravity = FNK_GUARDED_BINDING(Funke_FastBody2D_GetUseGravity);
		b.FastBody2D_SetUseGravity = FNK_GUARDED_BINDING(Funke_FastBody2D_SetUseGravity);
		b.FastBody2D_GetVelocity = FNK_GUARDED_BINDING(Funke_FastBody2D_GetVelocity);
		b.FastBody2D_SetVelocity = FNK_GUARDED_BINDING(Funke_FastBody2D_SetVelocity);
		b.FastBoxCollider2D_GetHalfExtents = FNK_GUARDED_BINDING(Funke_FastBoxCollider2D_GetHalfExtents);
		b.FastBoxCollider2D_SetHalfExtents = FNK_GUARDED_BINDING(Funke_FastBoxCollider2D_SetHalfExtents);
		b.FastCircleCollider2D_GetRadius = FNK_GUARDED_BINDING(Funke_FastCircleCollider2D_GetRadius);
		b.FastCircleCollider2D_SetRadius = FNK_GUARDED_BINDING(Funke_FastCircleCollider2D_SetRadius);
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
		b.Physics2D_Raycast = FNK_GUARDED_BINDING(Funke_Physics2D_Raycast);
		b.Physics2D_SetQueryLayerMask = FNK_GUARDED_BINDING(Funke_Physics2D_SetQueryLayerMask);
		b.Collider2D_GetSensor = FNK_GUARDED_BINDING(Funke_Collider2D_GetSensor);
		b.Collider2D_SetSensor = FNK_GUARDED_BINDING(Funke_Collider2D_SetSensor);
		b.Collider2D_GetLayer = FNK_GUARDED_BINDING(Funke_Collider2D_GetLayer);
		b.Collider2D_SetLayer = FNK_GUARDED_BINDING(Funke_Collider2D_SetLayer);
		b.Collider2D_GetCollisionMask = FNK_GUARDED_BINDING(Funke_Collider2D_GetCollisionMask);
		b.Collider2D_SetCollisionMask = FNK_GUARDED_BINDING(Funke_Collider2D_SetCollisionMask);
		b.Collider2D_GetFriction = FNK_GUARDED_BINDING(Funke_Collider2D_GetFriction);
		b.Collider2D_SetFriction = FNK_GUARDED_BINDING(Funke_Collider2D_SetFriction);
		b.Collider2D_GetBounciness = FNK_GUARDED_BINDING(Funke_Collider2D_GetBounciness);
		b.Collider2D_SetBounciness = FNK_GUARDED_BINDING(Funke_Collider2D_SetBounciness);
		b.SpriteAnimator2D_Command = FNK_GUARDED_BINDING(Funke_SpriteAnimator2D_Command);
		b.SpriteAnimator2D_IsPlaying = FNK_GUARDED_BINDING(Funke_SpriteAnimator2D_IsPlaying);
		b.SpriteAnimator2D_GetFrame = FNK_GUARDED_BINDING(Funke_SpriteAnimator2D_GetFrame);
		b.SpriteAnimator2D_SetFrame = FNK_GUARDED_BINDING(Funke_SpriteAnimator2D_SetFrame);
		b.SpriteAnimator2D_GetFramesPerSecond = FNK_GUARDED_BINDING(Funke_SpriteAnimator2D_GetFramesPerSecond);
		b.SpriteAnimator2D_SetFramesPerSecond = FNK_GUARDED_BINDING(Funke_SpriteAnimator2D_SetFramesPerSecond);
		b.Physics2D_OverlapCircle = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapCircle);
		b.Physics2D_OverlapBox = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapBox);
		b.Physics2D_OverlapPolygon = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapPolygon);
		b.Physics2D_OverlapCircleAll = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapCircleAll);
		b.Physics2D_OverlapBoxAll = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapBoxAll);
		b.Physics2D_OverlapPolygonAll = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapPolygonAll);
		b.Physics2D_ContainsPoint = FNK_GUARDED_BINDING(Funke_Physics2D_ContainsPoint);
		b.Physics2D_ContainsPointAll = FNK_GUARDED_BINDING(Funke_Physics2D_ContainsPointAll);
		b.FastPhysics2D_Raycast = FNK_GUARDED_BINDING(Funke_FastPhysics2D_Raycast);
		b.FastPhysics2D_OverlapCircle = FNK_GUARDED_BINDING(Funke_FastPhysics2D_OverlapCircle);
		b.FastPhysics2D_OverlapBox = FNK_GUARDED_BINDING(Funke_FastPhysics2D_OverlapBox);
		b.FastPhysics2D_OverlapPolygon = FNK_GUARDED_BINDING(Funke_FastPhysics2D_OverlapPolygon);
		b.FastPhysics2D_OverlapCircleAll = FNK_GUARDED_BINDING(Funke_FastPhysics2D_OverlapCircleAll);
		b.FastPhysics2D_OverlapBoxAll = FNK_GUARDED_BINDING(Funke_FastPhysics2D_OverlapBoxAll);
		b.FastPhysics2D_OverlapPolygonAll = FNK_GUARDED_BINDING(Funke_FastPhysics2D_OverlapPolygonAll);
		b.FastPhysics2D_ContainsPoint = FNK_GUARDED_BINDING(Funke_FastPhysics2D_ContainsPoint);
		b.FastPhysics2D_ContainsPointAll = FNK_GUARDED_BINDING(Funke_FastPhysics2D_ContainsPointAll);
		b.EntityPicker_TryPickEntity = FNK_GUARDED_BINDING(Funke_EntityPicker_TryPickEntity);
		b.SpriteRenderer_GetFilter = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetFilter);
		b.SpriteRenderer_SetFilter = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetFilter);
		b.SpriteRenderer_GetSpriteNameBuffer = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetSpriteNameBuffer);
		b.SpriteRenderer_SetSpriteName       = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetSpriteName);
		b.SpriteRenderer_GetMaterial = FNK_GUARDED_BINDING(Funke_SpriteRenderer_GetMaterial);
		b.SpriteRenderer_SetMaterial = FNK_GUARDED_BINDING(Funke_SpriteRenderer_SetMaterial);
		b.Rigidbody2D_GetFreezePositionX = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetFreezePositionX);
		b.Rigidbody2D_SetFreezePositionX = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetFreezePositionX);
		b.Rigidbody2D_GetFreezePositionY = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetFreezePositionY);
		b.Rigidbody2D_SetFreezePositionY = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetFreezePositionY);
		b.Rigidbody2D_GetFreezeRotation  = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetFreezeRotation);
		b.Rigidbody2D_SetFreezeRotation  = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetFreezeRotation);
		b.ParticleSystem2D_GetTexture = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_GetTexture);
		b.ParticleSystem2D_SetTexture = FNK_GUARDED_BINDING(Funke_ParticleSystem2D_SetTexture);
	}

}
