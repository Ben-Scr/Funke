#include <doctest/doctest.h>

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Physics/PhysicsSystem2D.hpp"

#include <algorithm>
#include <cmath>

#include <box2d/box2d.h>
#include <vector>

using namespace Funke;

namespace {
	double Area(const std::vector<Vec2>& p) {
		double a = 0.0;
		for (size_t i = 0; i < p.size(); ++i) {
			const Vec2& u = p[i];
			const Vec2& v = p[(i + 1) % p.size()];
			a += static_cast<double>(u.x) * v.y - static_cast<double>(v.x) * u.y;
		}
		return a * 0.5;
	}

	bool IsConvexCcw(const std::vector<Vec2>& p) {
		for (size_t i = 0; i < p.size(); ++i) {
			const Vec2& a = p[(i + p.size() - 1) % p.size()];
			const Vec2& b = p[i];
			const Vec2& c = p[(i + 1) % p.size()];
			const double cross = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y)
				- (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
			if (cross < -1e-6) return false;
		}
		return true;
	}
}

TEST_CASE("PolygonCollider2D splits a concave outline into convex pieces of at most 8 vertices")
{
	// A clockwise "U" with 12 points: concave and longer than one Box2D polygon.
	const std::vector<Vec2> outline = {
		{ 0, 0 }, { 0, 3 }, { 1, 3 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 }, { 4, 3 }, { 5, 3 }, { 5, 0 }, { 3, 0 }, { 2, 0 } };
	const auto pieces = PolygonCollider2DComponent::DecomposePath(outline);
	REQUIRE(pieces.size() >= 3);

	double total = 0.0;
	for (const auto& piece : pieces) {
		CHECK(piece.size() >= 3);
		CHECK(piece.size() <= static_cast<size_t>(PolygonCollider2DComponent::k_MaxVertices));
		CHECK(IsConvexCcw(piece));
		total += Area(piece);
	}
	CHECK(total == doctest::Approx(std::abs(Area(outline))));
}

TEST_CASE("PolygonCollider2D keeps the convex-hull behavior for small convex or unordered point sets")
{
	const std::vector<Vec2> square = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
	REQUIRE(PolygonCollider2DComponent::DecomposePath(square).size() == 1);

	// Same corners in a self-crossing order: still one hull, not a rejected path.
	const std::vector<Vec2> unordered = { { -1, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 } };
	REQUIRE(PolygonCollider2DComponent::DecomposePath(unordered).size() == 1);

	// A convex 12-gon exceeds one polygon and is split.
	std::vector<Vec2> circle;
	for (int i = 0; i < 12; ++i) {
		const float a = static_cast<float>(i) * 6.2831853f / 12.0f;
		circle.push_back({ std::cos(a), std::sin(a) });
	}
	const auto pieces = PolygonCollider2DComponent::DecomposePath(circle);
	CHECK(pieces.size() >= 2);
	double total = 0.0;
	for (const auto& piece : pieces) total += Area(piece);
	CHECK(total == doctest::Approx(Area(circle)));
}

TEST_CASE("PolygonCollider2D paths and collider density survive both serialize ladders")
{
	auto scene = Scene::CreateDetachedScene("Polygon Paths RoundTrip");
	Entity e = scene->CreateEntity("Chassis");
	auto& poly = e.AddComponent<PolygonCollider2DComponent>();
	const std::vector<std::vector<Vec2>> paths = {
		{ { 0, 0 }, { 2, 0 }, { 2, 1 }, { 0, 1 } },
		{ { 0, 1 }, { 1, 1 }, { 1, 2 }, { 0.5f, 2.5f }, { 0, 2 } } };
	poly.SetPaths(paths, *scene);
	poly.SetDensity(2.5f);
	e.AddComponent<BoxCollider2DComponent>();
	REQUIRE(poly.GetPathCount() == 2);

	const Json::Value value = SceneSerializer::SerializeEntityFull(*scene, e.GetHandle());
	const EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(*scene, value);
	REQUIRE(clone != entt::null);
	const auto& restored = scene->GetComponent<PolygonCollider2DComponent>(clone);
	REQUIRE(restored.GetPathCount() == 2);
	CHECK(restored.GetPath(1).size() == paths[1].size());
	CHECK(restored.GetPath(1)[3].y == doctest::Approx(2.5f));
	CHECK(restored.GetDensity() == doctest::Approx(2.5f));

	const Json::Value comp = SceneSerializer::SerializeComponent(*scene, e.GetHandle(), "PolygonCollider2D");
	Entity dst = scene->CreateEntity("Dst");
	dst.AddComponent<PolygonCollider2DComponent>();
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, dst.GetHandle(), "PolygonCollider2D", comp));
	CHECK(dst.GetComponent<PolygonCollider2DComponent>().GetPathCount() == 2);
	CHECK(dst.GetComponent<PolygonCollider2DComponent>().GetDensity() == doctest::Approx(2.5f));
}

TEST_CASE("2D joints round-trip their settings and connected body")
{
	auto scene = Scene::CreateDetachedScene("Joint RoundTrip");
	Entity chassis = scene->CreateEntity("Chassis");
	chassis.AddComponent<Rigidbody2DComponent>();
	Entity axle = scene->CreateEntity("Axle");
	axle.AddComponent<Rigidbody2DComponent>();

	auto& slider = axle.AddComponent<PrismaticJoint2DComponent>();
	slider.ConnectedBody = chassis.GetHandle();
	slider.Axis = { 0.0f, 1.0f };
	slider.Anchor = { 0.25f, -0.5f };
	slider.EnableLimit = true;
	slider.LowerTranslation = -0.2f;
	slider.EnableMotor = true;
	slider.MotorSpeed = 1.875f;
	slider.MaxMotorForce = 15.0f;

	auto& hinge = chassis.AddComponent<RevoluteJoint2DComponent>();
	hinge.EnableLimit = true;
	hinge.LowerAngle = -0.5f;
	hinge.UpperAngle = 0.75f;
	hinge.AutoConfigureConnectedAnchor = false;
	hinge.ConnectedAnchor = { 3.0f, 4.0f };

	Entity sensor = scene->CreateEntity("Sensor");
	sensor.AddComponent<WeldJoint2DComponent>().AngularHertz = 4.0f;

	// Round-trip both entities together so the connected-body reference resolves within the batch.
	const Json::Value axleValue = SceneSerializer::SerializeComponent(*scene, axle.GetHandle(), "PrismaticJoint2D");
	REQUIRE(axleValue.IsObject());
	Entity axle2 = scene->CreateEntity("Axle2");
	axle2.AddComponent<PrismaticJoint2DComponent>();
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, axle2.GetHandle(), "PrismaticJoint2D", axleValue));
	const auto& s = axle2.GetComponent<PrismaticJoint2DComponent>();
	CHECK(s.ConnectedBody == chassis.GetHandle());
	CHECK(s.Axis.y == doctest::Approx(1.0f));
	CHECK(s.Anchor.x == doctest::Approx(0.25f));
	CHECK(s.EnableLimit);
	CHECK(s.LowerTranslation == doctest::Approx(-0.2f));
	CHECK(s.EnableMotor);
	CHECK(s.MotorSpeed == doctest::Approx(1.875f));
	CHECK(s.MaxMotorForce == doctest::Approx(15.0f));

	const Json::Value chassisValue = SceneSerializer::SerializeEntityFull(*scene, chassis.GetHandle());
	const EntityHandle chassisClone = SceneSerializer::DeserializeEntityFromValue(*scene, chassisValue);
	REQUIRE(chassisClone != entt::null);
	const auto& h = scene->GetComponent<RevoluteJoint2DComponent>(chassisClone);
	CHECK(h.ConnectedBody == entt::null);
	CHECK(h.EnableLimit);
	CHECK(h.LowerAngle == doctest::Approx(-0.5f));
	CHECK(h.UpperAngle == doctest::Approx(0.75f));
	CHECK_FALSE(h.AutoConfigureConnectedAnchor);
	CHECK(h.ConnectedAnchor.y == doctest::Approx(4.0f));
	// Detached scenes never build Box2D joints.
	CHECK_FALSE(h.IsCreated());

	const Json::Value weldValue = SceneSerializer::SerializeComponent(*scene, sensor.GetHandle(), "WeldJoint2D");
	REQUIRE(weldValue.IsObject());
	CHECK(weldValue.FindMember("angularHertz")->AsDoubleOr(0.0) == doctest::Approx(4.0));
}

TEST_CASE("Physics bodies under a moved parent compose their world pose from the whole parent chain")
{
	// Scene load: every transform holds only its local pose (world = local) and is dirty until the systems run.
	auto scene = Scene::CreateDetachedScene("Compose Chain");
	Entity root = scene->CreateEntity("Root");
	Entity middle = scene->CreateEntity("Middle");
	Entity body = scene->CreateEntity("Body");
	middle.SetParent(root);
	body.SetParent(middle);

	auto place = [](Entity e, Vec2 local, float rotation) {
		auto& tf = e.GetComponent<Transform2DComponent>();
		tf.LocalPosition = local;
		tf.LocalRotation = rotation;
		tf.LocalScale = { 1.0f, 1.0f };
		tf.Position = local;
		tf.Rotation = rotation;
		tf.Scale = tf.LocalScale;
		tf.MarkDirty();
	};
	place(root, { 10.0f, 2.0f }, 0.0f);
	place(middle, { 0.0f, 1.0f }, 1.5707963f);
	place(body, { 2.0f, 0.0f }, 0.0f);

	auto& tf = body.GetComponent<Transform2DComponent>();
	PhysicsSystem2D::ComposeWorldFromLocalChain(scene->GetRegistry(), body.GetHandle(), tf);
	CHECK(tf.Position.x == doctest::Approx(10.0f));
	CHECK(tf.Position.y == doctest::Approx(5.0f));
	CHECK(tf.Rotation == doctest::Approx(1.5707963f));
}

TEST_CASE("PolygonCollider2D edge mode keeps open two-point lines through both serialize ladders")
{
	auto scene = Scene::CreateDetachedScene("Edge RoundTrip");
	Entity e = scene->CreateEntity("Ground");
	auto& poly = e.AddComponent<PolygonCollider2DComponent>();
	poly.SetEdge(true, *scene);
	poly.SetPaths({ { { -5, 0 }, { 5, 1 } }, { { 5, 1 }, { 8, 1 }, { 12, -2 } } }, *scene);
	REQUIRE(poly.GetPathCount() == 2);
	REQUIRE(poly.GetPath(0).size() == 2);

	const Json::Value value = SceneSerializer::SerializeEntityFull(*scene, e.GetHandle());
	const EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(*scene, value);
	REQUIRE(clone != entt::null);
	const auto& restored = scene->GetComponent<PolygonCollider2DComponent>(clone);
	CHECK(restored.IsEdge());
	REQUIRE(restored.GetPathCount() == 2);
	CHECK(restored.GetPath(0).size() == 2);
	CHECK(restored.GetPath(1)[2].y == doctest::Approx(-2.0f));
	// Outline pieces are the segments, for drawing without a live body.
	CHECK(restored.ComputeLocalPieces({ 1.0f, 1.0f }).size() == 3);

	const Json::Value comp = SceneSerializer::SerializeComponent(*scene, e.GetHandle(), "PolygonCollider2D");
	Entity dst = scene->CreateEntity("Dst");
	dst.AddComponent<PolygonCollider2DComponent>();
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, dst.GetHandle(), "PolygonCollider2D", comp));
	CHECK(dst.GetComponent<PolygonCollider2DComponent>().IsEdge());
	CHECK(dst.GetComponent<PolygonCollider2DComponent>().GetPath(0).size() == 2);
}

TEST_CASE("A reversed open chain with straight ghost points holds a body on top of a left-to-right line")
{
	// The point order PolygonCollider2D uses in edge mode (see RebuildEdges).
	b2WorldDef worldDef = b2DefaultWorldDef();
	const b2WorldId world = b2CreateWorld(&worldDef);
	b2BodyDef groundDef = b2DefaultBodyDef();
	const b2BodyId ground = b2CreateBody(world, &groundDef);
	std::vector<Vec2> line = { { -10, 0 }, { 0, 0.5f }, { 10, 0 } };
	std::reverse(line.begin(), line.end());
	std::vector<b2Vec2> points;
	const Vec2 first = line.front() * 2.0f - line[1];
	const Vec2 last = line.back() * 2.0f - line[line.size() - 2];
	points.push_back({ first.x, first.y });
	for (const Vec2& p : line) points.push_back({ p.x, p.y });
	points.push_back({ last.x, last.y });
	b2ChainDef chainDef = b2DefaultChainDef();
	chainDef.points = points.data();
	chainDef.count = static_cast<int>(points.size());
	const b2ChainId chain = b2CreateChain(ground, &chainDef);
	CHECK(b2Chain_GetSegmentCount(chain) == 2);   // one per edge of the line; the ghost points only smooth the ends

	for (float x : { -8.0f, -0.2f, 6.0f }) {
		CAPTURE(x);
		b2BodyDef ballDef = b2DefaultBodyDef();
		ballDef.type = b2_dynamicBody;
		ballDef.position = { x, 3.0f };
		const b2BodyId ball = b2CreateBody(world, &ballDef);
		b2ShapeDef ballShape = b2DefaultShapeDef();
		const b2Circle circle{ { 0.0f, 0.0f }, 0.25f };
		b2CreateCircleShape(ball, &ballShape, &circle);
		for (int i = 0; i < 240; ++i) b2World_Step(world, 1.0f / 120.0f, 4);
		CHECK(b2Body_GetPosition(ball).y > 0.0f);
		b2DestroyBody(ball);
	}
	b2DestroyWorld(world);
}
