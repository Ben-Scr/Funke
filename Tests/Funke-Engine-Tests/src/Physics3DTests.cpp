#include <doctest/doctest.h>

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Physics3D/PhysicsWorld3D.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <vector>

using namespace Funke;

namespace {
	constexpr float k_Dt = 1.0f / 60.0f;

	Entity CreateBox(Scene& scene, const Vec3& position, const Vec3& scale = Vec3(1.0f)) {
		Entity entity = scene.CreateEmptyEntity();
		auto& transform = entity.AddComponent<Transform3DComponent>();
		transform.Position = position;
		transform.Scale = scale;
		entity.AddComponent<BoxCollider3DComponent>();
		return entity;
	}

	struct ContactLog {
		std::vector<std::pair<Collision3D, CollisionPhase3D>> Events;

		bool Has(CollisionPhase3D phase, EntityHandle a, EntityHandle b, bool trigger) const {
			for (const auto& [collision, p] : Events) {
				const bool pairMatches = (collision.EntityA == a && collision.EntityB == b) || (collision.EntityA == b && collision.EntityB == a);
				if (p == phase && pairMatches && collision.IsTrigger == trigger) return true;
			}
			return false;
		}
	};

	void Run(Scene& scene, PhysicsWorld3D& world, int steps, ContactLog* log = nullptr) {
		for (int i = 0; i < steps; ++i) {
			Transform3DHierarchySystem::Propagate(scene);
			world.Simulate(scene, k_Dt);
			world.DispatchContacts([log](const Collision3D& collision, CollisionPhase3D phase) {
				if (log) log->Events.emplace_back(collision, phase);
			});
		}
	}
}

TEST_CASE("Physics3D: a dynamic box falls and rests on a static ground box") {
	auto scene = Scene::CreateDetachedScene("Physics3DFall");
	Entity ground = CreateBox(*scene, { 0.0f, 0.0f, 0.0f }, { 10.0f, 1.0f, 10.0f });
	Entity box = CreateBox(*scene, { 0.0f, 3.0f, 0.0f });
	box.AddComponent<Rigidbody3DComponent>();

	PhysicsWorld3D world;
	ContactLog log;
	Run(*scene, world, 180, &log);

	CHECK(world.GetBodyCount() == 2);
	// Ground top is y = 0.5, the box's half height 0.5.
	CHECK(box.GetComponent<Transform3DComponent>().Position.y == doctest::Approx(1.0f).epsilon(0.03));
	CHECK(log.Has(CollisionPhase3D::Enter, box.GetHandle(), ground.GetHandle(), false));
	CHECK(log.Has(CollisionPhase3D::Stay, box.GetHandle(), ground.GetHandle(), false));
	CHECK_FALSE(log.Has(CollisionPhase3D::Exit, box.GetHandle(), ground.GetHandle(), false));

	// Falling asleep must not read as separation; removing the box does, for the ground's side.
	CHECK(world.IsSleeping(box.GetHandle()));
	log.Events.clear();
	box.RemoveComponent<Rigidbody3DComponent>();
	box.RemoveComponent<BoxCollider3DComponent>();
	Run(*scene, world, 1, &log);
	CHECK(world.GetBodyCount() == 1);
	bool groundSawExit = false;
	for (const auto& [collision, phase] : log.Events) {
		groundSawExit = groundSawExit || (phase == CollisionPhase3D::Exit
			&& (collision.EntityA == ground.GetHandle() || collision.EntityB == ground.GetHandle()));
	}
	CHECK(groundSawExit);
}

TEST_CASE("Physics3D: raycasts hit the nearest non-trigger collider") {
	auto scene = Scene::CreateDetachedScene("Physics3DRaycast");
	Entity ground = CreateBox(*scene, { 0.0f, 0.0f, 0.0f }, { 10.0f, 1.0f, 10.0f });
	Entity trigger = scene->CreateEmptyEntity();
	trigger.AddComponent<Transform3DComponent>().Position = { 3.0f, 3.0f, 0.0f };
	trigger.AddComponent<SphereCollider3DComponent>().IsTrigger = true;

	PhysicsWorld3D world;
	Run(*scene, world, 1);

	RaycastHit3D hit;
	REQUIRE(world.Raycast({ 3.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f, hit));
	CHECK(hit.Entity == ground.GetHandle());
	CHECK(hit.Point.y == doctest::Approx(0.5f).epsilon(0.01));
	CHECK(hit.Normal.y == doctest::Approx(1.0f).epsilon(0.01));
	CHECK(hit.Distance == doctest::Approx(9.5f).epsilon(0.01));
	CHECK_FALSE(world.Raycast({ 3.0f, 10.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 100.0f, hit));

	// Moving a static collider's Transform3D moves its body.
	ground.GetComponent<Transform3DComponent>().Position = { 0.0f, -2.0f, 0.0f };
	Run(*scene, world, 1);
	REQUIRE(world.Raycast({ 3.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f, hit));
	CHECK(hit.Point.y == doctest::Approx(-1.5f).epsilon(0.01));
}

TEST_CASE("Physics3D: triggers report overlaps without stopping bodies") {
	auto scene = Scene::CreateDetachedScene("Physics3DTrigger");
	Entity zone = CreateBox(*scene, { 0.0f, 0.0f, 0.0f }, { 4.0f, 1.0f, 4.0f });
	zone.GetComponent<BoxCollider3DComponent>().IsTrigger = true;
	Entity ball = scene->CreateEmptyEntity();
	ball.AddComponent<Transform3DComponent>().Position = { 0.0f, 2.0f, 0.0f };
	ball.AddComponent<SphereCollider3DComponent>();
	ball.AddComponent<Rigidbody3DComponent>();

	PhysicsWorld3D world;
	ContactLog log;
	Run(*scene, world, 120, &log);

	CHECK(log.Has(CollisionPhase3D::Enter, ball.GetHandle(), zone.GetHandle(), true));
	CHECK(log.Has(CollisionPhase3D::Exit, ball.GetHandle(), zone.GetHandle(), true));
	CHECK(ball.GetComponent<Transform3DComponent>().Position.y < -1.0f);
}

TEST_CASE("Physics3D: child colliders join their Rigidbody3D ancestor's body") {
	auto scene = Scene::CreateDetachedScene("Physics3DCompound");
	CreateBox(*scene, { 0.0f, 0.0f, 0.0f }, { 10.0f, 1.0f, 10.0f });
	Entity body = scene->CreateEmptyEntity();
	body.AddComponent<Transform3DComponent>().Position = { 0.0f, 3.0f, 0.0f };
	body.AddComponent<Rigidbody3DComponent>();
	Entity child = CreateBox(*scene, { 0.0f, -1.0f, 0.0f });
	child.SetParent(body);
	Entity capsule = scene->CreateEmptyEntity();
	capsule.AddComponent<Transform3DComponent>().Position = { 0.0f, 0.0f, 0.0f };
	capsule.AddComponent<CapsuleCollider3DComponent>().Height = 1.0f;
	capsule.SetParent(body);

	PhysicsWorld3D world;
	Run(*scene, world, 1);
	CHECK(world.GetBodyCount() == 2);
	CHECK(world.FindBodyOwner(*scene, child.GetHandle()) == body.GetHandle());

	Run(*scene, world, 180);
	// The child box (half height 0.5, one unit below the body) rests on the ground top at 0.5.
	CHECK(body.GetComponent<Transform3DComponent>().Position.y == doctest::Approx(2.0f).epsilon(0.03));
	CHECK(child.GetComponent<Transform3DComponent>().Position.y == doctest::Approx(-1.0f));

	// Removing the rigidbody turns the colliders into separate static bodies.
	body.RemoveComponent<Rigidbody3DComponent>();
	Run(*scene, world, 1);
	CHECK(world.GetBodyCount() == 3);
}

TEST_CASE("Physics3D: forces, velocities and body rebuilds") {
	auto scene = Scene::CreateDetachedScene("Physics3DForces");
	Entity ball = scene->CreateEmptyEntity();
	ball.AddComponent<Transform3DComponent>();
	ball.AddComponent<SphereCollider3DComponent>();
	auto& rigidbody = ball.AddComponent<Rigidbody3DComponent>();
	rigidbody.GravityScale = 0.0f;
	rigidbody.LinearDrag = 0.0f;
	rigidbody.Mass = 2.0f;

	PhysicsWorld3D world;
	REQUIRE(world.EnsureBody(*scene, ball.GetHandle()));
	world.AddForce(ball.GetHandle(), { 4.0f, 0.0f, 0.0f }, ForceMode3D::Impulse);
	CHECK(world.GetLinearVelocity(ball.GetHandle()).x == doctest::Approx(2.0f));
	world.AddForce(ball.GetHandle(), { 0.0f, 0.0f, 3.0f }, ForceMode3D::VelocityChange);
	CHECK(world.GetLinearVelocity(ball.GetHandle()).z == doctest::Approx(3.0f));

	Run(*scene, world, 60);
	CHECK(ball.GetComponent<Transform3DComponent>().Position.x == doctest::Approx(2.0f).epsilon(0.02));

	// An inspector edit rebuilds the body but keeps its velocity.
	ball.GetComponent<SphereCollider3DComponent>().Radius = 1.0f;
	Run(*scene, world, 1);
	CHECK(world.GetLinearVelocity(ball.GetHandle()).x == doctest::Approx(2.0f));

	ball.GetComponent<Rigidbody3DComponent>().FreezePositionX = true;
	Run(*scene, world, 30);
	CHECK(world.GetLinearVelocity(ball.GetHandle()).x == doctest::Approx(0.0f));
}

TEST_CASE("Physics3D: seams between boxes of one compound body barely lift a fast ball") {
	auto scene = Scene::CreateDetachedScene("Physics3DSeams");
	// Separate static boxes throw a ball rolling at 12 m/s ~10 cm up at every seam. As children of one static
	// body they form a compound, whose internal edges enhanced internal edge removal mostly ignores. Fully
	// seamless rolling needs a welded MeshCollider3D surface (shared triangle edges).
	Entity course = scene->CreateEmptyEntity();
	course.AddComponent<Transform3DComponent>();
	course.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
	for (int i = 0; i < 12; ++i) {
		CreateBox(*scene, { 0.0f, -0.5f, -2.0f * static_cast<float>(i) }, { 4.0f, 1.0f, 2.0f }).SetParent(course);
	}
	Entity ball = scene->CreateEmptyEntity();
	ball.AddComponent<Transform3DComponent>().Position = { 0.0f, 0.5f, -0.5f }; // on the first box, not its edge
	ball.AddComponent<SphereCollider3DComponent>();
	auto& rigidbody = ball.AddComponent<Rigidbody3DComponent>();
	rigidbody.LinearDrag = 0.0f;

	PhysicsWorld3D world;
	REQUIRE(world.EnsureBody(*scene, ball.GetHandle()));
	// Already rolling (no slip), so friction doesn't have to spin it up first.
	world.SetLinearVelocity(ball.GetHandle(), { 0.0f, 0.0f, -12.0f });
	world.SetAngularVelocity(ball.GetHandle(), { -24.0f, 0.0f, 0.0f });
	float highest = 0.0f;
	for (int step = 0; step < 100; ++step) {
		Run(*scene, world, 1);
		highest = std::max(highest, ball.GetComponent<Transform3DComponent>().Position.y);
	}
	CHECK(ball.GetComponent<Transform3DComponent>().Position.z < -18.0f); // crossed the seams at speed
	CHECK(highest < 0.56f);
	CHECK(world.GetLinearVelocity(ball.GetHandle()).z < -11.0f);
}

TEST_CASE("Physics3D: interpolation blends between steps without reading as a teleport") {
	auto scene = Scene::CreateDetachedScene("Physics3DInterpolation");
	Entity ball = scene->CreateEmptyEntity();
	ball.AddComponent<Transform3DComponent>();
	ball.AddComponent<SphereCollider3DComponent>();
	auto& rigidbody = ball.AddComponent<Rigidbody3DComponent>();
	rigidbody.GravityScale = 0.0f;
	rigidbody.LinearDrag = 0.0f;

	PhysicsWorld3D world;
	REQUIRE(world.EnsureBody(*scene, ball.GetHandle()));
	world.SetLinearVelocity(ball.GetHandle(), { 6.0f, 0.0f, 0.0f });
	Run(*scene, world, 2);
	const float stepDistance = 6.0f * k_Dt;
	CHECK(ball.GetComponent<Transform3DComponent>().Position.x == doctest::Approx(2.0f * stepDistance).epsilon(0.01));

	world.Present(*scene, 0.5f);
	CHECK(ball.GetComponent<Transform3DComponent>().Position.x == doctest::Approx(1.5f * stepDistance).epsilon(0.01));

	// The blended pose must not be mistaken for a script move (which would teleport the body back).
	Run(*scene, world, 1);
	CHECK(ball.GetComponent<Transform3DComponent>().Position.x == doctest::Approx(3.0f * stepDistance).epsilon(0.01));
	CHECK(world.GetLinearVelocity(ball.GetHandle()).x == doctest::Approx(6.0f));

	// A real script move still teleports.
	ball.GetComponent<Transform3DComponent>().Position = { 0.0f, 5.0f, 0.0f };
	Run(*scene, world, 1);
	CHECK(ball.GetComponent<Transform3DComponent>().Position.y == doctest::Approx(5.0f).epsilon(0.01));
}

TEST_CASE("Physics3D: components register after the 3D slice and round-trip through JSON") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);

	const ComponentInfo* transform3D = registry.FindBySerializedName("Transform3D");
	REQUIRE(transform3D);
	for (const char* name : { "Rigidbody3D", "BoxCollider3D", "SphereCollider3D", "CapsuleCollider3D", "MeshCollider3D" }) {
		CAPTURE(name);
		const ComponentInfo* info = registry.FindBySerializedName(name);
		REQUIRE(info);
		CHECK(info->serialize);
		CHECK(info->deserialize);
		CHECK(info->typeIdU32 > transform3D->typeIdU32);
	}

	auto scene = Scene::CreateDetachedScene("Physics3DSerialize");
	Entity entity = scene->CreateEmptyEntity();
	entity.AddComponent<Transform3DComponent>();
	auto& capsule = entity.AddComponent<CapsuleCollider3DComponent>();
	capsule.Radius = 0.25f;
	capsule.Height = 3.0f;
	capsule.Direction = CapsuleAxis::Z;
	capsule.Center = { 1.0f, 2.0f, 3.0f };
	capsule.IsTrigger = true;
	auto& rigidbody = entity.AddComponent<Rigidbody3DComponent>();
	rigidbody.Type = BodyType::Kinematic;
	rigidbody.Mass = 7.5f;
	rigidbody.FreezeRotationY = true;

	const ComponentInfo* capsuleInfo = registry.FindBySerializedName("CapsuleCollider3D");
	const ComponentInfo* rigidbodyInfo = registry.FindBySerializedName("Rigidbody3D");
	const Json::Value capsuleJson = capsuleInfo->serialize(entity);
	const Json::Value rigidbodyJson = rigidbodyInfo->serialize(entity);
	capsule = CapsuleCollider3DComponent{};
	rigidbody = Rigidbody3DComponent{};
	capsuleInfo->deserialize(entity, capsuleJson);
	rigidbodyInfo->deserialize(entity, rigidbodyJson);

	const auto& restoredCapsule = entity.GetComponent<CapsuleCollider3DComponent>();
	CHECK(restoredCapsule.Radius == doctest::Approx(0.25f));
	CHECK(restoredCapsule.Height == doctest::Approx(3.0f));
	CHECK(restoredCapsule.Direction == CapsuleAxis::Z);
	CHECK(restoredCapsule.Center.z == doctest::Approx(3.0f));
	CHECK(restoredCapsule.IsTrigger);
	const auto& restoredBody = entity.GetComponent<Rigidbody3DComponent>();
	CHECK(restoredBody.Type == BodyType::Kinematic);
	CHECK(restoredBody.Mass == doctest::Approx(7.5f));
	CHECK(restoredBody.FreezeRotationY);
	CHECK_FALSE(restoredBody.FreezeRotationX);
}
