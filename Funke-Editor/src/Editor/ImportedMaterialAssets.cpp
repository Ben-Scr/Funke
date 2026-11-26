#include "pch.hpp"
#include "Editor/ImportedMaterialAssets.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/Material.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Hash.hpp"
#include <limits>
#include <unordered_map>
#include <unordered_set>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace Funke::ImportedMaterialAssets {
    namespace {
        std::string SafeName(std::string_view name) {
            std::string result;
            for (unsigned char c : name) {
                if (result.size() >= 32) break;
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
                    result.push_back(static_cast<char>(c));
                else result.push_back('_');
            }
            return result.empty() ? "Material" : result;
        }
        bool Within(const std::filesystem::path& root, const std::filesystem::path& path) {
            std::error_code ec;
            const auto resolved = std::filesystem::weakly_canonical(path, ec);
            if (ec) return false;
            const auto relative = resolved.lexically_relative(root);
            if (relative.empty() || relative.is_absolute()) return false;
            return std::none_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; });
        }
    }

    bool Extract(uint64_t modelGuid, const MeshAsset& model, const std::filesystem::path& assetsRoot,
                 const RegisterAsset& registerAsset, Result& result, std::string& error) {
        result = {}; error.clear();
        const auto fail = [&](std::string message) { error = std::move(message); return false; };
        if (modelGuid == 0 || !registerAsset || model.Materials.empty()) return fail("No imported materials are available.");
        for (const auto& range : model.Submeshes)
            if (range.MaterialIndex >= model.Materials.size()) return fail("An imported material index is invalid.");
        try {
            std::error_code ec;
            const auto root = std::filesystem::weakly_canonical(assetsRoot, ec);
            if (ec || !std::filesystem::is_directory(root, ec)) return fail("The project's Assets directory is unavailable.");
            result.Directory = root / "ImportedMaterials" / std::to_string(modelGuid);
            if (!Within(root, result.Directory)) return fail("The extraction directory is outside Assets.");
            std::filesystem::create_directories(result.Directory, ec);
            if (ec) return fail("Could not create the material directory: " + ec.message());
            const auto safeAsset = [&](const std::filesystem::path& path) {
                return Within(root, path) && Within(root, Path::FromUtf8(Path::ToUtf8(path) + ".meta"))
                    && Within(root, Path::FromUtf8(Path::ToUtf8(path) + ".tmp"))
                    && Within(root, Path::FromUtf8(Path::ToUtf8(path) + ".meta.tmp"));
            };
            std::unordered_map<int32_t, uint64_t> textures;
            const auto extractTexture = [&](int32_t index, uint64_t& guid) -> bool {
                guid = 0;
                if (index < 0) return true;
                if (auto found = textures.find(index); found != textures.end()) { guid = found->second; return true; }
                if (static_cast<size_t>(index) >= model.Textures.size()) return fail("An imported texture index is invalid.");
                const auto& texture = model.Textures[index];
                if (!texture.Image || !texture.Image->Pixels) return fail("An imported texture could not be decoded.");
                const auto& image = *texture.Image;
                if (image.Width <= 0 || image.Height <= 0 || image.Width > std::numeric_limits<int>::max() / 4
                    || static_cast<uint64_t>(image.Width) * image.Height > std::numeric_limits<size_t>::max() / 4)
                    return fail("An imported image has invalid dimensions.");
                const size_t bytes = static_cast<size_t>(image.Width) * image.Height * 4;
                const std::string signature = std::to_string(image.Width) + ":" + std::to_string(image.Height) + ":"
                    + std::to_string(static_cast<int>(texture.Filtering)) + ":" + std::to_string(static_cast<int>(texture.WrapU))
                    + ":" + std::to_string(static_cast<int>(texture.WrapV)) + ":" + Hash::Sha256OfBytes(image.Pixels, bytes);
                const auto path = result.Directory / ("Texture-" + Hash::Sha256OfBytes(signature.data(), signature.size()) + ".png");
                if (!safeAsset(path)) return fail("An extracted texture path points outside Assets.");
                if (!std::filesystem::exists(path)) {
                    std::vector<uint8_t> encoded;
                    const auto write = [](void* context, void* data, int size) {
                        auto& bytes = *static_cast<std::vector<uint8_t>*>(context);
                        const auto* first = static_cast<const uint8_t*>(data);
                        bytes.insert(bytes.end(), first, first + size);
                    };
                    if (!stbi_write_png_to_func(write, &encoded, image.Width, image.Height, 4, image.Pixels, image.Width * 4)
                        || !File::WriteAllBytes(Path::ToUtf8(path), encoded)) return fail("Could not write an extracted texture.");
                    ++result.CreatedTextures;
                }
                const TextureImportSettings settings{texture.Filtering, texture.WrapU, texture.WrapV};
                guid = registerAsset(Path::ToUtf8(path), &settings);
                if (guid == 0) return fail("Could not register an extracted texture.");
                textures[index] = guid;
                return true;
            };
            std::unordered_set<uint32_t> used;
            for (const auto& range : model.Submeshes) used.insert(range.MaterialIndex);
            for (size_t index = 0; index < model.Materials.size(); ++index) {
                if (!used.contains(static_cast<uint32_t>(index))) continue;
                const auto& imported = model.Materials[index];
                if (imported.Key.empty()) return fail("An imported material has no stable key.");
                const auto path = result.Directory / (SafeName(imported.Name) + "-"
                    + Hash::Sha256OfBytes(imported.Key.data(), imported.Key.size()) + ".material");
                if (!safeAsset(path)) return fail("An extracted material path points outside Assets.");
                Material material;
                if (std::filesystem::exists(path)) {
                    if (!MaterialLibrary::LoadFromFile(Path::ToUtf8(path), material))
                        return fail("An existing extracted material is unreadable; it was kept unchanged.");
                }
                else {
                    material = imported.Surface;
                    uint64_t* slots[] = {&material.BaseColorTexture, &material.NormalTexture, &material.MetallicRoughnessTexture,
                        &material.EmissiveTexture, &material.OcclusionTexture};
                    for (size_t slot = 0; slot < imported.Textures.size(); ++slot)
                        if (!extractTexture(imported.Textures[slot], *slots[slot])) return false;
                    if (!MaterialLibrary::SaveToFile(Path::ToUtf8(path), material)) return fail("Could not write an extracted material.");
                    ++result.CreatedMaterials;
                }
                const uint64_t guid = registerAsset(Path::ToUtf8(path), nullptr);
                if (guid == 0) return fail("Could not register an extracted material.");
                result.Materials.Set(modelGuid, imported.Key, guid);
            }
            return true;
        } catch (const std::filesystem::filesystem_error& exception) {
            return fail("Could not access extracted assets: " + std::string(exception.what()));
        }
    }
}
