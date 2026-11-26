#include "Systems/ImGuiEditorLayer.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Project/ProjectManager.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>

namespace Funke {
	namespace {
		struct LogSourceLocation {
			std::string FilePath;
			std::string LinkText;
			int Line = 0;
			// Byte offsets of the "<path>:N" link substring in the message; console renderer styles that span inline as the clickable link.
			size_t MessageStart = 0;
			size_t MessageEnd = 0;
		};

		bool IsLineBoundary(std::string_view text, size_t pos) {
			return pos == std::string_view::npos
				|| pos >= text.size()
				|| text[pos] == '\n'
				|| text[pos] == '\r';
		}

		bool IsExtensionBoundary(std::string_view text, size_t pos) {
			if (pos >= text.size()) return true;
			const unsigned char c = static_cast<unsigned char>(text[pos]);
			return !std::isalnum(c) && text[pos] != '_';
		}

		bool StartsWithIgnoreCase(std::string_view text, size_t pos, std::string_view prefix) {
			if (pos + prefix.size() > text.size()) return false;
			for (size_t i = 0; i < prefix.size(); i++) {
				const unsigned char a = static_cast<unsigned char>(text[pos + i]);
				const unsigned char b = static_cast<unsigned char>(prefix[i]);
				if (std::tolower(a) != std::tolower(b)) {
					return false;
				}
			}
			return true;
		}

		void SkipWhitespace(std::string_view text, size_t& pos) {
			while (pos < text.size() && text[pos] != '\n' && text[pos] != '\r'
				&& std::isspace(static_cast<unsigned char>(text[pos]))) {
				pos++;
			}
		}

		bool ParsePositiveInt(std::string_view text, size_t& pos, int& outValue) {
			SkipWhitespace(text, pos);
			if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
				return false;
			}

			int value = 0;
			while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
				const int digit = text[pos] - '0';
				if (value > (std::numeric_limits<int>::max() - digit) / 10) return false;
				value = value * 10 + digit;
				pos++;
			}

			outValue = value;
			return value > 0;
		}

		bool TryParseLineNumber(std::string_view text, size_t pos, int& outLine, size_t* outEnd = nullptr) {
			if (pos < text.size() && (text[pos] == '"' || text[pos] == '\'')) ++pos;
			SkipWhitespace(text, pos);
			if (pos >= text.size() || IsLineBoundary(text, pos)) {
				return false;
			}

			if (text[pos] == '(') {
				pos++;
				if (!ParsePositiveInt(text, pos, outLine)) return false;
				if (pos < text.size() && text[pos] == ')') pos++;
				if (outEnd) *outEnd = pos;
				return true;
			}

			if (text[pos] == ':' || text[pos] == ',') {
				pos++;
				SkipWhitespace(text, pos);
			}

			if (StartsWithIgnoreCase(text, pos, "line")) {
				pos += 4;
				SkipWhitespace(text, pos);
			}

			if (!ParsePositiveInt(text, pos, outLine)) return false;
			if (outEnd) *outEnd = pos;
			return true;
		}

		std::string CanonicalFilePath(const std::filesystem::path& path) {
			std::error_code ec;
			if (!std::filesystem::is_regular_file(path, ec) || ec) {
				return {};
			}
			std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
			return (ec ? path : canonical).lexically_normal().make_preferred().string();
		}

		std::string ResolveLogSourceFileUncached(std::string rawPath) {
			if (rawPath.rfind("file:///", 0) == 0) {
				rawPath.erase(0, 8);
			}
			if (rawPath.empty()) {
				return {};
			}

			const std::filesystem::path sourcePath(rawPath);
			std::vector<std::filesystem::path> candidates;
			if (sourcePath.is_absolute()) {
				candidates.push_back(sourcePath);
			}
			else {
				candidates.push_back(std::filesystem::current_path() / sourcePath);
				if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
					if (!project->RootDirectory.empty()) candidates.emplace_back(std::filesystem::path(project->RootDirectory) / sourcePath);
					if (!project->ScriptsDirectory.empty()) candidates.emplace_back(std::filesystem::path(project->ScriptsDirectory) / sourcePath);
					if (!project->AssetsDirectory.empty()) candidates.emplace_back(std::filesystem::path(project->AssetsDirectory) / sourcePath);
				}
			}

			for (const std::filesystem::path& candidate : candidates) {
				if (std::string resolved = CanonicalFilePath(candidate); !resolved.empty()) {
					return resolved;
				}
			}

			return {};
		}

		std::string ResolveLogSourceFile(const std::string& rawPath) {
			static std::unordered_map<std::string, std::string> cache;
			static std::string cachedRoot;
			static auto refreshed = std::chrono::steady_clock::now();
			const auto now = std::chrono::steady_clock::now();
			const auto* project = ProjectManager::GetCurrentProject();
			const std::string root = project ? project->RootDirectory : "";
			if (root != cachedRoot || now - refreshed > std::chrono::seconds(2) || cache.size() >= 1024) {
				cache.clear();
				cachedRoot = root;
				refreshed = now;
			}
			if (const auto found = cache.find(rawPath); found != cache.end()) return found->second;
			return cache.emplace(rawPath, ResolveLogSourceFileUncached(rawPath)).first->second;
		}

		std::string ExtractRawSourcePath(std::string_view message, size_t extStart, size_t extEnd, size_t* outPathStart = nullptr) {
			size_t lineStart = message.rfind('\n', extStart);
			lineStart = (lineStart == std::string_view::npos) ? 0 : lineStart + 1;

			size_t start = std::string_view::npos;
			const size_t doubleQuote = message.rfind('"', extStart);
			const size_t singleQuote = message.rfind('\'', extStart);
			if (doubleQuote != std::string_view::npos && doubleQuote >= lineStart) {
				start = doubleQuote + 1;
			}
			if (singleQuote != std::string_view::npos && singleQuote >= lineStart
				&& (start == std::string_view::npos || singleQuote + 1 > start)) {
				start = singleQuote + 1;
			}

			const size_t inToken = message.rfind(" in ", extStart);
			if (start == std::string_view::npos && inToken != std::string_view::npos && inToken >= lineStart) {
				start = inToken + 4;
			}

			if (start == std::string_view::npos) {
				for (size_t i = lineStart; i + 2 < extStart; ++i) {
					if (std::isalpha(static_cast<unsigned char>(message[i])) && message[i + 1] == ':'
						&& (message[i + 2] == '/' || message[i + 2] == '\\')) { start = i; break; }
				}
			}
			if (start == std::string_view::npos) {
				start = lineStart;
				SkipWhitespace(message, start);
				while (start < extStart && message[start] == '[') {
					const size_t tagEnd = message.find(']', start + 1);
					if (tagEnd == std::string_view::npos || tagEnd >= extStart) break;
					start = tagEnd + 1;
					SkipWhitespace(message, start);
				}
			}

			while (start < extStart && std::isspace(static_cast<unsigned char>(message[start]))) {
				start++;
			}

			if (outPathStart) *outPathStart = start;
			return std::string(message.substr(start, extEnd - start));
		}

		std::optional<LogSourceLocation> FindLogSourceLocation(std::string_view message) {
			static constexpr std::array<std::string_view, 15> kScriptExtensions = {
				".cs", ".cpp", ".bolt", ".hpp", ".h", ".c", ".lua", ".py",
				".js", ".ts", ".json", ".xml", ".yaml", ".yml", ".shader"
			};

			for (size_t pos = 0; pos < message.size(); pos++) {
				for (std::string_view ext : kScriptExtensions) {
					if (!StartsWithIgnoreCase(message, pos, ext)) {
						continue;
					}
					const size_t extEnd = pos + ext.size();
					if (!IsExtensionBoundary(message, extEnd)) {
						continue;
					}

					int line = 0;
					size_t lineEndPos = 0;
					if (!TryParseLineNumber(message, extEnd, line, &lineEndPos)) {
						continue;
					}

					size_t pathStartPos = 0;
					std::string rawPath = ExtractRawSourcePath(message, pos, extEnd, &pathStartPos);
					if (rawPath.empty()) {
						continue;
					}

					std::filesystem::path rawExtPath(rawPath);
					std::string loweredExt = rawExtPath.extension().string();
					std::transform(loweredExt.begin(), loweredExt.end(), loweredExt.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (!ExternalEditor::IsScriptExtension(loweredExt)) {
						continue;
					}

					std::string resolved = ResolveLogSourceFile(rawPath);
					if (resolved.empty()) {
						continue;
					}

					LogSourceLocation location;
					location.FilePath = resolved;
					location.Line = line;
					location.LinkText = std::filesystem::path(resolved).filename().string()
						+ ":" + std::to_string(line);
					location.MessageStart = pathStartPos;
					location.MessageEnd = lineEndPos;
					return location;
				}
			}

			return std::nullopt;
		}

		bool IsLogEntryVisible(Log::Level level, bool showInfo, bool showWarn, bool showError) {
			if (level <= Log::Level::Info) return showInfo;
			if (level == Log::Level::Warn) return showWarn;
			return showError;
		}

		const char* GetLogLevelPrefix(Log::Level level) {
			if (level <= Log::Level::Info) return "[Info] ";
			if (level == Log::Level::Warn) return "[Warn] ";
			return "[Error] ";
		}

		// EditorIcons key matching the level — same icons as the filter buttons above the list.
		const char* GetLogLevelIconName(Log::Level level) {
			if (level <= Log::Level::Info) return "info";
			if (level == Log::Level::Warn) return "warning";
			return "error";
		}

		// Info lines stay body-text coloured (the row icon carries the level); only warnings/errors are tinted.
		ImVec4 GetLogLevelColor(Log::Level level) {
			if (level <= Log::Level::Info) return ImGui::GetStyleColorVec4(ImGuiCol_Text);
			if (level == Log::Level::Warn) return EditorUI::ToneColor(EditorUI::Tone::Warning);
			return EditorUI::ToneColor(EditorUI::Tone::Error);
		}

		// Log console view state. File-static so the panel's look can evolve without touching the layer header.
		std::string s_LogFilter;
		bool s_LogCollapse = false;

		struct LogMessageParts {
			std::string_view Source;
			std::string_view Body;
		};

		LogMessageParts SplitLogMessage(std::string_view message) {
			if (!message.empty() && message.front() == '[') {
				const size_t end = message.find(']');
				if (end > 1 && end < 64 && message.substr(0, end).find('\n') == std::string_view::npos) {
					size_t bodyStart = end + 1;
					while (bodyStart < message.size() && message[bodyStart] == ' ') ++bodyStart;
					return { message.substr(1, end - 1), message.substr(bodyStart) };
				}
			}
			return { {}, message };
		}

		void DrawLogCell(std::string_view text, float width, const ImVec4& color) {
			text = text.substr(0, text.find_first_of("\r\n"));
			if (text.empty() || width <= 0.0f) return;
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->PushClipRect(pos, ImVec2(pos.x + width, pos.y + ImGui::GetTextLineHeight()), true);
			const char* end = text.data() + text.size();
			const bool truncated = ImGui::CalcTextSize(text.data(), end).x > width;
			if (truncated) {
				const float ellipsisWidth = ImGui::CalcTextSize("...").x;
				ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), std::max(1.0f, width - ellipsisWidth),
					0.0f, text.data(), end, &end);
				const float textWidth = ImGui::CalcTextSize(text.data(), end).x;
				drawList->AddText(ImVec2(pos.x + textWidth, pos.y), ImGui::GetColorU32(color), "...");
			}
			drawList->AddText(pos, ImGui::GetColorU32(color), text.data(), end);
			drawList->PopClipRect();
		}

		void DrawLogSeverity(Log::Level level) {
			const float size = ImGui::GetTextLineHeight();
			const uint64_t icon = EditorIcons::Get(GetLogLevelIconName(level), static_cast<int>(size));
			if (icon) {
				ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(icon)), ImVec2(size, size),
					ImVec2(0, 1), ImVec2(1, 0));
			}
			else {
				ImGui::TextColored(GetLogLevelColor(level), "%s", level <= Log::Level::Info ? "i" : "!");
			}
		}

		// `needleLower` must already be lowercase; empty needle matches everything.
		bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needleLower) {
			if (needleLower.empty()) return true;
			if (needleLower.size() > haystack.size()) return false;
			const auto it = std::search(haystack.begin(), haystack.end(), needleLower.begin(), needleLower.end(),
				[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
			return it != haystack.end();
		}

		std::string ToLowerAscii(std::string_view text) {
			std::string out(text);
			for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return out;
		}

		EditorUI::Tone GetLogLevelTone(Log::Level level) {
			if (level <= Log::Level::Info) return EditorUI::Tone::Neutral;
			if (level == Log::Level::Warn) return EditorUI::Tone::Warning;
			return EditorUI::Tone::Error;
		}

		void LogLevelToggle(const char* id, const char* iconName, const char* label, int count, bool& shown, Log::Level level) {
			char tooltip[64];
			std::snprintf(tooltip, sizeof(tooltip), "%s %s (%d)", shown ? "Hide" : "Show", label, count);
			const std::string countText = count > 999 ? "999+" : std::to_string(count);
			const ImGuiStyle& style = ImGui::GetStyle();
			const float iconSize = ImGui::GetFontSize();
			const float height = ImGui::GetFrameHeight();
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float width = iconSize + style.ItemInnerSpacing.x + ImGui::CalcTextSize(countText.c_str()).x + style.FramePadding.x * 2.0f;
			if (EditorUI::Button(id, shown ? EditorUI::ButtonVariant::Default : EditorUI::ButtonVariant::Ghost, ImVec2(width, height))) {
				shown = !shown;
			}
			EditorUI::Tooltip(tooltip);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 iconPos(pos.x + style.FramePadding.x, pos.y + (height - iconSize) * 0.5f);
			const uint64_t icon = EditorIcons::Get(iconName, static_cast<int>(iconSize));
			if (icon) {
				drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(icon)), iconPos,
					ImVec2(iconPos.x + iconSize, iconPos.y + iconSize), ImVec2(0, 1), ImVec2(1, 0),
					ImGui::GetColorU32(ImVec4(1, 1, 1, shown ? 1.0f : 0.35f)));
			}
			else {
				drawList->AddText(iconPos, ImGui::GetColorU32(GetLogLevelColor(level)), level <= Log::Level::Info ? "i" : "!");
			}
			const ImVec4 countColor = (!shown || count == 0) ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
				: (level <= Log::Level::Info ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : EditorUI::ToneColor(GetLogLevelTone(level)));
			drawList->AddText(ImVec2(iconPos.x + iconSize + style.ItemInnerSpacing.x, iconPos.y),
				ImGui::GetColorU32(countColor), countText.c_str());
		}
	}

	void ImGuiEditorLayer::DrainPendingLogEntries() {
		if (!m_LogDispatchState) {
			return;
		}

		std::vector<LogEntry> pendingEntries;
		{
			std::scoped_lock lock(m_LogDispatchState->Mutex);
			if (m_LogDispatchState->PendingEntries.empty()) {
				return;
			}

			pendingEntries.swap(m_LogDispatchState->PendingEntries);
		}

		for (LogEntry& entry : pendingEntries) {
			AppendLogEntry(std::move(entry));
		}
	}

	void ImGuiEditorLayer::AppendLogEntry(LogEntry entry) {
		const auto diagnosticLocation = entry.SourceFilePath.empty() || entry.Message.starts_with("[ScriptCompiler]")
			? FindLogSourceLocation(entry.Message) : std::nullopt;
		if (diagnosticLocation) {
			entry.SourceFilePath = diagnosticLocation->FilePath;
			entry.SourceIdentity = entry.SourceFilePath;
			entry.SourceLinkText = diagnosticLocation->LinkText;
			entry.SourceLine = diagnosticLocation->Line;
			entry.SourceLinkStart = diagnosticLocation->MessageStart;
			entry.SourceLinkEnd = diagnosticLocation->MessageEnd;
		} else if (!entry.SourceFilePath.empty()) {
			entry.SourceFilePath = ResolveLogSourceFile(entry.SourceFilePath);
			if (!entry.SourceFilePath.empty()) entry.SourceIdentity = entry.SourceFilePath;
			if (!entry.SourceFilePath.empty()) entry.SourceLinkText = std::filesystem::path(entry.SourceFilePath).filename().string() + ":" + std::to_string(entry.SourceLine);
		}

		m_LogEntries.push_back(std::move(entry));
		if (m_LogEntries.size() > 2000) {
			m_LogEntries.erase(m_LogEntries.begin(), m_LogEntries.begin() + 500);
			// The erase shifts every surviving entry down by 500; re-pin the selection to the same
			// log line (or drop it if that line was trimmed) so the highlight and Copy stay correct.
			if (m_SelectedLogIndex >= 0) {
				m_SelectedLogIndex = m_SelectedLogIndex >= 500 ? m_SelectedLogIndex - 500 : -1;
			}
		}
	}

	void ImGuiEditorLayer::ClearLogEntries() {
		m_LogEntries.clear();
		m_SelectedLogIndex = -1;
		if (m_LogDispatchState) {
			std::scoped_lock lock(m_LogDispatchState->Mutex);
			m_LogDispatchState->PendingEntries.clear();
		}
	}

	void ImGuiEditorLayer::RenderLogPanel() {
		DrainPendingLogEntries();
		if (!ImGui::Begin("Log")) {
			ImGui::End();
			return;
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float fontSize = ImGui::GetFontSize();
		const float frameHeight = ImGui::GetFrameHeight();
		const float gap = style.ItemSpacing.x;
		const float followWidth = ImGui::CalcTextSize("Auto-scroll").x + style.FramePadding.x * 2.0f;
		const ImVec4 muted = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
		const ImVec4 bodyColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		const ImVec4 listSurface = style.Colors[ImGuiCol_ChildBg];
		const ImVec4 detailSurface = ImLerp(style.Colors[ImGuiCol_WindowBg], style.Colors[ImGuiCol_FrameBg], 0.35f);
		const ImVec4 headerSurface = ImLerp(style.Colors[ImGuiCol_WindowBg], style.Colors[ImGuiCol_FrameBg], 0.65f);
		const float regionBorder = std::max(1.0f, style.ChildBorderSize);
		const ImVec2 detailPadding(std::round(fontSize * 0.65f), std::round(fontSize * 0.45f));
		int infoCount = 0, warnCount = 0, errorCount = 0;
		for (const auto& entry : m_LogEntries) {
			if (entry.Level <= Log::Level::Info) ++infoCount;
			else if (entry.Level == Log::Level::Warn) ++warnCount;
			else ++errorCount;
		}

		auto copyText = [this](const std::string& text) {
			if (text.empty()) return;
			ImGui::SetClipboardText(text.c_str());
			m_LogCopiedAt = ImGui::GetTime();
		};
		auto copyEntry = [&](const LogEntry& entry) {
			const std::string text = (entry.Timestamp.empty() ? "" : "[" + entry.Timestamp + "] ")
				+ GetLogLevelPrefix(entry.Level) + entry.Message;
			copyText(text);
		};
		auto countWidth = [&](int count) {
			return ImGui::CalcTextSize(count > 999 ? "999+" : std::to_string(count).c_str()).x;
		};
		bool scrollToEnd = m_LogScrollToEnd;
		m_LogScrollToEnd = false;
		bool copyVisible = false;
		EditorUI::BeginToolbar("##LogToolbar");
		const float toolbarWidth = ImGui::GetContentRegionAvail().x;
		const float levelsWidth = (fontSize + style.ItemInnerSpacing.x + style.FramePadding.x * 2.0f) * 3.0f + countWidth(infoCount)
			+ countWidth(warnCount) + countWidth(errorCount) + gap * 3.0f;
		const float actionsWidth = ImGui::CalcTextSize("Collapse").x + style.FramePadding.x * 2.0f
			+ frameHeight + gap * 3.0f + 7.0f;
		const bool showFollow = toolbarWidth > followWidth + frameHeight * 3.0f + gap * 2.0f;
		const float followReservedWidth = showFollow ? followWidth + gap : 0.0f;
		const bool showLevels = toolbarWidth > levelsWidth + followReservedWidth + frameHeight + gap + fontSize * 9.0f;
		const bool showActions = showLevels && toolbarWidth > levelsWidth + actionsWidth + followReservedWidth
			+ frameHeight + gap + fontSize * 12.0f;
		const float reservedWidth = frameHeight + gap + (showLevels ? levelsWidth : 0.0f)
			+ (showActions ? actionsWidth : 0.0f) + followReservedWidth;
		EditorUI::SearchField("##LogFilter", s_LogFilter, "Filter messages...",
			std::max(frameHeight, toolbarWidth - reservedWidth));
		if (showLevels) {
			ImGui::SameLine();
			LogLevelToggle("##FilterInfo", "info", "Info", infoCount, m_ShowLogInfo, Log::Level::Info);
			ImGui::SameLine();
			LogLevelToggle("##FilterWarn", "warning", "Warnings", warnCount, m_ShowLogWarn, Log::Level::Warn);
			ImGui::SameLine();
			LogLevelToggle("##FilterError", "error", "Errors", errorCount, m_ShowLogError, Log::Level::Error);
		}
		if (showActions) {
			EditorUI::ToolbarSeparator();
			if (EditorUI::ToggleButton("Collapse", s_LogCollapse, "Merge consecutive identical messages")) {
				s_LogCollapse = !s_LogCollapse;
			}
		}
		if (showFollow) {
			ImGui::SameLine();
			if (EditorUI::ToggleButton("Auto-scroll##LogToolbar", m_LogAutoScroll, "Follow new messages")) {
				m_LogAutoScroll = !m_LogAutoScroll;
				scrollToEnd = m_LogAutoScroll;
			}
		}
		if (showActions) {
			ImGui::SameLine();
			if (EditorUI::IconButton("##ClearLog", "delete", "Clear messages")) ClearLogEntries();
		}
		ImGui::SameLine();
		if (EditorUI::Button("...##LogOptions", EditorUI::ButtonVariant::Ghost, ImVec2(frameHeight, frameHeight))) {
			ImGui::OpenPopup("##LogOptionsMenu");
		}
		EditorUI::Tooltip("Console options");
		if (ImGui::BeginPopup("##LogOptionsMenu")) {
			ImGui::MenuItem("Info", nullptr, &m_ShowLogInfo);
			ImGui::MenuItem("Warnings", nullptr, &m_ShowLogWarn);
			ImGui::MenuItem("Errors", nullptr, &m_ShowLogError);
			ImGui::Separator();
			ImGui::MenuItem("Collapse repeats", nullptr, &s_LogCollapse);
			if (ImGui::MenuItem("Auto-scroll", nullptr, &m_LogAutoScroll)) scrollToEnd = m_LogAutoScroll;
			ImGui::MenuItem("Expand message details", nullptr, &m_LogDetailsExpanded);
			ImGui::Separator();
			copyVisible = ImGui::MenuItem("Copy all visible");
			if (ImGui::MenuItem("Clear messages")) ClearLogEntries();
			ImGui::EndPopup();
		}
		EditorUI::EndToolbar();

		const std::string filterLower = ToLowerAscii(s_LogFilter);
		auto passesFilters = [&](const LogEntry& entry) {
			return IsLogEntryVisible(entry.Level, m_ShowLogInfo, m_ShowLogWarn, m_ShowLogError)
				&& ContainsCaseInsensitive(entry.Message, filterLower);
		};
		struct VisibleLogRow { int Index; int LastIndex; int Count; };
		std::vector<VisibleLogRow> rows;
		rows.reserve(m_LogEntries.size());
		for (int i = 0; i < static_cast<int>(m_LogEntries.size()); ++i) {
			const LogEntry& entry = m_LogEntries[i];
			if (!passesFilters(entry)) continue;
			if (s_LogCollapse && !rows.empty()) {
				VisibleLogRow& previous = rows.back();
				const LogEntry& previousEntry = m_LogEntries[previous.Index];
				if (entry.Level == previousEntry.Level && entry.Message == previousEntry.Message
					&& entry.SourceIdentity == previousEntry.SourceIdentity && entry.SourceLine == previousEntry.SourceLine) {
					previous.LastIndex = i;
					++previous.Count;
					continue;
				}
			}
			rows.push_back({ i, i, 1 });
		}

		const bool hasSelection = m_SelectedLogIndex >= 0 && m_SelectedLogIndex < static_cast<int>(m_LogEntries.size())
			&& passesFilters(m_LogEntries[m_SelectedLogIndex]);
		const float availableHeight = ImGui::GetContentRegionAvail().y;
		const float footerHeight = availableHeight > frameHeight * 4.0f
			? fontSize + style.FramePadding.y * 2.0f + style.ItemSpacing.y : 0.0f;
		const float detailHeight = hasSelection && availableHeight > frameHeight * 6.0f
			? (m_LogDetailsExpanded ? std::min(fontSize * 10.0f, availableHeight * 0.50f)
				: frameHeight + detailPadding.y * 2.0f + regionBorder * 2.0f)
			: 0.0f;
		const float listHeight = std::max(1.0f, availableHeight
			- (detailHeight > 0.0f ? detailHeight + style.ItemSpacing.y : 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.FrameRounding);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, regionBorder);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, listSurface);
		ImGui::BeginChild("##LogListRegion", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar(3);
		const float listWidth = ImGui::GetContentRegionAvail().x;
		const float tableHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight);
		const bool showTime = listWidth >= fontSize * 24.0f;
		const bool showSource = listWidth >= fontSize * 34.0f;
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBody
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchProp;
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(fontSize * 0.4f, fontSize * 0.35f));
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, headerSurface);
		ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, style.Colors[ImGuiCol_Border]);
		if (ImGui::BeginTable("##LogEntries", 4, tableFlags, ImVec2(0.0f, tableHeight))) {
			ImGui::TableSetupColumn("##Severity", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, fontSize * 1.5f);
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed | (showTime ? 0 : ImGuiTableColumnFlags_Disabled), fontSize * 5.0f);
			ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed | (showSource ? 0 : ImGuiTableColumnFlags_Disabled), fontSize * 6.5f);
			ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::PushStyleColor(ImGuiCol_Text, muted);
			ImGui::TableHeadersRow();
			ImGui::PopStyleColor();
			const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - style.CellPadding.y;
			const bool scrollKey = ImGui::IsWindowFocused() && (ImGui::IsKeyPressed(ImGuiKey_PageUp)
				|| ImGui::IsKeyPressed(ImGuiKey_Home) || ImGui::IsKeyPressed(ImGuiKey_UpArrow));
			const bool scrollbarPressed = ImGui::IsMouseDown(ImGuiMouseButton_Left)
				&& ImGui::GetIO().MousePos.x >= ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - style.ScrollbarSize;
			const bool scrollingAway = ImGui::GetScrollMaxY() > 0.0f && (scrollKey
				|| (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
					&& (ImGui::GetIO().MouseWheel > 0.0f || (scrollbarPressed && !wasAtBottom))));
			if (scrollingAway) m_LogAutoScroll = false;
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step()) {
				for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex) {
					const VisibleLogRow& row = rows[rowIndex];
					const LogEntry& entry = m_LogEntries[row.Index];
					const LogMessageParts parts = SplitLogMessage(entry.Message);
					const bool selected = hasSelection && m_SelectedLogIndex >= row.Index && m_SelectedLogIndex <= row.LastIndex;
					ImGui::PushID(row.Index);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					const ImVec2 rowStart = ImGui::GetCursorScreenPos();
					const float iconOffset = std::max(0.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetTextLineHeight()) * 0.5f);
					if (ImGui::Selectable("##LogRow", selected, ImGuiSelectableFlags_SpanAllColumns
						| ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
						ImVec2(0.0f, ImGui::GetTextLineHeight()))) {
						m_SelectedLogIndex = row.Index;
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !entry.SourceFilePath.empty()) {
							ExternalEditor::OpenFile(entry.SourceFilePath, entry.SourceLine);
						}
					}
					if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_SelectedLogIndex = row.Index;
					const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
					const bool focused = ImGui::IsItemFocused();
					const bool rowSelected = m_SelectedLogIndex >= row.Index && m_SelectedLogIndex <= row.LastIndex;
					if (rowSelected || hovered || focused) {
						const ImGuiTable* table = ImGui::GetCurrentTable();
						const float inset = std::max(1.0f, fontSize * 0.1f);
						const ImVec2 min(table->WorkRect.Min.x + inset, rowStart.y - style.CellPadding.y + inset);
						const ImVec2 max(table->WorkRect.Max.x - inset, rowStart.y + fontSize + style.CellPadding.y - inset);
						ImGui::TablePushBackgroundChannel();
						ImDrawList* drawList = ImGui::GetWindowDrawList();
						const ImVec4 fill = rowSelected ? EditorTheme::Colors::RowSelection(ImGui::IsItemActive())
							: ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
						drawList->AddRectFilled(min, max, ImGui::GetColorU32(fill), style.FrameRounding);
						if (focused) drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_NavCursor), style.FrameRounding);
						ImGui::TablePopBackgroundChannel();
					}
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
						const ImVec2 workSize = ImGui::GetWindowViewport()->WorkSize;
						const float tooltipWidth = std::max(1.0f, std::min(fontSize * 40.0f, workSize.x * 0.8f) - style.WindowPadding.x * 2.0f);
						const float tooltipHeight = std::max(1.0f, std::min(fontSize * 16.0f, workSize.y * 0.6f) - style.WindowPadding.y * 2.0f);
						const ImVec2 textSize = ImGui::CalcTextSize(entry.Message.c_str(), nullptr, false, tooltipWidth);
						const bool clipped = textSize.y > tooltipHeight;
						if (ImGui::BeginTooltip()) {
							if (clipped) {
								const char* hint = "Preview truncated. Click the message for full details.";
								const float hintHeight = ImGui::CalcTextSize(hint, nullptr, false, tooltipWidth).y;
								const float previewHeight = std::max(1.0f, tooltipHeight - hintHeight - style.ItemSpacing.y);
								ImGui::BeginChild("##LogTooltipPreview", ImVec2(tooltipWidth, previewHeight), ImGuiChildFlags_None,
									ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
								ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tooltipWidth);
								ImGui::TextUnformatted(entry.Message.c_str());
								ImGui::PopTextWrapPos();
								ImGui::EndChild();
								ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tooltipWidth);
								ImGui::TextDisabled("%s", hint);
								ImGui::PopTextWrapPos();
							}
							else {
								ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tooltipWidth);
								ImGui::TextUnformatted(entry.Message.c_str());
								ImGui::PopTextWrapPos();
							}
							ImGui::EndTooltip();
						}
					}
					ImGui::SetCursorScreenPos(ImVec2(rowStart.x + iconOffset, rowStart.y));
					DrawLogSeverity(entry.Level);
					if (ImGui::TableSetColumnIndex(1)) DrawLogCell(entry.Timestamp, ImGui::GetContentRegionAvail().x, muted);
					if (ImGui::TableSetColumnIndex(2)) DrawLogCell(parts.Source, ImGui::GetContentRegionAvail().x, muted);
					ImGui::TableSetColumnIndex(3);
					const float messageWidth = ImGui::GetContentRegionAvail().x;
					const std::string repeats = row.Count > 1 ? "x" + std::to_string(row.Count) : std::string();
					const float repeatWidth = repeats.empty() ? 0.0f : ImGui::CalcTextSize(repeats.c_str()).x;
					DrawLogCell(showSource ? parts.Body : std::string_view(entry.Message),
						messageWidth - (repeats.empty() ? 0.0f : repeatWidth + gap), bodyColor);
					if (!repeats.empty()) {
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, messageWidth - repeatWidth));
						ImGui::TextColored(muted, "%s", repeats.c_str());
					}
					ImGui::PopID();
				}
			}
			if (rows.empty()) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(3);
				ImGui::TextColored(muted, "%s", m_LogEntries.empty() ? "No messages" : "No messages match the filter");
			}
			if (scrollToEnd || (m_LogAutoScroll && !scrollingAway)) ImGui::SetScrollY(ImGui::GetCursorPosY());
			if (ImGui::BeginPopupContextWindow("##LogTextCtx")) {
				const bool canCopy = m_SelectedLogIndex >= 0 && m_SelectedLogIndex < static_cast<int>(m_LogEntries.size());
				if (ImGui::MenuItem("Copy Selected", nullptr, false, canCopy)) copyEntry(m_LogEntries[m_SelectedLogIndex]);
				if (ImGui::MenuItem("Copy All Visible")) copyVisible = true;
				ImGui::EndPopup();
			}
			ImGui::EndTable();
		}
		ImGui::PopStyleColor(6);
		ImGui::PopStyleVar();
		if (footerHeight > 0.0f) {
			const ImVec2 pos = ImGui::GetWindowPos();
			const ImVec2 size = ImGui::GetWindowSize();
			const float footerY = size.y - footerHeight;
			ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x + regionBorder, pos.y + footerY),
				ImVec2(pos.x + size.x - regionBorder, pos.y + footerY), ImGui::GetColorU32(ImGuiCol_Border));
			ImGui::SetCursorPos(ImVec2(detailPadding.x, footerY + style.FramePadding.y));
			int visibleCount = 0;
			for (const VisibleLogRow& row : rows) visibleCount += row.Count;
			ImGui::TextColored(muted, "%d messages", visibleCount);
			if (s_LogCollapse) {
				ImGui::SameLine();
				ImGui::TextColored(muted, " / %d rows", static_cast<int>(rows.size()));
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		if (detailHeight > 0.0f && m_SelectedLogIndex >= 0) {
			LogEntry& selected = m_LogEntries[m_SelectedLogIndex];
			const LogMessageParts parts = SplitLogMessage(selected.Message);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, detailPadding);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.FrameRounding);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, regionBorder);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, detailSurface);
			ImGui::BeginChild("##LogDetails", ImVec2(0.0f, detailHeight),
				ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(3);
			const ImVec2 detailPos = ImGui::GetWindowPos();
			const float detailWidth = ImGui::GetWindowSize().x;
			const float headerBottom = ImGui::GetCursorScreenPos().y + frameHeight + detailPadding.y;
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(detailPos.x + regionBorder, detailPos.y + regionBorder),
				ImVec2(detailPos.x + detailWidth - regionBorder, headerBottom), ImGui::GetColorU32(headerSurface),
				style.FrameRounding, ImDrawFlags_RoundCornersTop);
			const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
			const bool hasSource = !selected.SourceFilePath.empty();
			const float detailActionsWidth = frameHeight * (hasSource ? 3.0f : 2.0f) + gap * (hasSource ? 2.0f : 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			if (EditorUI::IconButton("##ExpandLogDetails", GetLogLevelIconName(selected.Level),
				m_LogDetailsExpanded ? "Collapse message details" : "Expand message details", true, true)) {
				m_LogDetailsExpanded = !m_LogDetailsExpanded;
			}
			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y);
			const float headingWidth = std::max(0.0f, right - detailActionsWidth - gap - ImGui::GetCursorPosX());
			const float timeWidth = ImGui::CalcTextSize(selected.Timestamp.c_str()).x;
			const bool showDetailTime = headingWidth > timeWidth + fontSize * 5.0f;
			const std::string_view sourceLabel = parts.Source.empty() ? std::string_view("Message") : parts.Source;
			const float sourceWidth = std::min(ImGui::CalcTextSize(sourceLabel.data(), sourceLabel.data() + sourceLabel.size()).x,
				std::max(0.0f, headingWidth - (showDetailTime ? timeWidth + gap : 0.0f)));
			DrawLogCell(sourceLabel, sourceWidth, bodyColor);
			ImGui::Dummy(ImVec2(sourceWidth, fontSize));
			if (showDetailTime) {
				ImGui::SameLine();
				ImGui::TextColored(muted, "%s", selected.Timestamp.c_str());
			}
			ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - detailActionsWidth));
			if (hasSource) {
				if (EditorUI::IconButton("##OpenLogSource", "link", "Open source file")) {
					ExternalEditor::OpenFile(selected.SourceFilePath, selected.SourceLine);
				}
				ImGui::SameLine();
			}
			if (EditorUI::IconButton("##CopyLogDetails", "copy", "Copy message")) copyEntry(selected);
			ImGui::SameLine();
			const bool closeDetails = EditorUI::IconButton("##CloseLogDetails", "clear", "Close message details");
			ImGui::PopStyleColor();
			if (m_LogDetailsExpanded) {
				ImGui::GetWindowDrawList()->AddLine(ImVec2(detailPos.x + regionBorder, headerBottom),
					ImVec2(detailPos.x + detailWidth - regionBorder, headerBottom), ImGui::GetColorU32(ImGuiCol_Border));
				ImGui::SetCursorPosY(headerBottom - detailPos.y + detailPadding.y);
				if (hasSource) {
					DrawLogCell(selected.SourceLinkText, ImGui::GetContentRegionAvail().x, muted);
					ImGui::Dummy(ImVec2(0.0f, fontSize));
				}
				else {
					ImGui::TextColored(muted, "No source location available");
				}
				ImGui::PushID(m_SelectedLogIndex);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
				ImGui::InputTextMultiline("##MessageText", selected.Message.data() + selected.Message.size() - parts.Body.size(), parts.Body.size() + 1,
					ImVec2(-1.0f, std::max(frameHeight, ImGui::GetContentRegionAvail().y)),
					ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
			ImGui::EndChild();
			if (closeDetails) m_SelectedLogIndex = -1;
		}

		if (copyVisible) {
			std::string text;
			for (const LogEntry& entry : m_LogEntries) {
				if (!passesFilters(entry)) continue;
				if (!entry.Timestamp.empty()) text += "[" + entry.Timestamp + "] ";
				text += std::string(GetLogLevelPrefix(entry.Level)) + entry.Message + "\n";
			}
			copyText(text);
		}
		if (m_LogCopiedAt >= 0.0) {
			const ImVec2 pos = ImGui::GetWindowPos();
			EditorUI::DrawToast("Copied", "copy", static_cast<float>(ImGui::GetTime() - m_LogCopiedAt),
				ImVec2(pos.x + ImGui::GetWindowWidth() * 0.5f, ImGui::GetCursorStartPos().y + pos.y));
		}
		ImGui::End();
	}
}
