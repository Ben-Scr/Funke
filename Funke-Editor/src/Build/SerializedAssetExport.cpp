#include "Build/GameBuilder.hpp"
#include "Serialization/Path.hpp"
#include <algorithm>
#include <cctype>

namespace Funke::GameBuilder {
    namespace {
        std::string AssetName(const std::filesystem::path& file, const std::filesystem::path& root) {
            std::string name = Path::ToUtf8(file.lexically_relative(root));
            std::replace(name.begin(), name.end(), '\\', '/');
            return "Assets/" + name;
        }
    }
	bool IsSerializedAssetPath(const std::filesystem::path& path) {
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return ext == ".scene" || ext == ".prefab" || ext == ".dataasset";
	}

	int ConvertSerializedAssets(const std::filesystem::path& rootDir, SceneSerializationFormat format,
		const std::function<void(int, int, const std::string&)>& onProgress, std::string* failureMessage)
	{
		if (!std::filesystem::exists(rootDir)) {
			if (failureMessage) *failureMessage = "asset directory does not exist";
			return 0;
		}

		// Collected first so progress is determinate; the walk alone is slow on large projects.
		std::vector<std::filesystem::path> targets;
		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(rootDir,
				std::filesystem::directory_options::none, ec), end;
			it != end;
			it.increment(ec))
		{
			if (ec) {
				if (failureMessage && failureMessage->empty()) {
					*failureMessage = "failed to enumerate assets: " + ec.message();
				}
				return 0;
			}
			const bool regular = it->is_regular_file(ec);
			if (ec) {
				if (failureMessage) *failureMessage = "Failed to inspect " + AssetName(it->path(), rootDir) + ": " + ec.message();
				return 0;
			}
			if (!regular || !IsSerializedAssetPath(it->path())) continue;
			targets.push_back(it->path());
		}

		if (ec) {
			if (failureMessage) *failureMessage = "Failed to enumerate assets: " + ec.message();
			return 0;
		}
		int converted = 0;
		const int total = static_cast<int>(targets.size());
		for (int i = 0; i < total; ++i) {
			if (onProgress) {
				onProgress(i, total, Path::ToUtf8(targets[i].filename()));
			}
			std::string error;
			if (SceneSerializer::ConvertFileFormat(Path::ToUtf8(targets[i]), format, error)) {
				++converted;
			}
			else if (failureMessage && failureMessage->empty()) {
				*failureMessage = AssetName(targets[i], rootDir) + ": " + error;
			}
		}
		if (onProgress) {
			onProgress(total, total, {});
		}
		return converted;
	}

}
