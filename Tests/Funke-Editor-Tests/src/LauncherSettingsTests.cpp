#include <doctest/doctest.h>
#include "Systems/LauncherSettings.hpp"

using namespace Funke;

TEST_CASE("Launcher settings preserve directory naming conventions") {
	using Convention = LauncherSettings::DirectoryNameConvention;
	const std::string name = "my Game-demo_42";
	CHECK(LauncherSettings::ApplyDirectoryNameConvention(name, Convention::None) == name);
	CHECK(LauncherSettings::ApplyDirectoryNameConvention(name, Convention::TitleCase) == "MyGameDemo42");
	CHECK(LauncherSettings::ApplyDirectoryNameConvention(name, Convention::TitleDashCase) == "My-Game-Demo-42");
	CHECK(LauncherSettings::ApplyDirectoryNameConvention(name, Convention::TitleCamelCase) == "myGameDemo42");
	CHECK(LauncherSettings::ApplyDirectoryNameConvention(name, Convention::TitleSnakeCase) == "my_game_demo_42");
	CHECK(LauncherSettings::ApplyDirectoryNameConvention(name, Convention::TitleKebabCase) == "my-game-demo-42");
	CHECK(LauncherSettings::ApplyDirectoryNameConvention("myGame", Convention::TitleKebabCase) == "my-game");
	CHECK(LauncherSettings::ApplyDirectoryNameConvention(" --- ", Convention::TitleCase).empty());
}
