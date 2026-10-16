#include <pch.hpp>
#include "Localization/LanguageFile.hpp"

#include "Core/Log.hpp"
#include "Core/Platform.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <cctype>
#include <cstdlib>

namespace Funke::LanguageFile {

	bool Read(const std::filesystem::path& file, Meta* outMeta, StringTable* outStrings) {
		const std::string pathText = Path::ToUtf8(file);
		const std::string text = File::ReadAllText(pathText);
		if (text.empty()) return false;

		std::string parseError;
		Json::Value root = Json::Parse(text, &parseError);
		if (!root.IsObject()) {
			FNK_CORE_WARN_TAG("Localization", "Failed to parse '{}': {}", pathText, parseError);
			return false;
		}

		if (outMeta) {
			*outMeta = {};
			if (const Json::Value* meta = root.FindMember("meta"); meta && meta->IsObject()) {
				if (const Json::Value* code = meta->FindMember("code")) outMeta->Code = code->AsStringOr({});
				if (const Json::Value* name = meta->FindMember("displayName")) outMeta->DisplayName = name->AsStringOr({});
			}
		}

		if (outStrings) {
			const Json::Value* strings = root.FindMember("strings");
			if (!strings || !strings->IsObject()) {
				FNK_CORE_WARN_TAG("Localization", "'{}' has no 'strings' object", pathText);
				return false;
			}
			outStrings->clear();
			for (const auto& [key, value] : strings->GetObject()) {
				if (value.IsString()) outStrings->emplace(key, value.AsStringOr({}));
			}
		}
		return true;
	}

	std::string NormalizeLocaleTag(std::string_view raw) {
		std::string out;
		out.reserve(raw.size());
		for (char c : raw) {
			if (c == '-' || c == '_' || c == '.') break;
			out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return out;
	}

	std::string ProbeOsLocale() {
		// SDL reads the right source per OS (GetUserPreferredUILanguages, NSLocale, LANG, the Android
		// configuration, navigator.languages).
		std::string locale = Platform::GetPreferredLocale();
#if FNK_PLATFORM_POSIX
		// LC_ALL / LANG still win for desktop POSIX users who override the session language.
		if (const char* env = std::getenv("LC_ALL"); env && *env) locale = env;
#endif
		return locale;
	}

}
