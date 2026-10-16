#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	// The running game's string tables, read from <project>/Assets/Localization/<code>.json (same format as the
	// launcher's tables). Separate from Funke::Localization, which holds the launcher's own `launcher.*` keys.
	namespace GameLocalization {

		struct LanguageInfo {
			std::string Code;
			std::string DisplayName;

			bool operator==(const LanguageInfo&) const = default;
		};

		// Scans every *.json in the directory and activates the start language (see ChooseStartLanguage).
		// The directory is remembered even when it holds no language, so Reload() picks up files added later.
		// Returns false when no language was found.
		FUNKE_API bool Load(const std::filesystem::path& directory);
		// Re-reads the remembered directory and keeps the current language while it still exists (editor hot reload).
		// Change callbacks fire only when something actually changed.
		FUNKE_API bool Reload();
		FUNKE_API void Unload();
		FUNKE_API bool IsLoaded();
		FUNKE_API const std::filesystem::path& GetDirectory();

		// Current language, then the fallback language, then the key itself (a missing key is logged once).
		// The reference is valid until the next language change or reload.
		FUNKE_API const std::string& Get(std::string_view key);
		FUNKE_API bool Has(std::string_view key);

		// Fallback first, then by display name.
		FUNKE_API const std::vector<LanguageInfo>& GetAvailableLanguages();
		FUNKE_API bool IsLanguageAvailable(std::string_view code);
		FUNKE_API const std::string& GetCurrentLanguage();
		// "en" when the game has an en.json, otherwise its first language by code.
		FUNKE_API const std::string& GetFallbackLanguage();
		// The OS UI language as a bare lowercase code ("de"), whether or not the game has it; "en" when unknown.
		FUNKE_API std::string GetSystemLanguage();

		// Switches immediately; false (and no change) for a language the game doesn't have. Not persisted here:
		// the C# Localization.SetLanguage stores the player's choice in PlayerPrefs.
		FUNKE_API bool SetLanguage(std::string_view code);

		// Bumped by every change of language or tables; consumers re-resolve when it differs from what they applied.
		FUNKE_API std::uint32_t GetRevision();

		// Best language for an OS locale ("de-DE", "pt_BR.UTF-8"): exact code, then the bare language, then a
		// regional variant of it. Empty when none fits.
		FUNKE_API std::string MatchLanguage(std::string_view locale, const std::vector<LanguageInfo>& languages);
		// Saved choice if the game has it, else the OS locale's match, else the fallback.
		FUNKE_API std::string ChooseStartLanguage(std::string_view saved, std::string_view osLocale,
			const std::vector<LanguageInfo>& languages, std::string_view fallback);

		// Where the player's saved choice comes from (scripting installs one that reads PlayerPrefs).
		// Installing one while tables are loaded applies the saved choice right away.
		using SavedLanguageProvider = std::function<std::string()>;
		FUNKE_API void SetSavedLanguageProvider(SavedLanguageProvider provider);

		using ChangeCallbackHandle = std::uint32_t;
		FUNKE_API ChangeCallbackHandle RegisterChangeCallback(std::function<void()> callback);
		FUNKE_API void UnregisterChangeCallback(ChangeCallbackHandle handle);

		// std::vformat over Get(key); {0}, {1} placeholders. A malformed template is returned unformatted.
		template <typename... Args>
		std::string Format(std::string_view key, Args&&... args) {
			const std::string& pattern = Get(key);
			try {
				return std::vformat(pattern, std::make_format_args(args...));
			}
			catch (const std::format_error&) {
				return pattern;
			}
		}

	}

}
