#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Funke {
	class LauncherSettings {
	public:
		enum class SortMode : uint8_t {
			LastOpened = 0,
			Name = 1,
			CreatedAt = 2,
		};

		enum class FontScale : uint8_t {
			Auto = 0,
			P75 = 1,
			P100 = 2,
			P125 = 3,
			P150 = 4,
			P175 = 5,
			P200 = 6,
		};

		enum class ThemeSetting : uint8_t {
			Auto = 0,
			Dark = 1,
			Light = 2,
		};

		enum class DirectoryNameConvention : uint8_t {
			None = 0,
			TitleCase,
			TitleDashCase,
			TitleCamelCase,
			TitleSnakeCase,
			TitleKebabCase,
		};

		enum class ProjectViewMode : uint8_t { List = 0, Grid = 1 };

		void Load();
		void Save() const;
		static const char* DirectoryNameConventionLabel(DirectoryNameConvention convention);
		static std::string ApplyDirectoryNameConvention(std::string_view name, DirectoryNameConvention convention);

		SortMode Sort = SortMode::LastOpened;
		bool SortReverse = false;
		FontScale Scale = FontScale::Auto;
		ThemeSetting Theme = ThemeSetting::Auto;
		DirectoryNameConvention DirectoryConvention = DirectoryNameConvention::TitleCase;
		// Preserve follow-OS intent independently of the resolved language.
		bool LanguageAuto = false;
		std::string DefaultProjectsLocation;
		// Trust is persisted per source URL.
		std::unordered_map<std::string, bool> AssetLibraryTrustAck;
		ProjectViewMode ProjectView = ProjectViewMode::List;

	private:
		static std::string GetSettingsPath();
	};
}
