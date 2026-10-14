#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Index {
	struct IndexProject;
}

namespace Index::Cli {

	constexpr int k_ExitOk = 0;
	constexpr int k_ExitFailure = 1;
	constexpr int k_ExitUsage = 2;

	// Options may appear anywhere after the command: `--name value`, `--name=value`, `-n value`.
	class Args {
	public:
		explicit Args(std::vector<std::string> tokens);

		std::optional<std::string> Option(std::string_view name, std::string_view shortName = {});
		bool Flag(std::string_view name, std::string_view shortName = {});
		std::optional<std::string> Positional();
		std::vector<std::string> RemainingPositionals();
		bool Empty() const;

		// Fails on anything no command consumed, so typos never pass silently.
		bool Finish(std::string& outError) const;

	private:
		std::optional<std::size_t> FindOption(std::string_view name, std::string_view shortName, bool& outInlineValue) const;

		std::vector<std::string> m_Tokens;
		std::vector<bool> m_Used;
	};

	struct GlobalOptions {
		bool Json = false;
		bool Verbose = false;
		bool Quiet = false;
		// Read before the command runs: a command's Positional() would otherwise take the option's value.
		std::optional<std::string> ProjectRoot;
	};
	GlobalOptions& Options();

	// Results go to stdout; diagnostics and progress to stderr so `--json` output stays parseable.
	void Out(std::string_view text);
	void Note(std::string_view text);
	void Warn(std::string_view text);
	int Fail(std::string_view text, int exitCode = k_ExitFailure);

	// Engine log lines go to stderr: warnings and errors by default, everything with --verbose.
	void ConfigureEngineLogging();

	// Walks up from `start` to the first folder holding project.json.
	std::filesystem::path FindProjectRoot(const std::filesystem::path& start);

	// --project <dir> or the project containing the working directory. Becomes the engine's current
	// project so the asset registry resolves GUIDs against it.
	IndexProject* OpenProject(Args& args, std::string& outError);

	// Accepts an absolute path, a path relative to the working directory, or one relative to the project root.
	std::filesystem::path ResolveProjectPath(const IndexProject& project, const std::string& value);

	std::string ToLower(std::string_view value);
	std::string JsonQuote(std::string_view value);

	// Sibling tool of this exe (repo layout bin/<cfg>/<Tool>/, or next to it in an SDK bundle).
	std::filesystem::path FindSiblingExecutable(std::string_view folderName, std::string_view exeStem);

} // namespace Index::Cli
