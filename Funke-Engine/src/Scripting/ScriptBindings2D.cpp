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
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/Window.hpp"
#include "Graphics/TextureManager.hpp"
#include "Physics/Physics2D.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Scene/EntityPicker.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

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

	static Transform2DComponent ComputeWorldTransform2D(const Scene& scene, EntityHandle entity)
	{
		const auto& registry = scene.GetRegistry();
		std::vector<const Transform2DComponent*> ancestors;
		Transform2DComponent world;
		for (int depth = 0; depth < 1024 && registry.valid(entity); ++depth) {
			const auto* transform = registry.try_get<Transform2DComponent>(entity);
			if (!transform) break;
			// A clean physics body owns its world pose independently of its ancestors.
			if (!transform->IsDirty() && PhysicsSystem2D::IsTransformOwnedByPhysics(registry, entity)) {
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
			pointCount > PolygonCollider2DComponent::k_MaxPathPoints) {
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


	// ── 2D joints ───────────────────────────────────────────────────────
	// kind: 0 = Revolute, 1 = Prismatic, 2 = Weld (matches C# Joint2D.Kind). Packed fields: the 6 shared ones
	// (anchor x/y, connected anchor x/y, auto connected anchor, collide connected), then the type's own in
	// declaration order (see Joint2D.cs).

	constexpr int k_JointSharedValues = 6;
	constexpr int k_JointMaxValues = 17;

	static Joint2DBase* FindJoint(uint64_t entityID, int kind, Scene*& scene, EntityHandle& handle)
	{
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->IsValid(handle)) return nullptr;
		auto& registry = scene->GetRegistry();
		switch (kind) {
		case 0: return registry.try_get<RevoluteJoint2DComponent>(handle);
		case 1: return registry.try_get<PrismaticJoint2DComponent>(handle);
		case 2: return registry.try_get<WeldJoint2DComponent>(handle);
		default: return nullptr;
		}
	}

	static int PackJoint(const Joint2DBase& joint, int kind, float* v)
	{
		v[0] = joint.Anchor.x;
		v[1] = joint.Anchor.y;
		v[2] = joint.ConnectedAnchor.x;
		v[3] = joint.ConnectedAnchor.y;
		v[4] = joint.AutoConfigureConnectedAnchor ? 1.0f : 0.0f;
		v[5] = joint.CollideConnected ? 1.0f : 0.0f;
		int n = k_JointSharedValues;
		if (kind == 0) {
			const auto& j = static_cast<const RevoluteJoint2DComponent&>(joint);
			v[n++] = j.EnableLimit ? 1.0f : 0.0f; v[n++] = j.LowerAngle; v[n++] = j.UpperAngle;
			v[n++] = j.EnableMotor ? 1.0f : 0.0f; v[n++] = j.MotorSpeed; v[n++] = j.MaxMotorTorque;
			v[n++] = j.EnableSpring ? 1.0f : 0.0f; v[n++] = j.SpringHertz; v[n++] = j.SpringDampingRatio;
		}
		else if (kind == 1) {
			const auto& j = static_cast<const PrismaticJoint2DComponent&>(joint);
			v[n++] = j.Axis.x; v[n++] = j.Axis.y;
			v[n++] = j.EnableLimit ? 1.0f : 0.0f; v[n++] = j.LowerTranslation; v[n++] = j.UpperTranslation;
			v[n++] = j.EnableMotor ? 1.0f : 0.0f; v[n++] = j.MotorSpeed; v[n++] = j.MaxMotorForce;
			v[n++] = j.EnableSpring ? 1.0f : 0.0f; v[n++] = j.SpringHertz; v[n++] = j.SpringDampingRatio;
		}
		else if (kind == 2) {
			const auto& j = static_cast<const WeldJoint2DComponent&>(joint);
			v[n++] = j.LinearHertz; v[n++] = j.AngularHertz; v[n++] = j.LinearDampingRatio; v[n++] = j.AngularDampingRatio;
		}
		return n;
	}

	static int Funke_Joint2D_Get(uint64_t entityID, int kind, float* values, int count)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		const Joint2DBase* joint = FindJoint(entityID, kind, scene, handle);
		if (!joint || !values || count <= 0) return 0;
		float packed[k_JointMaxValues] = {};
		const int n = std::min(PackJoint(*joint, kind, packed), count);
		std::copy_n(packed, n, values);
		return n;
	}

	static void Funke_Joint2D_Set(uint64_t entityID, int kind, const float* values, int count)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		Joint2DBase* joint = FindJoint(entityID, kind, scene, handle);
		if (!joint || !values || count <= 0) return;
		float v[k_JointMaxValues] = {};
		const int total = PackJoint(*joint, kind, v);
		float before[k_JointMaxValues];
		std::copy_n(v, total, before);
		std::copy_n(values, std::min(count, total), v);

		joint->Anchor = { v[0], v[1] };
		joint->ConnectedAnchor = { v[2], v[3] };
		joint->AutoConfigureConnectedAnchor = v[4] != 0.0f;
		joint->CollideConnected = v[5] != 0.0f;
		int structuralEnd = k_JointSharedValues;
		int n = k_JointSharedValues;
		if (kind == 0) {
			auto& j = static_cast<RevoluteJoint2DComponent&>(*joint);
			j.EnableLimit = v[n++] != 0.0f; j.LowerAngle = v[n++]; j.UpperAngle = v[n++];
			j.EnableMotor = v[n++] != 0.0f; j.MotorSpeed = v[n++]; j.MaxMotorTorque = v[n++];
			j.EnableSpring = v[n++] != 0.0f; j.SpringHertz = v[n++]; j.SpringDampingRatio = v[n++];
		}
		else if (kind == 1) {
			auto& j = static_cast<PrismaticJoint2DComponent&>(*joint);
			j.Axis = { v[n], v[n + 1] };
			n += 2;
			structuralEnd = n;
			j.EnableLimit = v[n++] != 0.0f; j.LowerTranslation = v[n++]; j.UpperTranslation = v[n++];
			j.EnableMotor = v[n++] != 0.0f; j.MotorSpeed = v[n++]; j.MaxMotorForce = v[n++];
			j.EnableSpring = v[n++] != 0.0f; j.SpringHertz = v[n++]; j.SpringDampingRatio = v[n++];
		}
		else if (kind == 2) {
			auto& j = static_cast<WeldJoint2DComponent&>(*joint);
			j.LinearHertz = v[n++]; j.AngularHertz = v[n++]; j.LinearDampingRatio = v[n++]; j.AngularDampingRatio = v[n++];
		}

		// Motor speed set every frame must not rebuild the joint; only anchor/axis/collide changes do.
		if (!std::equal(before, before + structuralEnd, v)) joint->MarkStructuralChange();
		switch (kind) {
		case 0: static_cast<RevoluteJoint2DComponent&>(*joint).ApplyLive(); break;
		case 1: static_cast<PrismaticJoint2DComponent&>(*joint).ApplyLive(); break;
		case 2: static_cast<WeldJoint2DComponent&>(*joint).ApplyLive(); break;
		default: break;
		}
	}

	static uint64_t Funke_Joint2D_GetConnectedBody(uint64_t entityID, int kind)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		const Joint2DBase* joint = FindJoint(entityID, kind, scene, handle);
		if (!joint || joint->ConnectedBody == entt::null || !scene->IsValid(joint->ConnectedBody)) return 0;
		return GetEntityScriptId(*scene, joint->ConnectedBody);
	}

	static void Funke_Joint2D_SetConnectedBody(uint64_t entityID, int kind, uint64_t connectedID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		Joint2DBase* joint = FindJoint(entityID, kind, scene, handle);
		if (!joint) return;
		EntityHandle connected = entt::null;
		if (connectedID != 0) {
			Scene* otherScene = nullptr;
			if (!ResolveEntityReference(connectedID, otherScene, connected) || otherScene != scene) {
				FNK_CORE_WARN_TAG("Physics2D", "Joint ConnectedBody must be an entity of the same scene; keeping the old one");
				return;
			}
		}
		joint->ConnectedBody = connected;
		joint->MarkStructuralChange();
	}

	static void Funke_Joint2D_GetState(uint64_t entityID, int kind, float* values, int count)
	{
		if (!values || count <= 0) return;
		std::fill_n(values, count, 0.0f);
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		const Joint2DBase* joint = FindJoint(entityID, kind, scene, handle);
		if (!joint) return;
		float v[7] = {};
		v[0] = joint->IsCreated() ? 1.0f : 0.0f;
		const Vec2 force = joint->GetReactionForce();
		v[1] = force.x;
		v[2] = force.y;
		v[3] = joint->GetReactionTorque();
		if (kind == 0) {
			const auto& j = static_cast<const RevoluteJoint2DComponent&>(*joint);
			v[4] = j.GetJointAngle(); v[5] = j.GetJointSpeed(); v[6] = j.GetMotorTorque();
		}
		else if (kind == 1) {
			const auto& j = static_cast<const PrismaticJoint2DComponent&>(*joint);
			v[4] = j.GetJointTranslation(); v[5] = j.GetJointSpeed(); v[6] = j.GetMotorForce();
		}
		std::copy_n(v, std::min(count, 7), values);
	}

	static int Funke_Collider2D_GetGroupIndex(uint64_t entityID, int colliderType)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		return collider ? collider->GetGroupIndex() : 0;
	}

	static void Funke_Collider2D_SetGroupIndex(uint64_t entityID, int colliderType, int group)
	{
		Scene* scene = nullptr;
		if (Collider2D* collider = FindCollider(entityID, colliderType, scene)) collider->SetGroupIndex(group);
	}

	static float Funke_Collider2D_GetDensity(uint64_t entityID, int colliderType)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		return collider ? collider->GetDensity() : 0.0f;
	}

	static void Funke_Collider2D_SetDensity(uint64_t entityID, int colliderType, float density)
	{
		Scene* scene = nullptr;
		Collider2D* collider = FindCollider(entityID, colliderType, scene);
		if (!collider) return;
		collider->SetDensity(density);
		if (auto* rb = scene->GetRegistry().try_get<Rigidbody2DComponent>(collider->m_EntityHandle)) rb->ApplyMassFromState();
	}

	static int Funke_PolygonCollider2D_GetPathCount(uint64_t entityID)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, 0);
		return comp.GetPathCount();
	}

	static int Funke_PolygonCollider2D_GetPath(uint64_t entityID, int index, float* outPoints, int maxPoints)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, 0);
		if (index < 0 || index >= comp.GetPathCount()) return 0;
		const std::vector<Vec2>& path = comp.GetPath(index);
		const int n = outPoints ? std::min(static_cast<int>(path.size()), std::max(maxPoints, 0)) : 0;
		for (int i = 0; i < n; ++i) {
			outPoints[i * 2] = path[static_cast<size_t>(i)].x;
			outPoints[i * 2 + 1] = path[static_cast<size_t>(i)].y;
		}
		return static_cast<int>(path.size());
	}

	static void Funke_PolygonCollider2D_SetPaths(uint64_t entityID, const float* points, const int* counts, int pathCount)
	{
		GET_COMPONENT(PolygonCollider2DComponent, entityID, );
		if (!points || !counts || pathCount <= 0) return;
		std::vector<std::vector<Vec2>> paths;
		paths.reserve(static_cast<size_t>(std::min(pathCount, PolygonCollider2DComponent::k_MaxPaths)));
		size_t offset = 0;
		for (int p = 0; p < pathCount && p < PolygonCollider2DComponent::k_MaxPaths; ++p) {
			const int count = std::clamp(counts[p], 0, PolygonCollider2DComponent::k_MaxPathPoints);
			std::vector<Vec2> path;
			path.reserve(static_cast<size_t>(count));
			for (int i = 0; i < count; ++i) {
				path.push_back(Vec2{ points[(offset + i) * 2], points[(offset + i) * 2 + 1] });
			}
			offset += static_cast<size_t>(std::max(counts[p], 0));
			paths.push_back(std::move(path));
		}
		comp.SetPaths(paths, *scene);
	}

	static void Funke_Physics2D_GetGravity(float* outX, float* outY)
	{
		const Vec2 g = PhysicsSystem2D::IsInitialized() ? PhysicsSystem2D::GetMainPhysicsWorld().GetGravity() : Vec2{ 0.0f, 0.0f };
		if (outX) *outX = g.x;
		if (outY) *outY = g.y;
	}

	static void Funke_Physics2D_SetGravity(float x, float y)
	{
		if (PhysicsSystem2D::IsInitialized()) PhysicsSystem2D::GetMainPhysicsWorld().SetGravity(Vec2{ x, y });
	}

	// ── Rigidbody2D dynamics ─────────────────────────────────────────────

	static void Funke_Rigidbody2D_GetDynamics(uint64_t entityID, float* values, int count)
	{
		if (!values || count <= 0) return;
		std::fill_n(values, count, 0.0f);
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		const Vec2 local = comp.GetLocalCenterOfMass();
		const Vec2 world = comp.GetWorldCenterOfMass();
		const float v[8] = { comp.GetLinearDrag(), comp.GetAngularDrag(), comp.IsBullet() ? 1.0f : 0.0f, comp.GetInertia(),
			local.x, local.y, world.x, world.y };
		std::copy_n(v, std::min(count, 8), values);
	}

	static void Funke_Rigidbody2D_SetDynamics(uint64_t entityID, const float* values, int count)
	{
		if (!values || count < 3) return;
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		comp.SetLinearDrag(values[0]);
		comp.SetAngularDrag(values[1]);
		comp.SetBullet(values[2] != 0.0f);
	}

	static void Funke_Rigidbody2D_ApplyAtPoint(uint64_t entityID, int kind, float x, float y, float pointX, float pointY, int wake)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		if (kind == 0) comp.ApplyForceAtPoint(Vec2{ x, y }, Vec2{ pointX, pointY }, wake != 0);
		else comp.ApplyImpulseAtPoint(Vec2{ x, y }, Vec2{ pointX, pointY }, wake != 0);
	}

	static void Funke_Rigidbody2D_ApplyAngular(uint64_t entityID, int kind, float value, int wake)
	{
		GET_COMPONENT(Rigidbody2DComponent, entityID, );
		if (kind == 0) comp.ApplyTorque(value, wake != 0);
		else comp.ApplyAngularImpulse(value, wake != 0);
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
		b.Physics2D_OverlapCircle = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapCircle);
		b.Physics2D_OverlapBox = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapBox);
		b.Physics2D_OverlapPolygon = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapPolygon);
		b.Physics2D_OverlapCircleAll = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapCircleAll);
		b.Physics2D_OverlapBoxAll = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapBoxAll);
		b.Physics2D_OverlapPolygonAll = FNK_GUARDED_BINDING(Funke_Physics2D_OverlapPolygonAll);
		b.Physics2D_ContainsPoint = FNK_GUARDED_BINDING(Funke_Physics2D_ContainsPoint);
		b.Physics2D_ContainsPointAll = FNK_GUARDED_BINDING(Funke_Physics2D_ContainsPointAll);
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
		b.Joint2D_Get = FNK_GUARDED_BINDING(Funke_Joint2D_Get);
		b.Joint2D_Set = FNK_GUARDED_BINDING(Funke_Joint2D_Set);
		b.Joint2D_GetConnectedBody = FNK_GUARDED_BINDING(Funke_Joint2D_GetConnectedBody);
		b.Joint2D_SetConnectedBody = FNK_GUARDED_BINDING(Funke_Joint2D_SetConnectedBody);
		b.Joint2D_GetState = FNK_GUARDED_BINDING(Funke_Joint2D_GetState);
		b.Collider2D_GetDensity = FNK_GUARDED_BINDING(Funke_Collider2D_GetDensity);
		b.Collider2D_SetDensity = FNK_GUARDED_BINDING(Funke_Collider2D_SetDensity);
		b.PolygonCollider2D_GetPathCount = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_GetPathCount);
		b.PolygonCollider2D_GetPath = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_GetPath);
		b.PolygonCollider2D_SetPaths = FNK_GUARDED_BINDING(Funke_PolygonCollider2D_SetPaths);
		b.Physics2D_GetGravity = FNK_GUARDED_BINDING(Funke_Physics2D_GetGravity);
		b.Physics2D_SetGravity = FNK_GUARDED_BINDING(Funke_Physics2D_SetGravity);
		b.TerrainRenderer2D_Get = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_Get);
		b.TerrainRenderer2D_Set = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_Set);
		b.TerrainRenderer2D_GetPoints = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_GetPoints);
		b.TerrainRenderer2D_SetPoints = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_SetPoints);
		b.TerrainRenderer2D_GetTexture = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_GetTexture);
		b.TerrainRenderer2D_SetTexture = FNK_GUARDED_BINDING(Funke_TerrainRenderer2D_SetTexture);
		b.Rigidbody2D_GetDynamics = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetDynamics);
		b.Rigidbody2D_SetDynamics = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetDynamics);
		b.Rigidbody2D_ApplyAtPoint = FNK_GUARDED_BINDING(Funke_Rigidbody2D_ApplyAtPoint);
		b.Rigidbody2D_ApplyAngular = FNK_GUARDED_BINDING(Funke_Rigidbody2D_ApplyAngular);
		b.Collider2D_GetGroupIndex = FNK_GUARDED_BINDING(Funke_Collider2D_GetGroupIndex);
		b.Collider2D_SetGroupIndex = FNK_GUARDED_BINDING(Funke_Collider2D_SetGroupIndex);
	}

}
