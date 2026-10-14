#include "pch.hpp"
#include "Physics3D/PhysicsWorld3D.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Time.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/PrimitiveMesh.hpp"
#include "Scene/Scene.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <mutex>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Index {
	namespace {
		namespace Layers {
			constexpr JPH::ObjectLayer NonMoving = 0;
			constexpr JPH::ObjectLayer Moving = 1;
		}

		namespace BroadPhaseLayers {
			constexpr JPH::BroadPhaseLayer NonMoving(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
		}

		class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
				return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
			}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
				return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
			}
#endif
		};

		class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
				return layer != Layers::NonMoving || broadPhaseLayer == BroadPhaseLayers::Moving;
			}
		};

		class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter {
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
				return a != Layers::NonMoving || b == Layers::Moving;
			}
		};

		void JoltTrace(const char* format, ...) {
			char buffer[1024];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			IDX_CORE_WARN_TAG("Physics3D", "Jolt: {}", buffer);
		}

		// Type registration is process-wide and never undone; the job system and temp allocator live
		// while any world does and are shared, since worlds only step on the main thread.
		struct JoltRuntime {
			std::unique_ptr<JPH::TempAllocator> TempAllocator;
			std::unique_ptr<JPH::JobSystem> JobSystem;
		};

		std::mutex s_RuntimeMutex;
		int s_RuntimeRefs = 0;
		std::unique_ptr<JoltRuntime> s_Runtime;

		JoltRuntime& AcquireRuntime() {
			static std::once_flag s_TypesRegistered;
			std::call_once(s_TypesRegistered, [] {
				JPH::RegisterDefaultAllocator();
				JPH::Trace = &JoltTrace;
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			});

			std::lock_guard lock(s_RuntimeMutex);
			if (s_RuntimeRefs++ == 0) {
				s_Runtime = std::make_unique<JoltRuntime>();
				s_Runtime->TempAllocator = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(16 * 1024 * 1024);
#if defined(__EMSCRIPTEN__)
				s_Runtime->JobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
#else
				const unsigned hardwareThreads = std::max(2u, std::thread::hardware_concurrency());
				const int workerThreads = static_cast<int>(std::clamp(hardwareThreads - 1u, 1u, 8u));
				s_Runtime->JobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerThreads);
#endif
			}
			return *s_Runtime;
		}

		void ReleaseRuntime() {
			std::lock_guard lock(s_RuntimeMutex);
			if (--s_RuntimeRefs == 0) {
				s_Runtime.reset();
			}
		}

		JPH::Vec3 ToJolt(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
		JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
		Vec3 FromJolt(JPH::Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
		glm::quat FromJolt(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

		uint64_t EntityToUserData(EntityHandle entity) { return static_cast<uint64_t>(entt::to_integral(entity)) + 1; }
		EntityHandle UserDataToEntity(uint64_t data) {
			return data == 0 ? entt::null : static_cast<EntityHandle>(static_cast<entt::id_type>(data - 1));
		}

		struct Pose {
			Vec3 Position{ 0.0f };
			glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			Vec3 Scale{ 1.0f };
		};

		Pose DecomposePose(const glm::mat4& matrix) {
			Pose pose;
			pose.Position = Vec3(matrix[3]);
			Vec3 axes[3] = { Vec3(matrix[0]), Vec3(matrix[1]), Vec3(matrix[2]) };
			pose.Scale = { glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]) };
			if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f) {
				pose.Scale.x = -pose.Scale.x;
			}
			for (int i = 0; i < 3; ++i) {
				if (std::abs(pose.Scale[i]) < 1e-8f) {
					return pose; // Degenerate basis: keep identity rotation.
				}
				axes[i] /= pose.Scale[i];
			}
			pose.Rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
			return pose;
		}

		bool Near(float a, float b) {
			return std::abs(a - b) <= 1e-4f * std::max({ 1.0f, std::abs(a), std::abs(b) });
		}
		bool Near(const Vec3& a, const Vec3& b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }
		bool Near(const glm::quat& a, const glm::quat& b) { return std::abs(glm::dot(a, b)) >= 1.0f - 1e-6f; }
		bool Near(const glm::mat4& a, const glm::mat4& b) {
			for (int c = 0; c < 4; ++c) {
				for (int r = 0; r < 4; ++r) {
					if (!Near(a[c][r], b[c][r])) return false;
				}
			}
			return true;
		}

		enum class ColliderKind : uint8_t { Box, Sphere, Capsule, Mesh };

		// Everything a collider contributes to its body's shape, in body space.
		struct ColliderDesc {
			EntityHandle Entity = entt::null;
			ColliderKind Kind = ColliderKind::Box;
			Vec3 Size{ 1.0f };
			float Radius = 0.5f;
			float Height = 2.0f;
			CapsuleAxis Axis = CapsuleAxis::Y;
			PrimitiveMeshType Mesh = PrimitiveMeshType::Cube;
			uint64_t CustomMesh = 0;
			// MeshAsset::Revision of CustomMesh when gathered (0 = not loadable), so a reloaded file rebuilds the shape.
			uint32_t CustomMeshRevision = 0;
			bool Convex = false;
			Vec3 Center{ 0.0f };
			bool IsTrigger = false;
			float Friction = 0.5f;
			float Bounciness = 0.0f;
			Vec3 Scale{ 1.0f };
			Vec3 LocalPosition{ 0.0f };
			glm::quat LocalRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		};

		bool SameCollider(const ColliderDesc& a, const ColliderDesc& b) {
			return a.Entity == b.Entity && a.Kind == b.Kind && Near(a.Size, b.Size) && Near(a.Radius, b.Radius)
				&& Near(a.Height, b.Height) && a.Axis == b.Axis && a.Mesh == b.Mesh && a.CustomMesh == b.CustomMesh
			&& a.CustomMeshRevision == b.CustomMeshRevision && a.Convex == b.Convex
				&& Near(a.Center, b.Center) && a.IsTrigger == b.IsTrigger && Near(a.Friction, b.Friction)
				&& Near(a.Bounciness, b.Bounciness) && Near(a.Scale, b.Scale)
				&& Near(a.LocalPosition, b.LocalPosition) && Near(a.LocalRotation, b.LocalRotation);
		}

		struct BodyDesc {
			bool HasRigidbody = false;
			Rigidbody3DComponent Rigidbody{};
			std::vector<ColliderDesc> Colliders;
		};

		bool SameRigidbody(const Rigidbody3DComponent& a, const Rigidbody3DComponent& b) {
			return a.Type == b.Type && Near(a.Mass, b.Mass) && Near(a.LinearDrag, b.LinearDrag)
				&& Near(a.AngularDrag, b.AngularDrag) && Near(a.GravityScale, b.GravityScale)
				&& a.ContinuousCollision == b.ContinuousCollision && a.Interpolate == b.Interpolate
				&& a.FreezePositionX == b.FreezePositionX && a.FreezePositionY == b.FreezePositionY
				&& a.FreezePositionZ == b.FreezePositionZ && a.FreezeRotationX == b.FreezeRotationX
				&& a.FreezeRotationY == b.FreezeRotationY && a.FreezeRotationZ == b.FreezeRotationZ;
		}

		bool SameBody(const BodyDesc& a, const BodyDesc& b) {
			if (a.HasRigidbody != b.HasRigidbody || a.Colliders.size() != b.Colliders.size()) return false;
			if (a.HasRigidbody && !SameRigidbody(a.Rigidbody, b.Rigidbody)) return false;
			for (size_t i = 0; i < a.Colliders.size(); ++i) {
				if (!SameCollider(a.Colliders[i], b.Colliders[i])) return false;
			}
			return true;
		}

		uint8_t AllowedDofMask(const Rigidbody3DComponent& rb) {
			uint8_t mask = static_cast<uint8_t>(JPH::EAllowedDOFs::All);
			if (rb.FreezePositionX) mask &= ~static_cast<uint8_t>(JPH::EAllowedDOFs::TranslationX);
			if (rb.FreezePositionY) mask &= ~static_cast<uint8_t>(JPH::EAllowedDOFs::TranslationY);
			if (rb.FreezePositionZ) mask &= ~static_cast<uint8_t>(JPH::EAllowedDOFs::TranslationZ);
			if (rb.FreezeRotationX) mask &= ~static_cast<uint8_t>(JPH::EAllowedDOFs::RotationX);
			if (rb.FreezeRotationY) mask &= ~static_cast<uint8_t>(JPH::EAllowedDOFs::RotationY);
			if (rb.FreezeRotationZ) mask &= ~static_cast<uint8_t>(JPH::EAllowedDOFs::RotationZ);
			return mask;
		}

		JPH::EMotionType MotionTypeFor(const BodyDesc& desc) {
			if (!desc.HasRigidbody) return JPH::EMotionType::Static;
			switch (desc.Rigidbody.Type) {
			case BodyType::Static: return JPH::EMotionType::Static;
			case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
			case BodyType::Dynamic:
				// Jolt rejects a dynamic body with every degree of freedom locked; it can't move anyway.
				return AllowedDofMask(desc.Rigidbody) == 0 ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
			}
			return JPH::EMotionType::Static;
		}

		const PrimitiveMeshData& CachedPrimitiveMesh(PrimitiveMeshType type) {
			static std::array<PrimitiveMeshData, k_PrimitiveMeshTypeCount> s_Meshes;
			static std::array<bool, k_PrimitiveMeshTypeCount> s_Built{};
			const size_t index = std::min(static_cast<size_t>(type), k_PrimitiveMeshTypeCount - 1);
			if (!s_Built[index]) {
				s_Meshes[index] = BuildPrimitiveMesh(static_cast<PrimitiveMeshType>(index));
				s_Built[index] = true;
			}
			return s_Meshes[index];
		}

		JPH::Ref<JPH::Shape> CreateOrNull(const JPH::ShapeSettings& settings, EntityHandle entity, const char* what) {
			JPH::ShapeSettings::ShapeResult result = settings.Create();
			if (result.HasError()) {
				IDX_CORE_WARN_TAG("Physics3D", "Entity {}: could not build {} shape ({})",
					static_cast<uint32_t>(entt::to_integral(entity)), what, result.GetError().c_str());
				return nullptr;
			}
			return result.Get();
		}

		JPH::Ref<JPH::Shape> CreateBox(const Vec3& halfExtents, EntityHandle entity) {
			const JPH::Vec3 half(std::max(halfExtents.x, 1e-3f), std::max(halfExtents.y, 1e-3f), std::max(halfExtents.z, 1e-3f));
			const float convexRadius = std::min(JPH::cDefaultConvexRadius, half.ReduceMin());
			return CreateOrNull(JPH::BoxShapeSettings(half, convexRadius), entity, "box");
		}

		JPH::Ref<JPH::Shape> CreateMeshShape(const ColliderDesc& c, const Vec3& scale, bool forceConvex) {
			const MeshAsset* asset = c.CustomMesh != 0 ? MeshLibrary::Get(c.CustomMesh) : nullptr;
			if (c.CustomMesh != 0 && !asset) return nullptr;
			const PrimitiveMeshData& mesh = asset ? asset->Data : CachedPrimitiveMesh(c.Mesh);
			if (c.Convex || forceConvex) {
				JPH::Array<JPH::Vec3> points;
				points.reserve(mesh.Vertices.size());
				Vec3 minPoint(std::numeric_limits<float>::max()), maxPoint(std::numeric_limits<float>::lowest());
				for (const MeshVertex& v : mesh.Vertices) {
					const Vec3 p = Vec3(v.Position[0], v.Position[1], v.Position[2]) * scale;
					points.push_back(ToJolt(p));
					minPoint = glm::min(minPoint, p);
					maxPoint = glm::max(maxPoint, p);
				}
				JPH::ConvexHullShapeSettings settings(points);
				if (JPH::ShapeSettings::ShapeResult result = settings.Create(); !result.HasError()) {
					return result.Get();
				}
				// Flat meshes (the Plane primitive) have no hull: approximate with a thin box.
				const Vec3 half = glm::max((maxPoint - minPoint) * 0.5f, Vec3(0.01f));
				JPH::Ref<JPH::Shape> box = CreateBox(half, c.Entity);
				if (!box) return nullptr;
				const Vec3 center = (maxPoint + minPoint) * 0.5f;
				return CreateOrNull(JPH::RotatedTranslatedShapeSettings(ToJolt(center), JPH::Quat::sIdentity(), box), c.Entity, "convex mesh");
			}

			JPH::VertexList vertices;
			vertices.reserve(mesh.Vertices.size());
			for (const MeshVertex& v : mesh.Vertices) {
				vertices.push_back(JPH::Float3(v.Position[0] * scale.x, v.Position[1] * scale.y, v.Position[2] * scale.z));
			}
			JPH::IndexedTriangleList triangles;
			triangles.reserve(mesh.Indices.size() / 3);
			for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
				triangles.push_back(JPH::IndexedTriangle(mesh.Indices[i], mesh.Indices[i + 1], mesh.Indices[i + 2], 0));
			}
			return CreateOrNull(JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)), c.Entity, "mesh");
		}

		// Leaf shape centred on the collider's own origin (Center is applied by the caller).
		JPH::Ref<JPH::Shape> CreateColliderShape(const ColliderDesc& c, bool dynamic, glm::quat& outAxisRotation) {
			outAxisRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			const Vec3 scale = glm::abs(c.Scale);
			switch (c.Kind) {
			case ColliderKind::Box:
				return CreateBox(glm::abs(c.Size) * scale * 0.5f, c.Entity);
			case ColliderKind::Sphere: {
				const float radius = std::max(std::abs(c.Radius) * std::max({ scale.x, scale.y, scale.z }), 1e-3f);
				return CreateOrNull(JPH::SphereShapeSettings(radius), c.Entity, "sphere");
			}
			case ColliderKind::Capsule: {
				const int axis = static_cast<int>(c.Axis);
				const float radiusScale = std::max(scale[(axis + 1) % 3], scale[(axis + 2) % 3]);
				const float radius = std::max(std::abs(c.Radius) * radiusScale, 1e-3f);
				const float halfCylinder = std::abs(c.Height) * scale[axis] * 0.5f - radius;
				// Jolt capsules run along Y.
				if (c.Axis == CapsuleAxis::X) outAxisRotation = glm::angleAxis(glm::radians(-90.0f), Vec3(0.0f, 0.0f, 1.0f));
				else if (c.Axis == CapsuleAxis::Z) outAxisRotation = glm::angleAxis(glm::radians(90.0f), Vec3(1.0f, 0.0f, 0.0f));
				if (halfCylinder <= 1e-4f) {
					return CreateOrNull(JPH::SphereShapeSettings(radius), c.Entity, "capsule");
				}
				return CreateOrNull(JPH::CapsuleShapeSettings(halfCylinder, radius), c.Entity, "capsule");
			}
			case ColliderKind::Mesh:
				return CreateMeshShape(c, scale, dynamic);
			}
			return nullptr;
		}

		uint64_t PairKey(JPH::BodyID a, JPH::BodyID b) {
			return (static_cast<uint64_t>(a.GetIndexAndSequenceNumber()) << 32) | b.GetIndexAndSequenceNumber();
		}
	}

	struct PhysicsWorld3D::Impl final : public JPH::ContactListener {
		struct BodyRecord {
			JPH::BodyID Id;
			JPH::EMotionType Motion = JPH::EMotionType::Static;
			BodyDesc Desc;
			// The owner's Transform3D world matrix as of our last write or teleport.
			glm::mat4 LastWorld{ 1.0f };
			bool KinematicMoving = false;
			// Render interpolation between the last two steps (dynamic bodies with Interpolate on).
			bool Interpolate = false;
			Vec3 PreviousPosition{ 0.0f };
			Vec3 CurrentPosition{ 0.0f };
			glm::quat PreviousRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::quat CurrentRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			// The blended matrix Present() wrote; the Transform3D holding it is not a user move.
			bool Presented = false;
			glm::mat4 PresentedWorld{ 1.0f };

			void ResetInterpolation(const Vec3& position, const glm::quat& rotation) {
				PreviousPosition = CurrentPosition = position;
				PreviousRotation = CurrentRotation = rotation;
				Presented = false;
			}
		};

		struct RawContact {
			JPH::BodyID A, B;
			bool Added = false;
			bool IsTrigger = false;
			Vec3 Point{ 0.0f };
			Vec3 Normal{ 0.0f };
		};

		struct ActivePair {
			JPH::BodyID A, B;
			uint32_t Count = 0;
			bool IsTrigger = false;
			bool Entered = false;
			// Jolt drops the contacts of a body that falls asleep; the pair stays touching without Stay events.
			bool Sleeping = false;
			Vec3 Point{ 0.0f };
			Vec3 Normal{ 0.0f };
		};

		struct PendingEvent {
			JPH::BodyID A, B;
			CollisionPhase3D Phase = CollisionPhase3D::Enter;
			bool IsTrigger = false;
			Vec3 Point{ 0.0f };
			Vec3 Normal{ 0.0f };
		};

		JoltRuntime& Runtime;
		BroadPhaseLayerMap BroadPhaseMap;
		ObjectVsBroadPhaseFilter ObjectVsBroadPhase;
		ObjectLayerPairs ObjectPairs;
		JPH::PhysicsSystem System;

		Scene* OwningScene = nullptr;
		std::unordered_map<EntityHandle, BodyRecord> Bodies;
		std::unordered_map<JPH::uint32, EntityHandle> Owners;

		std::mutex ContactMutex;
		std::vector<RawContact> RawContacts;
		std::unordered_map<uint64_t, ActivePair> ActivePairs;
		std::vector<PendingEvent> PendingEvents;

		Impl() : Runtime(AcquireRuntime()) {
			constexpr JPH::uint k_MaxBodies = 65536;
			constexpr JPH::uint k_NumBodyMutexes = 0;
			constexpr JPH::uint k_MaxBodyPairs = 65536;
			constexpr JPH::uint k_MaxContactConstraints = 10240;
			System.Init(k_MaxBodies, k_NumBodyMutexes, k_MaxBodyPairs, k_MaxContactConstraints,
				BroadPhaseMap, ObjectVsBroadPhase, ObjectPairs);
			System.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
			System.SetContactListener(this);
		}

		~Impl() override {
			System.SetContactListener(nullptr);
			JPH::BodyInterface& bodies = System.GetBodyInterface();
			for (auto& [entity, record] : Bodies) {
				bodies.RemoveBody(record.Id);
				bodies.DestroyBody(record.Id);
			}
			Bodies.clear();
			ReleaseRuntime();
		}

		// ── ContactListener (job threads) ───────────────────────────

		void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override {
			RecordContact(body1, body2, manifold);
		}

		void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override {
			// Persisted contacts only refresh the reported point; pairing is driven by add/remove.
			RawContact contact = MakeContact(body1, body2, manifold);
			std::lock_guard lock(ContactMutex);
			if (auto it = ActivePairs.find(PairKey(contact.A, contact.B)); it != ActivePairs.end()) {
				it->second.Point = contact.Point;
				it->second.Normal = contact.Normal;
			}
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
			RawContact contact;
			contact.A = pair.GetBody1ID();
			contact.B = pair.GetBody2ID();
			if (contact.B < contact.A) std::swap(contact.A, contact.B);
			std::lock_guard lock(ContactMutex);
			RawContacts.push_back(contact);
		}

		static RawContact MakeContact(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold) {
			RawContact contact;
			contact.A = body1.GetID();
			contact.B = body2.GetID();
			contact.Added = true;
			contact.IsTrigger = body1.IsSensor() || body2.IsSensor();
			contact.Point = manifold.mRelativeContactPointsOn1.empty()
				? FromJolt(JPH::Vec3(manifold.mBaseOffset))
				: FromJolt(JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(0)));
			contact.Normal = FromJolt(manifold.mWorldSpaceNormal);
			if (contact.B < contact.A) {
				std::swap(contact.A, contact.B);
				contact.Normal = -contact.Normal;
			}
			return contact;
		}

		void RecordContact(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold) {
			RawContact contact = MakeContact(body1, body2, manifold);
			std::lock_guard lock(ContactMutex);
			RawContacts.push_back(contact);
		}

		bool IsAsleep(JPH::BodyID id) const {
			const auto owner = Owners.find(id.GetIndexAndSequenceNumber());
			if (owner == Owners.end()) return false;
			const auto record = Bodies.find(owner->second);
			return record != Bodies.end() && record->second.Motion != JPH::EMotionType::Static
				&& !System.GetBodyInterface().IsActive(id);
		}

		bool Exists(JPH::BodyID id) const { return Owners.contains(id.GetIndexAndSequenceNumber()); }

		// Main thread, after a step: fold sub-shape contacts into per-body-pair enter/stay/exit events.
		void ResolveContacts() {
			std::lock_guard lock(ContactMutex);
			// Adds first, so a pair that swaps one touching sub-shape for another never reaches zero.
			std::stable_partition(RawContacts.begin(), RawContacts.end(), [](const RawContact& c) { return c.Added; });
			for (ActivePair& pair : ActivePairs | std::views::values) {
				pair.Entered = false;
			}
			for (const RawContact& contact : RawContacts) {
				const uint64_t key = PairKey(contact.A, contact.B);
				if (contact.Added) {
					ActivePair& pair = ActivePairs[key];
					if (pair.Count++ == 0) {
						pair.Point = contact.Point;
						pair.Normal = contact.Normal;
						if (pair.Sleeping) {
							pair.Sleeping = false; // Woke up still touching: the contact continues.
							continue;
						}
						pair.A = contact.A;
						pair.B = contact.B;
						pair.IsTrigger = contact.IsTrigger;
						pair.Entered = true;
						PendingEvents.push_back({ contact.A, contact.B, CollisionPhase3D::Enter, pair.IsTrigger, pair.Point, pair.Normal });
					}
					continue;
				}
				auto it = ActivePairs.find(key);
				if (it == ActivePairs.end() || it->second.Count == 0) continue;
				if (--it->second.Count == 0) {
					ActivePair& pair = it->second;
					if (Exists(pair.A) && Exists(pair.B) && (IsAsleep(pair.A) || IsAsleep(pair.B))) {
						pair.Sleeping = true;
						continue;
					}
					PendingEvents.push_back({ pair.A, pair.B, CollisionPhase3D::Exit, pair.IsTrigger, pair.Point, pair.Normal });
					ActivePairs.erase(it);
				}
			}
			RawContacts.clear();
			for (auto it = ActivePairs.begin(); it != ActivePairs.end();) {
				const ActivePair& pair = it->second;
				if (pair.Sleeping && !IsAsleep(pair.A) && !IsAsleep(pair.B)) {
					// Woke up without the contact coming back, e.g. teleported away.
					PendingEvents.push_back({ pair.A, pair.B, CollisionPhase3D::Exit, pair.IsTrigger, pair.Point, pair.Normal });
					it = ActivePairs.erase(it);
					continue;
				}
				if (!pair.Entered && !pair.Sleeping) {
					PendingEvents.push_back({ pair.A, pair.B, CollisionPhase3D::Stay, pair.IsTrigger, pair.Point, pair.Normal });
				}
				++it;
			}
		}

		// ── Bodies ──────────────────────────────────────────────────

		// notifyExit: the other side of each touching pair gets an Exit (the body is gone for good,
		// not just rebuilt with a new shape).
		void ForgetBodyContacts(JPH::BodyID id, bool notifyExit) {
			std::lock_guard lock(ContactMutex);
			std::erase_if(PendingEvents, [id](const PendingEvent& e) { return e.A == id || e.B == id; });
			for (auto it = ActivePairs.begin(); it != ActivePairs.end();) {
				const ActivePair& pair = it->second;
				if (pair.A != id && pair.B != id) {
					++it;
					continue;
				}
				if (notifyExit) {
					PendingEvents.push_back({ pair.A, pair.B, CollisionPhase3D::Exit, pair.IsTrigger, pair.Point, pair.Normal });
				}
				it = ActivePairs.erase(it);
			}
		}

		void DestroyBody(BodyRecord& record, bool notifyExit) {
			JPH::BodyInterface& bodies = System.GetBodyInterface();
			Owners.erase(record.Id.GetIndexAndSequenceNumber());
			ForgetBodyContacts(record.Id, notifyExit);
			bodies.RemoveBody(record.Id);
			bodies.DestroyBody(record.Id);
			record.Id = JPH::BodyID();
		}

		bool CreateBody(EntityHandle owner, const BodyDesc& desc, const Pose& pose, BodyRecord& record) {
			const JPH::EMotionType motion = MotionTypeFor(desc);
			const bool dynamic = motion == JPH::EMotionType::Dynamic;

			JPH::Ref<JPH::Shape> shape;
			bool allTriggers = !desc.Colliders.empty();
			float friction = 0.0f;
			float restitution = 0.0f;
			std::vector<std::pair<JPH::Ref<JPH::Shape>, std::pair<Vec3, glm::quat>>> parts;
			for (const ColliderDesc& c : desc.Colliders) {
				if (dynamic && c.Kind == ColliderKind::Mesh && !c.Convex) {
					IDX_CORE_WARN_TAG("Physics3D", "Entity {}: a non-convex Mesh Collider 3D on a dynamic Rigidbody 3D uses its convex hull",
						static_cast<uint32_t>(entt::to_integral(c.Entity)));
				}
				glm::quat axisRotation;
				JPH::Ref<JPH::Shape> leaf = CreateColliderShape(c, dynamic, axisRotation);
				if (!leaf) continue;
				leaf->SetUserData(EntityToUserData(c.Entity));
				const Vec3 position = c.LocalPosition + c.LocalRotation * (c.Center * c.Scale);
				parts.push_back({ leaf, { position, c.LocalRotation * axisRotation } });
				allTriggers = allTriggers && c.IsTrigger;
				friction += c.Friction;
				restitution = std::max(restitution, c.Bounciness);
			}

			if (parts.empty()) {
				shape = CreateOrNull(JPH::EmptyShapeSettings(), owner, "empty");
				allTriggers = false;
			}
			else {
				friction /= static_cast<float>(parts.size());
				const auto& [first, firstTransform] = parts.front();
				const bool atOrigin = glm::length(firstTransform.first) < 1e-6f
					&& std::abs(firstTransform.second.w) >= 1.0f - 1e-7f;
				if (parts.size() == 1 && atOrigin) {
					shape = first;
				}
				else if (parts.size() == 1) {
					shape = CreateOrNull(JPH::RotatedTranslatedShapeSettings(ToJolt(firstTransform.first), ToJolt(firstTransform.second), first), owner, "offset");
				}
				else {
					JPH::StaticCompoundShapeSettings compound;
					for (const auto& [leaf, transform] : parts) {
						compound.AddShape(ToJolt(transform.first), ToJolt(transform.second), leaf);
					}
					shape = CreateOrNull(compound, owner, "compound");
				}
			}
			if (!shape) return false;

			const JPH::ObjectLayer layer = motion == JPH::EMotionType::Static ? Layers::NonMoving : Layers::Moving;
			JPH::BodyCreationSettings settings(shape, ToJolt(pose.Position), ToJolt(pose.Rotation), motion, layer);
			settings.mUserData = EntityToUserData(owner);
			settings.mIsSensor = allTriggers;
			// Lets kinematic triggers see static geometry too.
			settings.mCollideKinematicVsNonDynamic = allTriggers;
			settings.mFriction = parts.empty() ? 0.5f : friction;
			settings.mRestitution = restitution;
			if (desc.HasRigidbody) {
				const Rigidbody3DComponent& rb = desc.Rigidbody;
				settings.mLinearDamping = std::max(rb.LinearDrag, 0.0f);
				settings.mAngularDamping = std::max(rb.AngularDrag, 0.0f);
				settings.mGravityFactor = rb.GravityScale;
				settings.mMotionQuality = rb.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
				// Levels are built from many separate boxes; without this, a body rolling across a seam between
				// two flush surfaces catches on the internal edge and hops.
				settings.mEnhancedInternalEdgeRemoval = dynamic;
				if (dynamic) {
					settings.mAllowedDOFs = static_cast<JPH::EAllowedDOFs>(AllowedDofMask(rb));
				}
			}
			const float mass = desc.HasRigidbody ? std::max(desc.Rigidbody.Mass, 1e-4f) : 1.0f;
			if (dynamic && !parts.empty()) {
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = mass;
			}
			else if (motion != JPH::EMotionType::Static) {
				// Empty and triangle-mesh shapes have no volume to derive mass from.
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
				settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(JPH::Vec3::sReplicate(1.0f), mass);
			}

			JPH::BodyInterface& bodies = System.GetBodyInterface();
			const JPH::BodyID id = bodies.CreateAndAddBody(settings,
				motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
			if (id.IsInvalid()) {
				IDX_CORE_WARN_TAG("Physics3D", "Body limit reached; entity {} gets no physics body", static_cast<uint32_t>(entt::to_integral(owner)));
				return false;
			}
			record.Id = id;
			record.Motion = motion;
			record.Desc = desc;
			record.KinematicMoving = false;
			record.Interpolate = dynamic && desc.HasRigidbody && desc.Rigidbody.Interpolate;
			record.ResetInterpolation(pose.Position, pose.Rotation);
			Owners[id.GetIndexAndSequenceNumber()] = owner;
			return true;
		}

		bool IsEnabled(const entt::registry& registry, EntityHandle entity) const {
			return !registry.any_of<DisabledTag, InheritedDisabledTag>(entity);
		}

		EntityHandle FindOwner(const entt::registry& registry, EntityHandle entity) const {
			if (registry.all_of<Rigidbody3DComponent, Transform3DComponent>(entity)) return entity;
			constexpr int k_MaxDepth = 1024;
			const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(entity);
			for (int depth = 0; hierarchy && hierarchy->Parent != entt::null && depth < k_MaxDepth; ++depth) {
				const EntityHandle parent = hierarchy->Parent;
				if (!registry.valid(parent)) break;
				if (registry.all_of<Rigidbody3DComponent, Transform3DComponent>(parent) && IsEnabled(registry, parent)) {
					return parent;
				}
				hierarchy = registry.try_get<HierarchyComponent>(parent);
			}
			return entity;
		}

		template <typename TCollider, typename TFill>
		void GatherColliders(entt::registry& registry, std::unordered_map<EntityHandle, BodyDesc>& desired, ColliderKind kind, TFill&& fill) {
			auto view = registry.view<TCollider, Transform3DComponent>(entt::exclude<DisabledTag, InheritedDisabledTag>);
			for (auto [entity, collider, transform] : view.each()) {
				const EntityHandle owner = FindOwner(registry, entity);
				ColliderDesc desc;
				desc.Entity = entity;
				desc.Kind = kind;
				desc.Center = collider.Center;
				desc.IsTrigger = collider.IsTrigger;
				desc.Friction = collider.Friction;
				desc.Bounciness = collider.Bounciness;
				fill(entity, collider, desc);

				if (owner == entity) {
					desc.Scale = DecomposePose(transform.WorldMatrix).Scale;
				}
				else {
					// Body space = the owner's world pose without its scale.
					const Pose ownerPose = DecomposePose(registry.get<Transform3DComponent>(owner).WorldMatrix);
					const glm::mat4 bodyFrame = glm::translate(glm::mat4(1.0f), ownerPose.Position) * glm::mat4_cast(ownerPose.Rotation);
					const Pose relative = DecomposePose(glm::inverse(bodyFrame) * transform.WorldMatrix);
					desc.Scale = relative.Scale;
					desc.LocalPosition = relative.Position;
					desc.LocalRotation = relative.Rotation;
				}
				desired[owner].Colliders.push_back(desc);
			}
		}

		std::unordered_map<EntityHandle, BodyDesc> BuildDesiredBodies(entt::registry& registry) {
			std::unordered_map<EntityHandle, BodyDesc> desired;
			for (auto [entity, rb, transform] : registry.view<Rigidbody3DComponent, Transform3DComponent>(entt::exclude<DisabledTag, InheritedDisabledTag>).each()) {
				BodyDesc& desc = desired[entity];
				desc.HasRigidbody = true;
				desc.Rigidbody = rb;
			}
			GatherColliders<BoxCollider3DComponent>(registry, desired, ColliderKind::Box,
				[](EntityHandle, const BoxCollider3DComponent& c, ColliderDesc& d) { d.Size = c.Size; });
			GatherColliders<SphereCollider3DComponent>(registry, desired, ColliderKind::Sphere,
				[](EntityHandle, const SphereCollider3DComponent& c, ColliderDesc& d) { d.Radius = c.Radius; });
			GatherColliders<CapsuleCollider3DComponent>(registry, desired, ColliderKind::Capsule,
				[](EntityHandle, const CapsuleCollider3DComponent& c, ColliderDesc& d) {
					d.Radius = c.Radius;
					d.Height = c.Height;
					d.Axis = c.Direction;
				});
			GatherColliders<MeshCollider3DComponent>(registry, desired, ColliderKind::Mesh,
				[&registry](EntityHandle entity, const MeshCollider3DComponent& c, ColliderDesc& d) {
					const MeshRendererComponent* renderer = c.UseMeshRenderer ? registry.try_get<MeshRendererComponent>(entity) : nullptr;
					d.Mesh = renderer ? renderer->Mesh : c.Mesh;
					d.CustomMesh = renderer ? renderer->CustomMesh : c.CustomMesh;
					if (const MeshAsset* asset = MeshLibrary::Get(d.CustomMesh)) d.CustomMeshRevision = asset->Revision;
					d.Convex = c.Convex;
				});
			for (BodyDesc& desc : desired | std::views::values) {
				std::sort(desc.Colliders.begin(), desc.Colliders.end(), [](const ColliderDesc& a, const ColliderDesc& b) {
					if (a.Entity != b.Entity) return entt::to_integral(a.Entity) < entt::to_integral(b.Entity);
					return a.Kind < b.Kind;
				});
			}
			return desired;
		}

		void Sync(Scene& scene, float dt) {
			OwningScene = &scene;
			entt::registry& registry = scene.GetRegistry();
			std::unordered_map<EntityHandle, BodyDesc> desired = BuildDesiredBodies(registry);
			JPH::BodyInterface& bodies = System.GetBodyInterface();

			for (auto it = Bodies.begin(); it != Bodies.end();) {
				if (!registry.valid(it->first) || !desired.contains(it->first)) {
					DestroyBody(it->second, true);
					it = Bodies.erase(it);
				}
				else {
					++it;
				}
			}

			size_t created = 0;
			for (auto& [owner, desc] : desired) {
				const glm::mat4& world = registry.get<Transform3DComponent>(owner).WorldMatrix;
				auto it = Bodies.find(owner);
				if (it == Bodies.end() || !SameBody(it->second.Desc, desc)) {
					JPH::Vec3 linear = JPH::Vec3::sZero();
					JPH::Vec3 angular = JPH::Vec3::sZero();
					bool hadBody = false;
					if (it != Bodies.end()) {
						bodies.GetLinearAndAngularVelocity(it->second.Id, linear, angular);
						DestroyBody(it->second, false);
						hadBody = true;
					}
					BodyRecord record;
					if (!CreateBody(owner, desc, DecomposePose(world), record)) {
						if (it != Bodies.end()) Bodies.erase(it);
						continue;
					}
					record.LastWorld = world;
					if (hadBody && record.Motion == JPH::EMotionType::Dynamic) {
						bodies.SetLinearAndAngularVelocity(record.Id, linear, angular);
					}
					Bodies[owner] = std::move(record);
					++created;
					continue;
				}

				BodyRecord& record = it->second;
				if (Near(record.LastWorld, world) || (record.Presented && Near(record.PresentedWorld, world))) {
					if (record.KinematicMoving) {
						bodies.SetLinearAndAngularVelocity(record.Id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
						record.KinematicMoving = false;
					}
					continue;
				}
				// Moved by a script, the inspector or a parent: follow the Transform3D.
				const Pose pose = DecomposePose(world);
				if (record.Motion == JPH::EMotionType::Kinematic && dt > 0.0f) {
					bodies.MoveKinematic(record.Id, ToJolt(pose.Position), ToJolt(pose.Rotation), dt);
					record.KinematicMoving = true;
				}
				else {
					bodies.SetPositionAndRotation(record.Id, ToJolt(pose.Position), ToJolt(pose.Rotation),
						record.Motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
				}
				record.LastWorld = world;
				record.ResetInterpolation(pose.Position, pose.Rotation);
			}

			if (created > 16) {
				System.OptimizeBroadPhase();
			}
		}

		// Writes a world pose into the owner's local Transform3D (through its Transform3D parent, if any).
		static void ApplyWorldPose(entt::registry& registry, EntityHandle owner, Transform3DComponent& transform,
			const Vec3& worldPosition, const glm::quat& worldRotation)
		{
			const glm::mat4* parentWorld = nullptr;
			const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(owner);
			for (int depth = 0; hierarchy && hierarchy->Parent != entt::null && depth < 1024; ++depth) {
				if (!registry.valid(hierarchy->Parent)) break;
				if (const auto* parentTransform = registry.try_get<Transform3DComponent>(hierarchy->Parent)) {
					parentWorld = &parentTransform->WorldMatrix;
					break;
				}
				hierarchy = registry.try_get<HierarchyComponent>(hierarchy->Parent);
			}

			if (!parentWorld) {
				transform.Position = worldPosition;
				transform.SetLocalRotationQuat(worldRotation);
				transform.WorldMatrix = transform.GetLocalMatrix();
				return;
			}
			const Vec3 worldScale = DecomposePose(transform.WorldMatrix).Scale;
			const glm::mat4 world = glm::scale(glm::translate(glm::mat4(1.0f), worldPosition) * glm::mat4_cast(worldRotation), worldScale);
			Vec3 localPosition, localRotation, localScale;
			Transform3DComponent::Decompose(glm::inverse(*parentWorld) * world, localPosition, localRotation, localScale);
			transform.Position = localPosition;
			transform.Rotation = localRotation;
			transform.WorldMatrix = *parentWorld * transform.GetLocalMatrix();
		}

		void WriteBack(Scene& scene) {
			entt::registry& registry = scene.GetRegistry();
			const JPH::BodyInterface& bodies = System.GetBodyInterface();
			for (auto& [owner, record] : Bodies) {
				if (record.Motion != JPH::EMotionType::Dynamic || !registry.valid(owner)) continue;
				auto* transform = registry.try_get<Transform3DComponent>(owner);
				if (!transform) continue;
				if (!bodies.IsActive(record.Id)) {
					// Asleep: settle the interpolation onto the resting pose.
					record.PreviousPosition = record.CurrentPosition;
					record.PreviousRotation = record.CurrentRotation;
					continue;
				}

				JPH::RVec3 position;
				JPH::Quat rotation;
				bodies.GetPositionAndRotation(record.Id, position, rotation);
				record.PreviousPosition = record.CurrentPosition;
				record.PreviousRotation = record.CurrentRotation;
				record.CurrentPosition = FromJolt(JPH::Vec3(position));
				record.CurrentRotation = glm::normalize(FromJolt(rotation));
				record.Presented = false;

				ApplyWorldPose(registry, owner, *transform, record.CurrentPosition, record.CurrentRotation);
				record.LastWorld = transform->WorldMatrix;
			}
		}

		void Present(Scene& scene, float alpha) {
			entt::registry& registry = scene.GetRegistry();
			alpha = std::clamp(alpha, 0.0f, 1.0f);
			for (auto& [owner, record] : Bodies) {
				if (!record.Interpolate || !registry.valid(owner)) continue;
				auto* transform = registry.try_get<Transform3DComponent>(owner);
				if (!transform) continue;
				// A script moved it since the last step: show that, don't blend over it.
				if (!Near(record.LastWorld, transform->WorldMatrix) && !(record.Presented && Near(record.PresentedWorld, transform->WorldMatrix))) continue;
				const Vec3 position = glm::mix(record.PreviousPosition, record.CurrentPosition, alpha);
				const glm::quat rotation = glm::slerp(record.PreviousRotation, record.CurrentRotation, alpha);
				ApplyWorldPose(registry, owner, *transform, position, rotation);
				record.PresentedWorld = transform->WorldMatrix;
				record.Presented = true;
			}
		}

		const BodyRecord* FindRecord(EntityHandle owner) const {
			auto it = Bodies.find(owner);
			return it == Bodies.end() ? nullptr : &it->second;
		}

		float GetMass(JPH::BodyID id) const {
			JPH::BodyLockRead lock(System.GetBodyLockInterface(), id);
			if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return 0.0f;
			const float inverseMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
			return inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f;
		}
	};

	PhysicsWorld3D::PhysicsWorld3D() : m_Impl(std::make_unique<Impl>()) {}
	PhysicsWorld3D::~PhysicsWorld3D() = default;

	void PhysicsWorld3D::Simulate(Scene& scene, float dt) {
		SyncFromScene(scene, dt);
		Step(dt);
		WriteBackToScene(scene);
	}

	void PhysicsWorld3D::SyncFromScene(Scene& scene, float dt) {
		m_Impl->Sync(scene, dt);
	}

	void PhysicsWorld3D::Step(float dt) {
		if (dt <= 0.0f) return;
		constexpr float k_MaxCollisionStep = 1.0f / 60.0f;
		const int collisionSteps = std::max(1, static_cast<int>(std::ceil(dt / k_MaxCollisionStep - 1e-4f)));
		const JPH::EPhysicsUpdateError error = m_Impl->System.Update(dt, collisionSteps,
			m_Impl->Runtime.TempAllocator.get(), m_Impl->Runtime.JobSystem.get());
		if (error != JPH::EPhysicsUpdateError::None) {
			IDX_CORE_WARN_TAG("Physics3D", "Step overflowed Jolt's limits (error flags {}); raise the body pair or contact limits",
				static_cast<uint32_t>(error));
		}
		m_Impl->ResolveContacts();
	}

	void PhysicsWorld3D::WriteBackToScene(Scene& scene) {
		m_Impl->WriteBack(scene);
	}

	void PhysicsWorld3D::Present(Scene& scene, float alpha) {
		m_Impl->Present(scene, alpha);
	}

	void PhysicsWorld3D::DispatchContacts(const std::function<void(const Collision3D&, CollisionPhase3D)>& callback) {
		std::vector<Impl::PendingEvent> events;
		{
			std::lock_guard lock(m_Impl->ContactMutex);
			events.swap(m_Impl->PendingEvents);
		}
		// Resolved up front: a callback may destroy this world (scene unload), so the loop below
		// must not touch m_Impl.
		std::vector<std::pair<Collision3D, CollisionPhase3D>> resolved;
		resolved.reserve(events.size());
		for (const Impl::PendingEvent& e : events) {
			const auto a = m_Impl->Owners.find(e.A.GetIndexAndSequenceNumber());
			const auto b = m_Impl->Owners.find(e.B.GetIndexAndSequenceNumber());
			if (a == m_Impl->Owners.end() && b == m_Impl->Owners.end()) continue;
			Collision3D collision;
			collision.OwningScene = m_Impl->OwningScene;
			collision.EntityA = a != m_Impl->Owners.end() ? a->second : entt::null;
			collision.EntityB = b != m_Impl->Owners.end() ? b->second : entt::null;
			collision.ContactPoint = e.Point;
			collision.Normal = e.Normal;
			collision.IsTrigger = e.IsTrigger;
			resolved.emplace_back(collision, e.Phase);
		}
		for (const auto& [collision, phase] : resolved) {
			callback(collision, phase);
		}
	}

	bool PhysicsWorld3D::Raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit3D& outHit) const {
		const float length = glm::length(direction);
		if (length < 1e-8f || !(maxDistance > 0.0f)) return false;
		const Vec3 dir = direction / length;
		maxDistance = std::min(maxDistance, 1e6f);

		class IgnoreTriggers final : public JPH::BodyFilter {
		public:
			bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
		};

		const JPH::RRayCast ray{ JPH::RVec3(ToJolt(origin)), ToJolt(dir * maxDistance) };
		JPH::RayCastResult hit;
		IgnoreTriggers filter;
		if (!m_Impl->System.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) return false;

		JPH::BodyLockRead lock(m_Impl->System.GetBodyLockInterface(), hit.mBodyID);
		if (!lock.Succeeded()) return false;
		const JPH::Body& body = lock.GetBody();
		const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
		outHit.Point = FromJolt(JPH::Vec3(point));
		outHit.Normal = FromJolt(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
		outHit.Distance = hit.mFraction * maxDistance;
		const EntityHandle collider = UserDataToEntity(body.GetShape()->GetSubShapeUserData(hit.mSubShapeID2));
		outHit.Entity = collider != entt::null ? collider : UserDataToEntity(body.GetUserData());
		return true;
	}

	Vec3 PhysicsWorld3D::GetGravity() const { return FromJolt(m_Impl->System.GetGravity()); }
	void PhysicsWorld3D::SetGravity(const Vec3& gravity) { m_Impl->System.SetGravity(ToJolt(gravity)); }

	size_t PhysicsWorld3D::GetBodyCount() const { return m_Impl->Bodies.size(); }

	EntityHandle PhysicsWorld3D::FindBodyOwner(Scene& scene, EntityHandle entity) const {
		const entt::registry& registry = scene.GetRegistry();
		return registry.valid(entity) ? m_Impl->FindOwner(registry, entity) : entt::null;
	}

	bool PhysicsWorld3D::EnsureBody(Scene& scene, EntityHandle owner) {
		if (m_Impl->FindRecord(owner)) return true;
		m_Impl->Sync(scene, 0.0f);
		return m_Impl->FindRecord(owner) != nullptr;
	}

	Vec3 PhysicsWorld3D::GetLinearVelocity(EntityHandle owner) const {
		const auto* record = m_Impl->FindRecord(owner);
		return record ? FromJolt(m_Impl->System.GetBodyInterface().GetLinearVelocity(record->Id)) : Vec3(0.0f);
	}

	void PhysicsWorld3D::SetLinearVelocity(EntityHandle owner, const Vec3& velocity) {
		const auto* record = m_Impl->FindRecord(owner);
		if (!record || record->Motion == JPH::EMotionType::Static) return;
		m_Impl->System.GetBodyInterface().SetLinearVelocity(record->Id, ToJolt(velocity));
	}

	Vec3 PhysicsWorld3D::GetAngularVelocity(EntityHandle owner) const {
		const auto* record = m_Impl->FindRecord(owner);
		return record ? FromJolt(m_Impl->System.GetBodyInterface().GetAngularVelocity(record->Id)) : Vec3(0.0f);
	}

	void PhysicsWorld3D::SetAngularVelocity(EntityHandle owner, const Vec3& velocity) {
		const auto* record = m_Impl->FindRecord(owner);
		if (!record || record->Motion == JPH::EMotionType::Static) return;
		m_Impl->System.GetBodyInterface().SetAngularVelocity(record->Id, ToJolt(velocity));
	}

	void PhysicsWorld3D::AddForce(EntityHandle owner, const Vec3& force, ForceMode3D mode) {
		const auto* record = m_Impl->FindRecord(owner);
		if (!record || record->Motion != JPH::EMotionType::Dynamic) return;
		JPH::BodyInterface& bodies = m_Impl->System.GetBodyInterface();
		switch (mode) {
		case ForceMode3D::Force: bodies.AddForce(record->Id, ToJolt(force)); break;
		case ForceMode3D::Acceleration: bodies.AddForce(record->Id, ToJolt(force * m_Impl->GetMass(record->Id))); break;
		case ForceMode3D::Impulse: bodies.AddImpulse(record->Id, ToJolt(force)); break;
		case ForceMode3D::VelocityChange: bodies.AddImpulse(record->Id, ToJolt(force * m_Impl->GetMass(record->Id))); break;
		}
	}

	void PhysicsWorld3D::AddForceAtPosition(EntityHandle owner, const Vec3& force, const Vec3& worldPosition, ForceMode3D mode) {
		const auto* record = m_Impl->FindRecord(owner);
		if (!record || record->Motion != JPH::EMotionType::Dynamic) return;
		JPH::BodyInterface& bodies = m_Impl->System.GetBodyInterface();
		const JPH::RVec3 point(ToJolt(worldPosition));
		switch (mode) {
		case ForceMode3D::Force: bodies.AddForce(record->Id, ToJolt(force), point); break;
		case ForceMode3D::Acceleration: bodies.AddForce(record->Id, ToJolt(force * m_Impl->GetMass(record->Id)), point); break;
		case ForceMode3D::Impulse: bodies.AddImpulse(record->Id, ToJolt(force), point); break;
		case ForceMode3D::VelocityChange: bodies.AddImpulse(record->Id, ToJolt(force * m_Impl->GetMass(record->Id)), point); break;
		}
	}

	void PhysicsWorld3D::AddTorque(EntityHandle owner, const Vec3& torque, ForceMode3D mode) {
		const auto* record = m_Impl->FindRecord(owner);
		if (!record || record->Motion != JPH::EMotionType::Dynamic) return;
		JPH::BodyInterface& bodies = m_Impl->System.GetBodyInterface();
		switch (mode) {
		case ForceMode3D::Force: bodies.AddTorque(record->Id, ToJolt(torque)); break;
		case ForceMode3D::Impulse: bodies.AddAngularImpulse(record->Id, ToJolt(torque)); break;
		// Mass-independent modes change angular velocity directly (inertia is ignored, as the name says).
		case ForceMode3D::Acceleration:
			bodies.SetAngularVelocity(record->Id, bodies.GetAngularVelocity(record->Id) + ToJolt(torque * Time::GetUnscaledFixedDeltaTime()));
			bodies.ActivateBody(record->Id);
			break;
		case ForceMode3D::VelocityChange:
			bodies.SetAngularVelocity(record->Id, bodies.GetAngularVelocity(record->Id) + ToJolt(torque));
			bodies.ActivateBody(record->Id);
			break;
		}
	}

	bool PhysicsWorld3D::IsSleeping(EntityHandle owner) const {
		const auto* record = m_Impl->FindRecord(owner);
		return record && !m_Impl->System.GetBodyInterface().IsActive(record->Id);
	}

	void PhysicsWorld3D::WakeUp(EntityHandle owner) {
		if (const auto* record = m_Impl->FindRecord(owner); record && record->Motion != JPH::EMotionType::Static) {
			m_Impl->System.GetBodyInterface().ActivateBody(record->Id);
		}
	}

	void PhysicsWorld3D::Sleep(EntityHandle owner) {
		if (const auto* record = m_Impl->FindRecord(owner); record && record->Motion != JPH::EMotionType::Static) {
			m_Impl->System.GetBodyInterface().DeactivateBody(record->Id);
		}
	}

}
