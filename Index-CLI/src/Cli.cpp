#include "pch.hpp"
#include "Cli.hpp"

#include "Core/Log.hpp"
#include "Project/IndexProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>

namespace Index::Cli {

	Args::Args(std::vector<std::string> tokens)
		: m_Tokens(std::move(tokens)), m_Used(m_Tokens.size(), false) {}

	std::optional<std::size_t> Args::FindOption(std::string_view name, std::string_view shortName, bool& outInlineValue) const {
		const std::string longForm = "--" + std::string(name);
		const std::string shortForm = shortName.empty() ? std::string() : "-" + std::string(shortName);
		for (std::size_t i = 0; i < m_Tokens.size(); ++i) {
			if (m_Used[i]) continue;
			const std::string& token = m_Tokens[i];
			if (token == "--") break;
			if (token == longForm || (!shortForm.empty() && token == shortForm)) {
				outInlineValue = false;
				return i;
			}
			if (token.size() > longForm.size() && token.compare(0, longForm.size(), longForm) == 0
				&& token[longForm.size()] == '=') {
				outInlineValue = true;
				return i;
			}
		}
		return std::nullopt;
	}

	std::optional<std::string> Args::Option(std::string_view name, std::string_view shortName) {
		bool inlineValue = false;
		const std::optional<std::size_t> index = FindOption(name, shortName, inlineValue);
		if (!index) return std::nullopt;
		m_Used[*index] = true;
		if (inlineValue) return m_Tokens[*index].substr(name.size() + 3);
		const auto isOptionToken = [](const std::string& token) {
			return token.size() > 1 && token[0] == '-' && !std::isdigit(static_cast<unsigned char>(token[1]));
		};
		if (*index + 1 >= m_Tokens.size() || m_Used[*index + 1] || isOptionToken(m_Tokens[*index + 1])) {
			// Leave it unconsumed-looking so Finish reports the missing value.
			m_Used[*index] = false;
			return std::nullopt;
		}
		m_Used[*index + 1] = true;
		return m_Tokens[*index + 1];
	}

	bool Args::Flag(std::string_view name, std::string_view shortName) {
		bool inlineValue = false;
		const std::optional<std::size_t> index = FindOption(name, shortName, inlineValue);
		if (!index || inlineValue) return false;
		m_Used[*index] = true;
		return true;
	}

	std::optional<std::string> Args::Positional() {
		bool afterSeparator = false;
		for (std::size_t i = 0; i < m_Tokens.size(); ++i) {
			if (m_Tokens[i] == "--" && !afterSeparator) {
				afterSeparator = true;
				m_Used[i] = true;
				continue;
			}
			if (m_Used[i]) continue;
			const std::string& token = m_Tokens[i];
			// A lone "-" or a negative number is a value, not an option.
			const bool looksLikeOption = !afterSeparator && token.size() > 1 && token[0] == '-'
				&& !std::isdigit(static_cast<unsigned char>(token[1]));
			if (looksLikeOption) continue;
			m_Used[i] = true;
			return token;
		}
		return std::nullopt;
	}

	std::vector<std::string> Args::RemainingPositionals() {
		std::vector<std::string> values;
		while (std::optional<std::string> value = Positional()) values.push_back(std::move(*value));
		return values;
	}

	bool Args::Empty() const {
		return std::all_of(m_Used.begin(), m_Used.end(), [](bool used) { return used; });
	}

	bool Args::Finish(std::string& outError) const {
		for (std::size_t i = 0; i < m_Tokens.size(); ++i) {
			if (m_Used[i] || m_Tokens[i] == "--") continue;
			const std::string& token = m_Tokens[i];
			if (token.size() > 1 && token[0] == '-') {
				const bool hasValueSlot = i + 1 < m_Tokens.size();
				outError = hasValueSlot ? "unknown option '" + token + "'" : "unknown option or missing value: '" + token + "'";
			}
			else {
				outError = "unexpected argument '" + token + "'";
			}
			return false;
		}
		return true;
	}

	GlobalOptions& Options() {
		static GlobalOptions s_Options;
		return s_Options;
	}

	void Out(std::string_view text) {
		std::fwrite(text.data(), 1, text.size(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	}

	void Note(std::string_view text) {
		if (Options().Quiet) return;
		std::fwrite(text.data(), 1, text.size(), stderr);
		std::fputc('\n', stderr);
		std::fflush(stderr);
	}

	void Warn(std::string_view text) {
		std::fprintf(stderr, "warning: %.*s\n", static_cast<int>(text.size()), text.data());
		std::fflush(stderr);
	}

	int Fail(std::string_view text, int exitCode) {
		std::fprintf(stderr, "error: %.*s\n", static_cast<int>(text.size()), text.data());
		std::fflush(stderr);
		return exitCode;
	}

	void ConfigureEngineLogging() {
		auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
		sink->set_level(Options().Verbose ? spdlog::level::trace : spdlog::level::warn);
		sink->set_pattern("%^[%n] %l:%$ %v");
		for (const std::shared_ptr<spdlog::logger>& logger :
			{ Log::GetCoreLogger(), Log::GetClientLogger(), Log::GetEditorConsoleLogger() }) {
			if (!logger) continue;
			std::vector<spdlog::sink_ptr>& sinks = logger->sinks();
			// The stdout sink would interleave engine chatter with command output.
			sinks.erase(std::remove_if(sinks.begin(), sinks.end(), [](const spdlog::sink_ptr& existing) {
				return std::dynamic_pointer_cast<spdlog::sinks::stdout_color_sink_mt>(existing) != nullptr;
			}), sinks.end());
			sinks.push_back(sink);
		}
	}

	std::filesystem::path FindProjectRoot(const std::filesystem::path& start) {
		std::error_code error;
		std::filesystem::path current = std::filesystem::absolute(start, error);
		if (error) return {};
		while (!current.empty()) {
			if (std::filesystem::is_regular_file(current / "project.json", error)) return current;
			const std::filesystem::path parent = current.parent_path();
			if (parent == current) break;
			current = parent;
		}
		return {};
	}

	IndexProject* OpenProject(Args& args, std::string& outError) {
		std::filesystem::path root;
		if (const std::optional<std::string>& explicitRoot = Options().ProjectRoot) {
			std::filesystem::path candidate = Path::FromUtf8(*explicitRoot);
			// Accept the project.json itself as well as its folder.
			if (candidate.filename() == "project.json") candidate = candidate.parent_path();
			std::error_code error;
			root = std::filesystem::absolute(candidate, error);
			if (!std::filesystem::is_regular_file(root / "project.json", error)) {
				outError = "No project.json in " + Path::ToUtf8(root) + ".";
				return nullptr;
			}
		}
		else {
			root = FindProjectRoot(std::filesystem::current_path());
			if (root.empty()) {
				outError = "Not inside an Index project (no project.json here or above); pass --project <dir>.";
				return nullptr;
			}
		}

		auto project = std::make_unique<IndexProject>();
		std::string loadError;
		if (!IndexProject::TryLoad(Path::ToUtf8(root), *project, &loadError)) {
			outError = "Cannot load project at " + Path::ToUtf8(root) + (loadError.empty() ? "." : ": " + loadError);
			return nullptr;
		}
		ProjectManager::SetCurrentProject(std::move(project));
		return ProjectManager::GetCurrentProject();
	}

	std::filesystem::path ResolveProjectPath(const IndexProject& project, const std::string& value) {
		const std::filesystem::path path = Path::FromUtf8(value);
		if (path.is_absolute()) return path.lexically_normal();
		std::error_code error;
		const std::filesystem::path fromCwd = std::filesystem::absolute(path, error);
		if (!error && std::filesystem::exists(fromCwd, error)) return fromCwd.lexically_normal();
		return (Path::FromUtf8(project.RootDirectory) / path).lexically_normal();
	}

	std::string ToLower(std::string_view value) {
		std::string lower(value);
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return lower;
	}

	std::string JsonQuote(std::string_view value) {
		return Json::Stringify(Json::Value(std::string(value)), false);
	}

	std::filesystem::path FindSiblingExecutable(std::string_view folderName, std::string_view exeStem) {
#ifdef IDX_PLATFORM_WINDOWS
		const std::string fileName = std::string(exeStem) + ".exe";
#else
		const std::string fileName(exeStem);
#endif
		const std::filesystem::path exeDir = Path::FromUtf8(Path::ExecutableDir());
		std::error_code error;
		for (const std::filesystem::path& candidate : {
				exeDir / fileName,
				exeDir.parent_path() / std::string(folderName) / fileName,
				exeDir / std::string(folderName) / fileName }) {
			if (std::filesystem::is_regular_file(candidate, error)) return candidate.lexically_normal();
		}
		return {};
	}

} // namespace Index::Cli
