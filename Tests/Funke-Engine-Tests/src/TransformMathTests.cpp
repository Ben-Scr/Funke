#include <doctest/doctest.h>

#include "Collections/Mat2.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Scene/Scene.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <vector>

using namespace Funke;

TEST_CASE("Moving Transform3D preserves rotation and signed scale in both matrix accessors") {
	Transform3DComponent transform;
	transform.Rotation = { 31.0f, -57.0f, 13.0f };
	transform.Scale = { -2.0f, 0.0f, 3.5f };
	for (int step = 0; step < 8; ++step) {
		transform.Position = { float(step), float(-step * 2), 0.125f * step };
		if (step == 3) transform.Rotation.y = 82.0f;
		if (step == 5) transform.Scale = { 0.0f, 4.0f, -1.5f };
		const auto expected = glm::translate(glm::mat4(1), transform.Position)
			* glm::mat4_cast(transform.GetLocalRotationQuat())
			* glm::scale(glm::mat4(1), transform.Scale);
		const auto uncached = transform.GetLocalMatrix();
		const auto cached = transform.GetCachedLocalMatrix();
		for (int col = 0; col < 4; ++col) for (int row = 0; row < 4; ++row) {
			CHECK(uncached[col][row] == doctest::Approx(expected[col][row]));
			CHECK(cached[col][row] == doctest::Approx(expected[col][row]));
		}
	}
}

TEST_CASE("Transform2D model matrix preserves mirrored and degenerate axes") {
	for (const Vec2 scale : { Vec2{-2, 3}, Vec2{0, -4}, Vec2{1, 1} }) {
		Transform2DComponent transform({ 12, -7 }, scale, 0.73f);
		const auto matrix = transform.GetModelMatrix();
		for (const Vec2 point : { Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}, Vec2{-2, 5} }) {
			const auto expected = transform.TransformPoint(point);
			const auto actual = matrix * glm::vec3(point.x, point.y, 1);
			CHECK(actual.x == doctest::Approx(expected.x));
			CHECK(actual.y == doctest::Approx(expected.y));
			CHECK(actual.z == 1);
		}
	}
}

// Uses the engine DLL and the same source for both versions; excludes rendering and C# interop.
TEST_CASE("Engine movement benchmark" * doctest::skip()) {
	const char* path = std::getenv("FUNKE_MOVEMENT_BENCH_OUTPUT");
	REQUIRE(path != nullptr);
	std::ofstream output(path);
	REQUIRE(output.good());
	output << std::setprecision(17) << "case,count,sample,ms\n";
	auto measure = [&](const char* name, int count, auto&& run, auto&& validate) {
		for (int iteration = -5; iteration < 21; ++iteration) {
			const auto begin = std::chrono::steady_clock::now();
			run(iteration + 6);
			const auto end = std::chrono::steady_clock::now();
			validate(iteration + 6);
			if (iteration >= 0) output << name << ',' << count << ',' << iteration << ','
				<< std::chrono::duration<double, std::milli>(end - begin).count() << '\n';
		}
	};
	for (int count : { 1000, 100000 }) {
		std::vector<Transform3DComponent> transforms(count);
		for (auto& transform : transforms) {
			transform.Rotation = { 31, -57, 13 };
			transform.Scale = { -2, 0.5f, 3 };
			transform.GetCachedLocalMatrix();
		}
		float checksum = 0;
		measure("3d-local-translation", count, [&](int tick) {
			checksum = 0;
			for (auto& transform : transforms) {
				transform.Position.x = float(tick);
				checksum += transform.GetCachedLocalMatrix()[3].x;
			}
		}, [&](int tick) { REQUIRE(checksum == doctest::Approx(float(count * tick))); });
		std::vector<Transform2DComponent> transforms2D(count);
		for (auto& transform : transforms2D) { transform.Rotation = 0.73f; transform.Scale = { -2, 3 }; }
		measure("2d-model-matrix", count, [&](int tick) {
			checksum = 0;
			for (auto& transform : transforms2D) {
				transform.Position.x = float(tick);
				checksum += transform.GetModelMatrix()[2].x;
			}
		}, [&](int tick) { REQUIRE(checksum == doctest::Approx(float(count * tick))); });
	}
	for (int dimension : { 2, 3 }) for (int count : { 1000, 10000 }) {
		auto scene = Scene::CreateDetachedScene("Movement benchmark");
		std::vector<Entity> entities;
		for (int i = 0; i < count; ++i) {
			auto entity = scene->CreateEmptyEntity();
			if (dimension == 2) entity.AddComponent<Transform2DComponent>();
			else entity.AddComponent<Transform3DComponent>();
			entities.push_back(entity);
		}
		auto propagate = [&] {
			if (dimension == 2) TransformHierarchySystem::Propagate(*scene);
			else Transform3DHierarchySystem::Propagate(*scene);
		};
		propagate();
		for (int percent : { 1, 100, -100 }) {
			if (percent < 0 && dimension != 2) continue;
			const int changed = count * std::abs(percent) / 100;
			const std::string name = percent < 0 ? "2d-flat-full-traversal" :
				std::to_string(dimension) + "d-flat-move-" + std::to_string(percent) + "pct";
			measure(name.c_str(), count, [&](int tick) {
				for (int i = 0; i < changed; ++i) {
					if (percent < 0) entities[i].GetComponent<Transform2DComponent>().LocalPosition = { float(tick), 0 };
					else if (dimension == 2) entities[i].GetComponent<Transform2DComponent>().SetPosition({ float(tick), 0 });
					else entities[i].GetComponent<Transform3DComponent>().Position.x = float(tick);
				}
				if (percent < 0) scene->MarkTransformDirty(entt::null);
				propagate();
			}, [&](int tick) {
				const float x = dimension == 2 ? entities[changed - 1].GetComponent<Transform2DComponent>().Position.x
					: entities[changed - 1].GetComponent<Transform3DComponent>().GetWorldPosition().x;
				REQUIRE(x == float(tick));
			});
		}
	}
}

TEST_CASE("Transform rotation helpers share the same handedness")
{
	const float radians = Radians(35.0f);
	const Vec2 localPoint{ -5.0f, 0.0f };

	Transform2DComponent parent;
	parent.Position = { 0.0f, 0.0f };
	parent.Scale = { 1.0f, 1.0f };
	parent.Rotation = radians;

	const Vec2 transformed = parent.TransformPoint(localPoint);
	const Vec2 rotated = Rotation(radians) * localPoint;

	const float expectedX = -5.0f * Cos(radians);
	const float expectedY = -5.0f * Sin(radians);

	CHECK(transformed.x == doctest::Approx(expectedX));
	CHECK(transformed.y == doctest::Approx(expectedY));
	CHECK(rotated.x == doctest::Approx(transformed.x));
	CHECK(rotated.y == doctest::Approx(transformed.y));
}

TEST_CASE("TransformPoint composes scale, then rotation, then translation")
{
	Transform2DComponent t;
	t.Position = { 10.0f, -4.0f };
	t.Scale = { 2.0f, 3.0f };
	t.Rotation = Radians(90.0f);

	// local (1,0) --S--> (2,0) --R90--> (0,2) --T--> (10,-2)
	const Vec2 p = t.TransformPoint({ 1.0f, 0.0f });
	CHECK(p.x == doctest::Approx(10.0f));
	CHECK(p.y == doctest::Approx(-2.0f));
}

TEST_CASE("TransformVector applies scale and rotation but not translation")
{
	Transform2DComponent t;
	t.Position = { 100.0f, 50.0f };
	t.Scale = { 2.0f, 2.0f };
	t.Rotation = Radians(30.0f);

	const Vec2 local{ 3.0f, -1.0f };
	const Vec2 asVector = t.TransformVector(local);
	const Vec2 asPoint = t.TransformPoint(local);

	// The only difference between the two is the translation, which equals the
	// image of the origin (= Position).
	CHECK(asPoint.x - asVector.x == doctest::Approx(t.Position.x));
	CHECK(asPoint.y - asVector.y == doctest::Approx(t.Position.y));
}

TEST_CASE("Identity transform leaves points and vectors unchanged")
{
	Transform2DComponent t; // defaults: pos 0, scale 1, rotation 0
	const Vec2 v{ 7.0f, -2.5f };

	const Vec2 p = t.TransformPoint(v);
	const Vec2 d = t.TransformVector(v);
	CHECK(p.x == doctest::Approx(v.x));
	CHECK(p.y == doctest::Approx(v.y));
	CHECK(d.x == doctest::Approx(v.x));
	CHECK(d.y == doctest::Approx(v.y));
}

TEST_CASE("GetModelMatrix agrees with TransformPoint")
{
	// Guards against the two code paths drifting apart.
	Transform2DComponent t;
	t.Position = { -3.0f, 8.0f };
	t.Scale = { 1.5f, 0.5f };
	t.Rotation = Radians(40.0f);

	const Vec2 local{ 2.0f, -4.0f };
	const Vec2 viaMethod = t.TransformPoint(local);
	const glm::vec3 viaMatrix = t.GetModelMatrix() * glm::vec3(local.x, local.y, 1.0f);

	CHECK(viaMatrix.x == doctest::Approx(viaMethod.x));
	CHECK(viaMatrix.y == doctest::Approx(viaMethod.y));
}

TEST_CASE("FromPosition and FromScale set world + local fields and leave the rest default")
{
	const Transform2DComponent p = Transform2DComponent::FromPosition({ 5.0f, 6.0f });
	CHECK(p.Position.x == doctest::Approx(5.0f));
	CHECK(p.Position.y == doctest::Approx(6.0f));
	CHECK(p.LocalPosition.x == doctest::Approx(5.0f));
	CHECK(p.Scale.x == doctest::Approx(1.0f));   // untouched default
	CHECK(p.Rotation == doctest::Approx(0.0f));

	const Transform2DComponent s = Transform2DComponent::FromScale({ 2.0f, 4.0f });
	CHECK(s.Scale.x == doctest::Approx(2.0f));
	CHECK(s.Scale.y == doctest::Approx(4.0f));
	CHECK(s.LocalScale.y == doctest::Approx(4.0f));
	CHECK(s.Position.x == doctest::Approx(0.0f)); // untouched default
}

TEST_CASE("GetRotationDegrees round-trips radians")
{
	Transform2DComponent t;
	t.Rotation = Radians(135.0f);
	CHECK(t.GetRotationDegrees() == doctest::Approx(135.0f));
}

TEST_CASE("Transform direction axes follow world rotation")
{
	Transform2DComponent t;
	CHECK(t.Up().x == doctest::Approx(0.0f));
	CHECK(t.Up().y == doctest::Approx(1.0f));
	CHECK(t.Right().x == doctest::Approx(1.0f));
	CHECK(t.Right().y == doctest::Approx(0.0f));
	CHECK(t.GetForwardDirection().x == doctest::Approx(0.0f));
	CHECK(t.GetForwardDirection().y == doctest::Approx(1.0f));

	t.Rotation = Radians(90.0f);
	CHECK(t.Up().x == doctest::Approx(-1.0f));
	CHECK(t.Up().y == doctest::Approx(0.0f).epsilon(0.000001f));
	CHECK(t.Right().x == doctest::Approx(0.0f).epsilon(0.000001f));
	CHECK(t.Right().y == doctest::Approx(1.0f));
	CHECK(t.GetForwardDirection().x == doctest::Approx(-1.0f));
	CHECK(t.GetForwardDirection().y == doctest::Approx(0.0f));
}

TEST_CASE("Transform equality compares world TRS and ignores local/dirty state")
{
	Transform2DComponent a;
	a.Position = { 1.0f, 2.0f };
	a.Rotation = 0.5f;
	a.Scale = { 2.0f, 2.0f };

	Transform2DComponent b = a;
	b.LocalPosition = { 99.0f, 99.0f }; // Local* and the dirty flag are not part
	b.LocalRotation = 9.0f;             // of equality...
	b.ClearDirty();
	CHECK(a == b);

	b.Position.x += 1.0f;               // ...but world Position is.
	CHECK(a != b);
}
