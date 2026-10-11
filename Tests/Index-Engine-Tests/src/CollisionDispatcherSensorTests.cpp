#include <doctest/doctest.h>

#include "Physics/CollisionDispatcher.hpp"

#include <box2d/box2d.h>

#include <vector>

using namespace Index;

namespace {
	struct SensorWorld {
		b2WorldId World = b2_nullWorldId;
		b2BodyId SensorBody = b2_nullBodyId;
		b2BodyId VisitorBody = b2_nullBodyId;
		b2ShapeId SensorShape = b2_nullShapeId;
		b2ShapeId VisitorShape = b2_nullShapeId;

		explicit SensorWorld(b2BodyType sensorBodyType) {
			b2WorldDef worldDef = b2DefaultWorldDef();
			worldDef.gravity = { 0.0f, 0.0f };
			World = b2CreateWorld(&worldDef);

			b2BodyDef sensorDef = b2DefaultBodyDef();
			sensorDef.type = sensorBodyType;
			SensorBody = b2CreateBody(World, &sensorDef);

			// Mirrors Box2DWorld::CreateShape: every shape opts into sensor events.
			b2ShapeDef sensorShapeDef = b2DefaultShapeDef();
			sensorShapeDef.isSensor = true;
			sensorShapeDef.enableSensorEvents = true;
			const b2Polygon box = b2MakeBox(1.0f, 1.0f);
			SensorShape = b2CreatePolygonShape(SensorBody, &sensorShapeDef, &box);

			b2BodyDef visitorDef = b2DefaultBodyDef();
			visitorDef.type = b2_dynamicBody;
			visitorDef.position = { 10.0f, 0.0f };
			VisitorBody = b2CreateBody(World, &visitorDef);

			b2ShapeDef visitorShapeDef = b2DefaultShapeDef();
			visitorShapeDef.enableSensorEvents = true;
			const b2Polygon small = b2MakeBox(0.25f, 0.25f);
			VisitorShape = b2CreatePolygonShape(VisitorBody, &visitorShapeDef, &small);
		}

		~SensorWorld() { b2DestroyWorld(World); }

		void MoveVisitor(float x) {
			b2Body_SetTransform(VisitorBody, { x, 0.0f }, b2MakeRot(0.0f));
		}

		void Step() { b2World_Step(World, 1.0f / 60.0f, 4); }
	};

	struct Recorded {
		std::vector<Collision2D> Begin;
		std::vector<Collision2D> Stay;
		std::vector<Collision2D> End;
	};

	void Process(CollisionDispatcher& dispatcher, SensorWorld& world, Recorded& out) {
		dispatcher.Process(
			world.World,
			[](b2ShapeId) { return CollisionBodyRef{}; },
			[&](const Collision2D& c) { out.Begin.push_back(c); },
			[&](const Collision2D& c) { out.End.push_back(c); },
			[&](const Collision2D& c) { out.Stay.push_back(c); });
	}
}

TEST_CASE("Sensor overlaps dispatch begin, stay and end as triggers") {
	for (b2BodyType sensorType : { b2_staticBody, b2_kinematicBody, b2_dynamicBody }) {
		CAPTURE(static_cast<int>(sensorType));
		SensorWorld world(sensorType);
		CollisionDispatcher dispatcher;
		Recorded events;

		world.Step();
		Process(dispatcher, world, events);
		CHECK(events.Begin.empty());

		world.MoveVisitor(0.0f);
		world.Step();
		Process(dispatcher, world, events);
		REQUIRE(events.Begin.size() == 1);
		CHECK(events.Begin[0].isTrigger);
		CHECK(events.Stay.size() == 1);

		world.Step();
		Process(dispatcher, world, events);
		CHECK(events.Begin.size() == 1);
		CHECK(events.Stay.size() == 2);
		CHECK(events.Stay.back().isTrigger);

		world.MoveVisitor(10.0f);
		world.Step();
		Process(dispatcher, world, events);
		REQUIRE(events.End.size() == 1);
		CHECK(events.End[0].isTrigger);

		world.Step();
		Process(dispatcher, world, events);
		CHECK(events.Stay.size() == 2);
	}
}

TEST_CASE("Unregistering a shape drops its active sensor overlap") {
	SensorWorld world(b2_staticBody);
	CollisionDispatcher dispatcher;
	Recorded events;

	world.MoveVisitor(0.0f);
	world.Step();
	Process(dispatcher, world, events);
	REQUIRE(events.Begin.size() == 1);

	dispatcher.UnregisterShape(world.VisitorShape);
	b2DestroyBody(world.VisitorBody);
	world.Step();
	const size_t stayBefore = events.Stay.size();
	Process(dispatcher, world, events);

	CHECK(events.End.empty());
	CHECK(events.Stay.size() == stayBefore);
}

TEST_CASE("Solid contacts are not flagged as triggers") {
	b2WorldDef worldDef = b2DefaultWorldDef();
	worldDef.gravity = { 0.0f, 0.0f };
	b2WorldId world = b2CreateWorld(&worldDef);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.enableSensorEvents = true;
	shapeDef.enableContactEvents = true;
	const b2Polygon box = b2MakeBox(1.0f, 1.0f);

	b2BodyDef groundDef = b2DefaultBodyDef();
	b2CreatePolygonShape(b2CreateBody(world, &groundDef), &shapeDef, &box);

	b2BodyDef dynamicDef = b2DefaultBodyDef();
	dynamicDef.type = b2_dynamicBody;
	dynamicDef.position = { 0.0f, 1.5f };
	b2CreatePolygonShape(b2CreateBody(world, &dynamicDef), &shapeDef, &box);

	b2World_Step(world, 1.0f / 60.0f, 4);

	CollisionDispatcher dispatcher;
	std::vector<Collision2D> begins;
	dispatcher.Process(world, [](b2ShapeId) { return CollisionBodyRef{}; },
		[&](const Collision2D& c) { begins.push_back(c); });

	REQUIRE(begins.size() == 1);
	CHECK_FALSE(begins[0].isTrigger);

	b2DestroyWorld(world);
}
