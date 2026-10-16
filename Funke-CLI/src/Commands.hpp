#pragma once
#include "Cli.hpp"

#include <span>
#include <string_view>

namespace Funke::Cli {

	struct Command {
		std::string_view Name;
		std::string_view Summary;
		// Full help text printed by `funke help <command>`.
		std::string_view Usage;
		int (*Run)(Args& args);
	};

	std::span<const Command> AllCommands();
	const Command* FindCommand(std::string_view name);

	int RunCreate(Args& args);
	int RunInfo(Args& args);
	int RunBuild(Args& args);
	int RunPlay(Args& args);
	int RunOpen(Args& args);
	int RunConvert(Args& args);
	int RunValidate(Args& args);
	int RunAssets(Args& args);
	int RunSprite(Args& args);
	int RunPrefab(Args& args);
	int RunPackage(Args& args);
	int RunSetupPath(Args& args);

} // namespace Funke::Cli
