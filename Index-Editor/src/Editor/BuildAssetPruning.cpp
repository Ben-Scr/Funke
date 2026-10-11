#include "pch.hpp"
#include "BuildAssetPruning.hpp"

#include "Assets/AssetRegistry.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_set>

namespace Index::BuildAssetPruning {

	namespace {
		std::string ToLower(std::string s) {
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		struct Candidate {
			std::filesystem::path Relative;
			std::string Decimal;
			std::string LittleEndian;
			std::string LowerStem;
		};
	}

	// Text and binary assets both store the GUID as a decimal string; raw little-endian bytes are
	// matched too. Anything ambiguous counts as referenced.
	std::vector<std::filesystem::path> FindUnreferencedBuiltInFonts(
		const std::filesystem::path& indexAssetsRoot,
		const std::filesystem::path& projectRoot,
		const std::vector<std::filesystem::path>& skippedDirectories)
	{
		std::vector<Candidate> candidates;

		std::error_code ec;
		for (std::filesystem::recursive_directory_iterator it(indexAssetsRoot / "Fonts",
				std::filesystem::directory_options::skip_permission_denied, ec), end;
			it != end; it.increment(ec))
		{
			if (ec) return {};
			if (!it->is_regular_file(ec)) continue;
			const std::string ext = ToLower(it->path().extension().string());
			if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;

			const std::filesystem::path rel = it->path().lexically_relative(indexAssetsRoot);
			const std::string relLower = ToLower(rel.generic_string());
			// Loaded by fixed path from FontManager / ImGuiFonts, never by GUID.
			if (relLower == "fonts/googlesans-regular.ttf" || relLower.starts_with("fonts/notosanscjk/")) continue;

			const uint64_t guid = AssetRegistry::ComputeBuiltInPathGuid(rel.generic_string());
			Candidate c;
			c.Relative = rel;
			c.Decimal = std::to_string(guid);
			c.LittleEndian.resize(sizeof(guid));
			for (size_t i = 0; i < sizeof(guid); ++i) {
				c.LittleEndian[i] = static_cast<char>((guid >> (8 * i)) & 0xFF);
			}
			c.LowerStem = ToLower(rel.stem().string());
			candidates.push_back(std::move(c));
		}
		if (ec) return {};
		if (candidates.empty()) return {};

		static const std::unordered_set<std::string> k_SkippedExtensions{
			".ttf", ".otf", ".ttc", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tga", ".psd",
			".wav", ".mp3", ".ogg", ".opus", ".flac", ".mp4", ".webm", ".mov",
			".dll", ".exe", ".pdb", ".so", ".dylib",
		};
		static const std::unordered_set<std::string> k_SkippedDirectoryNames{
			"bin", "obj", ".index", ".git", ".vs", "indexassets",
		};
		constexpr std::uintmax_t k_MaxScannedFileSize = 256ull * 1024 * 1024;

		auto isSkippedDirectory = [&](const std::filesystem::path& dir) {
			if (k_SkippedDirectoryNames.contains(ToLower(dir.filename().string()))) return true;
			for (const auto& skipped : skippedDirectories) {
				std::error_code eq;
				if (!skipped.empty() && std::filesystem::equivalent(dir, skipped, eq)) return true;
			}
			return false;
		};

		for (std::filesystem::recursive_directory_iterator it(projectRoot,
				std::filesystem::directory_options::skip_permission_denied, ec), end;
			it != end && !candidates.empty(); it.increment(ec))
		{
			if (ec) return {};
			std::error_code entryEc;
			if (it->is_directory(entryEc)) {
				if (isSkippedDirectory(it->path())) it.disable_recursion_pending();
				continue;
			}
			if (!it->is_regular_file(entryEc)) continue;
			if (k_SkippedExtensions.contains(ToLower(it->path().extension().string()))) continue;
			const std::uintmax_t size = it->file_size(entryEc);
			// Unscanned files could hold a reference, so give up rather than guess.
			if (entryEc || size > k_MaxScannedFileSize) return {};

			std::ifstream in(it->path(), std::ios::binary);
			if (!in) return {};
			const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const std::string lowered = ToLower(content);

			std::erase_if(candidates, [&](const Candidate& c) {
				return content.find(c.Decimal) != std::string::npos
					|| content.find(c.LittleEndian) != std::string::npos
					|| lowered.find(c.LowerStem) != std::string::npos;
			});
		}
		if (ec) return {};

		std::vector<std::filesystem::path> unused;
		unused.reserve(candidates.size());
		for (const auto& c : candidates) unused.push_back(c.Relative);
		return unused;
	}

}
