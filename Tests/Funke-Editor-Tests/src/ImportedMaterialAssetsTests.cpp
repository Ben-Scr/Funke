#include <doctest/doctest.h>
#include "Editor/ImportedMaterialAssets.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Serialization/Path.hpp"
#include <chrono>
#include <fstream>
#include <map>

using namespace Funke;

namespace {
    struct ExtractionFixture {
        std::filesystem::path Root = std::filesystem::temp_directory_path() / ("fnk-material-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::map<std::string, uint64_t> Guids;
        std::map<uint64_t, TextureImportSettings> Samplers;
        MeshAsset Model;
        ExtractionFixture() {
            std::filesystem::create_directories(Root);
            ImportedMeshMaterial material;
            material.Name = "Leaf"; material.Key = "name:Leaf";
            material.Surface.Surface = MaterialSurface::Cutout;
            material.Surface.LinearColorFactors = true;
            material.Surface.AlphaCutoff = 0.3f;
            material.Textures = {0, 0, 0, 0, 0};
            Model.Materials.push_back(material);
            material.Name = "Glass"; material.Key = "name:Glass";
            material.Surface.Surface = MaterialSurface::Transparent;
            Model.Materials.push_back(material);
            material.Name = "Unused"; material.Key = "name:Unused";
            Model.Materials.push_back(material);
            Model.Submeshes = {{0, 3, 0}, {3, 3, 1}};
            ImportedMeshTexture texture;
            texture.Image = std::make_shared<ImageData>(1, 1, std::vector<unsigned char>{90, 60, 30, 128});
            texture.WrapU = Wrap::Mirror; texture.WrapV = Wrap::Clamp;
            Model.Textures.push_back(texture);
        }
        ~ExtractionFixture() { std::error_code ec; std::filesystem::remove_all(Root, ec); }
        ImportedMaterialAssets::RegisterAsset Register() {
            return [this](const std::string& path, const TextureImportSettings* settings) {
                auto [item, inserted] = Guids.try_emplace(path, 1000 + Guids.size());
                if (settings && inserted) Samplers.emplace(item->second, *settings);
                return item->second;
            };
        }
        bool Extract(ImportedMaterialAssets::Result& result, std::string& error, uint64_t model = 42) {
            return ImportedMaterialAssets::Extract(model, Model, Root, Register(), result, error);
        }
        std::string PathFor(uint64_t guid) const {
            for (const auto& [path, value] : Guids) if (value == guid) return path;
            return {};
        }
    };
}

TEST_CASE("Extracted material assets preserve maps, pixels, sampler state and user edits") {
    ExtractionFixture fixture;
    ImportedMaterialAssets::Result first;
    std::string error;
    REQUIRE(fixture.Extract(first, error));
    CHECK(error.empty());
    CHECK(first.CreatedMaterials == 2);
    CHECK(first.CreatedTextures == 1);
    CHECK(first.Materials.Slots.size() == 2);
    CHECK(first.Materials.Get(42, "name:Unused") == 0);
    const auto path = fixture.PathFor(first.Materials.Get(42, "name:Leaf"));
    Material leaf;
    REQUIRE(MaterialLibrary::LoadFromFile(path, leaf));
    CHECK(leaf.Surface == MaterialSurface::Cutout);
    CHECK(leaf.LinearColorFactors);
    CHECK(leaf.AlphaCutoff == doctest::Approx(0.3));
    CHECK(leaf.BaseColorTexture != 0);
    CHECK(leaf.NormalTexture == leaf.BaseColorTexture);
    CHECK(leaf.MetallicRoughnessTexture == leaf.BaseColorTexture);
    CHECK(leaf.EmissiveTexture == leaf.BaseColorTexture);
    CHECK(leaf.OcclusionTexture == leaf.BaseColorTexture);
    const auto& sampler = fixture.Samplers.at(leaf.BaseColorTexture);
    CHECK(sampler.FilterMode == Filter::Trilinear);
    CHECK(sampler.WrapU == Wrap::Mirror);
    CHECK(sampler.WrapV == Wrap::Clamp);
    const auto pixels = ImageProcessing::LoadFile(Path::FromUtf8(fixture.PathFor(leaf.BaseColorTexture)));
    REQUIRE(pixels);
    CHECK(pixels->Pixels[0] == 90);
    CHECK(pixels->Pixels[3] == 128);

    leaf.AlphaCutoff = 0.73f;
    leaf.DoubleSided = true;
    REQUIRE(MaterialLibrary::SaveToFile(path, leaf));
    // Source material reorder and changed source factors must not overwrite extracted copies.
    std::swap(fixture.Model.Materials[0], fixture.Model.Materials[1]);
    fixture.Model.Submeshes[0].MaterialIndex = 1;
    fixture.Model.Submeshes[1].MaterialIndex = 0;
    fixture.Model.Materials[1].Surface.AlphaCutoff = 0.12f;
    ImportedMaterialAssets::Result again;
    REQUIRE(fixture.Extract(again, error));
    CHECK(again.CreatedMaterials == 0);
    CHECK(again.CreatedTextures == 0);
    CHECK(again.Materials == first.Materials);
    Material edited;
    REQUIRE(MaterialLibrary::LoadFromFile(path, edited));
    CHECK(edited == leaf);
    ImportedMaterialAssets::Result other;
    REQUIRE(fixture.Extract(other, error, 43));
    CHECK(other.Materials.Get(43, "name:Leaf") != first.Materials.Get(42, "name:Leaf"));
}

TEST_CASE("Extracted textures deduplicate pixels but retain distinct samplers") {
    ExtractionFixture fixture;
    fixture.Model.Textures.push_back(fixture.Model.Textures.front());
    fixture.Model.Materials[0].Textures[1] = 1;
    SUBCASE("identical pixels and samplers share an asset") {
        ImportedMaterialAssets::Result result;
        std::string error;
        REQUIRE(fixture.Extract(result, error));
        CHECK(result.CreatedTextures == 1);
    }
    SUBCASE("different sampling stays independent") {
        fixture.Model.Textures[1].WrapU = Wrap::Repeat;
        ImportedMaterialAssets::Result result;
        std::string error;
        REQUIRE(fixture.Extract(result, error));
        CHECK(result.CreatedTextures == 2);
        Material material;
        REQUIRE(MaterialLibrary::LoadFromFile(fixture.PathFor(result.Materials.Get(42, "name:Leaf")), material));
        CHECK(material.BaseColorTexture != material.NormalTexture);
        CHECK(fixture.Samplers.at(material.NormalTexture).WrapU == Wrap::Repeat);
    }
}

TEST_CASE("Material extraction reports corrupt existing files and registration failures") {
    ExtractionFixture fixture;
    ImportedMaterialAssets::Result result;
    std::string error;
    SUBCASE("existing unreadable material is preserved") {
        REQUIRE(fixture.Extract(result, error));
        const auto path = fixture.PathFor(result.Materials.Get(42, "name:Leaf"));
        { std::ofstream output(Path::FromUtf8(path)); output << "{broken"; }
        CHECK_FALSE(fixture.Extract(result, error));
        CHECK(error.find("unchanged") != std::string::npos);
        std::ifstream input(Path::FromUtf8(path));
        const std::string contents{std::istreambuf_iterator<char>(input), {}};
        CHECK(contents == "{broken");
    }
    SUBCASE("failed registration leaves a retryable result") {
        CHECK_FALSE(ImportedMaterialAssets::Extract(42, fixture.Model, fixture.Root,
            [](const std::string&, const TextureImportSettings*) -> uint64_t { return 0; }, result, error));
        CHECK_FALSE(error.empty());
        CHECK(result.Materials.Slots.empty());
        REQUIRE(fixture.Extract(result, error));
        CHECK(result.Materials.Slots.size() == 2);
    }
    SUBCASE("invalid material range does not create assets") {
        fixture.Model.Submeshes[0].MaterialIndex = 999;
        CHECK_FALSE(fixture.Extract(result, error));
        CHECK(fixture.Guids.empty());
        CHECK_FALSE(std::filesystem::exists(fixture.Root / "ImportedMaterials"));
    }
}
