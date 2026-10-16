#include <pch.hpp>
#include "Localization/GameLocalization.hpp"

#include "Core/Log.hpp"
#include "Localization/LanguageFile.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace Index::GameLocalization {

	namespace {

		constexpr std::string_view k_PreferredFallback = "en";

		struct Tables {
			std::vector<LanguageInfo> Languages;
			std::unordered_map<std::string, std::filesystem::path> Files;
			std::string Current;
			std::string Fallback;
			// Empty while Current is the fallback; lookups fall through to FallbackStrings.
			LanguageFile::StringTable Active;
			LanguageFile::StringTable FallbackStrings;

			bool SameContent(const Tables& other) const {
				return Languages == other.Languages && Current == other.Current && Fallback == other.Fallback
					&& Active == other.Active && FallbackStrings == other.FallbackStrings;
			}
		};

		struct State {
			std::filesystem::path Directory;
			Tables Data;
			// Presence means "already warned" for this table set.
			std::unordered_map<std::string, std::string> MissingKeys;
			bool WarnedNoTables = false;
			std::uint32_t Revision = 0;
			SavedLanguageProvider Provider;
			std::unordered_map<ChangeCallbackHandle, std::function<void()>> Callbacks;
			ChangeCallbackHandle NextHandle = 1;
		};

		State& S() {
			static State s;
			return s;
		}

		// Lowercase with '_' as '-', cut at the ".UTF-8" / "@modifier" tail.
		std::string NormalizeCode(std::string_view raw) {
			std::string out;
			out.reserve(raw.size());
			for (char c : raw) {
				if (c == '.' || c == '@') break;
				out.push_back(c == '_' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			}
			return out;
		}

		std::string_view BareLanguage(std::string_view normalized) {
			return normalized.substr(0, normalized.find('-'));
		}

		void Scan(const std::filesystem::path& directory, Tables& out) {
			std::error_code ec;
			if (directory.empty() || !std::filesystem::is_directory(directory, ec)) return;

			std::vector<std::filesystem::path> files;
			for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code entryEc;
				if (!it->is_regular_file(entryEc)) continue;
				std::string extension = Path::ToUtf8(it->path().extension());
				std::transform(extension.begin(), extension.end(), extension.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (extension == ".json") files.push_back(it->path());
			}
			// Directory order is unspecified; sorting keeps duplicate-code resolution stable.
			std::sort(files.begin(), files.end());

			for (const std::filesystem::path& file : files) {
				LanguageFile::Meta meta;
				if (!LanguageFile::Read(file, &meta, nullptr)) continue;
				std::string code = meta.Code.empty() ? Path::ToUtf8(file.stem()) : std::move(meta.Code);
				if (out.Files.contains(code)) {
					IDX_CORE_WARN_TAG("Localization", "'{}' declares language '{}' a second time; the file is ignored.",
						Path::ToUtf8(file), code);
					continue;
				}
				out.Files.emplace(code, file);
				std::string displayName = meta.DisplayName.empty() ? code : std::move(meta.DisplayName);
				out.Languages.push_back({ std::move(code), std::move(displayName) });
			}
		}

		std::string PickFallback(const Tables& tables) {
			if (tables.Files.contains(std::string(k_PreferredFallback))) return std::string(k_PreferredFallback);
			std::string first;
			for (const auto& [code, path] : tables.Files) {
				if (first.empty() || code < first) first = code;
			}
			return first;
		}

		void SortLanguages(std::vector<LanguageInfo>& languages, const std::string& fallback) {
			std::sort(languages.begin(), languages.end(), [&](const LanguageInfo& a, const LanguageInfo& b) {
				if (a.Code == fallback) return b.Code != fallback;
				if (b.Code == fallback) return false;
				if (a.DisplayName != b.DisplayName) return a.DisplayName < b.DisplayName;
				return a.Code < b.Code;
			});
		}

		bool LoadActive(Tables& tables, const std::string& code) {
			if (code == tables.Fallback) {
				tables.Active.clear();
				tables.Current = code;
				return true;
			}
			const auto it = tables.Files.find(code);
			if (it == tables.Files.end()) return false;
			LanguageFile::StringTable strings;
			if (!LanguageFile::Read(it->second, nullptr, &strings)) return false;
			tables.Active = std::move(strings);
			tables.Current = code;
			return true;
		}

		// keep: language to stay on while it still exists; null picks the start language.
		Tables Build(const std::filesystem::path& directory, const std::string* keep) {
			Tables tables;
			Scan(directory, tables);
			if (tables.Languages.empty()) return tables;

			tables.Fallback = PickFallback(tables);
			if (!LanguageFile::Read(tables.Files.at(tables.Fallback), nullptr, &tables.FallbackStrings)) {
				IDX_CORE_WARN_TAG("Localization", "Fallback language '{}' could not be loaded; its keys show as raw identifiers.",
					tables.Fallback);
			}
			SortLanguages(tables.Languages, tables.Fallback);

			std::string wanted;
			if (keep && tables.Files.contains(*keep)) {
				wanted = *keep;
			}
			else {
				const SavedLanguageProvider& provider = S().Provider;
				wanted = ChooseStartLanguage(provider ? provider() : std::string{}, LanguageFile::ProbeOsLocale(),
					tables.Languages, tables.Fallback);
			}
			if (!LoadActive(tables, wanted)) {
				IDX_CORE_WARN_TAG("Localization", "Language '{}' could not be loaded; using '{}'.", wanted, tables.Fallback);
				LoadActive(tables, tables.Fallback);
			}
			return tables;
		}

		void NotifyChanged() {
			State& s = S();
			++s.Revision;
			// Copied: a callback may register or unregister callbacks.
			std::vector<std::function<void()>> callbacks;
			callbacks.reserve(s.Callbacks.size());
			for (const auto& [handle, callback] : s.Callbacks) callbacks.push_back(callback);
			for (const auto& callback : callbacks) {
				if (callback) callback();
			}
		}

		bool Commit(Tables&& next) {
			State& s = S();
			if (next.SameContent(s.Data)) {
				s.Data.Files = std::move(next.Files);
				return false;
			}
			s.Data = std::move(next);
			s.MissingKeys.clear();
			s.WarnedNoTables = false;
			NotifyChanged();
			return true;
		}

	}

	bool Load(const std::filesystem::path& directory) {
		State& s = S();
		s.Directory = directory;
		const bool changed = Commit(Build(directory, nullptr));
		if (changed && !s.Data.Languages.empty()) {
			IDX_CORE_INFO_TAG("Localization", "Game localization: {} language(s) in '{}', current '{}', fallback '{}'.",
				s.Data.Languages.size(), Path::ToUtf8(directory), s.Data.Current, s.Data.Fallback);
		}
		return !s.Data.Languages.empty();
	}

	bool Reload() {
		State& s = S();
		const std::string keep = s.Data.Current;
		const bool changed = Commit(Build(s.Directory, keep.empty() ? nullptr : &keep));
		if (changed) {
			IDX_CORE_INFO_TAG("Localization", "Game localization reloaded: {} language(s), current '{}'.",
				s.Data.Languages.size(), s.Data.Current);
		}
		return !s.Data.Languages.empty();
	}

	void Unload() {
		S().Directory.clear();
		Commit(Tables{});
	}

	bool IsLoaded() {
		return !S().Data.Languages.empty();
	}

	const std::filesystem::path& GetDirectory() {
		return S().Directory;
	}

	const std::string& Get(std::string_view key) {
		State& s = S();
		const std::string keyText(key);
		if (auto it = s.Data.Active.find(keyText); it != s.Data.Active.end()) return it->second;
		if (auto it = s.Data.FallbackStrings.find(keyText); it != s.Data.FallbackStrings.end()) return it->second;

		auto [it, inserted] = s.MissingKeys.emplace(keyText, keyText);
		if (inserted) {
			if (!s.Data.Languages.empty()) {
				IDX_CORE_WARN_TAG("Localization", "Missing key '{}' (language '{}', fallback '{}').",
					keyText, s.Data.Current, s.Data.Fallback);
			}
			else if (!s.WarnedNoTables) {
				s.WarnedNoTables = true;
				IDX_CORE_WARN_TAG("Localization",
					"No language files in Assets/Localization; '{}' and every other key show as raw identifiers.", keyText);
			}
		}
		return it->second;
	}

	bool Has(std::string_view key) {
		const State& s = S();
		const std::string keyText(key);
		return s.Data.Active.contains(keyText) || s.Data.FallbackStrings.contains(keyText);
	}

	const std::vector<LanguageInfo>& GetAvailableLanguages() {
		return S().Data.Languages;
	}

	bool IsLanguageAvailable(std::string_view code) {
		return S().Data.Files.contains(std::string(code));
	}

	const std::string& GetCurrentLanguage() {
		return S().Data.Current;
	}

	const std::string& GetFallbackLanguage() {
		return S().Data.Fallback;
	}

	std::string GetSystemLanguage() {
		std::string tag = LanguageFile::NormalizeLocaleTag(LanguageFile::ProbeOsLocale());
		return tag.empty() ? std::string(k_PreferredFallback) : tag;
	}

	bool SetLanguage(std::string_view code) {
		State& s = S();
		const std::string codeText(code);
		if (!s.Data.Files.contains(codeText)) {
			IDX_CORE_WARN_TAG("Localization", "SetLanguage('{}') ignored: no such language in Assets/Localization.", codeText);
			return false;
		}
		if (codeText == s.Data.Current) return true;
		if (!LoadActive(s.Data, codeText)) {
			IDX_CORE_WARN_TAG("Localization", "SetLanguage('{}') failed: the file could not be loaded.", codeText);
			return false;
		}
		s.MissingKeys.clear();
		IDX_CORE_INFO_TAG("Localization", "Game language switched to '{}'.", codeText);
		NotifyChanged();
		return true;
	}

	std::uint32_t GetRevision() {
		return S().Revision;
	}

	std::string MatchLanguage(std::string_view locale, const std::vector<LanguageInfo>& languages) {
		const std::string wanted = NormalizeCode(locale);
		if (wanted.empty()) return {};
		const std::string_view bare = BareLanguage(wanted);

		for (const LanguageInfo& language : languages) {
			if (NormalizeCode(language.Code) == wanted) return language.Code;
		}
		for (const LanguageInfo& language : languages) {
			if (NormalizeCode(language.Code) == bare) return language.Code;
		}
		for (const LanguageInfo& language : languages) {
			const std::string code = NormalizeCode(language.Code);
			if (BareLanguage(code) == bare) return language.Code;
		}
		return {};
	}

	std::string ChooseStartLanguage(std::string_view saved, std::string_view osLocale,
		const std::vector<LanguageInfo>& languages, std::string_view fallback)
	{
		if (!saved.empty()) {
			for (const LanguageInfo& language : languages) {
				if (language.Code == saved) return language.Code;
			}
		}
		if (std::string match = MatchLanguage(osLocale, languages); !match.empty()) return match;
		return std::string(fallback);
	}

	void SetSavedLanguageProvider(SavedLanguageProvider provider) {
		State& s = S();
		s.Provider = std::move(provider);
		if (!s.Provider || s.Data.Languages.empty()) return;
		const std::string saved = s.Provider();
		if (!saved.empty() && saved != s.Data.Current && s.Data.Files.contains(saved)) SetLanguage(saved);
	}

	ChangeCallbackHandle RegisterChangeCallback(std::function<void()> callback) {
		State& s = S();
		const ChangeCallbackHandle handle = s.NextHandle++;
		s.Callbacks.emplace(handle, std::move(callback));
		return handle;
	}

	void UnregisterChangeCallback(ChangeCallbackHandle handle) {
		S().Callbacks.erase(handle);
	}

}
