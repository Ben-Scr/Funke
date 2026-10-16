#include <doctest/doctest.h>

#include "Assets/AssetRegistry.hpp"
#include "Assets/TextureMeta.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Core/Application.hpp"
#include "Gui/NineSlice.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <array>
#include <filesystem>
#include <vector>

using namespace Funke;

namespace {
	const SpriteUVRect k_Full{ 0.0f, 0.0f, 1.0f, 1.0f, true };
	const NineSliceBorder k_Border24{ 24.0f, 24.0f, 24.0f, 24.0f };
}

TEST_CASE("9-slice keeps 24 px corners of a 64x64 sprite on wide and tall rects") {
	std::array<NineSliceQuad, 9> quads;
	for (const Vec2 rect : { Vec2(500.0f, 80.0f), Vec2(80.0f, 500.0f) }) {
		CAPTURE(rect.x);
		REQUIRE(BuildNineSlice(rect, { 64.0f, 64.0f }, k_Border24, 1.0f, k_Full, quads) == 9);
		// Row-major from the top-left corner.
		for (const int corner : { 0, 2, 6, 8 }) {
			CHECK(quads[corner].Size.x == doctest::Approx(24.0f));
			CHECK(quads[corner].Size.y == doctest::Approx(24.0f));
		}
		CHECK(quads[4].Size.x == doctest::Approx(rect.x - 48.0f));
		CHECK(quads[4].Size.y == doctest::Approx(rect.y - 48.0f));
		CHECK(quads[0].Center.x == doctest::Approx(-rect.x * 0.5f + 12.0f));
		CHECK(quads[0].Center.y == doctest::Approx(rect.y * 0.5f - 12.0f));
		// Top-left quad samples the texture's top-left 24x24 pixels (V0 is the top edge).
		CHECK(quads[0].Uv.U0 == doctest::Approx(0.0f));
		CHECK(quads[0].Uv.V0 == doctest::Approx(0.0f));
		CHECK(quads[0].Uv.U1 == doctest::Approx(24.0f / 64.0f));
		CHECK(quads[0].Uv.V1 == doctest::Approx(24.0f / 64.0f));
		CHECK(quads[8].Uv.U1 == doctest::Approx(1.0f));
		CHECK(quads[8].Uv.V1 == doctest::Approx(1.0f));
	}
}

TEST_CASE("9-slice shrinks borders that don't fit and honours the border scale") {
	std::array<NineSliceQuad, 9> quads;
	// 48 px of border on a 30 px rect: corners meet in the middle, the middle row/column vanish.
	REQUIRE(BuildNineSlice({ 30.0f, 30.0f }, { 64.0f, 64.0f }, k_Border24, 1.0f, k_Full, quads) == 4);
	CHECK(quads[0].Size.x == doctest::Approx(15.0f));

	REQUIRE(BuildNineSlice({ 200.0f, 200.0f }, { 64.0f, 64.0f }, k_Border24, 2.0f, k_Full, quads) == 9);
	CHECK(quads[0].Size.x == doctest::Approx(48.0f));
	CHECK(quads[0].Uv.U1 == doctest::Approx(24.0f / 64.0f));

	// No border: one quad covering the whole rect.
	REQUIRE(BuildNineSlice({ 100.0f, 50.0f }, { 64.0f, 64.0f }, {}, 1.0f, k_Full, quads) == 1);
	CHECK(quads[0].Size.x == doctest::Approx(100.0f));
	CHECK(quads[0].Center.x == doctest::Approx(0.0f));
}

TEST_CASE("Sliced Image round-trips through scene JSON and the C# binding") {
	Application app;
	auto scene = Scene::CreateDetachedScene("NineSlice");
	Entity src = scene->CreateEntity("Src");
	auto& image = src.AddComponent<ImageComponent>();
	image.Type = ImageType::Sliced;
	image.BorderLeft = 1.0f; image.BorderBottom = 2.0f; image.BorderRight = 3.0f; image.BorderTop = 4.0f;
	image.BorderScale = 0.5f;

	const Json::Value json = SceneSerializer::SerializeComponent(*scene, src.GetHandle(), "Image");
	Entity dst = scene->CreateEntity("Dst");
	dst.AddComponent<ImageComponent>();
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, dst.GetHandle(), "Image", json));
	const auto& copy = dst.GetComponent<ImageComponent>();
	CHECK(copy.Type == ImageType::Sliced);
	CHECK(copy.BorderBottom == doctest::Approx(2.0f));
	CHECK(copy.BorderTop == doctest::Approx(4.0f));
	CHECK(copy.BorderScale == doctest::Approx(0.5f));

	// Simple images keep the old file shape.
	Entity plain = scene->CreateEntity("Plain");
	plain.AddComponent<ImageComponent>();
	CHECK_FALSE(SceneSerializer::SerializeComponent(*scene, plain.GetHandle(), "Image").FindMember("border"));

	ScriptEngine::SetScene(scene.get());
	NativeBindings b{};
	ScriptBindings::PopulateNativeBindings(b);
	REQUIRE(b.Image_GetSlicing != nullptr);
	REQUIRE(b.Image_SetSlicing != nullptr);
	const uint64_t plainId = scene->GetEntityPersistentID(plain.GetHandle());
	const float values[6] = { 1.0f, 8.0f, 8.0f, 8.0f, -5.0f, 2.0f };
	b.Image_SetSlicing(plainId, values, 6);
	float read[6] = {};
	b.Image_GetSlicing(plainId, read, 6);
	ScriptEngine::SetScene(nullptr);
	CHECK(plain.GetComponent<ImageComponent>().Type == ImageType::Sliced);
	CHECK(read[1] == doctest::Approx(8.0f));
	CHECK(read[4] == doctest::Approx(0.0f));
	CHECK(read[5] == doctest::Approx(2.0f));
}

TEST_CASE("Tiled repeats the middle at its pixel size and crops the last tile") {
	std::vector<NineSliceQuad> tiles;
	// No border: a 32x32 sprite on 80x40 = 3x2 tiles, the right column 16 wide, the top row 8 high.
	REQUIRE(BuildTiled({ 80.0f, 40.0f }, { 32.0f, 32.0f }, {}, 1.0f, k_Full, tiles) == 6);
	float area = 0.0f;
	for (const NineSliceQuad& q : tiles) area += q.Size.x * q.Size.y;
	CHECK(area == doctest::Approx(80.0f * 40.0f));
	const NineSliceQuad& bottomLeft = tiles[0];
	CHECK(bottomLeft.Size.x == doctest::Approx(32.0f));
	CHECK(bottomLeft.Center.x == doctest::Approx(-40.0f + 16.0f));
	CHECK(bottomLeft.Center.y == doctest::Approx(-20.0f + 16.0f));
	const NineSliceQuad& topRight = tiles[5];
	CHECK(topRight.Size.x == doctest::Approx(16.0f));
	CHECK(topRight.Size.y == doctest::Approx(8.0f));
	// Cropped tile keeps the sprite's left and bottom part (V1 = bottom edge).
	CHECK(topRight.Uv.U0 == doctest::Approx(0.0f));
	CHECK(topRight.Uv.U1 == doctest::Approx(0.5f));
	CHECK(topRight.Uv.V1 == doctest::Approx(1.0f));
	CHECK(topRight.Uv.V0 == doctest::Approx(0.75f));

	// With a 24 px border the corners stay single quads; the 16 px middle repeats.
	REQUIRE(BuildTiled({ 112.0f, 64.0f }, { 64.0f, 64.0f }, k_Border24, 1.0f, k_Full, tiles) > 9);
	int corners = 0;
	for (const NineSliceQuad& q : tiles) {
		if (q.Size.x == doctest::Approx(24.0f) && q.Size.y == doctest::Approx(24.0f)) ++corners;
		CHECK(q.Size.x <= 24.0f + 1e-3f);
	}
	CHECK(corners == 4);

	// Huge rects grow the tiles instead of emitting unbounded quads.
	CHECK(BuildTiled({ 100000.0f, 100000.0f }, { 4.0f, 4.0f }, {}, 1.0f, k_Full, tiles, 1000) <= 1000);
}

TEST_CASE("Tiled Image type round-trips through scene JSON") {
	Application app;
	auto scene = Scene::CreateDetachedScene("Tiled");
	Entity src = scene->CreateEntity("Src");
	src.AddComponent<ImageComponent>().Type = ImageType::Tiled;
	const Json::Value json = SceneSerializer::SerializeComponent(*scene, src.GetHandle(), "Image");
	Entity dst = scene->CreateEntity("Dst");
	dst.AddComponent<ImageComponent>();
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, dst.GetHandle(), "Image", json));
	CHECK(dst.GetComponent<ImageComponent>().Type == ImageType::Tiled);
}

TEST_CASE("Texture and slice borders round-trip through the .meta") {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "FunkeSpriteBorderMeta";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const std::string texture = (dir / "panel.png").string();
	File::WriteAllText(texture, "not decoded");

	TextureMeta meta;
	meta.Border = { 1, 2, 3, 4 };
	SpriteSlice slice;
	slice.Name = "button";
	slice.W = 32; slice.H = 16;
	slice.Border = { 5, 6, 7, 8 };
	meta.Sprites.push_back(slice);
	SpriteSlice plain;
	plain.Name = "plain";
	plain.W = 8; plain.H = 8;
	meta.Sprites.push_back(plain);
	REQUIRE(AssetRegistry::WriteTextureMeta(texture, meta));

	TextureMeta read = AssetRegistry::ReadTextureMeta(texture);
	CHECK(read.Border == SpriteBorder{ 1, 2, 3, 4 });
	REQUIRE(read.Sprites.size() == 2);
	CHECK(read.Sprites[0].Border == SpriteBorder{ 5, 6, 7, 8 });
	CHECK(read.Sprites[1].Border.IsZero());

	// Clearing the texture border removes the key instead of writing zeros.
	read.Border = {};
	REQUIRE(AssetRegistry::WriteTextureMeta(texture, read));
	CHECK_FALSE(Json::Parse(File::ReadAllText(texture + ".meta")).FindMember("border"));
	CHECK(AssetRegistry::ReadTextureMeta(texture).Border.IsZero());
	CHECK(AssetRegistry::ReadTextureMeta(texture).Sprites[0].Border == SpriteBorder{ 5, 6, 7, 8 });
	fs::remove_all(dir);
}
