#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Physics/Physics2D.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Funke {
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);
	bool TryResolveEntityByUUID(const Scene& scene, uint64_t entityID, EntityHandle& outHandle);
	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle);
	Scene* GetScene();

	#define GET_COMPONENT(Type, entityID, failReturn) \
		Scene* scene = nullptr; \
		EntityHandle handle = entt::null; \
		if (!ResolveEntityReference(entityID, scene, handle)) return failReturn; \
		if (!scene->IsValid(handle)) return failReturn; \
		if (!scene->HasComponent<Type>(handle)) return failReturn; \
		auto& comp = scene->GetComponent<Type>(handle)

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

	#undef GET_COMPONENT

	void PopulatePhysics2DBindings(NativeBindings& b) {
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
		b.Rigidbody2D_GetFreezePositionX = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetFreezePositionX);
		b.Rigidbody2D_SetFreezePositionX = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetFreezePositionX);
		b.Rigidbody2D_GetFreezePositionY = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetFreezePositionY);
		b.Rigidbody2D_SetFreezePositionY = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetFreezePositionY);
		b.Rigidbody2D_GetFreezeRotation  = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetFreezeRotation);
		b.Rigidbody2D_SetFreezeRotation  = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetFreezeRotation);
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
		b.Rigidbody2D_GetDynamics = FNK_GUARDED_BINDING(Funke_Rigidbody2D_GetDynamics);
		b.Rigidbody2D_SetDynamics = FNK_GUARDED_BINDING(Funke_Rigidbody2D_SetDynamics);
		b.Rigidbody2D_ApplyAtPoint = FNK_GUARDED_BINDING(Funke_Rigidbody2D_ApplyAtPoint);
		b.Rigidbody2D_ApplyAngular = FNK_GUARDED_BINDING(Funke_Rigidbody2D_ApplyAngular);
		b.Collider2D_GetGroupIndex = FNK_GUARDED_BINDING(Funke_Collider2D_GetGroupIndex);
		b.Collider2D_SetGroupIndex = FNK_GUARDED_BINDING(Funke_Collider2D_SetGroupIndex);
	}
}
