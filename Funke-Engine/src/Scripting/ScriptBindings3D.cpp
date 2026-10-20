#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/ScriptGlue.hpp"

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Collections/Viewport.hpp"
#include "Core/Window.hpp"
#include "Scene/Scene.hpp"
#include "Systems/SkinnedMeshAnimationSystem.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// C# bindings for the 3D components. Split from ScriptBindings.cpp so 3D additions don't recompile that TU.
namespace Funke {
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);
	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle);
	Scene* GetScene();

	namespace {
		template <typename TComponent>
		TComponent* Resolve(uint64_t entityID) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(entityID, scene, handle) || !scene->IsValid(handle) || !scene->HasComponent<TComponent>(handle)) {
				return nullptr;
			}
			return &scene->GetComponent<TComponent>(handle);
		}

		void WriteVec3(const Vec3& value, float* x, float* y, float* z) {
			*x = value.x; *y = value.y; *z = value.z;
		}

		void WriteColor(const Color& value, float* r, float* g, float* b, float* a) {
			*r = value.r; *g = value.g; *b = value.b; *a = value.a;
		}

		// Packed arrays: callers pass their own count; only the overlapping prefix is read or written.
		struct PackedWriter {
			float* Values;
			int Count;
			int Index = 0;
			void Put(float value) { if (Values && Index < Count) Values[Index] = value; ++Index; }
			void Put(const Color& c) { Put(c.r); Put(c.g); Put(c.b); Put(c.a); }
		};

		struct PackedReader {
			const float* Values;
			int Count;
			int Index = 0;
			float Get(float fallback) { const float v = (Values && Index < Count) ? Values[Index] : fallback; ++Index; return v; }
			Color Get(const Color& fallback) {
				const float r = Get(fallback.r), g = Get(fallback.g), b = Get(fallback.b), a = Get(fallback.a);
				return Color(r, g, b, a);
			}
		};

		// ── Transform3D ─────────────────────────────────────────────────

		void Funke_Transform3D_GetPosition(uint64_t id, float* x, float* y, float* z) {
			const auto* c = Resolve<Transform3DComponent>(id);
			WriteVec3(c ? c->Position : Vec3(0.0f), x, y, z);
		}
		void Funke_Transform3D_SetPosition(uint64_t id, float x, float y, float z) {
			if (auto* c = Resolve<Transform3DComponent>(id)) c->Position = { x, y, z };
		}
		void Funke_Transform3D_GetRotation(uint64_t id, float* x, float* y, float* z) {
			const auto* c = Resolve<Transform3DComponent>(id);
			WriteVec3(c ? c->Rotation : Vec3(0.0f), x, y, z);
		}
		void Funke_Transform3D_SetRotation(uint64_t id, float x, float y, float z) {
			if (auto* c = Resolve<Transform3DComponent>(id)) c->Rotation = { x, y, z };
		}
		void Funke_Transform3D_GetScale(uint64_t id, float* x, float* y, float* z) {
			const auto* c = Resolve<Transform3DComponent>(id);
			WriteVec3(c ? c->Scale : Vec3(1.0f), x, y, z);
		}
		void Funke_Transform3D_SetScale(uint64_t id, float x, float y, float z) {
			if (auto* c = Resolve<Transform3DComponent>(id)) c->Scale = { x, y, z };
		}
		void Funke_Transform3D_GetWorldPosition(uint64_t id, float* x, float* y, float* z) {
			const auto* c = Resolve<Transform3DComponent>(id);
			WriteVec3(c ? c->GetWorldPosition() : Vec3(0.0f), x, y, z);
		}
		void Funke_Transform3D_GetForward(uint64_t id, float* x, float* y, float* z) {
			const auto* c = Resolve<Transform3DComponent>(id);
			WriteVec3(c ? c->Forward() : Vec3(0.0f, 0.0f, -1.0f), x, y, z);
		}

		// ── Camera3D ────────────────────────────────────────────────────

		int Funke_Camera3D_GetProjection(uint64_t id) {
			const auto* c = Resolve<Camera3DComponent>(id);
			return c ? static_cast<int>(c->Projection) : 0;
		}
		void Funke_Camera3D_SetProjection(uint64_t id, int projection) {
			if (projection < 0 || projection > static_cast<int>(CameraProjection::Orthographic)) return;
			if (auto* c = Resolve<Camera3DComponent>(id)) c->Projection = static_cast<CameraProjection>(projection);
		}
		float Funke_Camera3D_GetFieldOfView(uint64_t id) {
			const auto* c = Resolve<Camera3DComponent>(id);
			return c ? c->FieldOfView : 60.0f;
		}
		void Funke_Camera3D_SetFieldOfView(uint64_t id, float degrees) {
			if (auto* c = Resolve<Camera3DComponent>(id)) c->FieldOfView = degrees;
		}
		float Funke_Camera3D_GetOrthographicSize(uint64_t id) {
			const auto* c = Resolve<Camera3DComponent>(id);
			return c ? c->OrthographicSize : 5.0f;
		}
		void Funke_Camera3D_SetOrthographicSize(uint64_t id, float size) {
			if (auto* c = Resolve<Camera3DComponent>(id)) c->OrthographicSize = size;
		}
		float Funke_Camera3D_GetNearClip(uint64_t id) {
			const auto* c = Resolve<Camera3DComponent>(id);
			return c ? c->NearClip : 0.1f;
		}
		void Funke_Camera3D_SetNearClip(uint64_t id, float value) {
			if (auto* c = Resolve<Camera3DComponent>(id)) c->NearClip = value;
		}
		float Funke_Camera3D_GetFarClip(uint64_t id) {
			const auto* c = Resolve<Camera3DComponent>(id);
			return c ? c->FarClip : 1000.0f;
		}
		void Funke_Camera3D_SetFarClip(uint64_t id, float value) {
			if (auto* c = Resolve<Camera3DComponent>(id)) c->FarClip = value;
		}
		void Funke_Camera3D_GetClearColor(uint64_t id, float* r, float* g, float* b, float* a) {
			const auto* c = Resolve<Camera3DComponent>(id);
			WriteColor(c ? c->ClearColor : Camera3DComponent{}.ClearColor, r, g, b, a);
		}
		void Funke_Camera3D_SetClearColor(uint64_t id, float r, float g, float b, float a) {
			if (auto* c = Resolve<Camera3DComponent>(id)) c->ClearColor = Color(r, g, b, a);
		}
		uint64_t Funke_Camera3D_GetMainEntity() {
			Scene* scene = GetScene();
			if (!scene) return 0;
			const EntityHandle handle = Camera3DComponent::FindMain(*scene);
			return handle == entt::null ? 0 : GetEntityScriptId(*scene, handle);
		}

		// ── MeshRenderer ────────────────────────────────────────────────

		int Funke_MeshRenderer_GetMesh(uint64_t id) {
			const auto* c = Resolve<MeshRendererComponent>(id);
			return c ? static_cast<int>(c->Mesh) : 0;
		}
		void Funke_MeshRenderer_SetMesh(uint64_t id, int mesh) {
			if (mesh < 0 || mesh >= static_cast<int>(k_PrimitiveMeshTypeCount)) return;
			if (auto* c = Resolve<MeshRendererComponent>(id)) c->Mesh = static_cast<PrimitiveMeshType>(mesh);
		}
		void Funke_MeshRenderer_GetColor(uint64_t id, float* r, float* g, float* b, float* a) {
			const auto* c = Resolve<MeshRendererComponent>(id);
			WriteColor(c ? c->Color : Color(), r, g, b, a);
		}
		void Funke_MeshRenderer_SetColor(uint64_t id, float r, float g, float b, float a) {
			if (auto* c = Resolve<MeshRendererComponent>(id)) c->Color = Color(r, g, b, a);
		}
		int Funke_MeshRenderer_GetLit(uint64_t id) {
			const auto* c = Resolve<MeshRendererComponent>(id);
			return c && c->Lit ? 1 : 0;
		}
		void Funke_MeshRenderer_SetLit(uint64_t id, int lit) {
			if (auto* c = Resolve<MeshRendererComponent>(id)) c->Lit = lit != 0;
		}

		// ── DirectionalLight ────────────────────────────────────────────

		void Funke_DirectionalLight_GetColor(uint64_t id, float* r, float* g, float* b, float* a) {
			const auto* c = Resolve<DirectionalLightComponent>(id);
			WriteColor(c ? c->Color : Color(), r, g, b, a);
		}
		void Funke_DirectionalLight_SetColor(uint64_t id, float r, float g, float b, float a) {
			if (auto* c = Resolve<DirectionalLightComponent>(id)) c->Color = Color(r, g, b, a);
		}
		float Funke_DirectionalLight_GetIntensity(uint64_t id) {
			const auto* c = Resolve<DirectionalLightComponent>(id);
			return c ? c->Intensity : 1.0f;
		}
		void Funke_DirectionalLight_SetIntensity(uint64_t id, float intensity) {
			if (auto* c = Resolve<DirectionalLightComponent>(id)) c->Intensity = intensity;
		}
		void Funke_DirectionalLight_GetAmbient(uint64_t id, float* r, float* g, float* b, float* a) {
			const auto* c = Resolve<DirectionalLightComponent>(id);
			WriteColor(c ? c->Ambient : DirectionalLightComponent{}.Ambient, r, g, b, a);
		}
		void Funke_DirectionalLight_SetAmbient(uint64_t id, float r, float g, float b, float a) {
			if (auto* c = Resolve<DirectionalLightComponent>(id)) c->Ambient = Color(r, g, b, a);
		}
		int Funke_DirectionalLight_GetCastShadows(uint64_t id) {
			const auto* c = Resolve<DirectionalLightComponent>(id);
			return c && c->CastShadows ? 1 : 0;
		}
		void Funke_DirectionalLight_SetCastShadows(uint64_t id, int castShadows) {
			if (auto* c = Resolve<DirectionalLightComponent>(id)) c->CastShadows = castShadows != 0;
		}

		// ── MeshRenderer material / shadows ─────────────────────────────

		uint64_t Funke_MeshRenderer_GetMaterial(uint64_t id) {
			const auto* c = Resolve<MeshRendererComponent>(id);
			return c ? c->Material : 0;
		}
		void Funke_MeshRenderer_SetMaterial(uint64_t id, uint64_t materialGuid) {
			if (auto* c = Resolve<MeshRendererComponent>(id)) c->Material = materialGuid;
		}
		int Funke_MeshRenderer_GetCastShadows(uint64_t id) {
			const auto* c = Resolve<MeshRendererComponent>(id);
			return c && c->CastShadows ? 1 : 0;
		}
		void Funke_MeshRenderer_SetCastShadows(uint64_t id, int castShadows) {
			if (auto* c = Resolve<MeshRendererComponent>(id)) c->CastShadows = castShadows != 0;
		}

		uint64_t Funke_MeshRenderer_GetCustomMesh(uint64_t id) {
			const auto* c = Resolve<MeshRendererComponent>(id);
			return c ? c->CustomMesh : 0;
		}
		void Funke_MeshRenderer_SetCustomMesh(uint64_t id, uint64_t meshGuid) {
			if (auto* c = Resolve<MeshRendererComponent>(id)) c->CustomMesh = meshGuid;
		}

		// ── Mesh asset (read-only) ──────────────────────────────────────
		// Layout: vertexCount, triangleCount, boundsMin xyz, boundsMax xyz (8). Returns 0 when the asset can't be loaded.

		int Funke_Mesh_GetInfo(uint64_t guid, float* values, int count) {
			const MeshAsset* asset = MeshLibrary::Get(guid);
			PackedWriter out{ values, count };
			out.Put(asset ? static_cast<float>(asset->Data.Vertices.size()) : 0.0f);
			out.Put(asset ? static_cast<float>(asset->Data.Indices.size() / 3) : 0.0f);
			const glm::vec3 minPoint = asset ? asset->BoundsMin : glm::vec3(0.0f);
			const glm::vec3 maxPoint = asset ? asset->BoundsMax : glm::vec3(0.0f);
			out.Put(minPoint.x); out.Put(minPoint.y); out.Put(minPoint.z);
			out.Put(maxPoint.x); out.Put(maxPoint.y); out.Put(maxPoint.z);
			return asset ? 1 : 0;
		}

		// ── Runtime mesh ─────────────────────────────────────────────────

		uint64_t Funke_Mesh_CreateRuntime() {
			return MeshLibrary::CreateRuntime();
		}

		int Funke_Mesh_SetData(uint64_t guid, const float* positions, const float* normals, const float* uvs, int vertexCount,
			const uint32_t* indices, int indexCount) {
			if (vertexCount < 0 || indexCount < 0 || (vertexCount > 0 && !positions) || (indexCount > 0 && !indices)) return 0;
			if (!MeshLibrary::IsLiveRuntime(guid)) return 0;

			PrimitiveMeshData data;
			data.Vertices.resize(static_cast<size_t>(vertexCount));
			for (size_t i = 0; i < data.Vertices.size(); ++i) {
				MeshVertex& v = data.Vertices[i];
				for (int k = 0; k < 3; ++k) v.Position[k] = positions[i * 3 + k];
				for (int k = 0; k < 3; ++k) v.Normal[k] = normals ? normals[i * 3 + k] : 0.0f;
				for (int k = 0; k < 2; ++k) v.UV[k] = uvs ? uvs[i * 2 + k] : 0.0f;
			}
			data.Indices.assign(indices, indices + indexCount);
			if (!normals) RecalculateNormals(data);
			return MeshLibrary::SetRuntimeData(guid, std::move(data)) ? 1 : 0;
		}

		int Funke_Mesh_DestroyRuntime(uint64_t guid) {
			return MeshLibrary::DestroyRuntime(guid) ? 1 : 0;
		}

		int Funke_Mesh_IsRuntime(uint64_t guid) {
			return MeshLibrary::IsLiveRuntime(guid) ? 1 : 0;
		}

		// ── Material (live shared instance; files are not written) ──────
		// Layout: baseColor rgba, metallic, roughness, emissive rgb, emissiveIntensity, lit, tiling xy, offset xy, surface (16).

		int Funke_Material_Get(uint64_t guid, float* values, int count) {
			const Material* material = MaterialLibrary::Get(guid);
			const Material m = material ? *material : Material{};
			PackedWriter out{ values, count };
			out.Put(m.BaseColor);
			out.Put(m.Metallic);
			out.Put(m.Roughness);
			out.Put(m.EmissiveColor.r); out.Put(m.EmissiveColor.g); out.Put(m.EmissiveColor.b);
			out.Put(m.EmissiveIntensity);
			out.Put(m.Lit ? 1.0f : 0.0f);
			out.Put(m.Tiling.x); out.Put(m.Tiling.y);
			out.Put(m.Offset.x); out.Put(m.Offset.y);
			out.Put(static_cast<float>(m.Surface));
			return material ? 1 : 0;
		}
		void Funke_Material_Set(uint64_t guid, const float* values, int count) {
			const Material* current = MaterialLibrary::Get(guid);
			if (!current) return;
			Material m = *current;
			PackedReader in{ values, count };
			m.BaseColor = in.Get(m.BaseColor);
			m.Metallic = std::clamp(in.Get(m.Metallic), 0.0f, 1.0f);
			m.Roughness = std::clamp(in.Get(m.Roughness), 0.0f, 1.0f);
			const float er = in.Get(m.EmissiveColor.r), eg = in.Get(m.EmissiveColor.g), eb = in.Get(m.EmissiveColor.b);
			m.EmissiveColor = Color(er, eg, eb, 1.0f);
			m.EmissiveIntensity = std::max(in.Get(m.EmissiveIntensity), 0.0f);
			m.Lit = in.Get(m.Lit ? 1.0f : 0.0f) > 0.5f;
			m.Tiling.x = in.Get(m.Tiling.x); m.Tiling.y = in.Get(m.Tiling.y);
			m.Offset.x = in.Get(m.Offset.x); m.Offset.y = in.Get(m.Offset.y);
			const int surface = static_cast<int>(in.Get(static_cast<float>(m.Surface)) + 0.5f);
			if (surface >= 0 && surface < k_MaterialSurfaceCount) m.Surface = static_cast<MaterialSurface>(surface);
			MaterialLibrary::Set(guid, m);
		}
		uint64_t Funke_Material_GetTexture(uint64_t guid) {
			const Material* material = MaterialLibrary::Get(guid);
			return material ? material->BaseColorTexture : 0;
		}
		void Funke_Material_SetTexture(uint64_t guid, uint64_t textureGuid) {
			const Material* current = MaterialLibrary::Get(guid);
			if (!current) return;
			Material m = *current;
			m.BaseColorTexture = textureGuid;
			MaterialLibrary::Set(guid, m);
		}

		// ── Point / Spot lights ─────────────────────────────────────────
		// Point: color rgba, intensity, range (6). Spot: color rgba, intensity, range, innerAngle, outerAngle (8).

		void Funke_PointLight_Get(uint64_t id, float* values, int count) {
			const auto* c = Resolve<PointLightComponent>(id);
			const PointLightComponent v = c ? *c : PointLightComponent{};
			PackedWriter out{ values, count };
			out.Put(v.Color); out.Put(v.Intensity); out.Put(v.Range);
		}
		void Funke_PointLight_Set(uint64_t id, const float* values, int count) {
			auto* c = Resolve<PointLightComponent>(id);
			if (!c) return;
			PackedReader in{ values, count };
			c->Color = in.Get(c->Color);
			c->Intensity = std::max(in.Get(c->Intensity), 0.0f);
			c->Range = std::max(in.Get(c->Range), 0.0f);
		}
		void Funke_SpotLight_Get(uint64_t id, float* values, int count) {
			const auto* c = Resolve<SpotLightComponent>(id);
			const SpotLightComponent v = c ? *c : SpotLightComponent{};
			PackedWriter out{ values, count };
			out.Put(v.Color); out.Put(v.Intensity); out.Put(v.Range); out.Put(v.InnerAngle); out.Put(v.OuterAngle);
		}
		void Funke_SpotLight_Set(uint64_t id, const float* values, int count) {
			auto* c = Resolve<SpotLightComponent>(id);
			if (!c) return;
			PackedReader in{ values, count };
			c->Color = in.Get(c->Color);
			c->Intensity = std::max(in.Get(c->Intensity), 0.0f);
			c->Range = std::max(in.Get(c->Range), 0.0f);
			c->InnerAngle = std::clamp(in.Get(c->InnerAngle), 0.0f, 89.9f);
			c->OuterAngle = std::clamp(in.Get(c->OuterAngle), 0.1f, 89.9f);
		}

		// ── Camera3D sky ────────────────────────────────────────────────
		// Layout: background, skyTop rgba, skyHorizon rgba, skyGround rgba, sunSize, skyAmbient, ambientFromSkybox (16),
		// fog, fogColor rgba, fogStart, fogEnd (23).

		void Funke_Camera3D_GetSky(uint64_t id, float* values, int count) {
			const auto* c = Resolve<Camera3DComponent>(id);
			const Camera3DComponent v = c ? *c : Camera3DComponent{};
			PackedWriter out{ values, count };
			out.Put(static_cast<float>(v.Background));
			out.Put(v.SkyTopColor); out.Put(v.SkyHorizonColor); out.Put(v.SkyGroundColor);
			out.Put(v.SunSize); out.Put(v.SkyAmbient);
			out.Put(v.AmbientFromSkybox ? 1.0f : 0.0f);
			out.Put(v.FogEnabled ? 1.0f : 0.0f);
			out.Put(v.FogColor);
			out.Put(v.FogStart); out.Put(v.FogEnd);
		}
		void Funke_Camera3D_SetSky(uint64_t id, const float* values, int count) {
			auto* c = Resolve<Camera3DComponent>(id);
			if (!c) return;
			PackedReader in{ values, count };
			const int background = static_cast<int>(in.Get(static_cast<float>(c->Background)) + 0.5f);
			c->Background = background == static_cast<int>(CameraBackground::SolidColor) ? CameraBackground::SolidColor : CameraBackground::Skybox;
			c->SkyTopColor = in.Get(c->SkyTopColor);
			c->SkyHorizonColor = in.Get(c->SkyHorizonColor);
			c->SkyGroundColor = in.Get(c->SkyGroundColor);
			c->SunSize = std::clamp(in.Get(c->SunSize), 0.0f, 45.0f);
			c->SkyAmbient = std::max(in.Get(c->SkyAmbient), 0.0f);
			c->AmbientFromSkybox = in.Get(c->AmbientFromSkybox ? 1.0f : 0.0f) >= 0.5f;
			c->FogEnabled = in.Get(c->FogEnabled ? 1.0f : 0.0f) >= 0.5f;
			c->FogColor = in.Get(c->FogColor);
			c->FogStart = std::max(in.Get(c->FogStart), 0.0f);
			c->FogEnd = std::max(in.Get(c->FogEnd), 0.0f);
		}
		uint64_t Funke_Camera3D_GetSkyboxMaterial(uint64_t id) {
			const auto* c = Resolve<Camera3DComponent>(id);
			return c ? c->SkyboxMaterial : 0;
		}
		void Funke_Camera3D_SetSkyboxMaterial(uint64_t id, uint64_t materialGuid) {
			if (auto* c = Resolve<Camera3DComponent>(id)) c->SkyboxMaterial = materialGuid;
		}

		// The surface script screen points refer to (Input.MousePosition): the game view or aspect-locked region,
		// otherwise the main viewport.
		bool ScriptScreenSize(glm::vec2& size) {
			const Window::UIRegion region = Window::GetUIRegion();
			if (region.IsActive()) {
				size = { static_cast<float>(region.Width), static_cast<float>(region.Height) };
				return true;
			}
			const Viewport* viewport = Window::GetMainViewport();
			if (!viewport || viewport->GetWidth() <= 0 || viewport->GetHeight() <= 0) return false;
			size = { static_cast<float>(viewport->GetWidth()), static_cast<float>(viewport->GetHeight()) };
			return true;
		}

		bool ResolveCamera(uint64_t id, const Camera3DComponent*& camera, glm::mat4& world) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(id, scene, handle) || !scene->IsValid(handle) || !scene->HasComponent<Camera3DComponent>(handle)) {
				return false;
			}
			camera = &scene->GetComponent<Camera3DComponent>(handle);
			// Scripts may have moved the camera this frame; the hierarchy pass runs later.
			world = Transform3DHierarchySystem::ComputeWorldMatrix(scene->GetRegistry(), handle);
			return true;
		}

		int Funke_Camera3D_ScreenPointToRay(uint64_t id, float screenX, float screenY, float* values, int count) {
			if (!values || count < 6) return 0;
			const Camera3DComponent* camera = nullptr;
			glm::mat4 world(1.0f);
			glm::vec2 size(0.0f);
			if (!ResolveCamera(id, camera, world) || !ScriptScreenSize(size)) return 0;
			const Camera3DRay ray = Camera3DComponent::ScreenPointToRay(world, *camera, { screenX, screenY }, size);
			values[0] = ray.Origin.x; values[1] = ray.Origin.y; values[2] = ray.Origin.z;
			values[3] = ray.Direction.x; values[4] = ray.Direction.y; values[5] = ray.Direction.z;
			return 1;
		}

		int Funke_Camera3D_WorldToScreenPoint(uint64_t id, float x, float y, float z, float* values, int count) {
			if (!values || count < 3) return 0;
			const Camera3DComponent* camera = nullptr;
			glm::mat4 world(1.0f);
			glm::vec2 size(0.0f);
			if (!ResolveCamera(id, camera, world) || !ScriptScreenSize(size)) return 0;
			glm::vec3 screen(0.0f);
			const bool inFront = Camera3DComponent::WorldToScreenPoint(world, *camera, { x, y, z }, size, screen);
			values[0] = screen.x; values[1] = screen.y; values[2] = screen.z;
			return inFront ? 1 : 0;
		}
	}

	namespace {
		// ── ParticleSystem3D ────────────────────────────────────────────
		// Packed (21): lifeTime, speed, size, emitOverTime, color rgba, useGravity, gravity xyz, maxParticles,
		// playOnAwake, blendMode, simulationSpace, shape, radius, coneAngle, drag, angularVelocity.
		// Control ops: 0 Play, 1 Pause, 2 Stop, 3 Restart, 4 Clear (stop and remove live particles).

		void Funke_ParticleSystem3D_Get(uint64_t id, float* values, int count) {
			const auto* c = Resolve<ParticleSystem3DComponent>(id);
			const ParticleSystem3DComponent fallback;
			const ParticleSystem3DComponent& v = c ? *c : fallback;
			PackedWriter out{ values, count };
			out.Put(v.LifeTime); out.Put(v.Speed); out.Put(v.Size); out.Put(v.EmitOverTime); out.Put(v.Color);
			out.Put(v.UseGravity ? 1.0f : 0.0f); out.Put(v.Gravity.x); out.Put(v.Gravity.y); out.Put(v.Gravity.z);
			out.Put(static_cast<float>(v.MaxParticles)); out.Put(v.PlayOnAwake ? 1.0f : 0.0f);
			out.Put(static_cast<float>(v.BlendMode)); out.Put(static_cast<float>(v.SimulationSpace)); out.Put(static_cast<float>(v.Shape));
			out.Put(v.Radius); out.Put(v.ConeAngle); out.Put(v.Drag); out.Put(v.AngularVelocity);
		}
		void Funke_ParticleSystem3D_Set(uint64_t id, const float* values, int count) {
			auto* c = Resolve<ParticleSystem3DComponent>(id);
			if (!c) return;
			PackedReader in{ values, count };
			auto readEnum = [&in](auto current, int maxValue) {
				using TEnum = decltype(current);
				const int value = static_cast<int>(in.Get(static_cast<float>(current)));
				return (value < 0 || value > maxValue) ? current : static_cast<TEnum>(value);
			};
			c->LifeTime = std::max(in.Get(c->LifeTime), 0.001f);
			c->Speed = in.Get(c->Speed);
			c->Size = std::max(in.Get(c->Size), 0.0f);
			c->EmitOverTime = std::max(in.Get(c->EmitOverTime), 0.0f);
			c->Color = in.Get(c->Color);
			c->UseGravity = in.Get(c->UseGravity ? 1.0f : 0.0f) != 0.0f;
			c->Gravity.x = in.Get(c->Gravity.x);
			c->Gravity.y = in.Get(c->Gravity.y);
			c->Gravity.z = in.Get(c->Gravity.z);
			c->MaxParticles = static_cast<uint32_t>(std::clamp(in.Get(static_cast<float>(c->MaxParticles)), 1.0f, 1000000.0f));
			c->PlayOnAwake = in.Get(c->PlayOnAwake ? 1.0f : 0.0f) != 0.0f;
			c->BlendMode = readEnum(c->BlendMode, static_cast<int>(ParticleBlendMode3D::Additive));
			c->SimulationSpace = readEnum(c->SimulationSpace, static_cast<int>(ParticleSpace3D::World));
			c->Shape = readEnum(c->Shape, static_cast<int>(ParticleShape3D::Box));
			c->Radius = std::max(in.Get(c->Radius), 0.0f);
			c->ConeAngle = std::clamp(in.Get(c->ConeAngle), 0.0f, 89.9f);
			c->Drag = std::max(in.Get(c->Drag), 0.0f);
			c->AngularVelocity = in.Get(c->AngularVelocity);
		}
		void Funke_ParticleSystem3D_Control(uint64_t id, int op) {
			auto* c = Resolve<ParticleSystem3DComponent>(id);
			if (!c) return;
			switch (op) {
				case 0: c->Play(); break;
				case 1: c->Pause(); break;
				case 2: c->Stop(); break;
				case 3: c->Restart(); break;
				case 4: c->StopAndReset(); break;
				default: break;
			}
		}
		void Funke_ParticleSystem3D_Emit(uint64_t id, int count) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (count <= 0 || !ResolveEntityReference(id, scene, handle) || !scene->IsValid(handle)) return;
			auto* c = scene->GetRegistry().try_get<ParticleSystem3DComponent>(handle);
			if (!c) return;
			// A script that moved the emitter this frame expects the burst at the new pose, not the last Simulate's.
			c->SetEmitterWorld(Transform3DHierarchySystem::ComputeWorldMatrix(scene->GetRegistry(), handle));
			c->Emit(static_cast<size_t>(count));
		}
		// Bit 0 = emitting, bit 1 = simulating.
		int Funke_ParticleSystem3D_GetState(uint64_t id, int* particleCount) {
			const auto* c = Resolve<ParticleSystem3DComponent>(id);
			if (particleCount) *particleCount = c ? static_cast<int>(c->GetParticleCount()) : 0;
			if (!c) return 0;
			return (c->IsEmitting() ? 1 : 0) | (c->IsSimulating() ? 2 : 0);
		}
		uint64_t Funke_ParticleSystem3D_GetTexture(uint64_t id) {
			const auto* c = Resolve<ParticleSystem3DComponent>(id);
			return c ? c->Texture : 0;
		}
		void Funke_ParticleSystem3D_SetTexture(uint64_t id, uint64_t textureGuid) {
			if (auto* c = Resolve<ParticleSystem3DComponent>(id)) c->Texture = textureGuid;
		}
		// ── SkinnedMeshRenderer ─────────────────────────────────────────

		const SkinnedMeshData* SkinOf(const SkinnedMeshRendererComponent& c) {
			const MeshAsset* asset = MeshLibrary::Get(c.Model);
			return asset ? asset->Skin.get() : nullptr;
		}

		int CopyToBuffer(std::string_view value, char* outBuffer, int capacity) {
			const int required = static_cast<int>(value.size());
			if (outBuffer && capacity > 0) {
				const int count = std::min(required, capacity - 1);
				if (count > 0) std::memcpy(outBuffer, value.data(), static_cast<size_t>(count));
				outBuffer[count] = '\0';
			}
			return required;
		}

		// speed, loop, playOnAwake, time, playing, lit, castShadows, color rgba, cullingMode, distanceRate,
		// halfRateDistance, quarterRateDistance
		void Funke_SkinnedMeshRenderer_Get(uint64_t id, float* values, int count) {
			const auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			const SkinnedMeshRendererComponent fallback;
			const SkinnedMeshRendererComponent& v = c ? *c : fallback;
			PackedWriter out{ values, count };
			out.Put(v.Speed); out.Put(v.Loop ? 1.0f : 0.0f); out.Put(v.PlayOnAwake ? 1.0f : 0.0f); out.Put(v.Time);
			out.Put(v.Playing ? 1.0f : 0.0f); out.Put(v.Lit ? 1.0f : 0.0f); out.Put(v.CastShadows ? 1.0f : 0.0f); out.Put(v.Color);
			out.Put(static_cast<float>(v.CullingMode)); out.Put(static_cast<float>(v.DistanceRate));
			out.Put(v.HalfRateDistance); out.Put(v.QuarterRateDistance);
		}
		// Same layout; playing is read-only (Play / Control change it).
		void Funke_SkinnedMeshRenderer_Set(uint64_t id, const float* values, int count) {
			auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			if (!c) return;
			PackedReader in{ values, count };
			c->Speed = in.Get(c->Speed);
			c->Loop = in.Get(c->Loop ? 1.0f : 0.0f) != 0.0f;
			c->PlayOnAwake = in.Get(c->PlayOnAwake ? 1.0f : 0.0f) != 0.0f;
			const float time = in.Get(c->Time);
			const SkinnedMeshData* data = SkinOf(*c);
			const int clip = data ? SkinnedMeshAnimationSystem::ResolveClip(*c, *data) : -1;
			c->Time = clip >= 0 ? std::clamp(time, 0.0f, data->Clips[static_cast<size_t>(clip)].Duration) : std::max(time, 0.0f);
			in.Get(0.0f);
			c->Lit = in.Get(c->Lit ? 1.0f : 0.0f) != 0.0f;
			c->CastShadows = in.Get(c->CastShadows ? 1.0f : 0.0f) != 0.0f;
			c->Color = in.Get(c->Color);
			c->CullingMode = static_cast<AnimationCullingMode>(std::clamp(static_cast<int>(in.Get(static_cast<float>(c->CullingMode))), 0, 2));
			c->DistanceRate = static_cast<AnimationDistanceRate>(std::clamp(static_cast<int>(in.Get(static_cast<float>(c->DistanceRate))), 0, 2));
			c->HalfRateDistance = std::max(in.Get(c->HalfRateDistance), 0.0f);
			c->QuarterRateDistance = std::max(in.Get(c->QuarterRateDistance), 0.0f);
		}
		// which: 0 = model, 1 = material
		uint64_t Funke_SkinnedMeshRenderer_GetAsset(uint64_t id, int which) {
			const auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			if (!c) return 0;
			return which == 0 ? c->Model : c->Material;
		}
		void Funke_SkinnedMeshRenderer_SetAsset(uint64_t id, int which, uint64_t guid) {
			auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			if (!c) return;
			if (which == 0) c->Model = guid;
			else c->Material = guid;
		}
		void Funke_SkinnedMeshRenderer_Play(uint64_t id, const char* clip, float crossFade) {
			auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			if (!c) return;
			SkinnedMeshAnimationSystem::Play(*c, SkinOf(*c), clip ? std::string(clip) : std::string{}, std::max(crossFade, 0.0f));
		}
		// op: 0 = pause, 1 = resume, 2 = stop (back to the clip's start)
		void Funke_SkinnedMeshRenderer_Control(uint64_t id, int op) {
			auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			if (!c) return;
			c->Awoken = true;
			switch (op) {
				case 0: c->Playing = false; break;
				case 1: c->Playing = true; break;
				case 2:
					c->Playing = false;
					c->Time = 0.0f;
					c->PreviousClip = -1;
					c->Fade = 1.0f;
					break;
				default: break;
			}
		}
		// The clip that plays (the first one when Clip is empty); "" when the model has none.
		int Funke_SkinnedMeshRenderer_GetClip(uint64_t id, char* outBuffer, int capacity) {
			const auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			const SkinnedMeshData* data = c ? SkinOf(*c) : nullptr;
			const int clip = data ? SkinnedMeshAnimationSystem::ResolveClip(*c, *data) : -1;
			return CopyToBuffer(clip >= 0 ? std::string_view(data->Clips[static_cast<size_t>(clip)].Name) : std::string_view{}, outBuffer, capacity);
		}
		int Funke_SkinnedMeshRenderer_GetClipCount(uint64_t id) {
			const auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			const SkinnedMeshData* data = c ? SkinOf(*c) : nullptr;
			return data ? static_cast<int>(data->Clips.size()) : 0;
		}
		int Funke_SkinnedMeshRenderer_GetClipName(uint64_t id, int index, char* outBuffer, int capacity) {
			const auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			const SkinnedMeshData* data = c ? SkinOf(*c) : nullptr;
			const bool valid = data && index >= 0 && static_cast<size_t>(index) < data->Clips.size();
			return CopyToBuffer(valid ? std::string_view(data->Clips[static_cast<size_t>(index)].Name) : std::string_view{}, outBuffer, capacity);
		}
		float Funke_SkinnedMeshRenderer_GetClipDuration(uint64_t id, int index) {
			const auto* c = Resolve<SkinnedMeshRendererComponent>(id);
			const SkinnedMeshData* data = c ? SkinOf(*c) : nullptr;
			const bool valid = data && index >= 0 && static_cast<size_t>(index) < data->Clips.size();
			return valid ? data->Clips[static_cast<size_t>(index)].Duration : 0.0f;
		}
		// World position, forward (-Z) and up (+Y) of a bone or any other model node in the current pose:
		// 9 floats. Returns 0 when the node doesn't exist.
		int Funke_SkinnedMeshRenderer_GetBone(uint64_t id, const char* bone, float* values, int count) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!bone || !ResolveEntityReference(id, scene, handle) || !scene->IsValid(handle)) return 0;
			const auto* c = scene->GetRegistry().try_get<SkinnedMeshRendererComponent>(handle);
			const SkinnedMeshData* data = c ? SkinOf(*c) : nullptr;
			const int node = data ? data->FindNode(bone) : -1;
			if (node < 0) return 0;
			Transform3DHierarchySystem::Propagate(*scene);
			const auto* transform = scene->GetRegistry().try_get<Transform3DComponent>(handle);
			std::vector<glm::mat4> nodes;
			// The drawn pose, so attachments stay on a model whose pose is held back far away or off screen.
			SkeletalAnimation::EvaluatePose(*data, SkinnedMeshAnimationSystem::GetDrawnPose(*c, *data), nodes);
			const glm::mat4 world = (transform ? transform->WorldMatrix : glm::mat4(1.0f)) * nodes[static_cast<size_t>(node)];
			const glm::vec3 forward = glm::normalize(-glm::vec3(world[2]));
			const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
			PackedWriter out{ values, count };
			for (int i = 0; i < 3; ++i) out.Put(world[3][i]);
			for (int i = 0; i < 3; ++i) out.Put(forward[i]);
			for (int i = 0; i < 3; ++i) out.Put(up[i]);
			return 1;
		}
	}

	// Entity.SetParent(worldPositionStays): keeps the Transform3D world pose across the reparent.
	bool SetParentKeepingWorld3D(Scene& scene, EntityHandle handle, Entity newParent)
	{
		if (!scene.HasComponent<Transform3DComponent>(handle)) return false;
		// Not WorldMatrix: that is the last Propagate's cache and misses moves the script made since.
		const entt::registry& registry = scene.GetRegistry();
		const glm::mat4 world = Transform3DHierarchySystem::ComputeWorldMatrix(registry, handle);

		Entity entity = scene.GetEntity(handle);
		entity.SetParent(newParent);

		// The parent SetParent actually kept: it refuses cycles and foreign scenes.
		const Entity parent = entity.GetParent();
		const glm::mat4 parentWorld = parent.IsValid()
			? Transform3DHierarchySystem::ComputeWorldMatrix(registry, parent.GetHandle())
			: glm::mat4(1.0f);
		// A zero-scaled parent has no inverse; keep the local transform rather than writing NaNs.
		if (std::abs(glm::determinant(glm::mat3(parentWorld))) < 1e-12f) return true;

		auto& transform = scene.GetComponent<Transform3DComponent>(handle);
		Transform3DComponent::Decompose(glm::inverse(parentWorld) * world, transform.Position, transform.Rotation, transform.Scale);
		transform.WorldMatrix = world;
		return true;
	}

	void Populate3DBindings(NativeBindings& b) {
		b.Transform3D_GetPosition = FNK_GUARDED_BINDING(Funke_Transform3D_GetPosition);
		b.Transform3D_SetPosition = FNK_GUARDED_BINDING(Funke_Transform3D_SetPosition);
		b.Transform3D_GetRotation = FNK_GUARDED_BINDING(Funke_Transform3D_GetRotation);
		b.Transform3D_SetRotation = FNK_GUARDED_BINDING(Funke_Transform3D_SetRotation);
		b.Transform3D_GetScale = FNK_GUARDED_BINDING(Funke_Transform3D_GetScale);
		b.Transform3D_SetScale = FNK_GUARDED_BINDING(Funke_Transform3D_SetScale);
		b.Transform3D_GetWorldPosition = FNK_GUARDED_BINDING(Funke_Transform3D_GetWorldPosition);
		b.Transform3D_GetForward = FNK_GUARDED_BINDING(Funke_Transform3D_GetForward);
		b.Camera3D_GetProjection = FNK_GUARDED_BINDING(Funke_Camera3D_GetProjection);
		b.Camera3D_SetProjection = FNK_GUARDED_BINDING(Funke_Camera3D_SetProjection);
		b.Camera3D_GetFieldOfView = FNK_GUARDED_BINDING(Funke_Camera3D_GetFieldOfView);
		b.Camera3D_SetFieldOfView = FNK_GUARDED_BINDING(Funke_Camera3D_SetFieldOfView);
		b.Camera3D_GetOrthographicSize = FNK_GUARDED_BINDING(Funke_Camera3D_GetOrthographicSize);
		b.Camera3D_SetOrthographicSize = FNK_GUARDED_BINDING(Funke_Camera3D_SetOrthographicSize);
		b.Camera3D_GetNearClip = FNK_GUARDED_BINDING(Funke_Camera3D_GetNearClip);
		b.Camera3D_SetNearClip = FNK_GUARDED_BINDING(Funke_Camera3D_SetNearClip);
		b.Camera3D_GetFarClip = FNK_GUARDED_BINDING(Funke_Camera3D_GetFarClip);
		b.Camera3D_SetFarClip = FNK_GUARDED_BINDING(Funke_Camera3D_SetFarClip);
		b.Camera3D_GetClearColor = FNK_GUARDED_BINDING(Funke_Camera3D_GetClearColor);
		b.Camera3D_SetClearColor = FNK_GUARDED_BINDING(Funke_Camera3D_SetClearColor);
		b.Camera3D_GetMainEntity = FNK_GUARDED_BINDING(Funke_Camera3D_GetMainEntity);
		b.MeshRenderer_GetMesh = FNK_GUARDED_BINDING(Funke_MeshRenderer_GetMesh);
		b.MeshRenderer_SetMesh = FNK_GUARDED_BINDING(Funke_MeshRenderer_SetMesh);
		b.MeshRenderer_GetColor = FNK_GUARDED_BINDING(Funke_MeshRenderer_GetColor);
		b.MeshRenderer_SetColor = FNK_GUARDED_BINDING(Funke_MeshRenderer_SetColor);
		b.MeshRenderer_GetLit = FNK_GUARDED_BINDING(Funke_MeshRenderer_GetLit);
		b.MeshRenderer_SetLit = FNK_GUARDED_BINDING(Funke_MeshRenderer_SetLit);
		b.DirectionalLight_GetColor = FNK_GUARDED_BINDING(Funke_DirectionalLight_GetColor);
		b.DirectionalLight_SetColor = FNK_GUARDED_BINDING(Funke_DirectionalLight_SetColor);
		b.DirectionalLight_GetIntensity = FNK_GUARDED_BINDING(Funke_DirectionalLight_GetIntensity);
		b.DirectionalLight_SetIntensity = FNK_GUARDED_BINDING(Funke_DirectionalLight_SetIntensity);
		b.DirectionalLight_GetAmbient = FNK_GUARDED_BINDING(Funke_DirectionalLight_GetAmbient);
		b.DirectionalLight_SetAmbient = FNK_GUARDED_BINDING(Funke_DirectionalLight_SetAmbient);
		b.MeshRenderer_GetMaterial = FNK_GUARDED_BINDING(Funke_MeshRenderer_GetMaterial);
		b.MeshRenderer_SetMaterial = FNK_GUARDED_BINDING(Funke_MeshRenderer_SetMaterial);
		b.MeshRenderer_GetCastShadows = FNK_GUARDED_BINDING(Funke_MeshRenderer_GetCastShadows);
		b.MeshRenderer_SetCastShadows = FNK_GUARDED_BINDING(Funke_MeshRenderer_SetCastShadows);
		b.DirectionalLight_GetCastShadows = FNK_GUARDED_BINDING(Funke_DirectionalLight_GetCastShadows);
		b.DirectionalLight_SetCastShadows = FNK_GUARDED_BINDING(Funke_DirectionalLight_SetCastShadows);
		b.Material_Get = FNK_GUARDED_BINDING(Funke_Material_Get);
		b.Material_Set = FNK_GUARDED_BINDING(Funke_Material_Set);
		b.Material_GetTexture = FNK_GUARDED_BINDING(Funke_Material_GetTexture);
		b.Material_SetTexture = FNK_GUARDED_BINDING(Funke_Material_SetTexture);
		b.PointLight_Get = FNK_GUARDED_BINDING(Funke_PointLight_Get);
		b.PointLight_Set = FNK_GUARDED_BINDING(Funke_PointLight_Set);
		b.SpotLight_Get = FNK_GUARDED_BINDING(Funke_SpotLight_Get);
		b.SpotLight_Set = FNK_GUARDED_BINDING(Funke_SpotLight_Set);
		b.Camera3D_GetSky = FNK_GUARDED_BINDING(Funke_Camera3D_GetSky);
		b.Camera3D_SetSky = FNK_GUARDED_BINDING(Funke_Camera3D_SetSky);
		b.MeshRenderer_GetCustomMesh = FNK_GUARDED_BINDING(Funke_MeshRenderer_GetCustomMesh);
		b.MeshRenderer_SetCustomMesh = FNK_GUARDED_BINDING(Funke_MeshRenderer_SetCustomMesh);
		b.Mesh_GetInfo = FNK_GUARDED_BINDING(Funke_Mesh_GetInfo);
		b.Mesh_CreateRuntime = FNK_GUARDED_BINDING(Funke_Mesh_CreateRuntime);
		b.Mesh_SetData = FNK_GUARDED_BINDING(Funke_Mesh_SetData);
		b.Mesh_DestroyRuntime = FNK_GUARDED_BINDING(Funke_Mesh_DestroyRuntime);
		b.Mesh_IsRuntime = FNK_GUARDED_BINDING(Funke_Mesh_IsRuntime);
		b.ParticleSystem3D_Get = FNK_GUARDED_BINDING(Funke_ParticleSystem3D_Get);
		b.ParticleSystem3D_Set = FNK_GUARDED_BINDING(Funke_ParticleSystem3D_Set);
		b.ParticleSystem3D_Control = FNK_GUARDED_BINDING(Funke_ParticleSystem3D_Control);
		b.ParticleSystem3D_Emit = FNK_GUARDED_BINDING(Funke_ParticleSystem3D_Emit);
		b.ParticleSystem3D_GetState = FNK_GUARDED_BINDING(Funke_ParticleSystem3D_GetState);
		b.ParticleSystem3D_GetTexture = FNK_GUARDED_BINDING(Funke_ParticleSystem3D_GetTexture);
		b.ParticleSystem3D_SetTexture = FNK_GUARDED_BINDING(Funke_ParticleSystem3D_SetTexture);
		b.Camera3D_GetSkyboxMaterial = FNK_GUARDED_BINDING(Funke_Camera3D_GetSkyboxMaterial);
		b.Camera3D_SetSkyboxMaterial = FNK_GUARDED_BINDING(Funke_Camera3D_SetSkyboxMaterial);
		b.Camera3D_ScreenPointToRay = FNK_GUARDED_BINDING(Funke_Camera3D_ScreenPointToRay);
		b.Camera3D_WorldToScreenPoint = FNK_GUARDED_BINDING(Funke_Camera3D_WorldToScreenPoint);
		b.SkinnedMeshRenderer_Get = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_Get);
		b.SkinnedMeshRenderer_Set = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_Set);
		b.SkinnedMeshRenderer_GetAsset = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_GetAsset);
		b.SkinnedMeshRenderer_SetAsset = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_SetAsset);
		b.SkinnedMeshRenderer_Play = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_Play);
		b.SkinnedMeshRenderer_Control = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_Control);
		b.SkinnedMeshRenderer_GetClip = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_GetClip);
		b.SkinnedMeshRenderer_GetClipCount = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_GetClipCount);
		b.SkinnedMeshRenderer_GetClipName = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_GetClipName);
		b.SkinnedMeshRenderer_GetClipDuration = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_GetClipDuration);
		b.SkinnedMeshRenderer_GetBone = FNK_GUARDED_BINDING(Funke_SkinnedMeshRenderer_GetBone);
	}
}
