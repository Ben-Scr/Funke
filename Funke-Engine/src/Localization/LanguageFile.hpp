#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

// Internal to Localization.cpp and GameLocalization.cpp: the shared <code>.json format and OS locale probing.
namespace Funke::LanguageFile {

	using StringTable = std::unordered_map<std::string, std::string>;

	struct Meta {
		std::string Code;
		std::string DisplayName;
	};

	// {"meta": {"code", "displayName", ...}, "strings": {key: text}}. Either output may be null; missing meta
	// fields stay empty. False (with a warning) when the file is not a JSON object, or strings were requested
	// and there is no "strings" object. Only string values are loaded.
	bool Read(const std::filesystem::path& file, Meta* outMeta, StringTable* outStrings);

	// "de-DE" / "de_DE.UTF-8" -> "de".
	std::string NormalizeLocaleTag(std::string_view raw);

	// The OS UI locale as the platform reports it ("de-DE"); LC_ALL wins on desktop POSIX.
	std::string ProbeOsLocale();

}
