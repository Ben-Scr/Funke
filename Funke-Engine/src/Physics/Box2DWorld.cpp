#include "pch.hpp"
#include "Box2DWorld.hpp"
#include "Physics/PhysicsLayers.hpp"
#include "Scene/Scene.hpp"
#include <Components/General/Transform2DComponent.hpp>
#include "Physics/PhysicsSystem2D.hpp"
#include "Jobs/Job.hpp"
#include "Jobs/JobSystem.hpp"

#include <box2d/constants.h>
#include <algorithm>
#include <array>

namespace Funke {
	namespace {
		constexpr int k_ParallelBodyThreshold = 1024;
		constexpr int k_SerialBodyThreshold = 512;
		constexpr int k_MaxPhysicsWorkers = 4;

		struct StepTasks {
			std::array<JobHandle, B2_MAX_TASKS> Handles;
			size_t Count = 0;
			bool Parallel = false;
		};

		thread_local StepTasks* t_StepTasks = nullptr;

		void* EnqueuePhysicsTask(b2TaskCallback* callback, void* context, void*) {
			StepTasks* tasks = t_StepTasks;
			if (!tasks || !tasks->Parallel || !JobSystem::IsInitialized() || tasks->Count >= tasks->Handles.size()) {
				callback(context);
				return nullptr;
			}
			auto& handle = tasks->Handles[tasks->Count++];
			handle = Job::Schedule([callback, context] { callback(context); });
			return &handle;
		}

		void FinishPhysicsTask(void* task, void*) {
			auto& handle = *static_cast<JobHandle*>(task);
			Job::Wait(handle);
			handle = {};
		}
	}

	Box2DWorld::Box2DWorld() {
		b2WorldDef def = b2DefaultWorldDef();
		def.enableSleep = true;
		def.workerCount = 1;
		def.enqueueTask = EnqueuePhysicsTask;
		def.finishTask = FinishPhysicsTask;

		const auto* project = ProjectManager::GetCurrentProject();
		const Vec2 gravity = project ? project->PhysicsGravity2D : Vec2(0, -9.8f);
		def.gravity = b2Vec2{gravity.x, gravity.y};
		m_WorldId = b2CreateWorld(&def);
		m_LayerSettings = GetPhysicsLayerSettings();
		b2World_SetCustomFilterCallback(m_WorldId, [](b2ShapeId a, b2ShapeId b, void* context) {
			const auto& settings = static_cast<Box2DWorld*>(context)->m_LayerSettings;
			const b2Filter left = b2Shape_GetFilter(a), right = b2Shape_GetFilter(b);
			// Preserve Box2D's explicit group override.
			if (left.groupIndex != 0 && left.groupIndex == right.groupIndex) return left.groupIndex > 0;
			return (settings.GetCollisionMask(left.categoryBits) & right.categoryBits) != 0;
		}, this);
	}
	Box2DWorld::~Box2DWorld() {
		Destroy();
	}

	void Box2DWorld::Step(float dt) {
		const auto& settings = GetPhysicsLayerSettings();
		if (m_LayerSettings.CollisionMasks != settings.CollisionMasks) {
			m_LayerSettings.CollisionMasks = settings.CollisionMasks;
			// Box2D caches accepted contacts. Changing the filter wakes and rechecks sleeping pairs too.
			std::vector<b2ShapeId> shapes;
			for (const auto& [key, binding] : m_BodyBindings) {
				const b2BodyId body = b2LoadBodyId(key);
				if (!b2Body_IsValid(body)) continue;
				shapes.resize(b2Body_GetShapeCount(body));
				const int count = b2Body_GetShapes(body, shapes.data(), static_cast<int>(shapes.size()));
				for (int i = 0; i < count; ++i) {
					const b2Filter original = b2Shape_GetFilter(shapes[i]);
					b2Filter temporary = original;
					temporary.maskBits = ~original.maskBits;
					b2Shape_SetFilter(shapes[i], temporary);
					b2Shape_SetFilter(shapes[i], original);
				}
			}
		}
		const int availableWorkers = JobSystem::IsInitialized() ? JobSystem::GetWorkerCount() : 0;
		const int currentWorkers = b2World_GetWorkerCount(m_WorldId);
		// Hysteresis avoids reallocating Box2D worker contexts as bodies wake and sleep near the threshold.
		const int threshold = currentWorkers > 1 ? k_SerialBodyThreshold : k_ParallelBodyThreshold;
		const int workers = availableWorkers > 1 && b2World_GetAwakeBodyCount(m_WorldId) >= threshold
			? std::min(availableWorkers, k_MaxPhysicsWorkers) : 1;
		b2World_SetWorkerCount(m_WorldId, workers);
		StepTasks tasks;
		tasks.Parallel = workers > 1;
		// Box2D enqueues on the step's caller; nested worlds need independent handle storage.
		struct TaskScope {
			StepTasks* Previous = t_StepTasks;
			explicit TaskScope(StepTasks& current) { t_StepTasks = &current; }
			~TaskScope() { t_StepTasks = Previous; }
		} taskScope{ tasks };
		b2World_Step(m_WorldId, dt, 5);
	}

	int Box2DWorld::GetWorkerCount() const {
		return b2World_IsValid(m_WorldId) ? b2World_GetWorkerCount(m_WorldId) : 0;
	}

	void Box2DWorld::Destroy() {
		if (b2World_IsValid(m_WorldId)) {
			b2DestroyWorld(m_WorldId);
			m_WorldId = b2_nullWorldId;
		}
		m_Dispatcher.Clear();
		m_BodyBindings.clear();
		m_GroundBody = b2_nullBodyId;
	}

	b2BodyId Box2DWorld::GetGroundBody() {
		if (!b2Body_IsValid(m_GroundBody) && b2World_IsValid(m_WorldId)) {
			b2BodyDef def = b2DefaultBodyDef();
			def.type = b2_staticBody;
			m_GroundBody = b2CreateBody(m_WorldId, &def);
		}
		return m_GroundBody;
	}

	Vec2 Box2DWorld::GetGravity() const {
		if (!b2World_IsValid(m_WorldId)) return Vec2{ 0.0f, 0.0f };
		const b2Vec2 g = b2World_GetGravity(m_WorldId);
		return Vec2{ g.x, g.y };
	}

	void Box2DWorld::SetGravity(const Vec2& gravity) {
		if (b2World_IsValid(m_WorldId)) b2World_SetGravity(m_WorldId, b2Vec2{ gravity.x, gravity.y });
	}

	b2BodyId Box2DWorld::CreateBody(EntityHandle nativeEntity, Scene& scene, BodyType bodyType) {
		Transform2DComponent defaultTransform{};
		Transform2DComponent* tr = nullptr;
		if (!scene.TryGetComponent(nativeEntity, tr) || tr == nullptr) {
			FNK_CORE_WARN_TAG("PhysicsSystem", "CreateBody using default transform because entity {} has no Transform2DComponent", static_cast<uint32_t>(nativeEntity));
			tr = &defaultTransform;
		}
		// A pending local edit (scene/prefab load, instantiation under a moved parent) is not in the world pose yet.
		else if (tr->IsDirty()) {
			PhysicsSystem2D::ComposeWorldFromLocalChain(scene.GetRegistry(), nativeEntity, *tr);
		}

		b2Vec2 box2dPos(tr->Position.x, tr->Position.y);
		b2BodyDef bodyDef = b2DefaultBodyDef();
		bodyDef.type = bodyType == BodyType::Dynamic ? b2_dynamicBody : (bodyType == BodyType::Static ? b2_staticBody : b2_kinematicBody);
		bodyDef.gravityScale = 1.0f;
		bodyDef.position = box2dPos;
		bodyDef.rotation = tr->GetB2Rotation();
		bodyDef.isBullet = false;
		bodyDef.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(nativeEntity));
		bodyDef.linearDamping = 0.1f;

		b2BodyId bodyId = b2CreateBody(m_WorldId, &bodyDef);
		m_BodyBindings[b2StoreBodyId(bodyId)] = BodyBinding{ nativeEntity, &scene };
		return bodyId;
	}

	b2ShapeId Box2DWorld::CreateShape(EntityHandle nativeEntity, Scene& scene, b2BodyId bodyId, ShapeType shapeType, bool isSensor) {
		Transform2DComponent transform{};
		Transform2DComponent* found = nullptr;
		if (scene.TryGetComponent(nativeEntity, found) && found) {
			transform = *found;
		} else {
			FNK_CORE_WARN_TAG("PhysicsSystem", "CreateShape using default transform because entity {} has no Transform2DComponent", static_cast<uint32_t>(nativeEntity));
		}

		b2ShapeId shapeId = b2_nullShapeId;

		b2ShapeDef shapeDef = b2DefaultShapeDef();
		shapeDef.density = 1.f;
		shapeDef.material.friction = 0.3f;
		shapeDef.material.restitution = 0.f;
		shapeDef.isSensor = isSensor;
		// Box2D only reports a sensor overlap when BOTH shapes opt in, so every shape must be visible to sensors.
		shapeDef.enableSensorEvents = true;
		shapeDef.enableCustomFiltering = true;
		shapeDef.filter.categoryBits = GetEntityPhysicsLayer(scene.GetRegistry(), nativeEntity);

		if (shapeType == ShapeType::Square) {
			// Never build a zero-area box: a degenerate polygon makes Box2D's
			// mass-from-shapes divide by area 0, NaN-ing a Dynamic body's transform.
			// The floor also keeps mass (= density * (2h)^2) from being vanishingly
			// small, which would give a huge inverse mass and explode the solver
			// ("Box2D: unstable:"). Matches k_MinBoxHalfExtent in BoxCollider2DComponent.
			constexpr float kMinBoxHalfExtent = 1e-2f;
			const float hx = std::max(std::abs(0.5f * transform.Scale.x), kMinBoxHalfExtent);
			const float hy = std::max(std::abs(0.5f * transform.Scale.y), kMinBoxHalfExtent);
			b2Polygon b2Polygon = b2MakeBox(hx, hy);
			shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &b2Polygon);
		}
		else if (shapeType == ShapeType::Circle)
		{
			float r = 0.5f * std::max(std::abs(transform.Scale.x), std::abs(transform.Scale.y));
			b2Circle circle = { b2Vec2{0,0}, r };
			shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
		}
		else if (shapeType == ShapeType::Polygon)
		{
			// Default to a regular pentagon scaled by the entity's transform.
			// Box2D requires convex hulls; a regular n-gon is always convex.
			constexpr int kSides = 5;
			constexpr float kPi = 3.14159265358979323846f;
			b2Vec2 points[kSides];
			const float baseRadius = 0.5f;
			for (int i = 0; i < kSides; ++i) {
				const float angle = -static_cast<float>(i) * (2.0f * kPi) / static_cast<float>(kSides) + kPi * 0.5f;
				points[i] = b2Vec2{
					std::cos(angle) * baseRadius * transform.Scale.x,
					std::sin(angle) * baseRadius * transform.Scale.y };
			}
			b2Hull hull = b2ComputeHull(points, kSides);
			b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
			shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &polygon);
		}


		return shapeId;
	}

	bool Box2DWorld::OwnsBody(b2BodyId bodyId, const Scene& scene, EntityHandle entity) const {
		if (!b2Body_IsValid(bodyId)) return false;
		const auto found = m_BodyBindings.find(b2StoreBodyId(bodyId));
		return found != m_BodyBindings.end() && found->second.OwningScene == &scene && found->second.Entity == entity;
	}

	b2Counters Box2DWorld::GetResourceCounts() const {
		return b2World_IsValid(m_WorldId) ? b2World_GetCounters(m_WorldId) : b2Counters{};
	}

	CollisionBodyRef Box2DWorld::ResolveShape(b2ShapeId shapeId) const {
		if (!b2Shape_IsValid(shapeId)) {
			return {};
		}

		const b2BodyId bodyId = b2Shape_GetBody(shapeId);
		const auto it = m_BodyBindings.find(b2StoreBodyId(bodyId));
		if (it == m_BodyBindings.end()) {
			return {};
		}

		return CollisionBodyRef{
			.Entity = it->second.Entity,
			.OwningScene = it->second.OwningScene
		};
	}

	void Box2DWorld::UnregisterBodyBinding(b2BodyId bodyId) {
		if (!b2Body_IsValid(bodyId)) {
			return;
		}

		m_BodyBindings.erase(b2StoreBodyId(bodyId));
	}

	void Box2DWorld::DestroyAllBodiesForScene(Scene* scene) {
		if (scene == nullptr) {
			return;
		}

		// Two-pass: collect first because b2DestroyBody invalidates the b2BodyId,
		// and erasing the map entry mid-range invalidates iterators.
		std::vector<uint64_t> stored;
		stored.reserve(m_BodyBindings.size());
		for (const auto& [stored_id, binding] : m_BodyBindings) {
			if (binding.OwningScene == scene) {
				stored.push_back(stored_id);
			}
		}

		for (uint64_t id : stored) {
			b2BodyId bodyId = b2LoadBodyId(id);
			if (b2Body_IsValid(bodyId)) {
				// Unregister every shape from the dispatcher before destroying the
				// body so lingering b2ShapeId entries can't fire on recycled ids.
				constexpr int kInlineShapes = 8;
				b2ShapeId stack[kInlineShapes];
				const int count = b2Body_GetShapeCount(bodyId);
				if (count <= kInlineShapes) {
					b2Body_GetShapes(bodyId, stack, count);
					for (int i = 0; i < count; ++i) m_Dispatcher.UnregisterShape(stack[i]);
				} else {
					std::vector<b2ShapeId> heap(static_cast<size_t>(count));
					b2Body_GetShapes(bodyId, heap.data(), count);
					for (int i = 0; i < count; ++i) m_Dispatcher.UnregisterShape(heap[i]);
				}
				b2DestroyBody(bodyId);
			}
			m_BodyBindings.erase(id);
		}

		// UnregisterShape only removes by shape id; bodies torn down before their ids are walked leave dangling rows — sweep by Scene* to purge them.
		m_Dispatcher.PurgeContactsForScene(scene);
	}

	CollisionDispatcher& Box2DWorld::GetDispatcher() { return m_Dispatcher; }
}
