#include <doctest/doctest.h>
#include "Components/Graphics/CameraFollowRigComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Systems/CameraFollowRigSystem.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"
#include <cmath>

using namespace Funke;
TEST_CASE("Follow rig consumes the current post-movement pose and safely loses or changes its target") {
	auto scene = Scene::CreateDetachedScene("Follow");
	auto target = scene->CreateEmptyEntity(); target.AddComponent<Transform3DComponent>();
	auto camera = scene->CreateEmptyEntity(); camera.AddComponent<Transform3DComponent>(); camera.AddComponent<Camera3DComponent>();
	auto& rig = camera.AddComponent<CameraFollowRigComponent>(); rig.Target = target.GetHandle(); rig.Damping = 0;
	target.GetComponent<Transform3DComponent>().Position = {9, 0, 0};
	Transform3DHierarchySystem::Propagate(*scene); CameraFollowRigSystem::Step(*scene, 1.0f / 60);
	CHECK((camera.GetComponent<Transform3DComponent>().Position == Vec3(9, 2, 5)));
	CHECK((camera.GetComponent<Transform3DComponent>().GetWorldPosition() == Vec3(9, 2, 5)));
	scene->DestroyEntity(target);
	CameraFollowRigSystem::Step(*scene, 1.0f / 60); CHECK_FALSE(rig.Initialized);
	auto next = scene->CreateEmptyEntity(); next.AddComponent<Transform3DComponent>().Position = {100, 0, 0};
	rig.Target = next.GetHandle(); rig.Orbit = true; rig.Yaw = 90; rig.Pitch = 0; rig.Distance = 10;
	Transform3DHierarchySystem::Propagate(*scene); CameraFollowRigSystem::Step(*scene, 1.0f / 60);
	CHECK(rig.Initialized); CHECK(rig.FollowPosition.x == doctest::Approx(110));
	CHECK(rig.FollowPosition.z == doctest::Approx(0).epsilon(0.0001));
}
TEST_CASE("Follow damping stays stable with moving targets sampled at different physics rates") {
	auto simulate = [](int physicsRate) {
		auto scene = Scene::CreateDetachedScene("Rate");
		auto target = scene->CreateEmptyEntity(); target.AddComponent<Transform3DComponent>();
		auto camera = scene->CreateEmptyEntity(); camera.AddComponent<Transform3DComponent>(); camera.AddComponent<Camera3DComponent>();
		auto& rig = camera.AddComponent<CameraFollowRigComponent>(); rig.Target = target.GetHandle(); rig.Offset = Vec3(0, 0, 5);
		float previous = -1;
		for (int frame = 0; frame <= 240; ++frame) {
			const float time = frame / 120.0f;
			const float tick = std::floor(time * physicsRate);
			const float alpha = time * physicsRate - tick;
			// Render interpolation of a constant-velocity physics target, before the rig.
			target.GetComponent<Transform3DComponent>().Position.x = (tick + alpha) / physicsRate * 4;
			Transform3DHierarchySystem::Propagate(*scene); CameraFollowRigSystem::Step(*scene, 1.0f / 120);
			CHECK(rig.FollowPosition.x >= previous); CHECK(std::isfinite(rig.FollowPosition.x)); previous = rig.FollowPosition.x;
		}
		return rig.FollowPosition.x;
	};
	const float at30 = simulate(30), at50 = simulate(50), at120 = simulate(120);
	CHECK(at30 == doctest::Approx(at50)); CHECK(at30 == doctest::Approx(at120));
}

TEST_CASE("Follow rig converts world poses through parents without a transform and ignores singular parents") {
	auto scene = Scene::CreateDetachedScene("Parented camera");
	auto parent = scene->CreateEmptyEntity(); parent.AddComponent<Transform3DComponent>().Position = {10, 0, 0};
	auto group = scene->CreateEmptyEntity(); group.SetParent(parent);
	auto target = scene->CreateEmptyEntity(); target.AddComponent<Transform3DComponent>().Position = {20, 0, 0};
	auto camera = scene->CreateEmptyEntity(); camera.AddComponent<Transform3DComponent>(); camera.AddComponent<Camera3DComponent>(); camera.SetParent(group);
	auto& rig = camera.AddComponent<CameraFollowRigComponent>(); rig.Target = target.GetHandle(); rig.Damping = 0;
	Transform3DHierarchySystem::Propagate(*scene); CameraFollowRigSystem::Step(*scene, 1.0f / 60);
	CHECK((camera.GetComponent<Transform3DComponent>().Position == Vec3(10, 2, 5)));
	CHECK((camera.GetComponent<Transform3DComponent>().GetWorldPosition() == Vec3(20, 2, 5)));
	parent.GetComponent<Transform3DComponent>().Scale = Vec3(0);
	const auto previous = camera.GetComponent<Transform3DComponent>().Position;
	CameraFollowRigSystem::Step(*scene, 1.0f / 60);
	CHECK((camera.GetComponent<Transform3DComponent>().Position == previous));
}

#if FUNKE_WITH_PHYSICS3D
#include "Physics3D/PhysicsWorld3D.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
TEST_CASE("Follow rig tracks Jolt presentation at 30 50 and 120 fixed ticks per second") {
	std::vector<float> finalPositions;
	for (int rate : {30, 50, 120}) {
		CAPTURE(rate);
		auto scene = Scene::CreateDetachedScene("Physics follow");
		auto target = scene->CreateEmptyEntity(); target.AddComponent<Transform3DComponent>();
		target.AddComponent<BoxCollider3DComponent>(); auto& body = target.AddComponent<Rigidbody3DComponent>(); body.LinearDrag = 0; body.GravityScale = 0;
		auto camera = scene->CreateEmptyEntity(); camera.AddComponent<Transform3DComponent>(); camera.AddComponent<Camera3DComponent>();
		auto& rig = camera.AddComponent<CameraFollowRigComponent>(); rig.Target = target.GetHandle(); rig.Damping = 0;
		PhysicsWorld3D world; world.SetGravity(Vec3(0));
		Transform3DHierarchySystem::Propagate(*scene); world.SyncFromScene(*scene, 1.0f / rate);
		world.SetLinearVelocity(target.GetHandle(), Vec3(4, 0, 0));
		double accumulator = 0; float last = -1;
		for (int frame = 0; frame < 240; ++frame) {
			accumulator += 1.0 / 120;
			while (accumulator + 1e-9 >= 1.0 / rate) {
				world.Simulate(*scene, 1.0f / rate); accumulator -= 1.0 / rate;
			}
			world.Present(*scene, static_cast<float>(std::max(0.0, accumulator * rate)));
			Transform3DHierarchySystem::Propagate(*scene); CameraFollowRigSystem::Step(*scene, 1.0f / 120);
			const float renderedTarget = target.GetComponent<Transform3DComponent>().GetWorldPosition().x;
			CHECK(rig.FollowPosition.x == doctest::Approx(renderedTarget)); CHECK(rig.FollowPosition.x >= last - 1e-5f);
			last = rig.FollowPosition.x;
		}
		CHECK(last > 7.8f); CHECK(last <= 8.01f); finalPositions.push_back(last);
		scene->DestroyEntity(target); CameraFollowRigSystem::Step(*scene, 1.0f / 120); CHECK_FALSE(rig.Initialized);
	}
	CHECK(std::abs(finalPositions.front() - finalPositions.back()) < 0.15f);
}
#endif
