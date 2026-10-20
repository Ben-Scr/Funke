#include <doctest/doctest.h>

#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/TerrainRenderer2DComponent.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

using namespace Funke;

TEST_CASE("TerrainRenderer2D round-trips its surface and textures")
{
	auto scene = Scene::CreateDetachedScene("Terrain RoundTrip");
	Entity e = scene->CreateEntity("Hills");
	auto& terrain = e.AddComponent<TerrainRenderer2DComponent>();
	terrain.Points = { { 0.0f, 0.0f }, { 3.0f, 1.5f }, { 7.5f, -0.25f } };
	terrain.FillTexture = 1234567890123ull;
	terrain.EdgeTexture = 42ull;
	terrain.FillMapping = TerrainFillMapping::Draped;
	terrain.FillTileSize = { 16.0f, 8.0f };
	terrain.FillDepth = 30.0f;
	terrain.EdgeThickness = 1.0f;
	terrain.EdgeOffset = 0.25f;
	terrain.EdgeTileLength = 16.0f;
	terrain.EdgeColor = Color{ 0.5f, 0.25f, 1.0f, 0.75f };
	terrain.SortingLayer = 2;
	terrain.SortingOrder = -3;
	terrain.EdgeSortingOrder = 4;

	const Json::Value value = SceneSerializer::SerializeEntityFull(*scene, e.GetHandle());
	const EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(*scene, value);
	REQUIRE(clone != entt::null);
	const auto& t = scene->GetComponent<TerrainRenderer2DComponent>(clone);
	REQUIRE(t.Points.size() == 3);
	CHECK(t.Points[2].x == doctest::Approx(7.5f));
	CHECK(t.Points[2].y == doctest::Approx(-0.25f));
	CHECK(t.FillTexture == 1234567890123ull);
	CHECK(t.EdgeTexture == 42ull);
	CHECK(t.FillMapping == TerrainFillMapping::Draped);
	CHECK(t.FillTileSize.x == doctest::Approx(16.0f));
	CHECK(t.FillDepth == doctest::Approx(30.0f));
	CHECK(t.EdgeOffset == doctest::Approx(0.25f));
	CHECK(t.EdgeColor.a == doctest::Approx(0.75f));
	CHECK(t.SortingLayer == 2);
	CHECK(t.SortingOrder == -3);
	CHECK(t.EdgeSortingOrder == 4);
}
