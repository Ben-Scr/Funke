#include <doctest/doctest.h>
#include "Graphics/GltfImporter.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/ModelMaterialOverrides.hpp"
#include "Graphics/SkinningCompute.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <chrono>

using namespace Funke;

namespace {
    struct MaterialFixture {
        std::vector<uint8_t> Geometry;
        std::vector<uint8_t> Image;
        MaterialFixture() {
            // Positions, UVs, authored tangents; two primitives share their accessors.
            const float data[] = {0,0,0, 1,0,0, 0,1,0, 0,0, 1,0, 0,1,
                1,0,0,-1, 1,0,0,-1, 1,0,0,-1};
            const auto* bytes = reinterpret_cast<const uint8_t*>(data);
            Geometry.assign(bytes, bytes + sizeof(data));
            // 1x1 RGBA PNG, pixel (90,60,30,128).
            REQUIRE(DecodeBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGOIspFrAAAC3QE1m8hIowAAAABJRU5ErkJggg==", Image));
        }
        std::string Document(std::string image = R"({"uri":"paint%20color.png"})",
                             std::string normal = R"({"index":0,"scale":0.3})") const {
            return R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],
                "buffers":[{"uri":"mesh.bin","byteLength":108}],
                "bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},
                    {"buffer":0,"byteOffset":60,"byteLength":48}],
                "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},
                    {"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"}],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1,"TANGENT":2},"material":0},
                    {"attributes":{"POSITION":0,"TEXCOORD_0":1,"TANGENT":2},"material":1},
                    {"attributes":{"POSITION":0}}]}],
                "samplers":[{"wrapS":33071,"wrapT":33648}],"textures":[{"source":0,"sampler":0}],"images":[)"
                + image + R"(],"materials":[{"name":"Leaf","alphaMode":"MASK","alphaCutoff":0.4,"doubleSided":true,
                    "pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,0.1,0.8],"baseColorTexture":{"index":0},
                        "metallicFactor":0.7,"roughnessFactor":0.2,"metallicRoughnessTexture":{"index":0}},
                    "normalTexture":)" + normal + R"(,"occlusionTexture":{"index":0,"strength":0.6},
                    "emissiveTexture":{"index":0},"emissiveFactor":[0.1,0.2,0.3]},
                    {"name":"Glass","alphaMode":"BLEND","extensions":{"KHR_materials_unlit":{}}}]})";
        }
        bool Parse(const std::string& document, MeshAsset& out, std::string* error = nullptr, bool imageAvailable = true) const {
            return ParseGltf({reinterpret_cast<const uint8_t*>(document.data()), document.size()},
                [&](const std::string& path, std::vector<uint8_t>& bytes) {
                    if (path == "mesh.bin") { bytes = Geometry; return true; }
                    if (path == "paint color.png" && imageAvailable) { bytes = Image; return true; }
                    return false;
                }, out, error);
        }
    };
}

TEST_CASE("Imported materials preserve primitive ranges, factors, images and alpha modes") {
    MaterialFixture fixture;
    MeshAsset asset;
    REQUIRE(fixture.Parse(fixture.Document(), asset));
    REQUIRE(asset.Submeshes.size() == 3);
    REQUIRE(asset.Materials.size() == 3);
    REQUIRE(asset.Textures.size() == 1);
    for (uint32_t i = 0; i < 3; ++i) {
        CHECK(asset.Submeshes[i].FirstIndex == 3 * i);
        CHECK(asset.Submeshes[i].IndexCount == 3);
        CHECK(asset.Submeshes[i].MaterialIndex == i);
    }
    const auto& leaf = asset.Materials[0];
    CHECK(leaf.Name == "Leaf");
    CHECK(leaf.Key == "name:Leaf");
    CHECK(asset.Materials[1].Key == "name:Glass");
    CHECK(asset.Materials[2].Key == "default");
    CHECK(leaf.Surface.Surface == MaterialSurface::Cutout);
    CHECK(leaf.Surface.AlphaCutoff == doctest::Approx(0.4));
    CHECK(leaf.Surface.DoubleSided);
    CHECK(leaf.Surface.LinearColorFactors);
    CHECK(leaf.Surface.BaseColor.r == doctest::Approx(0.5));
    CHECK(leaf.Surface.BaseColor.a == doctest::Approx(0.8));
    CHECK(leaf.Surface.Metallic == doctest::Approx(0.7));
    CHECK(leaf.Surface.Roughness == doctest::Approx(0.2));
    CHECK(leaf.Surface.NormalScale == doctest::Approx(0.3));
    CHECK(leaf.Surface.OcclusionStrength == doctest::Approx(0.6));
    for (auto slot : leaf.Textures) CHECK(slot == 0);
    CHECK(asset.Materials[1].Surface.Surface == MaterialSurface::Transparent);
    CHECK_FALSE(asset.Materials[1].Surface.Lit);
    CHECK(asset.Materials[2].Surface.Metallic == 1);
    CHECK(asset.Materials[2].Surface.Roughness == 1);
    CHECK(asset.Textures[0].WrapU == Wrap::Clamp);
    CHECK(asset.Textures[0].WrapV == Wrap::Mirror);
    REQUIRE(asset.Textures[0].Image);
    CHECK(asset.Textures[0].Image->Width == 1);
    CHECK(asset.Textures[0].Image->Pixels[0] == 90);
    CHECK(asset.Textures[0].Image->Pixels[3] == 128);
    CHECK(asset.Data.Vertices[0].Tangent[0] == 1);
    CHECK(asset.Data.Vertices[0].Tangent[3] == -1);
    CHECK(asset.Data.Vertices[6].Tangent[3] == 0);
}

TEST_CASE("Imported materials decode embedded images and reject silent visual corruption") {
    MaterialFixture fixture;
    MeshAsset asset;
    // The same PNG encoded as a data URI.
    REQUIRE(fixture.Parse(fixture.Document(R"({"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGOIspFrAAAC3QE1m8hIowAAAABJRU5ErkJggg=="})"), asset));
    REQUIRE(asset.Textures[0].Image);
    CHECK(asset.Textures[0].Image->Pixels[3] == 128);
    std::string error;
    CHECK_FALSE(fixture.Parse(fixture.Document(), asset, &error, false));
    CHECK(error.find("could not read image") != std::string::npos);
    CHECK(asset.Data.Vertices.empty());
    CHECK_FALSE(fixture.Parse(fixture.Document(R"({"uri":"paint%20color.png"})", R"({"index":0,"texCoord":1})"), asset, &error));
    CHECK(error.find("TEXCOORD_0") != std::string::npos);
    CHECK_FALSE(fixture.Parse(fixture.Document(R"({"uri":"data:image/png;base64,AAAA"})"), asset, &error));
    CHECK(error.find("corrupt") != std::string::npos);
    auto unsupported = fixture.Document();
    unsupported.insert(1, R"("extensionsRequired":["KHR_materials_transmission"],)");
    CHECK_FALSE(fixture.Parse(unsupported, asset, &error));
    CHECK(error.find("KHR_materials_transmission") != std::string::npos);
}

TEST_CASE("Standard material maps and cutout round trip without changing legacy defaults") {
    Material material;
    material.Surface = MaterialSurface::Cutout;
    material.NormalTexture = UINT64_MAX - 1;
    material.MetallicRoughnessTexture = UINT64_MAX - 2;
    material.EmissiveTexture = UINT64_MAX - 3;
    material.OcclusionTexture = UINT64_MAX - 4;
    material.NormalScale = 0.25f;
    material.OcclusionStrength = 0.3f;
    material.AlphaCutoff = 0.8f;
    material.DoubleSided = true;
    material.LinearColorFactors = true;
    CHECK(MaterialFromJson(Json::Parse(Json::Stringify(MaterialToJson(material)))) == material);
    CHECK(MaterialFromJson(Json::Parse("{}")) == Material{});
    CHECK(MaterialFromJson(Json::Parse(R"({"surface":1})")).Surface == MaterialSurface::Transparent);
    auto clamped = MaterialFromJson(Json::Parse(R"({"normalScale":-1,"occlusionStrength":7,"alphaCutoff":-2})"));
    CHECK(clamped.NormalScale == 0);
    CHECK(clamped.OcclusionStrength == 1);
    CHECK(clamped.AlphaCutoff == 0);
}

TEST_CASE("Model material slots retain unique name identity across source reordering") {
    MaterialFixture fixture;
    auto json = Json::Parse(fixture.Document());
    auto& materials = json.FindMember("materials")->GetArray();
    std::swap(materials[0], materials[1]);
    auto& primitives = json.FindMember("meshes")->GetArray()[0].FindMember("primitives")->GetArray();
    *primitives[0].FindMember("material") = Json::Value(1);
    *primitives[1].FindMember("material") = Json::Value(0);
    MeshAsset model;
    REQUIRE(fixture.Parse(Json::Stringify(json), model));
    CHECK(model.Materials[model.Submeshes[0].MaterialIndex].Key == "name:Leaf");
    CHECK(model.Materials[model.Submeshes[1].MaterialIndex].Key == "name:Glass");
    *materials[0].FindMember("name") = Json::Value("Leaf");
    REQUIRE(fixture.Parse(Json::Stringify(json), model));
    CHECK(model.Materials[0].Key == "index:0");
    CHECK(model.Materials[1].Key == "index:1");
}

TEST_CASE("Per-model material overrides round trip full GUIDs and reject other model bindings") {
    ModelMaterialOverrides source;
    source.Set(UINT64_MAX, "name:Leaf", UINT64_MAX - 1);
    source.Set(UINT64_MAX, "index:2", 123);
    auto json = Json::Value::MakeObject();
    source.Serialize(json);
    ModelMaterialOverrides loaded;
    loaded.Deserialize(Json::Parse(Json::Stringify(json)), UINT64_MAX);
    CHECK(loaded == source);
    CHECK(loaded.Get(UINT64_MAX, "name:Leaf") == UINT64_MAX - 1);
    CHECK(loaded.Get(1, "name:Leaf") == 0);
    CHECK(loaded.Get(UINT64_MAX, "missing") == 0);
    loaded.Deserialize(json, 42);
    CHECK(loaded.Slots.empty());
    loaded = source;
    loaded.Set(42, "name:Other", 91);
    CHECK(loaded.Slots.size() == 1);
    CHECK(loaded.Get(42, "name:Other") == 91);
    loaded.Set(42, "name:Other", 0);
    CHECK(loaded.Model == 0);
    CHECK(loaded.Slots.empty());
    loaded.Deserialize(Json::Parse(R"({"materialOverrides":{"model":"42","slots":[
        {"slot":"invalid","material":"123x"},{"slot":"negative","material":"-1"},
        {"slot":"overflow","material":"18446744073709551616"},
        {"slot":"","material":"2"},{"slot":"valid","material":"123"}]}})"), 42);
    CHECK(loaded.Slots.size() == 1);
    CHECK(loaded.Get(42, "valid") == 123);
    loaded.Deserialize(Json::Parse("{}"), 42);
    CHECK(loaded.Slots.empty());
}

TEST_CASE("GLB bufferView images retain material assignment across repeated imports") {
    MaterialFixture fixture;
    auto root = Json::Parse(fixture.Document(R"({"bufferView":3,"mimeType":"image/png"})"));
    const auto offset = fixture.Geometry.size();
    fixture.Geometry.insert(fixture.Geometry.end(), fixture.Image.begin(), fixture.Image.end());
    root.FindMember("buffers")->GetArray()[0] = Json::Parse("{\"byteLength\":" + std::to_string(fixture.Geometry.size()) + "}");
    root.FindMember("bufferViews")->Append(Json::Parse("{\"buffer\":0,\"byteOffset\":" + std::to_string(offset)
        + ",\"byteLength\":" + std::to_string(fixture.Image.size()) + "}"));
    auto json = Json::Stringify(root);
    while (json.size() % 4) json.push_back(' ');
    while (fixture.Geometry.size() % 4) fixture.Geometry.push_back(0);
    std::vector<uint8_t> glb;
    auto append = [&](uint32_t value) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        glb.insert(glb.end(), bytes, bytes + 4);
    };
    append(0x46546c67); append(2); append(static_cast<uint32_t>(28 + json.size() + fixture.Geometry.size()));
    append(static_cast<uint32_t>(json.size())); append(0x4e4f534a);
    glb.insert(glb.end(), json.begin(), json.end());
    append(static_cast<uint32_t>(fixture.Geometry.size())); append(0x004e4942);
    glb.insert(glb.end(), fixture.Geometry.begin(), fixture.Geometry.end());
    MeshAsset asset;
    for (int pass = 0; pass < 3; ++pass) {
        REQUIRE(ParseGltf(glb, nullptr, asset));
        REQUIRE(asset.Textures.size() == 1);
        REQUIRE(asset.Textures[0].Image);
        CHECK(asset.Textures[0].Image->Pixels[3] == 128);
        CHECK(asset.Materials.size() == 3);
        CHECK(asset.Submeshes.size() == 3);
    }
}

TEST_CASE("Authored tangents survive skinning and reflected transforms") {
    SkinnedMeshData data;
    MeshVertex vertex{};
    vertex.Normal[2] = 1;
    vertex.Tangent[0] = 1;
    vertex.Tangent[3] = -1;
    data.BindVertices.push_back(vertex);
    data.VertexJoints.push_back({0,0,0,0});
    data.VertexWeights.push_back({1,0,0,0});
    std::vector<MeshVertex> output;
    SkeletalAnimation::SkinVertices(data, {glm::scale(glm::mat4(1), {-2,3,4})}, output);
    REQUIRE(output.size() == 1);
    CHECK(output[0].Tangent[0] == -1);
    CHECK(output[0].Tangent[3] == 1);
    CHECK(output[0].Normal[2] == 1);
    std::vector<SkinningCompute::Vertex> packed;
    REQUIRE(SkinningCompute::PackVertices(data, packed));
    CHECK(packed[0].Tangent[0] == 1);
    CHECK(packed[0].Tangent[3] == -1);
}

TEST_CASE("Model image dependencies reload atomically and release replaced CPU images") {
    MaterialFixture fixture;
    const auto folder = std::filesystem::temp_directory_path() / ("FunkeMaterialReload_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    constexpr uint64_t guid = 0x12345678765;
    struct Cleanup {
        std::filesystem::path Folder;
        ~Cleanup() { MeshLibrary::Invalidate(guid); std::error_code ec; std::filesystem::remove_all(Folder,ec); }
    } cleanup{folder};
    const auto model = (folder / "model.gltf").string();
    const auto image = folder / "paint color.png";
    const auto write = [&](const std::filesystem::path& path, const auto& bytes) {
        std::ofstream file(path,std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        file.close(); REQUIRE(file.good());
    };
    write(model,fixture.Document()); write(folder / "mesh.bin",fixture.Geometry); write(image,fixture.Image);
    MeshAsset imported;
    REQUIRE(MeshLibrary::LoadAssetFromFile(model,imported));
    CHECK(imported.SourceDependencies.size()==3);
    auto* live=MeshLibrary::StoreLoadedAsset(guid,model,std::move(imported));
    REQUIRE(live);
    const auto revision=live->Revision;
    std::weak_ptr<const ImageData> oldImage=live->Textures[0].Image;
    std::filesystem::last_write_time(image,std::filesystem::last_write_time(image)+std::chrono::seconds(2));
    CHECK(MeshLibrary::ReloadChangedFromDisk()==1);
    CHECK(MeshLibrary::FindLoaded(guid)==live);
    CHECK(live->Revision!=revision);
    CHECK(oldImage.expired());
    const auto validRevision=live->Revision;
    write(image,std::vector<uint8_t>{0,1,2});
    CHECK(MeshLibrary::ReloadChangedFromDisk()==0);
    CHECK(live->Revision==validRevision);
    CHECK(live->Textures[0].Image->Pixels[3]==128);
    write(image,fixture.Image);
    CHECK(MeshLibrary::ReloadChangedFromDisk()==1);
    std::weak_ptr<const ImageData> lastImage=live->Textures[0].Image;
    MeshLibrary::Invalidate(guid);
    CHECK(lastImage.expired());
}
