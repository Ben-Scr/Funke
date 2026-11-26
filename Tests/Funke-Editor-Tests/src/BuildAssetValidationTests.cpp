#include <doctest/doctest.h>
#include "Build/GameBuilder.hpp"
#include "Editor/BuildAssetPruning.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include <chrono>
#include <fstream>

using namespace Funke;

TEST_CASE("Serialized asset export preserves source errors and relative asset paths") {
    const auto root = std::filesystem::temp_directory_path() / ("FunkeAssetErrors-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path Path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(Path, ec); } } cleanup{root};
    std::filesystem::create_directories(root / "Sub");
    const auto file = root / "Sub/Empty.dataasset";
    std::ofstream(file).close();
    std::string error;
    CHECK(GameBuilder::ConvertSerializedAssets(root, SceneSerializationFormat::Binary, {}, &error) == 0);
    CHECK(error.find("Assets/Sub/Empty.dataasset") != std::string::npos);
    CHECK(error.find("empty") != std::string::npos);
    CHECK(std::filesystem::file_size(file) == 0);
    REQUIRE(File::WriteAllText(Path::ToUtf8(file), "[]"));
    error.clear();
    CHECK(GameBuilder::ConvertSerializedAssets(root, SceneSerializationFormat::Binary, {}, &error) == 0);
    CHECK(error.find("object") != std::string::npos);
    REQUIRE(File::WriteAllText(Path::ToUtf8(file), "{}"));
    error.clear();
    CHECK(GameBuilder::ConvertSerializedAssets(root, SceneSerializationFormat::Binary, {}, &error) == 1);
    CHECK(error.empty());
}
