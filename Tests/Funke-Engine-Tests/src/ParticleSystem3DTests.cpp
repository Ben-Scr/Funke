#include <doctest/doctest.h>

#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Math/Random.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Serialization/Json.hpp"
#include "Core/Application.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

using namespace Funke;

namespace {
	const glm::mat4 k_Identity(1.0f);

	ParticleSystem3DComponent MakeEmitter() {
		ParticleSystem3DComponent ps;
		ps.EmitOverTime = 10.0f;
		ps.LifeTime = 1.0f;
		ps.Speed = 2.0f;
		ps.Play();
		return ps;
	}
}

TEST_CASE("ParticleSystem3D emits at its rate, carries the remainder and expires particles") {
	Random::SetSeed(1234);
	ParticleSystem3DComponent ps = MakeEmitter();

	ps.Simulate(0.25f, k_Identity);  // 2.5 -> 2
	CHECK(ps.GetParticleCount() == 2);
	ps.Simulate(0.25f, k_Identity);  // 2.5 + 0.5 carried -> 3
	CHECK(ps.GetParticleCount() == 5);

	ps.Stop();
	ps.Simulate(0.6f, k_Identity);
	CHECK(ps.GetParticleCount() == 3);  // the first two are 1.1 s old
	ps.Simulate(0.6f, k_Identity);
	CHECK(ps.GetParticleCount() == 0);
	CHECK(ps.IsSimulating());
	CHECK_FALSE(ps.IsEmitting());
}

TEST_CASE("ParticleSystem3D cone sprays upward inside the cone angle") {
	Random::SetSeed(42);
	ParticleSystem3DComponent ps = MakeEmitter();
	ps.Shape = ParticleShape3D::Cone;
	ps.ConeAngle = 30.0f;
	ps.Radius = 0.5f;
	ps.EmitOverTime = 1000.0f;
	ps.Simulate(0.1f, k_Identity);
	REQUIRE(ps.GetParticleCount() == 100);

	const float minCos = std::cos(glm::radians(30.0f)) - 1e-4f;
	for (const auto& p : ps.GetParticles()) {
		const glm::vec3 direction = glm::normalize(p.Velocity);
		CHECK(direction.y >= minCos);
		CHECK(glm::length(p.Velocity) == doctest::Approx(2.0f));
	}
}

TEST_CASE("ParticleSystem3D sphere surface emission lands on the radius and flies outward") {
	Random::SetSeed(7);
	ParticleSystem3DComponent ps = MakeEmitter();
	ps.Shape = ParticleShape3D::Sphere;
	ps.EmitFromSurface = true;
	ps.Radius = 2.0f;
	ps.Speed = 0.0f;
	ps.Emit(50);
	REQUIRE(ps.GetParticleCount() == 50);
	for (const auto& p : ps.GetParticles()) {
		CHECK(glm::length(p.Position) == doctest::Approx(2.0f).epsilon(1e-3));
	}
}

TEST_CASE("ParticleSystem3D world space bakes the emitter pose, local space follows it") {
	Random::SetSeed(99);
	const glm::mat4 moved = glm::translate(k_Identity, glm::vec3(10.0f, 0.0f, 0.0f));

	ParticleSystem3DComponent world = MakeEmitter();
	world.Shape = ParticleShape3D::Box;
	world.BoxSize = glm::vec3(0.0f);
	world.Speed = 0.0f;
	world.SimulationSpace = ParticleSpace3D::World;
	world.Simulate(0.1f, moved);
	REQUIRE(world.GetParticleCount() == 1);
	CHECK(world.GetParticles()[0].Position.x == doctest::Approx(10.0f));

	ParticleSystem3DComponent local = world;
	local.StopAndReset();
	local.SimulationSpace = ParticleSpace3D::Local;
	local.Play();
	local.Simulate(0.1f, moved);
	REQUIRE(local.GetParticleCount() == 1);
	CHECK(local.GetParticles()[0].Position.x == doctest::Approx(0.0f));
}

TEST_CASE("ParticleSystem3D respects gravity, max particles, bursts and quality") {
	Random::SetSeed(5);
	SUBCASE("gravity accelerates downward") {
		ParticleSystem3DComponent ps = MakeEmitter();
		ps.Speed = 0.0f;
		ps.UseGravity = true;
		ps.Gravity = { 0.0f, -10.0f, 0.0f };
		ps.Emit(1);
		ps.Pause();
		ps.Play();
		ps.EmitOverTime = 0.0f;
		ps.Simulate(0.5f, k_Identity);
		REQUIRE(ps.GetParticleCount() == 1);
		CHECK(ps.GetParticles()[0].Velocity.y == doctest::Approx(-5.0f));
	}
	SUBCASE("max particles caps emission") {
		ParticleSystem3DComponent ps = MakeEmitter();
		ps.MaxParticles = 8;
		ps.EmitOverTime = 1000.0f;
		ps.Simulate(0.1f, k_Identity);
		CHECK(ps.GetParticleCount() == 8);
	}
	SUBCASE("a burst fires on the first update, then every interval") {
		ParticleSystem3DComponent ps = MakeEmitter();
		ps.EmitOverTime = 0.0f;
		ps.LifeTime = 10.0f;
		ps.AddBurst({ 20, 1.0f, 0.0f });
		ps.Simulate(0.1f, k_Identity);
		CHECK(ps.GetParticleCount() == 20);
		ps.Simulate(0.5f, k_Identity);
		CHECK(ps.GetParticleCount() == 20);
		ps.Simulate(0.5f, k_Identity);
		CHECK(ps.GetParticleCount() == 40);
	}
	SUBCASE("quality Off clears and Low thins out emission") {
		ParticleSystem3DComponent ps = MakeEmitter();
		ps.EmitOverTime = 100.0f;
		ps.Simulate(0.1f, k_Identity, ParticleQualityLevel::Low);
		CHECK(ps.GetParticleCount() == 2);  // 10 * 0.25, floored
		ps.Simulate(0.1f, k_Identity, ParticleQualityLevel::Off);
		CHECK(ps.GetParticleCount() == 0);
	}
	SUBCASE("Emit wakes a stopped system so the particles expire") {
		ParticleSystem3DComponent ps;
		ps.LifeTime = 0.5f;
		ps.Emit(3);
		CHECK(ps.IsSimulating());
		CHECK_FALSE(ps.IsEmitting());
		ps.Simulate(0.6f, k_Identity);
		CHECK(ps.GetParticleCount() == 0);
	}
}

TEST_CASE("ParticleSystem3D lifetime modules scale and tint over normalized age") {
	Random::SetSeed(3);
	ParticleSystem3DComponent ps = MakeEmitter();
	ps.EmitOverTime = 0.0f;
	ps.Size = 2.0f;
	ps.LifeTime = 1.0f;
	ps.ScaleOverLifetimeEnabled = true;
	ps.ScaleOverLifetime.Keys = { { { 0.0f, 1.0f }, { -0.2f, 0.0f }, { 0.2f, 0.0f } }, { { 1.0f, 0.0f }, { -0.2f, 0.0f }, { 0.2f, 0.0f } } };
	ps.ColorOverLifetimeEnabled = true;
	ps.ColorOverLifetime.AlphaKeys = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
	ps.Emit(1);
	ps.Simulate(0.5f, k_Identity);
	REQUIRE(ps.GetParticleCount() == 1);
	const auto& p = ps.GetParticles()[0];
	CHECK(p.Size < 2.0f);
	CHECK(p.Size > 0.0f);
	CHECK(p.Color.a == doctest::Approx(0.5f));
}

TEST_CASE("ParticleSystem3D round-trips through the component registry and resets from {}") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);
	const ComponentInfo* info = registry.FindBySerializedName("ParticleSystem3D");
	REQUIRE(info);
	CHECK(info->displayName == "Particle System 3D");
	auto scene = Scene::CreateDetachedScene("Particles3DSerialization");

	Entity source = scene->CreateEmptyEntity();
	auto& ps = source.AddComponent<ParticleSystem3DComponent>();
	ps.PlayOnAwake = false;
	ps.LifeTime = 3.5f;
	ps.SpeedVariation = 0.25f;
	ps.Size = 0.75f;
	ps.RandomRotation = true;
	ps.UseGravity = true;
	ps.Gravity = { 1.0f, -2.0f, 3.0f };
	ps.Drag = 0.4f;
	ps.EmitOverTime = 42.5f;
	ps.SimulationSpace = ParticleSpace3D::Local;
	ps.Shape = ParticleShape3D::Box;
	ps.BoxSize = { 2.0f, 3.0f, 4.0f };
	ps.ScaleOverLifetimeEnabled = true;
	ps.ColorOverLifetimeEnabled = true;
	ps.ColorOverLifetime.AlphaKeys = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
	ps.Texture = 18446744073709551533ull;
	ps.Color = Color(0.1f, 0.2f, 0.3f, 0.4f);
	ps.BlendMode = ParticleBlendMode3D::Additive;
	ps.MaxParticles = 321;
	ps.AddBurst({ 7, 0.5f, 0.0f });

	const Json::Value json = info->serialize(source);
	CHECK(json.FindMember("texture")->IsString());

	Entity target = scene->CreateEmptyEntity();
	target.AddComponent<ParticleSystem3DComponent>();
	info->deserialize(target, Json::Parse(Json::Stringify(json)));
	const auto& loaded = target.GetComponent<ParticleSystem3DComponent>();
	CHECK_FALSE(loaded.PlayOnAwake);
	CHECK(loaded.LifeTime == doctest::Approx(3.5f));
	CHECK(loaded.SpeedVariation == doctest::Approx(0.25f));
	CHECK(loaded.Size == doctest::Approx(0.75f));
	CHECK(loaded.RandomRotation);
	CHECK(loaded.UseGravity);
	CHECK(loaded.Gravity.z == doctest::Approx(3.0f));
	CHECK(loaded.Drag == doctest::Approx(0.4f));
	CHECK(loaded.EmitOverTime == doctest::Approx(42.5f));
	CHECK(loaded.SimulationSpace == ParticleSpace3D::Local);
	CHECK(loaded.Shape == ParticleShape3D::Box);
	CHECK(loaded.BoxSize.y == doctest::Approx(3.0f));
	CHECK(loaded.ScaleOverLifetimeEnabled);
	CHECK(loaded.ColorOverLifetimeEnabled);
	REQUIRE(loaded.ColorOverLifetime.AlphaKeys.size() == 2);
	CHECK(loaded.ColorOverLifetime.AlphaKeys[1].Alpha == doctest::Approx(0.0f));
	CHECK(loaded.Texture == ps.Texture);
	CHECK(loaded.Color.b == doctest::Approx(0.3f));
	CHECK(loaded.BlendMode == ParticleBlendMode3D::Additive);
	CHECK(loaded.MaxParticles == 321);
	REQUIRE(loaded.GetBursts().size() == 1);
	CHECK(loaded.GetBursts()[0].Count == 7);
	CHECK(loaded.GetBursts()[0].Interval == doctest::Approx(0.5f));

	// Reset Component deserializes {}: every field returns to its default.
	info->deserialize(target, Json::Parse("{}"));
	const ParticleSystem3DComponent defaults;
	const auto& reset = target.GetComponent<ParticleSystem3DComponent>();
	CHECK(reset.PlayOnAwake == defaults.PlayOnAwake);
	CHECK(reset.Shape == defaults.Shape);
	CHECK(reset.Texture == 0);
	CHECK(reset.MaxParticles == defaults.MaxParticles);
	CHECK(reset.GetBursts().empty());
	CHECK(reset.ColorOverLifetime.AlphaKeys.size() == defaults.ColorOverLifetime.AlphaKeys.size());

	// Out-of-range enums fall back instead of producing invalid values.
	info->deserialize(target, Json::Parse(R"({"shape":99,"blendMode":-1})"));
	CHECK(target.GetComponent<ParticleSystem3DComponent>().Shape == defaults.Shape);
	CHECK(target.GetComponent<ParticleSystem3DComponent>().BlendMode == defaults.BlendMode);
}

TEST_CASE("ParticleSystem3D depends on Transform3D and conflicts with ParticleSystem2D") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);
	const ComponentInfo* info = registry.FindBySerializedName("ParticleSystem3D");
	REQUIRE(info);
	const ComponentInfo* transform = registry.FindBySerializedName("Transform3D");
	const ComponentInfo* particles2D = registry.FindBySerializedName("ParticleSystem2D");
	REQUIRE(transform);
	REQUIRE(particles2D);
	auto contains = [](const auto& list, const auto& id) { return std::find(list.begin(), list.end(), id) != list.end(); };
	CHECK(contains(info->dependsOn, transform->typeId));
	CHECK(contains(info->conflictsWith, particles2D->typeId));
}

TEST_CASE("ParticleSystem3D Emit binding uses the emitter pose a script just set, including parents") {
	Application app;
	auto scene = Scene::CreateDetachedScene("Emit Binding");
	Entity parent = scene->CreateEntity("Parent");
	parent.AddComponent<Transform3DComponent>().Position = { 0.0f, 5.0f, 0.0f };
	Entity emitter = scene->CreateEntity("Emitter");
	emitter.AddComponent<Transform3DComponent>();
	emitter.SetParent(parent);
	auto& ps = emitter.AddComponent<ParticleSystem3DComponent>();
	ps.Shape = ParticleShape3D::Box;
	ps.BoxSize = glm::vec3(0.0f);
	ps.Speed = 0.0f;
	ps.SimulationSpace = ParticleSpace3D::World;
	ps.Simulate(0.0f, k_Identity);
	const uint64_t emitterId = scene->GetEntityPersistentID(emitter.GetHandle());
	ScriptEngine::SetScene(scene.get());

	NativeBindings b{};
	ScriptBindings::PopulateNativeBindings(b);
	REQUIRE(b.ParticleSystem3D_Emit != nullptr);
	// Moved after the last Simulate, like a script does right before Emit; WorldMatrix is still stale here.
	emitter.GetComponent<Transform3DComponent>().Position = { 3.0f, 0.0f, 0.0f };
	b.ParticleSystem3D_Emit(emitterId, 1);
	ScriptEngine::SetScene(nullptr);

	const auto& emitted = emitter.GetComponent<ParticleSystem3DComponent>();
	REQUIRE(emitted.GetParticleCount() == 1);
	CHECK(emitted.GetParticles()[0].Position.x == doctest::Approx(3.0f));
	CHECK(emitted.GetParticles()[0].Position.y == doctest::Approx(5.0f));
}
