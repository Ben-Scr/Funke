#pragma once
#include "Assets/TextureMeta.hpp"
#include "Graphics/ModelMaterialOverrides.hpp"
#include <filesystem>
#include <functional>
#include <string>

namespace Funke {
    struct MeshAsset;
    namespace ImportedMaterialAssets {
        // Registration is supplied by the editor; extraction itself is CPU-only and independently testable.
        using RegisterAsset = std::function<uint64_t(const std::string&, const TextureImportSettings*)>;
        struct Result {
            ModelMaterialOverrides Materials;
            std::filesystem::path Directory;
            size_t CreatedMaterials = 0;
            size_t CreatedTextures = 0;
        };
        // Explicit extraction into user-owned copies. Existing files/metadata are never overwritten.
        // Material identity: model GUID + import key; image identity: decoded pixels + sampler settings.
        bool Extract(uint64_t modelGuid, const MeshAsset& model, const std::filesystem::path& assetsRoot,
                     const RegisterAsset& registerAsset, Result& result, std::string& error);
    }
}
