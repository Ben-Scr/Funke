#include "Systems/LauncherSettings.hpp"
#include "Core/Log.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include <cctype>
#include <filesystem>
#include <vector>

namespace Funke {
	std::string LauncherSettings::GetSettingsPath() {
		return Path::Combine(
			Path::GetSpecialFolderPath(SpecialFolder::LocalAppData),
			"Funke", "Launcher", "launcher_settings.json");
	}

	// One-time move of a launcher file from the old LocalAppData/Funke root into
	// the new Launcher/ subfolder so existing settings survive. No-op once moved.
	static void MigrateLegacyLauncherFile(const std::string& legacyPath, const std::string& newPath) {
		if (legacyPath == newPath || !File::Exists(legacyPath) || File::Exists(newPath)) return;

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(newPath).parent_path(), ec);
		std::filesystem::rename(legacyPath, newPath, ec);
		if (ec) {
			ec.clear();
			std::filesystem::copy_file(legacyPath, newPath,
				std::filesystem::copy_options::overwrite_existing, ec);
			if (!ec) std::filesystem::remove(legacyPath, ec);
		}
	}

	const char* LauncherSettings::DirectoryNameConventionLabel(DirectoryNameConvention convention) {
		switch (convention) {
		case DirectoryNameConvention::None:           return "None";
		case DirectoryNameConvention::TitleCase:      return "TitleCase";
		case DirectoryNameConvention::TitleDashCase:  return "Title-Case";
		case DirectoryNameConvention::TitleCamelCase: return "titleCase";
		case DirectoryNameConvention::TitleSnakeCase: return "title_case";
		case DirectoryNameConvention::TitleKebabCase: return "title-case";
		}
		return "TitleCase";
	}

	std::string LauncherSettings::ApplyDirectoryNameConvention(std::string_view name,
		DirectoryNameConvention convention)
	{
		if (convention == DirectoryNameConvention::None) {
			return std::string(name);
		}

		std::vector<std::string> words;
		std::string current;
		char previous = '\0';
		for (char raw : name) {
			const unsigned char c = static_cast<unsigned char>(raw);
			if (!std::isalnum(c)) {
				if (!current.empty()) {
					words.push_back(current);
					current.clear();
				}
				previous = '\0';
				continue;
			}

			const bool splitCamel = !current.empty()
				&& std::islower(static_cast<unsigned char>(previous))
				&& std::isupper(c);
			if (splitCamel) {
				words.push_back(current);
				current.clear();
			}
			current.push_back(raw);
			previous = raw;
		}
		if (!current.empty()) {
			words.push_back(current);
		}

		auto titleWord = [](std::string word) {
			if (word.empty()) return word;
			for (char& ch : word) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
			return word;
		};
		auto lowerWord = [](std::string word) {
			for (char& ch : word) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			return word;
		};

		std::string result;
		const char separator =
			(convention == DirectoryNameConvention::TitleDashCase) ? '-' :
			(convention == DirectoryNameConvention::TitleSnakeCase) ? '_' :
			(convention == DirectoryNameConvention::TitleKebabCase) ? '-' : '\0';

		for (std::size_t i = 0; i < words.size(); ++i) {
			std::string word;
			if (convention == DirectoryNameConvention::TitleCamelCase) {
				word = (i == 0) ? lowerWord(words[i]) : titleWord(words[i]);
			}
			else if (convention == DirectoryNameConvention::TitleSnakeCase
				|| convention == DirectoryNameConvention::TitleKebabCase) {
				word = lowerWord(words[i]);
			}
			else {
				word = titleWord(words[i]);
			}

			if (!result.empty() && separator != '\0') {
				result.push_back(separator);
			}
			result += word;
		}

		return result;
	}

	void LauncherSettings::Load() {
		const std::string path = GetSettingsPath();
		MigrateLegacyLauncherFile(
			Path::Combine(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData),
				"Funke", "launcher_settings.json"),
			path);
		if (!File::Exists(path)) return;

		Json::Value root;
		std::string parseError;
		const std::string text = File::ReadAllText(path);
		if (!Json::TryParse(text, root, &parseError) || !root.IsObject()) {
			FNK_CORE_WARN_TAG("Launcher", "Failed to parse '{}': {}", path, parseError);
			return;
		}

		if (const Json::Value* v = root.FindMember("defaultProjectsLocation")) {
			DefaultProjectsLocation = v->AsStringOr();
		}
		if (const Json::Value* v = root.FindMember("sortMode")) {
			int mode = v->AsIntOr(static_cast<int>(SortMode::LastOpened));
			if (mode >= 0 && mode <= static_cast<int>(SortMode::CreatedAt)) {
				Sort = static_cast<SortMode>(mode);
			}
		}
		if (const Json::Value* v = root.FindMember("sortReverse")) {
			SortReverse = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("projectView")) {
			ProjectView = v->AsIntOr(0) == 1 ? ProjectViewMode::Grid : ProjectViewMode::List;
		}
		if (const Json::Value* v = root.FindMember("fontScale")) {
			int scale = v->AsIntOr(static_cast<int>(FontScale::Auto));
			if (scale >= 0 && scale <= static_cast<int>(FontScale::P200)) {
				Scale = static_cast<FontScale>(scale);
			}
		}
		if (const Json::Value* v = root.FindMember("themeSetting")) {
			int setting = v->AsIntOr(static_cast<int>(ThemeSetting::Auto));
			if (setting >= 0 && setting <= static_cast<int>(ThemeSetting::Light)) {
				Theme = static_cast<ThemeSetting>(setting);
			}
		}
		if (const Json::Value* v = root.FindMember("directoryNameConvention")) {
			int convention = v->AsIntOr(static_cast<int>(DirectoryNameConvention::TitleCase));
			if (convention >= 0 && convention <= static_cast<int>(DirectoryNameConvention::TitleKebabCase)) {
				DirectoryConvention = static_cast<DirectoryNameConvention>(convention);
			}
		}
		if (const Json::Value* v = root.FindMember("languageAuto")) {
			LanguageAuto = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("assetLibraryTrustAck"); v && v->IsArray()) {
			for (const auto& item : v->GetArray()) {
				if (item.IsString()) AssetLibraryTrustAck[item.AsStringOr("")] = true;
			}
		}
	}

	void LauncherSettings::Save() const {
		const std::string path = GetSettingsPath();

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

		Json::Value root = Json::Value::MakeObject();
		root.AddMember("defaultProjectsLocation", DefaultProjectsLocation);
		root.AddMember("sortMode", Json::Value(static_cast<int>(Sort)));
		root.AddMember("sortReverse", Json::Value(SortReverse));
		root.AddMember("projectView", Json::Value(static_cast<int>(ProjectView)));
		root.AddMember("fontScale", Json::Value(static_cast<int>(Scale)));
		root.AddMember("themeSetting", Json::Value(static_cast<int>(Theme)));
		root.AddMember("directoryNameConvention", Json::Value(static_cast<int>(DirectoryConvention)));
		root.AddMember("languageAuto", Json::Value(LanguageAuto));
		{
			Json::Value ack = Json::Value::MakeArray();
			for (const auto& [url, acked] : AssetLibraryTrustAck) {
				if (acked) ack.Append(Json::Value(url));
			}
			root.AddMember("assetLibraryTrustAck", std::move(ack));
		}
		if (!File::WriteAllText(path, Json::Stringify(root, true))) {
			FNK_CORE_WARN_TAG("Launcher", "Failed to write launcher settings to '{}'", path);
		}
	}

}
