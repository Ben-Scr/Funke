#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/ScriptGlue.hpp"

#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Physics3D/PhysicsSystem3D.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>

// C# bindings for Jolt 3D physics. Packed layouts:
//   Rigidbody3D_Get/Set:  [type, mass, linearDrag, angularDrag, gravityScale, continuous, freezeMask]
//                         freezeMask bits: 0..2 = position XYZ, 3..5 = rotation XYZ
//   Collider3D_Get/Set:   kind 0 = Box, 1 = Sphere, 2 = Capsule, 3 = Mesh
//                         [centerX, centerY, centerZ, isTrigger, friction, bounciness, p0, p1, p2]
//                         Box p = size XYZ; Sphere p0 = radius; Capsule p = radius, height, direction;
//                         Mesh p = useMeshRenderer, mesh, convex
//   Rigidbody3D_AddForce: kind 0 = force, 1 = force at position (px,py,pz), 2 = torque; mode = ForceMode3D
//   Physics3D_Raycast:    hitValues = [pointX, pointY, pointZ, normalX, normalY, normalZ, distance]
namespace Funke {
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);
	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle);
	Scene* GetScene();

	namespace {
		constexpr int k_RigidbodyValueCount = 7;
		constexpr int k_ColliderValueCount = 9;
		constexpr int k_RaycastValueCount = 7;

		template <typename TComponent>
		TComponent* Resolve(uint64_t entityID, Scene** outScene = nullptr, EntityHandle* outHandle = nullptr) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(entityID, scene, handle) || !scene->IsValid(handle) || !scene->HasComponent<TComponent>(handle)) {
				return nullptr;
			}
			if (outScene) *outScene = scene;
			if (outHandle) *outHandle = handle;
			return &scene->GetComponent<TComponent>(handle);
		}

		// The live body behind a Rigidbody3D; null outside play mode.
		PhysicsWorld3D* ResolveBody(uint64_t entityID, EntityHandle& outOwner) {
			Scene* scene = nullptr;
			if (!Resolve<Rigidbody3DComponent>(entityID, &scene, &outOwner)) return nullptr;
			PhysicsWorld3D* world = PhysicsSystem3D::FindWorld(*scene);
			return world && world->EnsureBody(*scene, outOwner) ? world : nullptr;
		}

		void Funke_Rigidbody3D_Get(uint64_t id, float* values, int count) {
			const Rigidbody3DComponent c = [&] {
				const auto* rb = Resolve<Rigidbody3DComponent>(id);
				return rb ? *rb : Rigidbody3DComponent{};
			}();
			const int freezeMask = (c.FreezePositionX ? 1 : 0) | (c.FreezePositionY ? 2 : 0) | (c.FreezePositionZ ? 4 : 0)
				| (c.FreezeRotationX ? 8 : 0) | (c.FreezeRotationY ? 16 : 0) | (c.FreezeRotationZ ? 32 : 0);
			const float packed[k_RigidbodyValueCount] = {
				static_cast<float>(c.Type), c.Mass, c.LinearDrag, c.AngularDrag, c.GravityScale,
				c.ContinuousCollision ? 1.0f : 0.0f, static_cast<float>(freezeMask) };
			std::copy_n(packed, std::clamp(count, 0, k_RigidbodyValueCount), values);
		}

		void Funke_Rigidbody3D_Set(uint64_t id, const float* values, int count) {
			auto* c = Resolve<Rigidbody3DComponent>(id);
			if (!c || count < k_RigidbodyValueCount) return;
			const int type = static_cast<int>(values[0]);
			if (type >= static_cast<int>(BodyType::Static) && type <= static_cast<int>(BodyType::Dynamic)) {
				c->Type = static_cast<BodyType>(type);
			}
			c->Mass = std::max(values[1], 0.0001f);
			c->LinearDrag = std::max(values[2], 0.0f);
			c->AngularDrag = std::max(values[3], 0.0f);
			c->GravityScale = values[4];
			c->ContinuousCollision = values[5] != 0.0f;
			const int freezeMask = static_cast<int>(values[6]);
			c->FreezePositionX = (freezeMask & 1) != 0;
			c->FreezePositionY = (freezeMask & 2) != 0;
			c->FreezePositionZ = (freezeMask & 4) != 0;
			c->FreezeRotationX = (freezeMask & 8) != 0;
			c->FreezeRotationY = (freezeMask & 16) != 0;
			c->FreezeRotationZ = (freezeMask & 32) != 0;
		}

		void Funke_Rigidbody3D_GetVelocity(uint64_t id, int angular, float* x, float* y, float* z) {
			EntityHandle owner = entt::null;
			Vec3 velocity(0.0f);
			if (PhysicsWorld3D* world = ResolveBody(id, owner)) {
				velocity = angular ? world->GetAngularVelocity(owner) : world->GetLinearVelocity(owner);
			}
			*x = velocity.x; *y = velocity.y; *z = velocity.z;
		}

		void Funke_Rigidbody3D_SetVelocity(uint64_t id, int angular, float x, float y, float z) {
			EntityHandle owner = entt::null;
			PhysicsWorld3D* world = ResolveBody(id, owner);
			if (!world) return;
			if (angular) world->SetAngularVelocity(owner, { x, y, z });
			else world->SetLinearVelocity(owner, { x, y, z });
		}

		void Funke_Rigidbody3D_AddForce(uint64_t id, int kind, int mode, float x, float y, float z, float px, float py, float pz) {
			if (mode < 0 || mode > static_cast<int>(ForceMode3D::VelocityChange)) return;
			EntityHandle owner = entt::null;
			PhysicsWorld3D* world = ResolveBody(id, owner);
			if (!world) return;
			const ForceMode3D forceMode = static_cast<ForceMode3D>(mode);
			switch (kind) {
			case 0: world->AddForce(owner, { x, y, z }, forceMode); break;
			case 1: world->AddForceAtPosition(owner, { x, y, z }, { px, py, pz }, forceMode); break;
			case 2: world->AddTorque(owner, { x, y, z }, forceMode); break;
			default: break;
			}
		}

		int Funke_Rigidbody3D_IsSleeping(uint64_t id) {
			EntityHandle owner = entt::null;
			PhysicsWorld3D* world = ResolveBody(id, owner);
			return world && world->IsSleeping(owner) ? 1 : 0;
		}

		void Funke_Rigidbody3D_SetSleeping(uint64_t id, int sleeping) {
			EntityHandle owner = entt::null;
			PhysicsWorld3D* world = ResolveBody(id, owner);
			if (!world) return;
			if (sleeping) world->Sleep(owner);
			else world->WakeUp(owner);
		}

		Collider3DBase* ResolveCollider(uint64_t id, int kind) {
			switch (kind) {
			case 0: return Resolve<BoxCollider3DComponent>(id);
			case 1: return Resolve<SphereCollider3DComponent>(id);
			case 2: return Resolve<CapsuleCollider3DComponent>(id);
			case 3: return Resolve<MeshCollider3DComponent>(id);
			default: return nullptr;
			}
		}

		void Funke_Collider3D_Get(uint64_t id, int kind, float* values, int count) {
			float packed[k_ColliderValueCount] = {};
			if (const Collider3DBase* c = ResolveCollider(id, kind)) {
				packed[0] = c->Center.x; packed[1] = c->Center.y; packed[2] = c->Center.z;
				packed[3] = c->IsTrigger ? 1.0f : 0.0f;
				packed[4] = c->Friction;
				packed[5] = c->Bounciness;
				switch (kind) {
				case 0: {
					const auto* box = static_cast<const BoxCollider3DComponent*>(c);
					packed[6] = box->Size.x; packed[7] = box->Size.y; packed[8] = box->Size.z;
					break;
				}
				case 1: packed[6] = static_cast<const SphereCollider3DComponent*>(c)->Radius; break;
				case 2: {
					const auto* capsule = static_cast<const CapsuleCollider3DComponent*>(c);
					packed[6] = capsule->Radius; packed[7] = capsule->Height; packed[8] = static_cast<float>(capsule->Direction);
					break;
				}
				case 3: {
					const auto* mesh = static_cast<const MeshCollider3DComponent*>(c);
					packed[6] = mesh->UseMeshRenderer ? 1.0f : 0.0f; packed[7] = static_cast<float>(mesh->Mesh); packed[8] = mesh->Convex ? 1.0f : 0.0f;
					break;
				}
				default: break;
				}
			}
			std::copy_n(packed, std::clamp(count, 0, k_ColliderValueCount), values);
		}

		void Funke_Collider3D_Set(uint64_t id, int kind, const float* values, int count) {
			Collider3DBase* c = ResolveCollider(id, kind);
			if (!c || count < k_ColliderValueCount) return;
			c->Center = { values[0], values[1], values[2] };
			c->IsTrigger = values[3] != 0.0f;
			c->Friction = std::max(values[4], 0.0f);
			c->Bounciness = std::clamp(values[5], 0.0f, 1.0f);
			switch (kind) {
			case 0: static_cast<BoxCollider3DComponent*>(c)->Size = { values[6], values[7], values[8] }; break;
			case 1: static_cast<SphereCollider3DComponent*>(c)->Radius = std::max(values[6], 0.0f); break;
			case 2: {
				auto* capsule = static_cast<CapsuleCollider3DComponent*>(c);
				capsule->Radius = std::max(values[6], 0.0f);
				capsule->Height = std::max(values[7], 0.0f);
				const int direction = static_cast<int>(values[8]);
				if (direction >= 0 && direction <= static_cast<int>(CapsuleAxis::Z)) capsule->Direction = static_cast<CapsuleAxis>(direction);
				break;
			}
			case 3: {
				auto* mesh = static_cast<MeshCollider3DComponent*>(c);
				mesh->UseMeshRenderer = values[6] != 0.0f;
				const int type = static_cast<int>(values[7]);
				if (type >= 0 && type < static_cast<int>(k_PrimitiveMeshTypeCount)) mesh->Mesh = static_cast<PrimitiveMeshType>(type);
				mesh->Convex = values[8] != 0.0f;
				break;
			}
			default: break;
			}
		}

		int Funke_Physics3D_Raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance,
			uint64_t* hitEntityID, float* hitValues, int count)
		{
			*hitEntityID = 0;
			Scene* scene = GetScene();
			PhysicsWorld3D* world = scene ? PhysicsSystem3D::FindWorld(*scene) : nullptr;
			RaycastHit3D hit;
			if (!world || !world->Raycast({ ox, oy, oz }, { dx, dy, dz }, maxDistance, hit)) return 0;
			*hitEntityID = scene->IsValid(hit.Entity) ? GetEntityScriptId(*scene, hit.Entity) : 0;
			const float packed[k_RaycastValueCount] = {
				hit.Point.x, hit.Point.y, hit.Point.z, hit.Normal.x, hit.Normal.y, hit.Normal.z, hit.Distance };
			std::copy_n(packed, std::clamp(count, 0, k_RaycastValueCount), hitValues);
			return 1;
		}

		void Funke_Physics3D_GetGravity(float* x, float* y, float* z) {
			Scene* scene = GetScene();
			PhysicsWorld3D* world = scene ? PhysicsSystem3D::FindWorld(*scene) : nullptr;
			const Vec3 gravity = world ? world->GetGravity() : Vec3(0.0f, -9.81f, 0.0f);
			*x = gravity.x; *y = gravity.y; *z = gravity.z;
		}

		uint64_t Funke_MeshCollider3D_GetCustomMesh(uint64_t id) {
			const auto* c = Resolve<MeshCollider3DComponent>(id);
			return c ? c->CustomMesh : 0;
		}
		void Funke_MeshCollider3D_SetCustomMesh(uint64_t id, uint64_t meshGuid) {
			if (auto* c = Resolve<MeshCollider3DComponent>(id)) c->CustomMesh = meshGuid;
		}

		void Funke_Physics3D_SetGravity(float x, float y, float z) {
			Scene* scene = GetScene();
			if (PhysicsWorld3D* world = scene ? PhysicsSystem3D::FindWorld(*scene) : nullptr) {
				world->SetGravity({ x, y, z });
			}
		}
	}

	void PopulatePhysics3DBindings(NativeBindings& b) {
		b.Rigidbody3D_Get = FNK_GUARDED_BINDING(Funke_Rigidbody3D_Get);
		b.Rigidbody3D_Set = FNK_GUARDED_BINDING(Funke_Rigidbody3D_Set);
		b.Rigidbody3D_GetVelocity = FNK_GUARDED_BINDING(Funke_Rigidbody3D_GetVelocity);
		b.Rigidbody3D_SetVelocity = FNK_GUARDED_BINDING(Funke_Rigidbody3D_SetVelocity);
		b.Rigidbody3D_AddForce = FNK_GUARDED_BINDING(Funke_Rigidbody3D_AddForce);
		b.Rigidbody3D_IsSleeping = FNK_GUARDED_BINDING(Funke_Rigidbody3D_IsSleeping);
		b.Rigidbody3D_SetSleeping = FNK_GUARDED_BINDING(Funke_Rigidbody3D_SetSleeping);
		b.Collider3D_Get = FNK_GUARDED_BINDING(Funke_Collider3D_Get);
		b.Collider3D_Set = FNK_GUARDED_BINDING(Funke_Collider3D_Set);
		b.Physics3D_Raycast = FNK_GUARDED_BINDING(Funke_Physics3D_Raycast);
		b.Physics3D_GetGravity = FNK_GUARDED_BINDING(Funke_Physics3D_GetGravity);
		b.Physics3D_SetGravity = FNK_GUARDED_BINDING(Funke_Physics3D_SetGravity);
		b.MeshCollider3D_GetCustomMesh = FNK_GUARDED_BINDING(Funke_MeshCollider3D_GetCustomMesh);
		b.MeshCollider3D_SetCustomMesh = FNK_GUARDED_BINDING(Funke_MeshCollider3D_SetCustomMesh);
	}
}
