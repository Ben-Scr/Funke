#include <pch.hpp>
#include "Core/EngineModules.hpp"
#include "Systems/ImGuiEditorLayer.hpp"

#include <imgui.h>

#include "Assets/AssetRegistry.hpp"
#include "Build/GameBuilder.hpp"
#include "Collections/AspectRatio.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Editor/BuildAssetPruning.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Editor/FunkePackageInstaller.hpp"
#include "Graphics/ImageData.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/RenderSettings.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorUI.hpp"
#include "Graphics/SpriteUVResolver.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/MaterialShaderInspector.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Packages/GitHubSource.hpp"
#include "Packages/NuGetSource.hpp"
#include "Project/BuildPlatformSupport.hpp"
#include "Project/BuildOutputSafety.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/SplashAssetResolve.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Utils/Process.hpp"
#include "Editor/PlatformBuilder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <unordered_set>

#include <stb_image_write.h>

#ifdef FNK_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Funke {
	namespace {
#if !defined(FNK_PLATFORM_WINDOWS)
		std::string ShellQuote(std::string_view value) {
			std::string quoted = "'";
			for (char c : value) {
				if (c == '\'') quoted += "'\\''";
				else quoted += c;
			}
			return quoted + "'";
		}
#endif

		std::vector<FunkeProject::BuildSceneEntry>::iterator FindBuildScene(
			std::vector<FunkeProject::BuildSceneEntry>& sceneList, const std::string& sceneName)
		{
			return std::find_if(sceneList.begin(), sceneList.end(),
				[&](const FunkeProject::BuildSceneEntry& entry) { return entry.Name == sceneName; });
		}

		// Builds/<Platform> follows the target platform; a folder the user named themselves is left alone.
		std::string PlatformBuildOutputDirectory(const std::string& current, const std::string& projectRoot,
			BuildPlatform platform)
		{
			const char* platformFolder = FunkeBuildProfile::PlatformToString(platform);
			if (current.empty()) return Path::Combine(projectRoot, "Builds", platformFolder);

			std::filesystem::path path = Path::FromUtf8(current);
			if (!path.has_filename()) path = path.parent_path();
			const std::string leaf = Path::ToUtf8(path.filename());
			const auto equalsIgnoreCase = [](std::string_view a, std::string_view b) {
				return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
					return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
				});
			};
			for (BuildPlatform candidate : FunkeBuildProfile::AllPlatforms()) {
				if (!equalsIgnoreCase(leaf, FunkeBuildProfile::PlatformToString(candidate))) continue;
				if (candidate == platform) return current;
				return Path::ToUtf8(path.parent_path() / platformFolder);
			}
			return current;
		}

		std::string FirstEnabledBuildScene(const std::vector<FunkeProject::BuildSceneEntry>& sceneList) {
			for (const FunkeProject::BuildSceneEntry& entry : sceneList) {
				if (entry.Enabled) return entry.Name;
			}
			return {};
		}

		bool BrowseAcceptImageDrop(std::span<const std::string_view> extWhitelist,
			const std::function<void(const std::string&)>& onDrop)
		{
			if (!ImGui::BeginDragDropTarget()) return false;
			bool applied = false;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				if (payload->Data && payload->DataSize > 0) {
					std::string dropped(static_cast<const char*>(payload->Data),
						static_cast<std::size_t>(payload->DataSize));
					if (!dropped.empty() && dropped.back() == '\0') dropped.pop_back();
					if (!dropped.empty()) {
						bool extOk = extWhitelist.empty();
						if (!extOk) {
							std::string ext = std::filesystem::path(dropped).extension().string();
							std::transform(ext.begin(), ext.end(), ext.begin(),
								[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
							for (std::string_view allowed : extWhitelist) {
								if (ext == allowed) { extOk = true; break; }
							}
						}
						if (extOk) {
							onDrop(dropped);
							applied = true;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
			return applied;
		}

		std::string ToProjectRelativeAssetPath(const std::filesystem::path& absPath,
			const std::string& assetsDirectory)
		{
			std::filesystem::path assetsDir(assetsDirectory);
			if (absPath.string().find(assetsDir.string()) == 0) {
				return std::filesystem::relative(absPath, assetsDir.parent_path()).string();
			}
			return absPath.filename().string();
		}

		constexpr std::string_view k_ImageExts[] = {
			".png", ".jpg", ".jpeg", ".bmp"
		};

		struct RenderBackendOption {
			FunkeProject::RenderBackend Backend;
			const char* Label;
		};

		constexpr RenderBackendOption k_RenderBackendOptions[] = {
			{ FunkeProject::RenderBackend::Auto,       "Auto (platform best)" },
			{ FunkeProject::RenderBackend::Vulkan,     "Vulkan" },
			{ FunkeProject::RenderBackend::Direct3D11, "Direct3D 11" },
			{ FunkeProject::RenderBackend::Direct3D12, "Direct3D 12" },
			{ FunkeProject::RenderBackend::Metal,      "Metal" },
			{ FunkeProject::RenderBackend::OpenGL,     "OpenGL" },
			{ FunkeProject::RenderBackend::OpenGLES,   "OpenGL ES" },
		};

		const char* RenderBackendLabel(FunkeProject::RenderBackend backend) {
			for (const RenderBackendOption& option : k_RenderBackendOptions) {
				if (option.Backend == backend) return option.Label;
			}
			return "Auto (platform best)";
		}

		bool IsRenderBackendSupportedOnHost(FunkeProject::RenderBackend backend) {
			if (backend == FunkeProject::RenderBackend::Auto) return true;
#if defined(FNK_PLATFORM_WINDOWS)
			return backend == FunkeProject::RenderBackend::Vulkan
				|| backend == FunkeProject::RenderBackend::Direct3D11
				|| backend == FunkeProject::RenderBackend::Direct3D12
				|| backend == FunkeProject::RenderBackend::OpenGL;
#elif defined(FNK_PLATFORM_MACOS)
			return backend == FunkeProject::RenderBackend::Metal;
#elif defined(FNK_PLATFORM_LINUX)
			return backend == FunkeProject::RenderBackend::Vulkan
				|| backend == FunkeProject::RenderBackend::OpenGL
				|| backend == FunkeProject::RenderBackend::OpenGLES;
#else
			return false;
#endif
		}

		const char* RenderBackendUnsupportedReason(FunkeProject::RenderBackend backend) {
			if (IsRenderBackendSupportedOnHost(backend)) return nullptr;
			switch (backend) {
				case FunkeProject::RenderBackend::Direct3D11:
				case FunkeProject::RenderBackend::Direct3D12:
					return "Direct3D backends are only available on Windows.";
				case FunkeProject::RenderBackend::Metal:
					return "Metal is only available on macOS.";
				case FunkeProject::RenderBackend::Vulkan:
					return "Vulkan is not available for this host platform.";
				case FunkeProject::RenderBackend::OpenGL:
					return "OpenGL is not available for this host platform.";
				case FunkeProject::RenderBackend::OpenGLES:
					return "OpenGL ES is not available for this host platform.";
				case FunkeProject::RenderBackend::Auto:
					break;
			}
			return "This backend is not available for this host platform.";
		}

		uint64_t ParsePickerAssetId(const std::string& value) {
			if (value.empty()) return 0;
			try {
				size_t parsed = 0;
				uint64_t id = std::stoull(value, &parsed, 10);
				return parsed == value.size() ? id : 0;
			}
			catch (...) {
				return 0;
			}
		}

		std::string NormalizePreviewTexturePath(const std::filesystem::path& path) {
			std::error_code ec;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
			if (ec) {
				ec.clear();
				normalized = std::filesystem::absolute(path, ec);
				if (ec) {
					normalized = path.lexically_normal();
				}
			}

			return normalized.lexically_normal().make_preferred().string();
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

		ImVec4 GetLogLinkColor() {
			return EditorUI::ToneColor(EditorUI::Tone::Info);
		}

		// Log console view state. File-static so the panel's look can evolve without touching the layer header.
		std::string s_LogFilter;
		bool s_LogCollapse = false;

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

		// Level filter toggle followed by its entry count; the count is clickable too so the hit target isn't just the icon.
		void LogLevelToggle(const char* id, const char* iconName, const char* label, int count, bool& shown, Log::Level level) {
			char tooltip[64];
			std::snprintf(tooltip, sizeof(tooltip), "%s %s (%d)", shown ? "Hide" : "Show", label, count);
			if (EditorUI::IconButton(id, iconName, tooltip, shown, true)) {
				shown = !shown;
			}
			ImGui::SameLine(0.0f, 3.0f);
			ImGui::AlignTextToFramePadding();
			// Info counts stay body-coloured like info rows; a hidden or empty level reads as disabled.
			const ImVec4 countColor = (!shown || count == 0) ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
				: (level <= Log::Level::Info ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : EditorUI::ToneColor(GetLogLevelTone(level)));
			if (count > 999) ImGui::TextColored(countColor, "999+");
			else ImGui::TextColored(countColor, "%d", count);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
				shown = !shown;
			}
			EditorUI::Tooltip(tooltip);
		}

		// Word-wraps `text` to `wrapWidth` px. When `draw`, renders it via the window draw list at
		// the current cursor: bytes in [linkStart, linkEnd) become an underlined, double-click-to-open
		// link in `linkColor`, bytes in [0, dimEnd) (the timestamp) in `dimColor`, the rest in `textColor`.
		// Honors embedded '\n'. Returns the wrapped block's pixel height. Measure (draw=false) and draw
		// share one path, so a row's Selectable height always matches what is drawn beneath it.
		float DrawWrappedLogMessage(
			std::string_view text,
			size_t linkStart,
			size_t linkEnd,
			const std::string& filePath,
			int linkLine,
			const ImVec4& textColor,
			const ImVec4& linkColor,
			float wrapWidth,
			bool draw,
			size_t dimEnd = 0,
			const ImVec4& dimColor = ImVec4(0, 0, 0, 0))
		{
			ImFont* font = ImGui::GetFont();
			const float fontSize = ImGui::GetFontSize();
			const float lineHeight = ImGui::GetTextLineHeight();
			if (wrapWidth < 1.0f) wrapWidth = 1.0f;

			ImDrawList* drawList = draw ? ImGui::GetWindowDrawList() : nullptr;
			const ImVec2 origin = draw ? ImGui::GetCursorScreenPos() : ImVec2(0.0f, 0.0f);
			const ImU32 textCol = draw ? ImGui::GetColorU32(textColor) : 0u;
			const ImU32 linkCol = draw ? ImGui::GetColorU32(linkColor) : 0u;
			const ImU32 dimCol = draw ? ImGui::GetColorU32(dimColor) : 0u;
			const bool hasLink = draw && !filePath.empty()
				&& linkEnd > linkStart && linkEnd <= text.size();
			// AllowWhenBlockedByActiveItem: the full-row Selectable grabs ActiveId on the second mouse-down,
			// which makes a plain IsWindowHovered() return false on the exact frame IsMouseDoubleClicked() fires.
			// The IsMouseHoveringRect test below still scopes the action to the link's glyphs.
			const bool windowHovered = draw && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

			const char* const base = text.data();
			const char* const textEnd = base + text.size();
			float y = 0.0f;

			// 0 = body, 1 = link, 2 = dimmed prefix.
			auto spanKind = [&](size_t idx) -> int {
				if (hasLink && idx >= linkStart && idx < linkEnd) return 1;
				return idx < dimEnd ? 2 : 0;
			};

			// Draws one visual (already-wrapped) line [lineBegin, lineEnd), splitting runs at the
			// link/prefix boundaries so those bytes get their own color (and the link its underline + hover rect).
			auto drawVisualLine = [&](const char* lineBegin, const char* lineEnd) {
				if (!draw || lineBegin >= lineEnd) return;
				float penX = 0.0f;
				const char* p = lineBegin;
				while (p < lineEnd) {
					const int kind = spanKind(static_cast<size_t>(p - base));
					const bool inLink = kind == 1;
					const char* runEnd = p;
					while (runEnd < lineEnd && spanKind(static_cast<size_t>(runEnd - base)) == kind) {
						++runEnd;
					}

					const ImVec2 pos(origin.x + penX, origin.y + y);
					drawList->AddText(pos, inLink ? linkCol : (kind == 2 ? dimCol : textCol), p, runEnd);
					const float advance = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, p, runEnd).x;

					if (inLink) {
						drawList->AddLine(
							ImVec2(pos.x, pos.y + lineHeight - 1.0f),
							ImVec2(pos.x + advance, pos.y + lineHeight - 1.0f),
							linkCol, 1.0f);
						if (windowHovered
							&& ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + advance, pos.y + lineHeight))) {
							ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
							ImGui::SetTooltip("Double-click to open");
							if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
								ExternalEditor::OpenFile(filePath, linkLine);
							}
						}
					}

					penX += advance;
					p = runEnd;
				}
			};

			const char* lineStart = base;
			while (true) {
				const char* nl = static_cast<const char*>(
					std::memchr(lineStart, '\n', static_cast<size_t>(textEnd - lineStart)));
				const char* logicalEnd = nl ? nl : textEnd;

				if (lineStart == logicalEnd) {
					y += lineHeight; // blank line preserves vertical gap
				}
				else {
					const char* p = lineStart;
					while (p < logicalEnd) {
						const char* wrapAt = font->CalcWordWrapPosition(fontSize, p, logicalEnd, wrapWidth);
						if (wrapAt <= p) {
							// Degenerate: one glyph wider than the whole wrap width. Step a full UTF-8
							// code point (skip continuation bytes) so we force progress without splitting it.
							wrapAt = p + 1;
							while (wrapAt < logicalEnd && (static_cast<unsigned char>(*wrapAt) & 0xC0) == 0x80) ++wrapAt;
						}
						drawVisualLine(p, wrapAt);
						y += lineHeight;
						p = wrapAt;
						while (p < logicalEnd && (*p == ' ' || *p == '\t')) ++p; // ImGui drops blanks after a wrap
					}
				}

				if (!nl) break;
				lineStart = nl + 1;
				if (lineStart == textEnd) { y += lineHeight; break; } // trailing '\n' -> trailing blank line
			}

			return y < lineHeight ? lineHeight : y;
		}

		// EVERY EditorUI::SectionHeader inside a RenderSettings_* helper MUST have a matching k_SettingsIndex entry — SectionVisible() fails closed for unlisted (category, section) pairs.
		struct SettingsIndexEntry {
			ImGuiEditorLayer::SettingsCategory Category;
			const char* Section;   // EXACTLY as passed to EditorUI::SectionHeader.
			const char* Keywords;  // Space-separated extra terms (lowercase).
		};

		constexpr SettingsIndexEntry k_SettingsIndex[] = {
			// Display (RenderSettings_Display)
			{ ImGuiEditorLayer::SettingsCategory::Display,  "Window",          "width height fullscreen resizable resolution window size run background" },
			{ ImGuiEditorLayer::SettingsCategory::Display,  "Mobile",          "mobile android ios phone tablet orientation landscape portrait rotation rotate upside down" },
			{ ImGuiEditorLayer::SettingsCategory::Display,  "UI Scaling",      "ui scaling reference resolution canvas scaler scale mode fit fill expand match width height layout" },

			// Graphics (RenderSettings_Graphics)
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Render Backend",  "render backend rendering api dawn webgpu vulkan d3d11 d3d12 opengl metal auto" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Runtime Quality", "render scale upscale filter post processing post-processing particles quality performance texture quality mipmap mip anisotropic anisotropy filtering anti-aliasing antialiasing aliasing fxaa jaggies edges" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "3D Rendering", "3d shadows shadow quality distance lights max lights exposure tone mapping aces reinhard hdr" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Animation",       "animation animator skinned mesh distance rate update rate lod level of detail far frames fps culling performance" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Default Font",    "default font typeface text font asset" },
			{ ImGuiEditorLayer::SettingsCategory::Graphics, "Font Baking",     "font baking atlas memory budget step granularity bake size lru cache vram ram" },

			// Branding (RenderSettings_Branding)
			{ ImGuiEditorLayer::SettingsCategory::Branding, "Splash Screen",   "splash screen logo intro startup fade duration enable image background color text preview" },
			{ ImGuiEditorLayer::SettingsCategory::Branding, "App Icon",        "app icon application exe executable window taskbar shipped game" },
			{ ImGuiEditorLayer::SettingsCategory::Branding, "Cursors",         "cursor mouse pointer ui hover default" },

			// Build (RenderSettings_Build)
			{ ImGuiEditorLayer::SettingsCategory::Build,    "Engine Modules",  "modules module 2d 3d sprites physics box2d jolt meshes lights funke.2d funke.3d strip size" },
			{ ImGuiEditorLayer::SettingsCategory::Build,    "Executable",      "executable exe name output binary" },
			{ ImGuiEditorLayer::SettingsCategory::Build,    "Diagnostics",     "diagnostics runtime stats logs overlay fps f6 f7 debug" },
			{ ImGuiEditorLayer::SettingsCategory::Build,    "ECS Entity Bits", "ecs entity bits entt entities versions rebuild engine 16 20 22 24 28" },

			// Editor (RenderSettings_Editor)
			{ ImGuiEditorLayer::SettingsCategory::Editor,   "Asset Browser",   "asset browser duplicate suffix asset naming" },
			{ ImGuiEditorLayer::SettingsCategory::Editor,   "Entities",        "entities entity naming suffix unique duplicate" },
			{ ImGuiEditorLayer::SettingsCategory::Editor,   "Serialization",   "serialization serialize format json binary scene prefab assets" },
			{ ImGuiEditorLayer::SettingsCategory::Editor,   "Scripting",       "scripting scripts recompile auto build play mode dotnet csharp c#" },

			// Systems (RenderSettings_Systems)
			{ ImGuiEditorLayer::SettingsCategory::Systems,  "Global Systems",  "global systems globalsystem scene system registration project scope" },

			// Physics (RenderSettings_Physics)
			{ ImGuiEditorLayer::SettingsCategory::Physics,  "Physics World",   "physics world hz hertz tick rate fixed timestep fixed delta time simulation step frequency box2d fixedupdate gravity solver" },
		};

		constexpr const char* k_SettingsCategoryNamesLower[] = {
			"display", "graphics", "branding", "build", "editor", "systems", "physics",
		};
		static_assert(IM_ARRAYSIZE(k_SettingsCategoryNamesLower)
			== static_cast<int>(ImGuiEditorLayer::SettingsCategory::Physics) + 1,
			"k_SettingsCategoryNamesLower out of sync with SettingsCategory enum");

		bool SettingsContains(const char* haystackLower, const std::string& needleLower) {
			if (needleLower.empty()) return true;
			if (!haystackLower) return false;
			return std::string_view(haystackLower).find(needleLower) != std::string_view::npos;
		}

		bool SettingsEntryMatches(const std::string& filterLower,
			const SettingsIndexEntry& entry, const char* categoryNameLower)
		{
			if (filterLower.empty()) return true;
			if (SettingsContains(categoryNameLower, filterLower)) return true;

			std::string sectionLower(entry.Section ? entry.Section : "");
			std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (sectionLower.find(filterLower) != std::string::npos) return true;

			return SettingsContains(entry.Keywords, filterLower);
		}

		bool CategoryHasMatches(ImGuiEditorLayer::SettingsCategory cat,
			const std::string& filterLower)
		{
			if (filterLower.empty()) return true;
			const int idx = static_cast<int>(cat);
			const char* categoryNameLower =
				(idx >= 0 && idx < IM_ARRAYSIZE(k_SettingsCategoryNamesLower))
					? k_SettingsCategoryNamesLower[idx]
					: "";
			for (const SettingsIndexEntry& entry : k_SettingsIndex) {
				if (entry.Category != cat) continue;
				if (SettingsEntryMatches(filterLower, entry, categoryNameLower)) return true;
			}
			return false;
		}

		bool SectionVisible(ImGuiEditorLayer::SettingsCategory cat,
			const char* section, const std::string& filterLower)
		{
			if (filterLower.empty()) return true;
			const int idx = static_cast<int>(cat);
			const char* categoryNameLower =
				(idx >= 0 && idx < IM_ARRAYSIZE(k_SettingsCategoryNamesLower))
					? k_SettingsCategoryNamesLower[idx]
					: "";
			for (const SettingsIndexEntry& entry : k_SettingsIndex) {
				if (entry.Category != cat) continue;
				if (std::strcmp(entry.Section, section) != 0) continue;
				return SettingsEntryMatches(filterLower, entry, categoryNameLower);
			}
			// Section not in k_SettingsIndex: fail closed so unlisted sections disappear under any filter (loudly visible).
			return false;
		}

		// Sections are forced open while a search filter is active. SectionHeader's leading spacer consumes
		// SetNextItemOpen, so write the tree-node open state (same ID TreeNodeEx uses) directly instead.
		bool SettingsSectionHeader(const char* section, const std::string& filterLower) {
			if (!filterLower.empty()) ImGui::GetStateStorage()->SetInt(ImGui::GetID(section), 1);
			return EditorUI::SectionHeader(section, true);
		}
	}

	void ImGuiEditorLayer::RenderLogPanel() {
		DrainPendingLogEntries();
		ImGui::Begin("Log");

		int infoCount = 0, warnCount = 0, errorCount = 0;
		for (const auto& entry : m_LogEntries) {
			if (entry.Level <= Log::Level::Info) infoCount++;
			else if (entry.Level == Log::Level::Warn) warnCount++;
			else errorCount++;
		}

		EditorUI::BeginToolbar("##LogToolbar");
		if (EditorUI::Button("Clear")) {
			ClearLogEntries();
		}
		EditorUI::ToolbarSeparator();

		LogLevelToggle("##FilterInfo", "info", "Info", infoCount, m_ShowLogInfo, Log::Level::Info);
		ImGui::SameLine(0.0f, 8.0f);
		LogLevelToggle("##FilterWarn", "warning", "Warnings", warnCount, m_ShowLogWarn, Log::Level::Warn);
		ImGui::SameLine(0.0f, 8.0f);
		LogLevelToggle("##FilterError", "error", "Errors", errorCount, m_ShowLogError, Log::Level::Error);
		EditorUI::ToolbarSeparator();

		if (EditorUI::ToggleButton("Collapse", s_LogCollapse, "Merge consecutive identical messages")) {
			s_LogCollapse = !s_LogCollapse;
		}
		ImGui::SameLine();
		EditorUI::SearchField("##LogFilter", s_LogFilter, "Filter messages...");
		EditorUI::EndToolbar();

		const std::string filterLower = ToLowerAscii(s_LogFilter);
		auto passesFilters = [&](const LogEntry& e) {
			return IsLogEntryVisible(e.Level, m_ShowLogInfo, m_ShowLogWarn, m_ShowLogError)
				&& ContainsCaseInsensitive(e.Message, filterLower);
		};

		ImGui::BeginChild("##LogEntries", ImVec2(-1.0f, -1.0f), true);

		const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
		constexpr float kLogIconSize = 14.0f;
		constexpr float kIconAdvance = kLogIconSize + 5.0f; // icon width + the SameLine spacing below
		const ImVec4 linkColor = GetLogLinkColor();
		const ImVec4 timestampColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
		// Wrap the message to the panel width minus the icon gutter. Captured once from the child's
		// inner width so every row — and the measure/draw passes within a row — wrap identically.
		const float contentWidth = ImGui::GetContentRegionAvail().x;
		const float wrapWidth = std::max(1.0f, contentWidth - kIconAdvance);
		const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
		const ImU32 altRowColor = ImGui::GetColorU32(ImGuiCol_Text, 0.035f);
		int visibleRows = 0;

		for (size_t i = 0; i < m_LogEntries.size(); ++i) {
			const LogEntry& entry = m_LogEntries[i];
			if (!passesFilters(entry)) {
				continue;
			}

			// Collapse absorbs the following visible duplicates. The row is addressed by the run's first
			// entry, so its index (selection, ID) stays stable while new repeats append to the run.
			size_t runEnd = i;
			int repeatCount = 1;
			if (s_LogCollapse) {
				for (size_t j = i + 1; j < m_LogEntries.size(); ++j) {
					const LogEntry& next = m_LogEntries[j];
					if (!passesFilters(next)) continue;
					if (next.Level != entry.Level || next.Message != entry.Message) break;
					runEnd = j;
					++repeatCount;
				}
			}

			// "[HH:MM:SS] " precedes the message (level is conveyed by the row icon + color); the source-link byte span shifts by its length.
			const std::string displayPrefix = entry.Timestamp.empty() ? std::string() : "[" + entry.Timestamp + "] ";
			const ImVec4 levelColor = GetLogLevelColor(entry.Level);
			const std::string fullText = displayPrefix + entry.Message;

			const bool hasInlineLink = !entry.SourceFilePath.empty()
				&& entry.SourceLinkEnd > entry.SourceLinkStart
				&& entry.SourceLinkEnd <= entry.Message.size();
			const size_t linkStart = hasInlineLink ? entry.SourceLinkStart + displayPrefix.size() : 0;
			const size_t linkEnd = hasInlineLink ? entry.SourceLinkEnd + displayPrefix.size() : 0;

			char repeatText[16] = {};
			float badgeWidth = 0.0f;
			if (repeatCount > 1) {
				std::snprintf(repeatText, sizeof(repeatText), "%d", repeatCount);
				badgeWidth = ImGui::CalcTextSize(repeatText).x + 12.0f; // Badge's 6px side padding
			}
			// The repeat badge sits at the right edge; text wraps short of it.
			const float rowWrapWidth = badgeWidth > 0.0f ? std::max(1.0f, wrapWidth - badgeWidth - itemSpacing.x) : wrapWidth;

			// Row height = the wrapped text height (measure pass; matches the draw pass exactly),
			// never shorter than the severity icon.
			const float textHeight = DrawWrappedLogMessage(fullText, linkStart, linkEnd,
				entry.SourceFilePath, entry.SourceLine, levelColor, linkColor, rowWrapWidth, /*draw=*/false);
			const float rowHeight = std::max(textHeight, kLogIconSize);

			// Zebra stripe over the same rect the Selectable highlights (it extends by half the item spacing).
			if (visibleRows++ % 2 == 1) {
				const ImVec2 s = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImVec2(s.x - itemSpacing.x * 0.5f, s.y - itemSpacing.y * 0.5f),
					ImVec2(s.x + contentWidth + itemSpacing.x * 0.5f, s.y + rowHeight + itemSpacing.y * 0.5f),
					altRowColor);
			}

			ImGui::PushID(static_cast<int>(i));

			// A full-width Selectable underneath gives each row its own click/selection + highlight
			// that spans the entire wrapped block; the icon and message draw on top. AllowOverlap
			// keeps the inline source link's hover/click working through the selectable.
			const ImVec2 rowStart = ImGui::GetCursorPos();
			const bool rowSelected = m_SelectedLogIndex >= static_cast<int>(i)
				&& m_SelectedLogIndex <= static_cast<int>(runEnd);
			if (ImGui::Selectable("##logrow", rowSelected,
				ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowHeight))) {
				m_SelectedLogIndex = static_cast<int>(i);
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				m_SelectedLogIndex = static_cast<int>(i);
			}
			const ImVec2 rowEnd = ImGui::GetCursorPos();

			if (repeatCount > 1) {
				// Badge centres itself in a frame-height line; lift it so it centres on the first text line.
				const float lift = (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
				ImGui::SetCursorPos(ImVec2(rowStart.x + contentWidth - badgeWidth, rowStart.y - lift));
				EditorUI::Badge(repeatText, GetLogLevelTone(entry.Level));
			}

			// Overlay the severity icon then the wrapped message, starting back at the row's origin.
			ImGui::SetCursorPos(rowStart);
			const uint64_t levelIcon = EditorIcons::Get(GetLogLevelIconName(entry.Level), 16);
			if (levelIcon) {
				ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(levelIcon)),
					ImVec2(kLogIconSize, kLogIconSize), ImVec2(0, 1), ImVec2(1, 0));
			}
			else {
				// No icon (atlas load failure): still reserve the gutter so the message origin
				// matches wrapWidth's kIconAdvance budget and text stays aligned across rows.
				ImGui::Dummy(ImVec2(kLogIconSize, kLogIconSize));
			}
			ImGui::SameLine(0.0f, 5.0f);

			DrawWrappedLogMessage(fullText, linkStart, linkEnd, entry.SourceFilePath,
				entry.SourceLine, levelColor, linkColor, rowWrapWidth, /*draw=*/true,
				displayPrefix.size(), timestampColor);

			// Resume below the row regardless of how tall the overlaid content turned out.
			ImGui::SetCursorPos(rowEnd);
			ImGui::PopID();

			i = runEnd;
		}

		// The last row's SetCursorPos() isn't followed by an item, so ImGui won't grow the
		// child's content/scroll region to it — submit a zero-size item to claim the extent.
		ImGui::Dummy(ImVec2(0.0f, 0.0f));

		if (visibleRows == 0) {
			EditorUI::EmptyState(m_LogEntries.empty() ? "No messages" : "No messages match the filter");
		}

		if (wasAtBottom) {
			ImGui::SetScrollHereY(1.0f);
		}

		if (ImGui::BeginPopupContextWindow("##LogTextCtx")) {
			const bool hasSelection = m_SelectedLogIndex >= 0
				&& m_SelectedLogIndex < static_cast<int>(m_LogEntries.size());
			if (ImGui::MenuItem("Copy Selected", nullptr, false, hasSelection)) {
				const LogEntry& sel = m_LogEntries[m_SelectedLogIndex];
				std::string text;
				if (!sel.Timestamp.empty()) text += "[" + sel.Timestamp + "] ";
				text += std::string(GetLogLevelPrefix(sel.Level)) + sel.Message;
				ImGui::SetClipboardText(text.c_str());
			}
			if (ImGui::MenuItem("Copy All Visible")) {
				std::string all;
				for (const auto& visibleEntry : m_LogEntries) {
					if (!passesFilters(visibleEntry)) {
						continue;
					}
					if (!visibleEntry.Timestamp.empty()) all += "[" + visibleEntry.Timestamp + "] ";
					all += std::string(GetLogLevelPrefix(visibleEntry.Level)) + visibleEntry.Message + "\n";
				}
				ImGui::SetClipboardText(all.c_str());
			}
			ImGui::EndPopup();
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderProjectPanel() {
		if (!m_AssetBrowserInitialized) {
			std::string assetsRoot;
			FunkeProject* project = ProjectManager::GetCurrentProject();
			if (project) assetsRoot = project->AssetsDirectory;
			else assetsRoot = Path::Combine(Path::ExecutableDir(), "Assets");

			if (!Directory::Exists(assetsRoot)) {
				Directory::Create(assetsRoot);
			}

			m_AssetBrowser.Initialize(assetsRoot);
			m_AssetBrowserInitialized = true;
		}

		m_AssetBrowser.Render();
		if (m_AssetBrowser.TakeSelectionActivated() && !m_AssetBrowser.GetSelectedPath().empty()) {
			ClearEntitySelection();
		}
	}

	void ImGuiEditorLayer::ReportBuildProgress(float progress, std::string_view stage) {
		// Update progress and stage as a pair under the same lock so the UI
		// thread can never observe a stage that doesn't match the bar fraction.
		std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
		m_BuildProgress = std::clamp(progress, 0.0f, 1.0f);
		m_BuildStage.assign(stage);
	}

	bool ImGuiEditorLayer::RequestCommandLineBuild(const std::string& profileName, const std::string& outputDirectory, std::string& outError) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			outError = "No project loaded (pass --project=<path>).";
			return false;
		}
		if (m_BuildState != 0 || m_ReserializeState != 0) {
			outError = "Another build or reserialization is already running.";
			return false;
		}

		const std::string resolvedProfile = GameBuilder::ResolveProfileName(*project, profileName, std::nullopt, outError);
		if (resolvedProfile.empty()) {
			return false;
		}
		project->ActiveBuildProfileName = resolvedProfile;
		if (!project->Save()) {
			outError = "Failed to save project.json.";
			return false;
		}

		std::filesystem::path output = Path::FromUtf8(outputDirectory.empty()
			? GameBuilder::DefaultOutputDirectory(*project, resolvedProfile)
			: outputDirectory);
		std::error_code absoluteError;
		output = std::filesystem::absolute(output, absoluteError);
		m_BuildOutputDir = Path::ToUtf8(output);
		std::snprintf(m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer), "%s", m_BuildOutputDir.c_str());

		m_BuildAndPlay = false;
		m_CommandLineBuild = true;
		m_BuildStartTime = std::chrono::steady_clock::now();
		m_BuildState = 1;
		FNK_INFO_TAG("Build", "Command-line build queued: profile '{}', output '{}'", resolvedProfile, m_BuildOutputDir);
		return true;
	}

	void ImGuiEditorLayer::ExecuteBuildAsync() {
		{
			std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
			m_BuildProgress = 0.0f;
		}
		m_BuildSucceeded.store(true, std::memory_order_relaxed);
		ReportBuildProgress(0.0f, "Starting build");

		FunkeProject* project = ProjectManager::GetCurrentProject();
		auto completeWithoutWorker = [this]() {
			m_BuildFuture = std::async(std::launch::async, []() {});
		};
		if (!project) {
			FNK_ERROR_TAG("Build", "No project loaded.");
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "No project loaded");
			completeWithoutWorker();
			return;
		}

		// Freeze authored disk state before the worker starts copying it.
		if (m_PrefabEditScene && (m_PrefabEditScene->IsDirty() || m_PrefabEditDirty)
			&& !SavePrefabEditChanges()) {
			FNK_ERROR_TAG("Build", "Failed to save the open prefab edit before building.");
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "Prefab save failed");
			completeWithoutWorker();
			return;
		}
		if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()
			&& !SavePrefabInspectorChanges()) {
			FNK_ERROR_TAG("Build", "Failed to save the open prefab inspector before building.");
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "Prefab save failed");
			completeWithoutWorker();
			return;
		}

		bool sceneSaveFailed = false;
		SceneManager::ForeachLoadedScene([&](Scene& scene) {
			if (sceneSaveFailed || !scene.IsDirty()) return;
			if (!SceneSerializer::SaveToFile(scene, project->GetSceneFilePath(scene.GetName()))) {
				FNK_ERROR_TAG("Build", "Failed to save scene '{}' before building.", scene.GetName());
				sceneSaveFailed = true;
				return;
			}
			scene.ClearDirty();
		});
		if (sceneSaveFailed) {
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "Scene save failed");
			completeWithoutWorker();
			return;
		}

		Scene* active = SceneManager::GetActiveScene();
		if (active) project->LastOpenedScene = active->GetName();
		GameBuilder::Request request;
		std::string prepareError;
		if (!GameBuilder::Prepare(*project, request, prepareError)) {
			FNK_ERROR_TAG("Build", "{}", prepareError);
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, prepareError);
			completeWithoutWorker();
			return;
		}
		if (!project->Save()) {
			FNK_ERROR_TAG("Build", "Failed to save project.json before building.");
			m_BuildSucceeded.store(false, std::memory_order_relaxed);
			ReportBuildProgress(1.0f, "Project save failed");
			completeWithoutWorker();
			return;
		}

		request.OutputDirectory = m_BuildOutputDirBuffer;
		m_BuildStartTime = std::chrono::steady_clock::now();
		m_BuiltExecutablePath.clear();
		m_BuildFuture = std::async(std::launch::async, [this, request = std::move(request)]() mutable {
			const GameBuilder::Result result = GameBuilder::Build(std::move(request),
				[this](float progress, std::string_view stage) { ReportBuildProgress(progress, stage); });
			m_BuildOutputDir = result.OutputDirectory;
			m_BuiltExecutablePath = result.ExecutablePath;
			m_BuildSucceeded.store(result.Succeeded, std::memory_order_relaxed);
		});
	}

	void ImGuiEditorLayer::ReportReserializeProgress(float progress, std::string_view stage) {
		// Progress + stage written under one lock so the UI never observes a mismatched pair.
		std::lock_guard<std::mutex> lk(m_ReserializeProgressMutex);
		m_ReserializeProgress = std::clamp(progress, 0.0f, 1.0f);
		m_ReserializeStage.assign(stage);
	}

	void ImGuiEditorLayer::ExecuteReserializeAsync() {
		m_ReserializeConverted.store(0, std::memory_order_relaxed);
		ReportReserializeProgress(0.0f, "Scanning project assets");

		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			return; // poll's !valid() guard clears the state next frame
		}

		const SceneSerializationFormat format = GameBuilder::ToSceneSerializationFormat(project->AssetSerializationFormat);
		const std::string assetsDir = project->AssetsDirectory;

		if (m_PrefabEditScene && (m_PrefabEditScene->IsDirty() || m_PrefabEditDirty)
			&& !SavePrefabEditChanges()) {
			FNK_ERROR_TAG("ProjectSettings", "Failed to save the open prefab before reserialization.");
			ReportReserializeProgress(1.0f, "Prefab save failed");
			return;
		}
		if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()
			&& !SavePrefabInspectorChanges()) {
			FNK_ERROR_TAG("ProjectSettings", "Failed to save the prefab inspector before reserialization.");
			ReportReserializeProgress(1.0f, "Prefab save failed");
			return;
		}

		bool sceneSaveFailed = false;
		SceneManager::ForeachLoadedScene([&](Scene& scene) {
			if (sceneSaveFailed || !scene.IsDirty()) return;
			const std::string scenePath = project->GetSceneFilePath(scene.GetName());
			if (scenePath.empty() || !SceneSerializer::SaveToFile(scene, scenePath, format)) {
				sceneSaveFailed = true;
				FNK_ERROR_TAG("ProjectSettings", "Failed to save scene '{}' before reserialization.", scene.GetName());
			}
		});
		if (sceneSaveFailed) {
			ReportReserializeProgress(1.0f, "Scene save failed");
			return;
		}

		m_ReserializeFuture = std::async(std::launch::async, [this, assetsDir, format]() {
			const int converted = GameBuilder::ConvertSerializedAssets(assetsDir, format,
				[this](int done, int total, const std::string& fileName) {
					// done+1 = the item now being converted, matching the "(k/total)" label; clamped in
					// ReportReserializeProgress so the final done==total call lands at 1.0.
					const float fraction = total > 0
						? static_cast<float>(done + 1) / static_cast<float>(total)
						: 1.0f;
					if (fileName.empty()) {
						ReportReserializeProgress(fraction, "Finishing");
					}
					else {
						ReportReserializeProgress(fraction,
							"Converting " + fileName + "  ("
							+ std::to_string(done + 1) + "/" + std::to_string(total) + ")");
					}
				});
			m_ReserializeConverted.store(converted, std::memory_order_relaxed);
		});
	}

	void ImGuiEditorLayer::RenderBuildPanel() {
		if (!m_ShowBuildPanel) return;

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		// Panes and footer are sized to the window, so the window itself never scrolls.
		ImGui::Begin("Build", &m_ShowBuildPanel, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		FunkeProject* project = ProjectManager::GetCurrentProject();
		std::vector<FunkeBuildProfile> diskProfiles;
		if (project) {
			// Reconcile the build scene list against on-disk scenes only when the registry actually
			// changed AND is already clean. NEVER force a rebuild here: the old AssetRegistry::Sync()
			// ran the full ~9k-.meta directory walk on the UI thread — that was the ~1s freeze when
			// this panel opened. While the registry is mid-edit (dirty) we keep the cached list; the
			// next consumer rebuilds it and bumps the change version, and we pick it up then.
			// Until then the list persisted in project.json is shown as-is, so it is never blank.
			std::vector<FunkeProject::BuildSceneEntry>& sceneList = project->BuildSceneList;
			bool sceneListChanged = false;
			const uint64_t registryVersion = AssetRegistry::GetChangeVersion();
			if (registryVersion != m_BuildSceneListVersion && !AssetRegistry::IsDirty()) {
				m_BuildSceneListVersion = registryVersion;

				std::vector<std::string> diskScenes;
				diskScenes.reserve(sceneList.size() + 4);
				for (const AssetRegistry::Record& record : AssetRegistry::FindAll(AssetKind::Scene)) {
					diskScenes.push_back(std::filesystem::path(record.Path).stem().string());
				}

				// Drop entries whose .scene file no longer exists.
				const size_t sizeBefore = sceneList.size();
				sceneList.erase(
					std::remove_if(sceneList.begin(), sceneList.end(),
						[&](const FunkeProject::BuildSceneEntry& entry) {
							return std::find(diskScenes.begin(), diskScenes.end(), entry.Name) == diskScenes.end();
						}),
					sceneList.end());
				sceneListChanged |= sceneList.size() != sizeBefore;

				// New scenes on disk join the build by default; opting out is a checkbox.
				for (const std::string& stem : diskScenes) {
					if (FindBuildScene(sceneList, stem) == sceneList.end()) {
						sceneList.push_back({ stem, true });
						sceneListChanged = true;
					}
				}
			}

			// StartupScene is always the first enabled entry, so the [Startup] badge and the
			// runtime's first-loaded scene agree with project.json.
			auto startupIt = FindBuildScene(sceneList, project->StartupScene);
			if (startupIt != sceneList.end() && startupIt->Enabled) {
				const auto firstEnabledIt = std::find_if(sceneList.begin(), sceneList.end(),
					[](const FunkeProject::BuildSceneEntry& entry) { return entry.Enabled; });
				if (startupIt != firstEnabledIt) {
					const auto insertAt = std::distance(sceneList.begin(), firstEnabledIt);
					FunkeProject::BuildSceneEntry startupEntry = std::move(*startupIt);
					sceneList.erase(startupIt);
					sceneList.insert(sceneList.begin() + insertAt, std::move(startupEntry));
					sceneListChanged = true;
				}
			}
			// A missing StartupScene is only trusted as "gone" once the list was reconciled with disk.
			else if (startupIt != sceneList.end() || m_BuildSceneListVersion != 0) {
				const std::string firstEnabled = FirstEnabledBuildScene(sceneList);
				if (!firstEnabled.empty() && firstEnabled != project->StartupScene) {
					project->StartupScene = firstEnabled;
					sceneListChanged = true;
				}
			}

			if (sceneListChanged) {
				project->Save();
			}

			if (!project->GetBuildProfilesDirectory().empty()) {
				std::error_code ec;
				const std::string dir = project->GetBuildProfilesDirectory();
				if (std::filesystem::exists(dir)) {
					const std::string ext(FunkeBuildProfile::FileExtension);
					for (const auto& entry : std::filesystem::directory_iterator(
							 dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
						if (ec) { ec.clear(); continue; }
						if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
						if (entry.path().extension().string() != ext) continue;
						diskProfiles.push_back(FunkeBuildProfile::Load(entry.path().string()));
					}
					std::sort(diskProfiles.begin(), diskProfiles.end(),
						[](const FunkeBuildProfile& a, const FunkeBuildProfile& b) {
							return a.Name < b.Name;
						});
				}
			}

			// Clear stale ActiveBuildProfileName if the file was deleted
			// externally — keeps the UI honest after a manual delete.
			if (!project->ActiveBuildProfileName.empty()) {
				bool stillExists = false;
				for (const auto& p : diskProfiles) {
					if (p.Name == project->ActiveBuildProfileName) { stillExists = true; break; }
				}
				if (!stillExists) {
					project->ActiveBuildProfileName.clear();
					project->Save();
				}
			}

			// Default to the first profile for this desktop; leave empty if none exists (don't silently target another OS).
			if (project->ActiveBuildProfileName.empty()) {
				for (const auto& p : diskProfiles) {
					if (BuildPlatformSupport::IsHostDesktop(p.Platform)) {
						project->ActiveBuildProfileName = p.Name;
						project->Save();
						break;
					}
				}
			}
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float frameH = ImGui::GetFrameHeight();
		constexpr float k_CardPad = 8.0f;
		// Footer = separator + spacing + one button row; the body takes the rest.
		const float footerH = std::ceil(frameH + style.ItemSpacing.y * 3.0f + 1.0f);
		const float bodyH = std::max(frameH * 4.0f, ImGui::GetContentRegionAvail().y - footerH);

		auto sectionTitle = [](const char* label) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::SeparatorText(label);
			ImGui::PopStyleColor();
		};
		// BeginCard draws no inner padding, so pad the content here.
		const float cardPadY = std::max(0.0f, k_CardPad - style.ItemSpacing.y);
		auto beginCard = [&](const char* id) {
			EditorUI::BeginCard(id);
			ImGui::Dummy(ImVec2(0.0f, cardPadY));
			ImGui::Indent(k_CardPad);
		};
		auto endCard = [&]() {
			ImGui::Unindent(k_CardPad);
			ImGui::Dummy(ImVec2(0.0f, cardPadY));
			EditorUI::EndCard();
		};
		float labelW = 0.0f;
		for (const char* label : { "Active profile", "Platform", "Rendering API", "Build profile", "Output directory", "Defines", "Project" }) {
			labelW = std::max(labelW, ImGui::CalcTextSize(label).x);
		}
		labelW += style.ItemSpacing.x * 2.0f;
		auto rowLabel = [&](const char* label) {
			const float x = ImGui::GetCursorPosX();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", label);
			ImGui::SameLine(x + labelW);
		};

		if (!project) {
			ImGui::BeginChild("##BuildBody", ImVec2(0.0f, bodyH));
			EditorUI::EmptyState("No project loaded");
			ImGui::EndChild();
		}
		else {
			const float leftW = std::floor((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.45f);

			// ── Left: scenes in build ──
			ImGui::BeginChild("##BuildScenesPane", ImVec2(leftW, bodyH), ImGuiChildFlags_None,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			sectionTitle("Scenes in build");
			const float footerRowH = frameH + style.ItemSpacing.y;
			const float listH = std::max(frameH * 2.0f, ImGui::GetContentRegionAvail().y - footerRowH);
			std::vector<FunkeProject::BuildSceneEntry>& sceneList = project->BuildSceneList;
			// Row edits are applied after the loop so the list isn't mutated mid-iteration.
			int moveFrom = -1;
			int moveTo = -1;
			bool sceneListEdited = false;

			EditorUI::BeginCard("##BuildSceneCard");
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(k_CardPad, k_CardPad));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			ImGui::BeginChild("##BuildSceneList", ImVec2(0.0f, listH), ImGuiChildFlags_AlwaysUseWindowPadding);
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();

			if (sceneList.empty()) {
				EditorUI::EmptyState("No scenes", "Create a scene, or drag a .scene asset here.");
			}
			else {
				const float indexW = ImGui::CalcTextSize("00").x + style.ItemSpacing.x;
				const float startupBadgeW = ImGui::CalcTextSize("Startup").x + 12.0f; // Badge pads 6px per side
				const int sceneCount = static_cast<int>(sceneList.size());
				int startupIndex = -1;
				for (int i = 0; i < sceneCount; i++) {
					if (sceneList[i].Enabled) { startupIndex = i; break; }
				}
				// Shown index is the runtime build index, so excluded rows don't consume one.
				int buildIndex = 0;
				for (int i = 0; i < sceneCount; i++) {
					FunkeProject::BuildSceneEntry& entry = sceneList[i];
					ImGui::PushID(i);
					const bool isStartup = (i == startupIndex);

					const float rowX = ImGui::GetCursorPosX();
					ImGui::AlignTextToFramePadding();
					if (entry.Enabled) ImGui::TextDisabled("%d", buildIndex++);
					else ImGui::TextDisabled(" ");
					ImGui::SameLine(rowX + indexW);

					if (ImGui::Checkbox("##IncludeInBuild", &entry.Enabled)) {
						sceneListEdited = true;
					}
					EditorUI::Tooltip(entry.Enabled ? "Included in the build" : "Excluded from the build");
					ImGui::SameLine();

					const float nameW = ImGui::GetContentRegionAvail().x
						- (isStartup ? startupBadgeW + style.ItemSpacing.x : 0.0f);
					const std::string sceneItemId = std::to_string(i);
					if (!entry.Enabled) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					}
					ImGuiUtils::SelectableEllipsis(entry.Name, sceneItemId.c_str(), false, 0,
						ImVec2(std::max(1.0f, nameW), 0.0f));
					if (!entry.Enabled) {
						ImGui::PopStyleColor();
					}

					if (ImGui::BeginPopupContextItem("##BuildSceneRowMenu")) {
						if (ImGui::MenuItem(entry.Enabled ? "Exclude from build" : "Include in build")) {
							entry.Enabled = !entry.Enabled;
							sceneListEdited = true;
						}
						if (ImGui::MenuItem("Set as startup scene", nullptr, false, !isStartup)) {
							entry.Enabled = true;
							moveFrom = i;
							moveTo = 0;
							sceneListEdited = true;
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) {
							moveFrom = i;
							moveTo = i - 1;
						}
						if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < sceneCount)) {
							moveFrom = i;
							moveTo = i + 1;
						}
						ImGui::EndPopup();
					}

					if (ImGui::BeginDragDropSource()) {
						ImGui::SetDragDropPayload("SCENE_LIST_ITEM", &i, sizeof(int));
						ImGui::Text("Move: %s", entry.Name.c_str());
						ImGui::EndDragDropSource();
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_LIST_ITEM")) {
							moveFrom = *static_cast<const int*>(payload->Data);
							moveTo = i;
						}
						ImGui::EndDragDropTarget();
					}

					if (isStartup) {
						ImGui::SameLine();
						EditorUI::Badge("Startup", EditorUI::Tone::Success);
						EditorUI::Tooltip("Loaded first when the built game starts.");
					}

					ImGui::PopID();
				}
			}
			ImGui::EndChild();

			// The whole list area takes asset drops; rows inside take reorder drops.
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
					// Use 1-arg ctor: payload includes trailing '\0' in DataSize, so (data,DataSize) embeds it and breaks extension comparison.
					std::string droppedPath(static_cast<const char*>(payload->Data));
					if (std::filesystem::path(droppedPath).extension() == ".scene") {
						const std::string sceneName = std::filesystem::path(droppedPath).stem().string();
						auto it = FindBuildScene(sceneList, sceneName);
						if (it == sceneList.end()) {
							sceneList.push_back({ sceneName, true });
						}
						else {
							it->Enabled = true;
						}
						sceneListEdited = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			EditorUI::EndCard();

			Scene* openScene = SceneManager::GetActiveScene();
			const std::string openSceneName = openScene ? openScene->GetName() : std::string();
			const auto openSceneIt = FindBuildScene(sceneList, openSceneName);
			const bool canAddOpenScene = !openSceneName.empty()
				&& (openSceneIt == sceneList.end() || !openSceneIt->Enabled);
			ImGui::BeginDisabled(!canAddOpenScene);
			if (ImGui::Button("Add Open Scene")) {
				if (openSceneIt != sceneList.end()) {
					openSceneIt->Enabled = true;
					sceneListEdited = true;
				}
				else if (const std::string scenePath = project->GetSceneFilePath(openSceneName);
					!scenePath.empty() && File::Exists(scenePath)) {
					sceneList.push_back({ openSceneName, true });
					sceneListEdited = true;
				}
				else {
					FNK_WARN_TAG("Build", "Scene '{}' has no .scene file yet; save it before adding it to the build.",
						openSceneName);
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGuiUtils::TextDisabledEllipsis("Drag to reorder, right-click for options.");

			const int sceneCount = static_cast<int>(sceneList.size());
			if (moveFrom >= 0 && moveFrom < sceneCount && moveTo >= 0 && moveTo < sceneCount && moveFrom != moveTo) {
				FunkeProject::BuildSceneEntry moved = std::move(sceneList[moveFrom]);
				sceneList.erase(sceneList.begin() + moveFrom);
				sceneList.insert(sceneList.begin() + moveTo, std::move(moved));
				sceneListEdited = true;
			}
			if (sceneListEdited) {
				if (std::string firstEnabled = FirstEnabledBuildScene(sceneList); !firstEnabled.empty()) {
					project->StartupScene = std::move(firstEnabled);
				}
				project->Save();
			}
			ImGui::EndChild();

			// ── Right: target + settings ──
			ImGui::SameLine();
			ImGui::BeginChild("##BuildTargetPane", ImVec2(0.0f, bodyH));

			sectionTitle("Target");
			beginCard("##BuildTargetCard");

			rowLabel("Active profile");
			const float manageW = ImGui::CalcTextSize("Manage…").x + style.FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(std::max(1.0f,
				ImGui::GetContentRegionAvail().x - manageW - style.ItemInnerSpacing.x - k_CardPad));
			const char* previewLabel = project->ActiveBuildProfileName.empty()
				? "<none>" : project->ActiveBuildProfileName.c_str();
			if (ImGui::BeginCombo("##ActiveBuildProfile", previewLabel)) {
				if (ImGui::Selectable("<none>", project->ActiveBuildProfileName.empty())) {
					if (!project->ActiveBuildProfileName.empty()) {
						project->ActiveBuildProfileName.clear();
						project->Save();
					}
				}
				for (const auto& p : diskProfiles) {
					const bool selected = (project->ActiveBuildProfileName == p.Name);
					if (ImGui::Selectable(p.Name.c_str(), selected)) {
						if (project->ActiveBuildProfileName != p.Name) {
							project->ActiveBuildProfileName = p.Name;
							project->Save();
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			if (ImGui::Button("Manage…")) {
				m_ShowBuildProfilesPanel = true;
			}
			EditorUI::Tooltip("Open the Build Profiles window.");

			const FunkeBuildProfile* active = nullptr;
			for (const auto& p : diskProfiles) {
				if (p.Name == project->ActiveBuildProfileName) { active = &p; break; }
			}

			// Echo what Build will actually target without flipping over to the Build Profiles window.
			if (!project->ActiveBuildProfileName.empty()) {
				if (active) {
					rowLabel("Platform");
					ImGui::Text("%s", FunkeBuildProfile::PlatformDisplayName(active->Platform));
					rowLabel("Rendering API");
					ImGui::Text("%s", FunkeProject::RenderBackendToString(active->RenderBackend));
					if (!BuildPlatformSupport::IsAvailable(active->Platform)) {
						const std::string reason = "[!] " + BuildPlatformSupport::UnavailableReason(active->Platform);
						ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - k_CardPad);
						EditorUI::StatusText(EditorUI::Tone::Warning, reason.c_str());
						ImGui::PopTextWrapPos();
					}
				}
			}
			else {
				EditorUI::EmptyState("No active profile", "Pick one above or click 'Manage…'.");
			}
			endCard();

			ImGui::Spacing();
			sectionTitle("Settings");
			beginCard("##BuildSettingsCard");
			const float valueX = ImGui::GetCursorPosX() + labelW;

			rowLabel("Configuration");
			ImGui::SetNextItemWidth(-k_CardPad);
			constexpr FunkeProject::BuildProfile k_Configurations[] = {
				FunkeProject::BuildProfile::Debug,
				FunkeProject::BuildProfile::Development,
				FunkeProject::BuildProfile::Release,
			};
			const bool profileSetsConfiguration = active && active->Configuration.has_value();
			const FunkeProject::BuildProfile shownConfiguration = profileSetsConfiguration
				? *active->Configuration : project->ActiveBuildProfile;
			ImGui::BeginDisabled(profileSetsConfiguration);
			if (ImGui::BeginCombo("##BuildConfiguration", FunkeProject::BuildProfileToString(shownConfiguration))) {
				for (const FunkeProject::BuildProfile configuration : k_Configurations) {
					if (ImGui::Selectable(FunkeProject::BuildProfileToString(configuration), configuration == shownConfiguration)
						&& configuration != project->ActiveBuildProfile) {
						project->ActiveBuildProfile = configuration;
						// Let the overlays follow the new configuration (Release = off, otherwise on).
						project->ShowRuntimeStats.reset();
						project->ShowRuntimeLogs.reset();
						project->Save();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			if (profileSetsConfiguration) {
				EditorUI::Tooltip("Set by the active build profile. Change it in Build Profiles.");
			}
			else {
				EditorUI::Tooltip("Debug: debug binaries and symbols for attaching a debugger.\n"
					"Development: optimized, with overlays and the dev console.\nRelease: the shipped game.");
			}

			if (active && m_BuildState == 0) {
				const std::string retargeted = PlatformBuildOutputDirectory(
					m_BuildOutputDirBuffer, project->RootDirectory, active->Platform);
				if (retargeted != m_BuildOutputDirBuffer) {
					std::snprintf(m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer), "%s", retargeted.c_str());
				}
			}

			rowLabel("Output directory");
			ImGui::SetNextItemWidth(-k_CardPad);
			ImGui::InputText("##BuildOutputDir", m_BuildOutputDirBuffer, sizeof(m_BuildOutputDirBuffer));

			rowLabel("Defines");
			const float addBtnWidth = ImGui::CalcTextSize("Add").x
				+ style.FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x
				- addBtnWidth - style.ItemInnerSpacing.x - k_CardPad));
			const bool entryEnter = ImGui::InputTextWithHint("##NewCustomDefine",
				"Add a symbol (e.g. STEAM_BUILD)…",
				m_CustomDefineEntryBuffer, sizeof(m_CustomDefineEntryBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			bool addClicked = ImGui::Button("Add##CustomDefine") || entryEnter;
			const bool entryReady = m_CustomDefineEntryBuffer[0] != '\0';
			if (addClicked && entryReady) {
				std::string newDef(m_CustomDefineEntryBuffer);
				// Trim whitespace + reject duplicates / empty strings.
				while (!newDef.empty() && std::isspace(static_cast<unsigned char>(newDef.back()))) newDef.pop_back();
				std::size_t firstNonSpace = 0;
				while (firstNonSpace < newDef.size()
					&& std::isspace(static_cast<unsigned char>(newDef[firstNonSpace]))) ++firstNonSpace;
				newDef.erase(0, firstNonSpace);
				if (!newDef.empty()
					&& std::find(project->CustomDefines.begin(), project->CustomDefines.end(), newDef) == project->CustomDefines.end()) {
					project->CustomDefines.push_back(std::move(newDef));
					project->Save();
				}
				m_CustomDefineEntryBuffer[0] = '\0';
			}

			if (project->CustomDefines.empty()) {
				ImGui::SetCursorPosX(valueX);
				ImGui::TextDisabled("No custom defines.");
			}
			else {
				// Defines flow as chips under the input, wrapping at the card edge.
				const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - k_CardPad;
				int removeIdx = -1;
				for (int i = 0; i < static_cast<int>(project->CustomDefines.size()); ++i) {
					ImGui::PushID(i);
					const std::string& define = project->CustomDefines[i];
					const float chipW = ImGui::CalcTextSize(define.c_str()).x + 12.0f + style.ItemInnerSpacing.x + frameH;
					if (i > 0 && ImGui::GetItemRectMax().x + style.ItemSpacing.x + chipW <= rightEdge) {
						ImGui::SameLine();
					}
					else {
						ImGui::SetCursorPosX(valueX);
					}
					EditorUI::Badge(define.c_str());
					ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
					if (EditorUI::IconButton("##RemoveDefine", "clear", "Remove define", false, false, 0.0f, "x")) {
						removeIdx = i;
					}
					ImGui::PopID();
				}
				if (removeIdx >= 0) {
					project->CustomDefines.erase(project->CustomDefines.begin() + removeIdx);
					project->Save();
				}
			}
			if (active && !active->Defines.empty()) {
				std::string profileDefines;
				for (const std::string& define : active->Defines) profileDefines += (profileDefines.empty() ? "" : " ") + define;
				ImGui::SetCursorPosX(valueX);
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - k_CardPad);
				ImGui::TextDisabled("Profile adds: %s", profileDefines.c_str());
				ImGui::PopTextWrapPos();
			}

			rowLabel("Project");
			if (EditorUI::Button("Open Project Settings")) {
				m_ShowProjectSettings = true;
			}
			endCard();

			ImGui::EndChild();
		}

		// ── Footer: status on the left, actions on the right ──
		ImGui::Separator();
		ImGui::Spacing();

		FunkeBuildProfile activeProfile;
		bool activeProfileResolved = false;
		if (project && !project->ActiveBuildProfileName.empty()) {
			const std::string profilePath =
				(std::filesystem::path(project->GetBuildProfilesDirectory()) /
				 (project->ActiveBuildProfileName + std::string(FunkeBuildProfile::FileExtension))).string();
			if (std::filesystem::exists(profilePath)) {
				activeProfile = FunkeBuildProfile::Load(profilePath);
				activeProfileResolved = true;
			}
		}

		const bool platformAvailable = activeProfileResolved
			&& BuildPlatformSupport::IsAvailable(activeProfile.Platform);

		const float buttonW = std::max(110.0f, ImGui::CalcTextSize("Build and Play").x + style.FramePadding.x * 4.0f);
		const float buttonsW = buttonW * 2.0f + style.ItemSpacing.x;
		const float footerAvail = ImGui::GetContentRegionAvail().x;
		const float buttonsX = ImGui::GetCursorPosX() + std::max(0.0f, footerAvail - buttonsW);
		const float statusW = std::max(1.0f, footerAvail - buttonsW - style.ItemSpacing.x * 2.0f);

		if (m_BuildState > 0) {
			float progress = 0.0f;
			std::string stage;
			{
				std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
				progress = m_BuildProgress;
				stage = m_BuildStage;
			}
			ImGui::ProgressBar(progress, ImVec2(statusW, 0.0f), stage.empty() ? "Building..." : stage.c_str());
		}
		else {
			auto statusLine = [&](EditorUI::Tone tone, const std::string& text) {
				ImGui::AlignTextToFramePadding();
				ImGui::PushStyleColor(ImGuiCol_Text, EditorUI::ToneColor(tone));
				ImGuiUtils::TextEllipsis(text, statusW);
				ImGui::PopStyleColor();
			};
			// Explain a disabled Build first (disabled buttons have no tooltip); otherwise echo the last result.
			if (!project) {
				statusLine(EditorUI::Tone::Neutral, "No project loaded.");
			}
			else if (Application::GetIsPlaying()) {
				statusLine(EditorUI::Tone::Neutral, "Exit play mode to build.");
			}
			else if (!activeProfileResolved) {
				statusLine(EditorUI::Tone::Neutral, "Select an active build profile to enable Build.");
			}
			else if (!platformAvailable) {
				statusLine(EditorUI::Tone::Warning,
					"[!] Cannot build: " + BuildPlatformSupport::UnavailableReason(activeProfile.Platform));
			}
			else {
				std::string lastStage;
				{
					std::lock_guard<std::mutex> lk(m_BuildProgressMutex);
					lastStage = m_BuildStage;
				}
				if (lastStage.empty()) {
					statusLine(EditorUI::Tone::Neutral, "Ready to build.");
				}
				else if (m_BuildSucceeded.load(std::memory_order_relaxed)) {
					statusLine(EditorUI::Tone::Success, "Last build complete: " + m_BuildOutputDir);
				}
				else {
					statusLine(EditorUI::Tone::Error, "Last build failed: " + lastStage);
				}
			}
		}
		ImGui::SameLine(buttonsX);

		bool canBuild = project && !Application::GetIsPlaying() && m_BuildState == 0
			&& activeProfileResolved && platformAvailable;
		if (!canBuild) ImGui::BeginDisabled();

		if (EditorUI::Button("Build and Play", EditorUI::ButtonVariant::Default, ImVec2(buttonW, 0.0f))) {
			if (activeProfileResolved && project && BuildPlatformSupport::IsHostDesktop(activeProfile.Platform)) {
				project->ActiveRenderBackend = activeProfile.RenderBackend;
				project->Save();
			}
			m_BuildState = 1;
			m_BuildAndPlay = true;
			m_BuildStartTime = std::chrono::steady_clock::now();
		}
		ImGui::SameLine();
		if (EditorUI::Button("Build", EditorUI::ButtonVariant::Primary, ImVec2(buttonW, 0.0f))) {
			if (activeProfileResolved && project && BuildPlatformSupport::IsHostDesktop(activeProfile.Platform)) {
				project->ActiveRenderBackend = activeProfile.RenderBackend;
				project->Save();
			}
			m_BuildState = 1;
			m_BuildAndPlay = false;
			m_BuildStartTime = std::chrono::steady_clock::now();
		}

		if (!canBuild) ImGui::EndDisabled();
		ImGui::End();
	}

	void ImGuiEditorLayer::RenderBuildProfilesPanel() {
		if (!m_ShowBuildProfilesPanel) return;
		if (!m_BuildProfilesPanelInitialized) {
			m_BuildProfilesPanel.Initialize();
			m_BuildProfilesPanelInitialized = true;
		}
		m_BuildProfilesPanel.Render(&m_ShowBuildProfilesPanel);
	}

	void ImGuiEditorLayer::RenderProjectSettingsPanel() {
		if (!m_ShowProjectSettings) return;

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		if (!ImGui::Begin("Project Settings", &m_ShowProjectSettings)) {
			ImGui::End();
			return;
		}

		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			EditorUI::EmptyState("No project loaded");
			ImGui::End();
			return;
		}

		constexpr const char* k_CategoryLabels[] = {
			"Display",
			"Graphics",
			"Branding",
			"Build",
			"Editor",
			"Systems",
			"Physics",
		};
		static_assert(IM_ARRAYSIZE(k_CategoryLabels)
			== static_cast<int>(SettingsCategory::Physics) + 1,
			"k_CategoryLabels out of sync with SettingsCategory enum");

		EditorUI::SearchField("##SettingsSearch", m_ProjectSettingsSearchBuffer,
			sizeof(m_ProjectSettingsSearchBuffer), "Search settings...");

		std::string filterLower(m_ProjectSettingsSearchBuffer);
		std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const bool filterActive = !filterLower.empty();

		if (filterActive && !CategoryHasMatches(m_SelectedSettingsCategory, filterLower)) {
			for (int i = 0; i < IM_ARRAYSIZE(k_CategoryLabels); ++i) {
				auto cat = static_cast<SettingsCategory>(i);
				if (CategoryHasMatches(cat, filterLower)) {
					m_SelectedSettingsCategory = cat;
					break;
				}
			}
		}

		ImGui::Separator();

		const float listColumnWidth = ImGui::GetContentRegionAvail().x * 0.22f;
		const float listW = std::max(160.0f, listColumnWidth);

		bool anyVisible = false;
		ImGui::BeginChild("##SettingsNav", ImVec2(listW, 0), /*border*/ true);
		for (int i = 0; i < IM_ARRAYSIZE(k_CategoryLabels); ++i) {
			auto cat = static_cast<SettingsCategory>(i);
			if (filterActive && !CategoryHasMatches(cat, filterLower)) continue;
			anyVisible = true;
			const bool selected = static_cast<int>(m_SelectedSettingsCategory) == i;
			if (ImGui::Selectable(k_CategoryLabels[i], selected)) {
				m_SelectedSettingsCategory = cat;
			}
		}
		if (filterActive && !anyVisible) {
			EditorUI::EmptyState("No matches");
		}
		ImGui::EndChild();

		ImGui::SameLine();

		bool changed = false;
		bool globalSystemsChanged = false;

		ImGui::BeginChild("##SettingsContent", ImVec2(0, 0));
		if (filterActive && !anyVisible) {
			const std::string noMatch = std::string("No settings match \"") + m_ProjectSettingsSearchBuffer + "\"";
			EditorUI::EmptyState(noMatch.c_str(), "Try a different search term.");
		}
		else {
			switch (m_SelectedSettingsCategory) {
				case SettingsCategory::Display:  RenderSettings_Display(*project, changed, filterLower);  break;
				case SettingsCategory::Graphics: RenderSettings_Graphics(*project, changed, filterLower); break;
				case SettingsCategory::Branding: RenderSettings_Branding(*project, changed, filterLower); break;
				case SettingsCategory::Build:    RenderSettings_Build(*project, changed, filterLower);    break;
				case SettingsCategory::Editor:   RenderSettings_Editor(*project, changed, filterLower);   break;
				case SettingsCategory::Systems:
					RenderSettings_Systems(*project, changed, globalSystemsChanged, filterLower);
					break;
				case SettingsCategory::Physics: RenderSettings_Physics(*project, changed, filterLower); break;
			}
		}
		ImGui::EndChild();

		if (changed) {
			project->Save();
			if (globalSystemsChanged) {
				std::vector<std::string> activeGlobalSystems;
				for (const auto& registration : project->GlobalSystems) {
					if (registration.Active && !registration.ClassName.empty()) {
						activeGlobalSystems.push_back(registration.ClassName);
					}
				}
				ScriptEngine::ShutdownGlobalSystems();
				ScriptEngine::InitializeGlobalSystems(activeGlobalSystems);
			}
		}

		ReferencePicker::RenderPopup();

		ImGui::End();
	}

	void ImGuiEditorLayer::RenderSettings_Display(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Display, "Window", filterLower)) {
			if (SettingsSectionHeader("Window", filterLower)) {
				ImGui::Indent(8);

				ImGui::TextDisabled("Startup");
				ImGui::Separator();

				changed |= ImGui::InputInt("Width", &project.BuildWidth, 0, 0);
				changed |= ImGui::InputInt("Height", &project.BuildHeight, 0, 0);
				if (project.BuildWidth < 320) project.BuildWidth = 320;
				if (project.BuildHeight < 240) project.BuildHeight = 240;

				changed |= ImGuiUtils::Switch("Fullscreen", &project.BuildFullscreen);

				struct FullscreenModeOption {
					const char* Label;
					FullscreenMode Mode;
				};
				static constexpr std::array<FullscreenModeOption, 3> k_FullscreenModeOptions = {{
					{ "Exclusive",          FullscreenMode::Exclusive },
					{ "Borderless Windowed", FullscreenMode::BorderlessWindowed },
					{ "Maximized",          FullscreenMode::Maximized },
				}};
				const bool fullscreenEnabled = project.BuildFullscreen;
				if (!fullscreenEnabled) ImGui::BeginDisabled();
				const char* currentLabel = "Exclusive";
				for (const auto& opt : k_FullscreenModeOptions) {
					if (opt.Mode == project.BuildFullscreenMode) {
						currentLabel = opt.Label;
						break;
					}
				}
				ImGui::SetNextItemWidth(220.0f);
				if (ImGui::BeginCombo("Fullscreen Mode", currentLabel)) {
					for (const auto& opt : k_FullscreenModeOptions) {
						const bool selected = (opt.Mode == project.BuildFullscreenMode);
						if (ImGui::Selectable(opt.Label, selected)) {
							project.BuildFullscreenMode = opt.Mode;
							changed = true;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				if (!fullscreenEnabled) ImGui::EndDisabled();
				EditorUI::Tooltip(
					"How the runtime realises fullscreen at launch:\n"
					"  Exclusive — true fullscreen, takes over the display.\n"
					"  Borderless Windowed — undecorated window sized to the\n"
					"      monitor; Alt-Tab is instant.\n"
					"  Maximized — decorated, maximised window.\n"
					"\n"
					"Disabled when 'Fullscreen' is off — the value is\n"
					"ignored in windowed launches.");

				ImGui::Spacing();
				ImGui::TextDisabled("Runtime");
				ImGui::Separator();

				changed |= ImGuiUtils::Switch("Resizable", &project.BuildResizable);
				changed |= ImGuiUtils::Switch("Run in background", &project.BuildRunInBackground);

				ImGui::Spacing();
				ImGui::TextDisabled("Resize Limits (0 = unbounded)");
				changed |= ImGui::InputInt("Min Width",  &project.BuildMinWidth, 0, 0);
				changed |= ImGui::InputInt("Min Height", &project.BuildMinHeight, 0, 0);
				changed |= ImGui::InputInt("Max Width",  &project.BuildMaxWidth, 0, 0);
				changed |= ImGui::InputInt("Max Height", &project.BuildMaxHeight, 0, 0);
				if (project.BuildMinWidth  < 0) project.BuildMinWidth  = 0;
				if (project.BuildMinHeight < 0) project.BuildMinHeight = 0;
				if (project.BuildMaxWidth  < 0) project.BuildMaxWidth  = 0;
				if (project.BuildMaxHeight < 0) project.BuildMaxHeight = 0;
				if (project.BuildMaxWidth  > 0 && project.BuildMinWidth  > project.BuildMaxWidth)
					EditorUI::StatusText(EditorUI::Tone::Warning, "Min Width > Max Width");
				if (project.BuildMaxHeight > 0 && project.BuildMinHeight > project.BuildMaxHeight)
					EditorUI::StatusText(EditorUI::Tone::Warning, "Min Height > Max Height");

				ImGui::Spacing();
				int aspectIndex = AspectRatioPresetIndexFromLabel(project.BuildAspect);
				ImGui::SetNextItemWidth(220.0f);
				if (ImGui::BeginCombo("Aspect Ratio", k_AspectRatioPresets[aspectIndex].Label)) {
					for (int i = 0; i < static_cast<int>(k_AspectRatioPresets.size()); ++i) {
						const bool selected = (i == aspectIndex);
						if (ImGui::Selectable(k_AspectRatioPresets[i].Label, selected)) {
							project.BuildAspect = k_AspectRatioPresets[i].Label;
							changed = true;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				EditorUI::Tooltip(
					"Locks the shipped game to this aspect ratio.\n"
					"When the window's aspect doesn't match, the renderer\n"
					"adds black bars (letterbox/pillarbox) so the game\n"
					"always appears at the chosen aspect.\n"
					"\n"
					"Independent of the editor's Game View aspect.\n"
					"Select 'Free Aspect' to disable and fill the full window.");
				ImGui::Unindent(8);
			}
		}

		if (SectionVisible(SettingsCategory::Display, "Mobile", filterLower)) {
			if (SettingsSectionHeader("Mobile", filterLower)) {
				ImGui::Indent(8);
				struct OrientationOption {
					const char* Label;
					ScreenOrientation Value;
				};
				static constexpr std::array<OrientationOption, 9> k_OrientationOptions = {{
					{ "Auto (from window size)",               ScreenOrientation::Auto },
					{ "Landscape (either side)",               ScreenOrientation::Landscape },
					{ "Landscape Left (top points left)",      ScreenOrientation::LandscapeLeft },
					{ "Landscape Right (top points right)",    ScreenOrientation::LandscapeRight },
					{ "Portrait",                              ScreenOrientation::Portrait },
					{ "Portrait Upside Down",                  ScreenOrientation::PortraitUpsideDown },
					{ "Portrait (either way up)",              ScreenOrientation::PortraitAny },
					{ "Any except Upside Down",                ScreenOrientation::AnyExceptUpsideDown },
					{ "Any",                                   ScreenOrientation::Any },
				}};
				const char* currentLabel = k_OrientationOptions[0].Label;
				for (const auto& option : k_OrientationOptions) {
					if (option.Value == project.MobileOrientation) currentLabel = option.Label;
				}
				ImGui::SetNextItemWidth(260.0f);
				if (ImGui::BeginCombo("Orientation", currentLabel)) {
					for (const auto& option : k_OrientationOptions) {
						const bool selected = option.Value == project.MobileOrientation;
						if (ImGui::Selectable(option.Label, selected)) {
							project.MobileOrientation = option.Value;
							changed = true;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				EditorUI::Tooltip(
					"Which way up the game may be held on Android and iOS.\n"
					"Left/Right name the side the top of the device points to.\n"
					"Options with more than one direction turn with the device.\n"
					"Auto uses landscape when the window is wider than tall.\n"
					"Desktop and Web builds ignore this.");
				ImGui::Unindent(8);
			}
		}

		if (SectionVisible(SettingsCategory::Display, "UI Scaling", filterLower)) {
			if (SettingsSectionHeader("UI Scaling", filterLower)) {
				ImGui::Indent(8);
				changed |= ImGui::InputInt("Reference Width##UIRef", &project.UIReferenceWidth, 0, 0);
				changed |= ImGui::InputInt("Reference Height##UIRef", &project.UIReferenceHeight, 0, 0);
				if (project.UIReferenceWidth  < 1) project.UIReferenceWidth  = 1;
				if (project.UIReferenceHeight < 1) project.UIReferenceHeight = 1;

				using UIScaleMode = FunkeProject::UIScaleMode;
				struct ScaleModeOption {
					const char* Label;
					UIScaleMode Value;
				};
				static constexpr std::array<ScaleModeOption, 3> k_ScaleModeOptions = {{
					{ "Match Width or Height", UIScaleMode::MatchWidthOrHeight },
					{ "Fit (always fully visible)", UIScaleMode::Fit },
					{ "Fill (covers the screen)", UIScaleMode::Fill },
				}};
				const char* currentScaleLabel = k_ScaleModeOptions[0].Label;
				for (const auto& option : k_ScaleModeOptions) {
					if (option.Value == project.UIScaling) currentScaleLabel = option.Label;
				}
				ImGui::SetNextItemWidth(260.0f);
				if (ImGui::BeginCombo("Scale Mode##UIRef", currentScaleLabel)) {
					for (const auto& option : k_ScaleModeOptions) {
						const bool selected = option.Value == project.UIScaling;
						if (ImGui::Selectable(option.Label, selected)) {
							project.UIScaling = option.Value;
							changed = true;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				EditorUI::Tooltip(
					"How the reference resolution is scaled to the screen.\n"
					"Fit: the whole reference area stays visible on every aspect ratio\n"
					"(wide phones, tall windows); extra space appears around it.\n"
					"Fill: the reference area covers the screen; edges may be cut off.\n"
					"Match Width or Height: blend set by the slider below.");

				if (project.UIScaling == UIScaleMode::MatchWidthOrHeight) {
					changed |= ImGui::SliderFloat("Match Width / Height", &project.UIScaleMatch, 0.0f, 1.0f, "%.2f");
					EditorUI::Tooltip(
						"0 = scale UI by window WIDTH only.\n"
						"1 = scale UI by window HEIGHT only.\n"
						"0.5 = balanced (geometric mean).\n"
						"\n"
						"Move toward 1 if your UI hugs the top/bottom edges,\n"
						"toward 0 if it hugs the left/right edges.");
				}
				ImGui::Unindent(8);
			}
		}
	}

	void ImGuiEditorLayer::RenderSettings_Graphics(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Graphics, "Render Backend", filterLower)) {
		if (SettingsSectionHeader("Render Backend", filterLower)) {
			ImGui::Indent(8);

			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("Rendering API", RenderBackendLabel(project.ActiveRenderBackend))) {
				for (const RenderBackendOption& option : k_RenderBackendOptions) {
					const bool selected = option.Backend == project.ActiveRenderBackend;
					const bool supported = IsRenderBackendSupportedOnHost(option.Backend);
					if (!supported) ImGui::BeginDisabled();
					if (ImGui::Selectable(option.Label, selected)) {
						// The editor's reload prompt picks the change up (RenderEditorReloadPrompt).
						if (supported && option.Backend != project.ActiveRenderBackend) {
							project.ActiveRenderBackend = option.Backend;
							changed = true;
						}
					}
					if (!supported) ImGui::EndDisabled();
					if (!supported) EditorUI::Tooltip(RenderBackendUnsupportedReason(option.Backend));
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			EditorUI::Tooltip(
				"Which native API Dawn (WebGPU) uses at runtime. The\n"
				"choice is applied when the engine requests its GPU\n"
				"adapter at startup, so the change only takes effect\n"
				"after restarting the editor / built game.\n"
				"\n"
				"  • Auto: Dawn picks the most capable backend for\n"
				"    the host platform (D3D12 on Windows, Metal on\n"
				"    macOS, Vulkan on Linux).\n"
				"  • Vulkan / D3D11 / D3D12 / OpenGL: explicit backend.\n"
				"    Selecting one not supported by the host platform\n"
				"    (e.g. D3D12 on Linux) makes adapter-request fail\n"
				"    at startup; the log line says exactly which\n"
				"    backend was requested.");

			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Render Backend")

		if (SectionVisible(SettingsCategory::Graphics, "Runtime Quality", filterLower)) {
		if (SettingsSectionHeader("Runtime Quality", filterLower)) {
			ImGui::Indent(8);
			bool graphicsChanged = false;
			graphicsChanged |= ImGui::SliderFloat(
				"Render Scale", &project.Graphics.RenderScale,
				GraphicsSettings::MinRenderScale, GraphicsSettings::MaxRenderScale, "%.2f",
				ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("Resolution used for the 2D scene; UI and gizmos stay at native resolution.");

			const char* upscaleLabels[] = { "Point", "Linear" };
			int upscale = static_cast<int>(project.Graphics.UpscaleFilter);
			if (ImGui::Combo("Upscale Filter", &upscale, upscaleLabels, IM_ARRAYSIZE(upscaleLabels))) {
				project.Graphics.UpscaleFilter = static_cast<UpscaleFilterMode>(upscale);
				graphicsChanged = true;
			}

			graphicsChanged |= ImGuiUtils::Switch(
				"Enable Post-Processing", &project.Graphics.PostProcessingEnabled);

			const char* postQualityLabels[] = { "Low", "Medium", "High" };
			int postQuality = static_cast<int>(project.Graphics.PostProcessingQuality);
			if (ImGui::Combo("Post-Processing Quality", &postQuality,
				postQualityLabels, IM_ARRAYSIZE(postQualityLabels))) {
				project.Graphics.PostProcessingQuality =
					static_cast<PostProcessingQualityLevel>(postQuality);
				graphicsChanged = true;
			}

			const char* particleQualityLabels[] = { "Off", "Low", "Medium", "High" };
			int particleQuality = static_cast<int>(project.Graphics.ParticleQuality);
			if (ImGui::Combo("Particle Quality", &particleQuality,
				particleQualityLabels, IM_ARRAYSIZE(particleQualityLabels))) {
				project.Graphics.ParticleQuality = static_cast<ParticleQualityLevel>(particleQuality);
				graphicsChanged = true;
			}

			const char* textureQualityLabels[] = { "Full", "Half", "Quarter" };
			int textureQuality = static_cast<int>(project.Graphics.TextureQuality);
			if (ImGui::Combo("Texture Quality", &textureQuality,
				textureQualityLabels, IM_ARRAYSIZE(textureQualityLabels))) {
				project.Graphics.TextureQuality = static_cast<TextureQualityLevel>(textureQuality);
				graphicsChanged = true;
			}
			EditorUI::Tooltip("Half and Quarter skip the top mip levels of scene textures.\n"
				"Point-filtered textures (pixel art) and UI always stay at full resolution.");

			const char* anisotropyLabels[] = { "Off", "2x", "4x", "8x", "16x" };
			int anisotropy = static_cast<int>(project.Graphics.AnisotropicFiltering);
			if (ImGui::Combo("Anisotropic Filtering", &anisotropy,
				anisotropyLabels, IM_ARRAYSIZE(anisotropyLabels))) {
				project.Graphics.AnisotropicFiltering = static_cast<AnisotropicFilteringLevel>(anisotropy);
				graphicsChanged = true;
			}
			EditorUI::Tooltip("Keeps textures sharp on surfaces seen at a grazing angle.\n"
				"Applies to textures whose filter is Trilinear or Anisotropic.");

			const char* antiAliasingLabels[] = { "Off", "FXAA", "MSAA 4x" };
			int antiAliasing = static_cast<int>(project.Graphics.AntiAliasing);
			if (ImGui::Combo("Anti-Aliasing", &antiAliasing,
				antiAliasingLabels, IM_ARRAYSIZE(antiAliasingLabels))) {
				project.Graphics.AntiAliasing = static_cast<AntiAliasingMode>(antiAliasing);
				graphicsChanged = true;
			}
			EditorUI::Tooltip("Smooths jagged edges of the scene; UI and gizmos stay as drawn.\n"
				"FXAA: 2D and 3D, also softens hard edges inside textures a little.\n"
				"MSAA 4x: edges of 3D geometry only; textures (pixel art) stay exactly as they are.");

			ImGui::BeginDisabled(project.Graphics.AntiAliasing != AntiAliasingMode::FXAA);
			const char* antiAliasingQualityLabels[] = { "Low", "Medium", "High" };
			int antiAliasingQuality = static_cast<int>(project.Graphics.AntiAliasingQuality);
			if (ImGui::Combo("Anti-Aliasing Quality", &antiAliasingQuality,
				antiAliasingQualityLabels, IM_ARRAYSIZE(antiAliasingQualityLabels))) {
				project.Graphics.AntiAliasingQuality = static_cast<AntiAliasingQualityLevel>(antiAliasingQuality);
				graphicsChanged = true;
			}
			ImGui::EndDisabled();

			if (graphicsChanged) {
				changed = true;
				RenderSettings::Apply(project.Graphics);
			}
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Runtime Quality")

		if (EngineModules::IsActive(EngineModule::ThreeD) && SectionVisible(SettingsCategory::Graphics, "3D Rendering", filterLower)) {
		if (SettingsSectionHeader("3D Rendering", filterLower)) {
			ImGui::Indent(8);
			bool graphicsChanged = false;

			const char* shadowLabels[] = { "Off", "Low (1024)", "Medium (2048)", "High (4096)" };
			int shadowQuality = static_cast<int>(project.Graphics.ShadowQuality);
			if (ImGui::Combo("Shadow Quality", &shadowQuality, shadowLabels, IM_ARRAYSIZE(shadowLabels))) {
				project.Graphics.ShadowQuality = static_cast<ShadowQualityLevel>(shadowQuality);
				graphicsChanged = true;
			}
			EditorUI::Tooltip("Shadow map resolution of the main Directional Light.");

			graphicsChanged |= ImGui::DragFloat("Shadow Distance", &project.Graphics.ShadowDistance, 0.5f,
				GraphicsSettings::MinShadowDistance, GraphicsSettings::MaxShadowDistance, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("World units from the camera that receive directional shadows; smaller is sharper.");

			graphicsChanged |= ImGui::SliderInt("Max Lights", &project.Graphics.MaxLights,
				GraphicsSettings::MinMaxLights, GraphicsSettings::MaxMaxLights, "%d", ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("Lights per frame: directional lights first, then the point and spot lights nearest the camera.");

			graphicsChanged |= ImGui::DragFloat("Exposure", &project.Graphics.Exposure, 0.01f,
				GraphicsSettings::MinExposure, GraphicsSettings::MaxExposure, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			const char* toneMappingLabels[] = { "None", "Reinhard", "ACES" };
			int toneMapping = static_cast<int>(project.Graphics.ToneMapping);
			if (ImGui::Combo("Tone Mapping", &toneMapping, toneMappingLabels, IM_ARRAYSIZE(toneMappingLabels))) {
				project.Graphics.ToneMapping = static_cast<ToneMappingMode>(toneMapping);
				graphicsChanged = true;
			}

			if (graphicsChanged) {
				changed = true;
				RenderSettings::Apply(project.Graphics);
			}
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("3D Rendering")

		if (SectionVisible(SettingsCategory::Graphics, "Animation", filterLower)) {
		if (SettingsSectionHeader("Animation", filterLower)) {
			ImGui::Indent(8);
			bool graphicsChanged = ImGuiUtils::Switch("Distance Rate", &project.Graphics.AnimationDistanceRateEnabled);
			EditorUI::Tooltip("Animators and Skinned Mesh Renderers whose Distance Rate is Project Setting update less often far "
				"from the game camera; the skipped time is added to their next update.");
			ImGui::BeginDisabled(!project.Graphics.AnimationDistanceRateEnabled);
			graphicsChanged |= ImGui::DragFloat("Half Rate From", &project.Graphics.AnimationHalfRateDistance, 0.5f,
				0.0f, GraphicsSettings::MaxAnimationRateDistance, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("World units from the camera from which animations update every 2nd frame (30 of 60 fps).");
			graphicsChanged |= ImGui::DragFloat("Quarter Rate From", &project.Graphics.AnimationQuarterRateDistance, 0.5f,
				0.0f, GraphicsSettings::MaxAnimationRateDistance, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("World units from the camera from which animations update every 4th frame.");
			ImGui::EndDisabled();
			if (graphicsChanged) {
				changed = true;
				RenderSettings::Apply(project.Graphics);
			}
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Animation")

		if (SectionVisible(SettingsCategory::Graphics, "Default Font", filterLower)) {
		if (SettingsSectionHeader("Default Font", filterLower)) {
			ImGui::Indent(8);
			constexpr const char* k_DefaultFontPickerKey = "ProjectSettings.DefaultFont";
			if (auto pending = ReferencePicker::ConsumeSelection(k_DefaultFontPickerKey); pending) {
				uint64_t pickedId = ParsePickerAssetId(*pending);
				project.DefaultFontAssetId = pickedId != 0 ? pickedId : k_DefaultFontAssetId;
				changed = true;
			}

			bool fontMissing = false;
			std::string fontSecondary;
			std::string fontDisplay = ReferencePicker::ResolveAssetDisplay(
				project.DefaultFontAssetId, AssetKind::Font, fontMissing, &fontSecondary);
			ImGui::TextUnformatted("Default Font");
			ImGui::SameLine(130.0f);
			ImGui::PushID("ProjectDefaultFont");
			if (ImGui::Button(fontDisplay.c_str(), ImVec2(260.0f, 0.0f))) {
				ReferencePicker::OpenForFieldKey(k_DefaultFontPickerKey, "Select Default Font",
					ReferencePicker::CollectAssetsByKind(AssetKind::Font),
					ReferencePicker::Style::Plain);
			}
			if (!fontSecondary.empty()) EditorUI::Tooltip(fontSecondary.c_str());
			ImGui::PopID();
			ImGui::SameLine();
			if (ImGui::Button("Reset##ProjectDefaultFont")) {
				project.DefaultFontAssetId = k_DefaultFontAssetId;
				changed = true;
			}

			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Default Font")

		if (SectionVisible(SettingsCategory::Graphics, "Font Baking", filterLower)) {
		if (SettingsSectionHeader("Font Baking", filterLower)) {
			ImGui::Indent(8);

			// Bake-step: 0 = adaptive ladder, N = snap every N px. Applied live so
			// the change takes effect without a restart.
			int bakeStep = project.FontBakeStep;
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::SliderInt("Bake size step", &bakeStep, 0, 32,
				bakeStep == 0 ? "Adaptive" : "%d px", ImGuiSliderFlags_AlwaysClamp)) {
				project.FontBakeStep = bakeStep;
				FontManager::SetBakeStep(bakeStep);
				changed = true;
			}
			EditorUI::Tooltip(
				"Pixel sizes the font auto-bakes at.\n"
				"0 = Adaptive: fine steps for small text, coarse for large.\n"
				"N = bake only at multiples of N px. Higher N = fewer atlases = less memory,\n"
				"but in-between sizes are scaled from the nearest baked size.");

			int budgetMB = project.FontAtlasBudgetMB;
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::SliderInt("Atlas memory budget", &budgetMB, 0, 512,
				budgetMB == 0 ? "Unlimited" : "%d MB", ImGuiSliderFlags_AlwaysClamp)) {
				project.FontAtlasBudgetMB = budgetMB;
				FontManager::SetAtlasMemoryBudgetMB(budgetMB);
				changed = true;
			}
			EditorUI::Tooltip(
				"Cap on resident font-atlas memory. When exceeded, the\n"
				"least-recently-used baked sizes are evicted (and re-baked on demand).\n"
				"0 = unlimited (atlases accumulate until the scene reloads).");

			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Font Baking")
	}

	void ImGuiEditorLayer::RenderSettings_Physics(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Physics, "Physics World", filterLower)) {
		if (SettingsSectionHeader("Physics World", filterLower)) {
			ImGui::Indent(8);

			// Apply immediately while persisting the per-run default; shared bounds match Time's clamp.
			int hz = project.PhysicsTickRate;
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::SliderInt("Tick rate", &hz,
				FunkeProject::k_MinPhysicsHz, FunkeProject::k_MaxPhysicsHz,
				"%d Hz", ImGuiSliderFlags_AlwaysClamp)) {
				project.PhysicsTickRate = hz;
				Time::SetFixedDeltaTime(project.GetFixedDeltaTimeSeconds());
				changed = true;
			}
			EditorUI::Tooltip(
				"How many times per second the physics world steps and\n"
				"FixedUpdate runs (fixed timestep = 1 / Hz).\n"
				"Higher = more accurate simulation, more CPU. Default 50.\n"
				"\n"
				"This is the per-run default applied at startup and on each\n"
				"Play; scripts can still override it at runtime via\n"
				"Time.fixedDeltaTime.");

			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Physics World")
	}

	void ImGuiEditorLayer::RenderSettings_Branding(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Branding, "Splash Screen", filterLower)) {
		if (SettingsSectionHeader("Splash Screen", filterLower)) {
			ImGui::Indent(8);
			constexpr const char* k_SplashPickerKey = "ProjectSettings.SplashImage";

			if (auto pending = ReferencePicker::ConsumeSelection(k_SplashPickerKey); pending) {
				const std::string raw = *pending;
				uint64_t pickedId = 0;
				try { pickedId = std::stoull(raw); } catch (...) { pickedId = 0; }
				if (pickedId == 0) {
					project.SplashScreen.ImagePath.clear();
					changed = true;
				}
				else {
					std::string absPath = AssetRegistry::ResolvePath(pickedId);
					if (!absPath.empty()) {
						project.SplashScreen.ImagePath =
							SplashAssetResolve::NormalizeForStorage(absPath, &project);
						changed = true;
					}
				}
			}

			changed |= ImGuiUtils::Switch("Enabled##Splash", &project.SplashScreen.Enabled);
			EditorUI::Tooltip("When off, the runtime loads the startup scene immediately with no splash.");

			if (project.SplashScreen.Enabled) {
				ImGui::Spacing();
				changed |= ImGui::SliderFloat("Duration (s)##Splash",
					&project.SplashScreen.DurationSeconds, 0.5f, 10.0f, "%.2f");
				changed |= ImGui::SliderFloat("Fade In (s)##Splash",
					&project.SplashScreen.FadeInSeconds, 0.0f, 3.0f, "%.2f");
				changed |= ImGui::SliderFloat("Fade Out (s)##Splash",
					&project.SplashScreen.FadeOutSeconds, 0.0f, 3.0f, "%.2f");

				ImGui::Spacing();
				float bg[3] = {
					project.SplashScreen.BackgroundR,
					project.SplashScreen.BackgroundG,
					project.SplashScreen.BackgroundB,
				};
				if (ImGui::ColorEdit3("Background##Splash", bg)) {
					project.SplashScreen.BackgroundR = bg[0];
					project.SplashScreen.BackgroundG = bg[1];
					project.SplashScreen.BackgroundB = bg[2];
					changed = true;
				}

				float fontColor[3] = {
					project.SplashScreen.FontColorR,
					project.SplashScreen.FontColorG,
					project.SplashScreen.FontColorB,
				};
				if (ImGui::ColorEdit3("Font Color##Splash", fontColor)) {
					project.SplashScreen.FontColorR = fontColor[0];
					project.SplashScreen.FontColorG = fontColor[1];
					project.SplashScreen.FontColorB = fontColor[2];
					changed = true;
				}
				if (ImGui::DragFloat("Font Size##Splash", &project.SplashScreen.FontSize,
						0.5f, 6.0f, 96.0f, "%.0f px")) {
					if (project.SplashScreen.FontSize < 1.0f) project.SplashScreen.FontSize = 1.0f;
					changed = true;
				}

				ImGui::Spacing();
				ImGui::TextUnformatted("Image (optional, replaces default Funke logo):");
				if (project.SplashScreen.ImagePath.empty()) {
					// Render the engine default logo as a placeholder so
					// the user sees what'll ship. Same source the runtime
					// splash falls back to.
					const std::string defaultLogoPath = SplashAssetResolve::DefaultLogoPath();
					if (!defaultLogoPath.empty()) {
						TextureHandle h = TextureManager::LoadTexture(defaultLogoPath);
						Texture2D* tex = TextureManager::GetTexture(h);
						if (tex && tex->IsValid()) {
							ImGui::Image(
								static_cast<ImTextureID>(static_cast<intptr_t>(tex->GetHandle())),
								ImVec2(48, 48));
							ImGui::SameLine();
						}
					}
					ImGui::TextDisabled("(Default)");
				}
				else {
					ImGuiUtils::TextDisabledEllipsis(project.SplashScreen.ImagePath);
				}
				if (ImGui::Button("Browse...##SplashImage")) {
					ReferencePicker::OpenForFieldKey(k_SplashPickerKey, "Select Splash Image",
						ReferencePicker::CollectAssetsByKind(AssetKind::Texture),
						ReferencePicker::Style::Thumbnails);
				}
				if (BrowseAcceptImageDrop(k_ImageExts, [&](const std::string& dropped) {
					project.SplashScreen.ImagePath =
						SplashAssetResolve::NormalizeForStorage(dropped, &project);
					changed = true;
				})) {}
				if (!project.SplashScreen.ImagePath.empty()) {
					ImGui::SameLine();
					if (ImGui::Button("Clear##SplashImage")) {
						project.SplashScreen.ImagePath.clear();
						changed = true;
					}
				}

				ImGui::Spacing();
				ImGui::TextUnformatted("Background Image (optional, painted full-screen behind logo):");
				constexpr const char* k_SplashBgPickerKey = "ProjectSettings.SplashBackground";

				// Apply a pending background-image picker selection — same
				// path-normalisation flow as the foreground image so the
				// stored value stays project-relative.
				if (auto pending = ReferencePicker::ConsumeSelection(k_SplashBgPickerKey); pending) {
					const std::string raw = *pending;
					uint64_t pickedId = 0;
					try { pickedId = std::stoull(raw); } catch (...) { pickedId = 0; }
					if (pickedId == 0) {
						project.SplashScreen.BackgroundImagePath.clear();
						changed = true;
					}
					else {
						std::string absPath = AssetRegistry::ResolvePath(pickedId);
						if (!absPath.empty()) {
							project.SplashScreen.BackgroundImagePath =
								SplashAssetResolve::NormalizeForStorage(absPath, &project);
							changed = true;
						}
					}
				}

				if (project.SplashScreen.BackgroundImagePath.empty()) {
					ImGui::TextDisabled("(no background image — solid colour fill above)");
				}
				else {
					ImGuiUtils::TextDisabledEllipsis(project.SplashScreen.BackgroundImagePath);
				}
				if (ImGui::Button("Browse...##SplashBgImage")) {
					ReferencePicker::OpenForFieldKey(k_SplashBgPickerKey, "Select Splash Background",
						ReferencePicker::CollectAssetsByKind(AssetKind::Texture),
						ReferencePicker::Style::Thumbnails);
				}
				if (BrowseAcceptImageDrop(k_ImageExts, [&](const std::string& dropped) {
					project.SplashScreen.BackgroundImagePath =
						SplashAssetResolve::NormalizeForStorage(dropped, &project);
					changed = true;
				})) {}
				if (!project.SplashScreen.BackgroundImagePath.empty()) {
					ImGui::SameLine();
					if (ImGui::Button("Clear##SplashBgImage")) {
						project.SplashScreen.BackgroundImagePath.clear();
						changed = true;
					}
				}

				ImGui::Spacing();
				ImGui::TextUnformatted("Custom Text (optional, replaces version + platform line):");
				char textBuf[256];
				std::snprintf(textBuf, sizeof(textBuf), "%s", project.SplashScreen.CustomText.c_str());
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputTextWithHint("##SplashText", "Leave empty for engine default",
					textBuf, sizeof(textBuf))) {
					project.SplashScreen.CustomText = textBuf;
					changed = true;
				}

				ImGui::Spacing();
				if (ImGui::Button("Show Preview")) {
					m_SplashPreviewRequest = true;
				}
				EditorUI::Tooltip(
					"Replay the splash screen using the current\n"
					"settings, drawn on top of the editor for the\n"
					"FadeIn + Duration + FadeOut window.");
			}
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Splash Screen")

		if (SectionVisible(SettingsCategory::Branding, "App Icon", filterLower)) {
		if (SettingsSectionHeader("App Icon", filterLower)) {
			ImGui::Indent(8);

			constexpr const char* k_AppIconPickerKey = "ProjectSettings.AppIcon";

			// AppIconPath is the shipped game's icon only; do NOT push to SetWindowIcon — the editor keeps its own embedded RT_ICON.
			if (auto pending = ReferencePicker::ConsumeSelection(k_AppIconPickerKey); pending) {
				const std::string raw = *pending;
				uint64_t pickedId = 0;
				try { pickedId = std::stoull(raw); } catch (...) { pickedId = 0; }
				if (pickedId == 0) {
					project.AppIconPath.clear();
					changed = true;
				}
				else {
					std::string absPath = AssetRegistry::ResolvePath(pickedId);
					if (!absPath.empty()) {
						project.AppIconPath =
							SplashAssetResolve::NormalizeForStorage(absPath, &project);
						changed = true;
					}
				}
			}

			if (!project.AppIconPath.empty()) {
				TextureHandle iconHandle = TextureManager::LoadTexture(
					SplashAssetResolve::Resolve(project.AppIconPath, &project));
				Texture2D* iconTex = TextureManager::GetTexture(iconHandle);
				if (iconTex && iconTex->IsValid()) {
					ImGuiUtils::ImageFiltered(
						static_cast<ImTextureID>(static_cast<intptr_t>(iconTex->GetHandle())),
						ImVec2(64, 64), iconTex->GetFilter());
					ImGui::SameLine();
				}
				else {
					EditorUI::StatusText(EditorUI::Tone::Error, "Failed to load:");
					ImGuiUtils::TextDisabledEllipsis(project.AppIconPath);
				}

				ImGui::BeginGroup();
				if (ImGui::Button("Browse...##AppIcon")) {
					ReferencePicker::OpenForFieldKey(k_AppIconPickerKey, "Select App Icon",
						ReferencePicker::CollectAssetsByKind(AssetKind::Texture),
						ReferencePicker::Style::Thumbnails);
				}
				if (BrowseAcceptImageDrop(k_ImageExts, [&](const std::string& dropped) {
					project.AppIconPath =
						SplashAssetResolve::NormalizeForStorage(dropped, &project);
					changed = true;
				})) {}
				ImGui::SameLine();
				if (ImGui::Button("Clear##AppIcon")) {
					project.AppIconPath.clear();
					changed = true;
				}
				ImGui::EndGroup();

				if (iconTex && iconTex->IsValid()) {
					ImGuiUtils::TextDisabledEllipsis(project.AppIconPath);
				}
			}
			else {
				const std::string defaultIconPath = SplashAssetResolve::DefaultLogoPath();
				if (!defaultIconPath.empty()) {
					TextureHandle defIconHandle = TextureManager::LoadTexture(defaultIconPath);
					Texture2D* defIconTex = TextureManager::GetTexture(defIconHandle);
					if (defIconTex && defIconTex->IsValid()) {
						ImGui::Image(
							static_cast<ImTextureID>(static_cast<intptr_t>(defIconTex->GetHandle())),
							ImVec2(64, 64));
						ImGui::SameLine();
					}
				}

				ImGui::BeginGroup();
				if (defaultIconPath.empty()) {
					ImGui::TextDisabled("No icon set");
				}
				else {
					ImGui::TextDisabled("(Default)");
				}
				if (ImGui::Button("Browse...##AppIcon")) {
					ReferencePicker::OpenForFieldKey(k_AppIconPickerKey, "Select App Icon",
						ReferencePicker::CollectAssetsByKind(AssetKind::Texture),
						ReferencePicker::Style::Thumbnails);
				}
				if (BrowseAcceptImageDrop(k_ImageExts, [&](const std::string& dropped) {
					project.AppIconPath =
						SplashAssetResolve::NormalizeForStorage(dropped, &project);
					changed = true;
				})) {}
				ImGui::SameLine();
				ImGui::TextDisabled("or drag an image onto the Browse button");
				ImGui::EndGroup();
			}

			ImGui::Unindent(8);
		}
		}  // end SectionVisible("App Icon")

		if (SectionVisible(SettingsCategory::Branding, "Cursors", filterLower)) {
		if (SettingsSectionHeader("Cursors", filterLower)) {
			ImGui::Indent(8);

			auto resolveProjectRelativeCursor = [&](const std::string& rel) -> std::string {
				std::filesystem::path p(rel);
				if (p.is_absolute()) return rel;
				return (std::filesystem::path(project.RootDirectory) / p).string();
			};

			// Apply a project-relative path to the live Window cursor so
			// the change is visible immediately rather than requiring a
			// restart. Empty path clears the slot back to the OS default.
			auto loadCursorTexture = [&](const std::string& path) -> Texture2D* {
				if (path.empty()) return nullptr;
				const std::string abs = resolveProjectRelativeCursor(path);
				if (!std::filesystem::exists(abs)) return nullptr;
				Texture2D* tex = TextureManager::GetTexture(TextureManager::LoadTexture(abs));
				return tex && tex->IsValid() ? tex : nullptr;
			};

			using CursorSetter = void (Window::*)(const Texture2D*, Vec2Int);
			auto applyCursorLive = [&](const std::string& path, Vec2Int hotspot, CursorSetter setter)
			{
				Window* win = Application::GetInstance() ? Application::GetInstance()->GetWindow() : nullptr;
				if (!win) return;
				if (path.empty()) {
					(win->*setter)(nullptr, {});
					return;
				}
				if (Texture2D* tex = loadCursorTexture(path)) {
					(win->*setter)(tex, hotspot);
				}
				else {
					FNK_WARN_TAG("Editor", "Cursor image not found: {}", resolveProjectRelativeCursor(path));
				}
			};

			auto renderCursorSlot = [&](const char* label,
				const char* pickerKey, std::string& slotPath, Vec2Int& hotspot,
				CursorSetter liveSetter)
			{
				ImGui::PushID(label);
				ImGui::TextUnformatted(label);

				if (auto pending = ReferencePicker::ConsumeSelection(pickerKey); pending) {
					const std::string raw = *pending;
					uint64_t pickedId = 0;
					try { pickedId = std::stoull(raw); } catch (...) { pickedId = 0; }
					if (pickedId == 0) {
						slotPath.clear();
						changed = true;
						applyCursorLive("", hotspot, liveSetter);
					}
					else {
						std::string absPath = AssetRegistry::ResolvePath(pickedId);
						if (!absPath.empty()) {
							std::filesystem::path absFs(absPath);
							std::filesystem::path assetsDir(project.AssetsDirectory);
							if (absFs.string().find(assetsDir.string()) == 0) {
								slotPath = std::filesystem::relative(absFs, assetsDir.parent_path()).string();
							}
							else {
								slotPath = absFs.filename().string();
							}
							changed = true;
							applyCursorLive(slotPath, hotspot, liveSetter);
						}
					}
				}

				if (slotPath.empty()) {
					ImGui::TextDisabled("(OS default)");
				}
				else {
					ImGuiUtils::TextDisabledEllipsis(slotPath);
				}

				if (ImGui::Button("Browse...")) {
					ReferencePicker::OpenForFieldKey(pickerKey, "Select Cursor Image",
						ReferencePicker::CollectAssetsByKind(AssetKind::Texture),
						ReferencePicker::Style::Thumbnails);
				}
				if (BrowseAcceptImageDrop(k_ImageExts, [&](const std::string& dropped) {
					slotPath = ToProjectRelativeAssetPath(dropped, project.AssetsDirectory);
					changed = true;
					applyCursorLive(slotPath, hotspot, liveSetter);
				})) {}
				if (!slotPath.empty()) {
					ImGui::SameLine();
					if (ImGui::Button("Clear")) {
						slotPath.clear();
						hotspot = { 0, 0 };
						changed = true;
						applyCursorLive("", hotspot, liveSetter);
					}

					const Texture2D* tex = loadCursorTexture(slotPath);
					const Vec2Int imageSize = tex
						? Vec2Int(static_cast<int>(tex->GetWidth()), static_cast<int>(tex->GetHeight()))
						: Vec2Int(0, 0);
					int values[2] = { hotspot.x, hotspot.y };
					ImGui::SetNextItemWidth(140.0f);
					bool hotspotChanged = ImGui::DragInt2("Hotspot", values, 0.25f, 0,
						std::max(imageSize.x, imageSize.y) > 0 ? std::max(imageSize.x, imageSize.y) - 1 : 4096);
					// Rebuild the OS cursor only once a drag settles; regenerating every DPI image per frame stalls the editor.
					bool applyNow = ImGui::IsItemDeactivatedAfterEdit();
					EditorUI::Tooltip("Click point in image pixels, measured from the top-left corner.\n"
						"0, 0 suits arrow cursors; use Center for crosshairs.");
					if (imageSize.x > 0) {
						ImGui::SameLine();
						if (ImGui::SmallButton("Top-left")) { values[0] = 0; values[1] = 0; hotspotChanged = applyNow = true; }
						ImGui::SameLine();
						if (ImGui::SmallButton("Center")) { values[0] = imageSize.x / 2; values[1] = imageSize.y / 2; hotspotChanged = applyNow = true; }
						values[0] = std::clamp(values[0], 0, imageSize.x - 1);
						values[1] = std::clamp(values[1], 0, imageSize.y - 1);
					}
					if (hotspotChanged) {
						hotspot = { std::max(0, values[0]), std::max(0, values[1]) };
						changed = true;
					}
					if (applyNow) {
						applyCursorLive(slotPath, hotspot, liveSetter);
					}
				}
				ImGui::PopID();
			};

			renderCursorSlot("Default cursor", "ProjectSettings.DefaultCursor",
				project.CursorImagePath, project.CursorHotspot, &Window::SetCursorImage);
			ImGui::Spacing();
			renderCursorSlot("UI hover cursor", "ProjectSettings.UICursor",
				project.UIInteractableCursorImagePath, project.UIInteractableCursorHotspot, &Window::SetUICursorImage);
			EditorUI::Tooltip(
				"Shown while the cursor sits over a Funke UI element\n"
				"with an Interactable component (Button / Slider / etc.).\n"
				"Falls back to the default cursor when unset.");

			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Cursors")
	}

	void ImGuiEditorLayer::RenderSettings_Build(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Build, "Engine Modules", filterLower)) {
		if (SettingsSectionHeader("Engine Modules", filterLower)) {
			ImGui::Indent(8);
			// Managed next to packages so a package that needs a module can enable it; shown here read-only.
			std::string enabled;
			for (EngineModule module : k_AllEngineModules) {
				if (project.HasModule(EngineModules::GetId(module))) {
					enabled += (enabled.empty() ? "" : ", ") + std::string(EngineModules::GetId(module));
				}
			}
			ImGui::TextUnformatted(("Enabled: " + (enabled.empty() ? std::string("none (UI only)") : enabled)).c_str());
			ImGui::TextDisabled("Add or remove Funke.2D and Funke.3D in the Package Manager.");
			if (ImGui::Button("Open Package Manager")) {
				m_ShowPackageManager = true;
			}
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Engine Modules")

		if (SectionVisible(SettingsCategory::Build, "Executable", filterLower)) {
		if (SettingsSectionHeader("Executable", filterLower)) {
			ImGui::Indent(8);
			ImGui::TextUnformatted("Output Name:");
			char exeBuf[256];
			std::string current = project.ExecutableName.empty() ? project.Name : project.ExecutableName;
			std::snprintf(exeBuf, sizeof(exeBuf), "%s", current.c_str());
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##ExecutableName", exeBuf, sizeof(exeBuf))) {
				const std::string newName(exeBuf);
				project.ExecutableName = (newName == project.Name) ? "" : newName;
				changed = true;
			}
			ImGui::TextDisabled("Default: project name (\"%s\"). The platform extension is appended automatically.",
				project.Name.c_str());

			ImGui::Spacing();
			ImGui::TextUnformatted("Version:");
			char versionBuf[256];
			std::snprintf(versionBuf, sizeof(versionBuf), "%s", project.BuildVersion.c_str());
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##BuildVersion", versionBuf, sizeof(versionBuf))) {
				project.BuildVersion = versionBuf;
				changed = true;
			}
			ImGui::TextDisabled("Surfaced to scripts as Application.Version (e.g. 1.0.0).");

			ImGui::Spacing();
			ImGui::TextUnformatted("Company:");
			char companyBuf[256];
			std::snprintf(companyBuf, sizeof(companyBuf), "%s", project.BuildCompany.c_str());
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##BuildCompany", companyBuf, sizeof(companyBuf))) {
				project.BuildCompany = companyBuf;
				changed = true;
			}
			ImGui::TextDisabled("Surfaced to scripts as Application.Company.");
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Executable")

		if (SectionVisible(SettingsCategory::Build, "Diagnostics", filterLower)) {
		if (SettingsSectionHeader("Diagnostics", filterLower)) {
			ImGui::Indent(8);
			bool showStats = project.ResolveShowRuntimeStats();
			if (ImGuiUtils::Switch("Show runtime stats overlay (F6)", &showStats)) {
				project.ShowRuntimeStats = showStats;
				changed = true;
			}
			bool showLogs = project.ResolveShowRuntimeLogs();
			if (ImGuiUtils::Switch("Show runtime log overlay (F7)", &showLogs)) {
				project.ShowRuntimeLogs = showLogs;
				changed = true;
			}
			bool devConsole = project.ResolveDevConsole();
			if (ImGuiUtils::Switch("Dev console (` key, physics debug on F8)", &devConsole)) {
				project.DevConsole = devConsole;
				changed = true;
			}
			ImGui::TextDisabled("Default: on for Debug and Development builds, off for Release builds.");
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Diagnostics")

		if (SectionVisible(SettingsCategory::Build, "ECS Entity Bits", filterLower)) {
		if (SettingsSectionHeader("ECS Entity Bits", filterLower)) {
			ImGui::Indent(8);
			ImGui::TextUnformatted("Entity ID bits:");
			const char* entityBitsLabels[] = {
				"16 bits  (65,535 entities / 65,536 versions)",
				"20 bits  (1,048,575 entities / 4,096 versions)  [default]",
				"22 bits  (4,194,303 entities / 1,024 versions)",
				"24 bits  (16,777,215 entities / 256 versions)",
				"28 bits  (268,435,455 entities / 16 versions)"
			};
			constexpr int entityBitsValues[] = { 16, 20, 22, 24, 28 };
			int entityBitsIdx = 1; // matches the 20-bit default
			for (int i = 0; i < IM_ARRAYSIZE(entityBitsValues); ++i) {
				if (entityBitsValues[i] == project.EntityBits) {
					entityBitsIdx = i;
					break;
				}
			}
			ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo("##EntityBits", &entityBitsIdx, entityBitsLabels, IM_ARRAYSIZE(entityBitsLabels))) {
				const int newBits = entityBitsValues[entityBitsIdx];
				if (newBits != project.EntityBits) {
					project.EntityBits = newBits;
					changed = true;
				}
			}
			EditorUI::Tooltip(
				"Maximum number of live entities the scene can hold.\n"
				"Trades against the per-slot version count EnTT uses\n"
				"to detect stale handles to recycled IDs.\n"
				"\n"
				"Compile-time setting — the engine must be rebuilt for\n"
				"the change to take effect. Click \"Rebuild Engine\" below\n"
				"to do that automatically.");

			const int compiledBits = Funke::GetCompiledEntityBits();
			ImGui::Spacing();
			if (compiledBits != project.EntityBits) {
				char bitsStatus[64];
				std::snprintf(bitsStatus, sizeof(bitsStatus), "Compiled: %d bits   Pending: %d bits",
					compiledBits, project.EntityBits);
				EditorUI::StatusText(EditorUI::Tone::Warning, bitsStatus);
				ImGui::Spacing();
				if (ImGui::Button("Rebuild Engine")) {
					ImGui::OpenPopup("Rebuild Engine?");
				}
				EditorUI::Tooltip(
					"Closes the editor and reopens it after the engine has\n"
					"been rebuilt with the new entityBits setting.\n"
					"\n"
					"A separate \"Rebuilding Engine\" window will appear\n"
					"with progress. Don't close it.");
			}
			else {
				ImGui::TextDisabled("Engine matches the selected width. Choose a different size to enable rebuild.");
			}

			// Build runs in a detached terminal (not in-process) because the editor/launcher both hold Funke-Engine.dll locked — MSBuild would fail with LNK1104.
			// Editor writes FunkeEntityBitsConfig.h directly so MSBuild incremental only recompiles TUs that include entt (no premake regen needed).
			ImGuiUtils::CenterNextModal();
			ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
			if (ImGui::BeginPopupModal("Rebuild Engine?", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
				ImGui::TextWrapped(
					"Rebuild the engine with entityBits = %d?\n\n"
					"The editor will close and a terminal window will open showing the build "
					"progress. The editor reopens automatically when the build finishes. "
					"Don't close the terminal window.",
					project.EntityBits);
				const int clicked = EditorUI::DialogFooter({ "Rebuild", "Cancel" }, 0);
				if (clicked == 0) {
					if (!project.Save()) {
						FNK_ERROR_TAG("Editor", "Failed to save project settings; engine rebuild aborted.");
					}
					else {

					const std::filesystem::path editorExeDir = Path::ExecutableDir();
					const std::filesystem::path engineRoot =
						editorExeDir.parent_path().parent_path().parent_path();
					const std::string msbuildPath = FunkeProject::GetMSBuildPath();
					const std::filesystem::path slnPath = engineRoot / "Funke.sln";
				#ifdef FNK_PLATFORM_WINDOWS
					const std::filesystem::path editorExe = editorExeDir / "Funke-Editor.exe";
				#else
					const std::filesystem::path editorExe = editorExeDir / "Funke-Editor";
				#endif
					const std::string config = FunkeProject::GetActiveBuildConfiguration();
					const std::string projectRootDir = project.RootDirectory;
					const std::string userCsprojPath = project.CsprojPath;
					const std::filesystem::path packageBuildScript = engineRoot
						/ "scripts" / "BuildProjectPackages.ps1";
					const int newEntityBits = project.EntityBits;

				#ifdef FNK_PLATFORM_WINDOWS
					const unsigned long editorPid = ::GetCurrentProcessId();
				#else
					const unsigned long editorPid = static_cast<unsigned long>(::getpid());
				#endif

					const std::filesystem::path generatedDir = engineRoot
						/ "Funke-Engine" / "src" / "Generated";
					const std::filesystem::path headerPath = generatedDir
						/ "FunkeEntityBitsConfig.h";
					bool headerWritten = false;
					{
						std::error_code dirEc;
						std::filesystem::create_directories(generatedDir, dirEc);
						if (!dirEc) {
							std::ofstream headerOut(headerPath,
								std::ios::binary | std::ios::trunc);
							if (headerOut) {
								headerOut <<
									"// Auto-generated by the editor's Rebuild Engine flow.\n"
									"// Matches premake's WriteFunkeEntityBitsConfigHeader().\n"
									"// Do not edit by hand.\n"
									"#pragma once\n"
									"#undef  FUNKE_ENTITY_BITS\n"
									"#define FUNKE_ENTITY_BITS " << newEntityBits << "\n";
								headerWritten = static_cast<bool>(headerOut);
							}
						}
						if (!headerWritten) {
							FNK_ERROR_TAG("Editor",
								"Failed to write {}; engine rebuild aborted.",
								headerPath.string());
						}
					}

					if (headerWritten) {
					#ifdef FNK_PLATFORM_WINDOWS
						std::ostringstream bat;
						bat <<
							"@echo off\r\n"
							"setlocal\r\n"
							"title Rebuilding Funke Engine (entityBits=" << newEntityBits << ")\r\n"
							"echo.\r\n"
							"echo ====================================================\r\n"
							"echo   Rebuilding Funke Engine  (entityBits=" << newEntityBits << ")\r\n"
							"echo ====================================================\r\n"
							"echo.\r\n"
							"echo Waiting for editor (PID " << editorPid << ") to exit...\r\n"
							"set /a WAIT_ELAPSED=0\r\n"
							":wait_loop\r\n"
							"tasklist /fi \"PID eq " << editorPid << "\" 2>nul | findstr /i \"" << editorPid << "\" >nul\r\n"
							"if errorlevel 1 goto wait_done\r\n"
							"set /a WAIT_ELAPSED+=1\r\n"
							"if %WAIT_ELAPSED% geq 30 (\r\n"
							"  echo Warning: editor still alive after 30 seconds, proceeding anyway.\r\n"
							"  goto wait_done\r\n"
							")\r\n"
							"timeout /t 1 /nobreak >nul\r\n"
							"goto wait_loop\r\n"
							":wait_done\r\n"
							"echo.\r\n"
							"echo [1/3] Building user scripts (dotnet build)...\r\n"
							"echo.\r\n"
							"dotnet build \"" << userCsprojPath << "\""
							" -c " << config <<
							" --nologo -v q -nodeReuse:false -p:UseSharedCompilation=false\r\n"
							"if errorlevel 1 (\r\n"
							"  echo.\r\n"
							"  echo *** User script build FAILED. C# errors above. ***\r\n"
							"  echo.\r\n"
							"  pause\r\n"
							"  exit /b 1\r\n"
							")\r\n"
							"echo.\r\n"
							"echo [2/3] Building solution (this is the long step)...\r\n"
							"echo.\r\n"
							"pushd \"" << engineRoot.string() << "\"\r\n"
							"\"" << msbuildPath << "\" \"" << slnPath.string() << "\""
							" /p:Configuration=" << config <<
							" /p:Platform=x64 /m /v:minimal /nologo\r\n"
							"if errorlevel 1 (\r\n"
							"  echo.\r\n"
							"  echo *** MSBuild FAILED. Scroll up for compile errors. ***\r\n"
							"  echo.\r\n"
							"  pause\r\n"
							"  exit /b 1\r\n"
							")\r\n"
							"popd\r\n"
							"echo.\r\n"
							"echo [3/3] Rebuilding enabled project packages...\r\n"
							"echo.\r\n"
							"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" << packageBuildScript.string() << "\""
							" -EngineRoot \"" << engineRoot.string() << "\""
							" -ProjectRoot \"" << projectRootDir << "\""
							" -Configuration \"" << config << "\""
							" -Platform \"x64\""
							" -MSBuildPath \"" << msbuildPath << "\"\r\n"
							"if errorlevel 1 (\r\n"
							"  echo.\r\n"
							"  echo *** Project package build FAILED. Scroll up for errors. ***\r\n"
							"  echo.\r\n"
							"  pause\r\n"
							"  exit /b 1\r\n"
							")\r\n"
							"echo.\r\n"
							"echo [Relaunching editor...]\r\n"
							"echo.\r\n"
							"start \"\" \"" << editorExe.string() << "\" --project=\"" << projectRootDir << "\"\r\n"
							"endlocal\r\n"
							"exit /b 0\r\n";
						const std::string scriptText = bat.str();
						const char* const scriptName = "engine-rebuild.bat";
					#else
						(void)msbuildPath;
						(void)slnPath;
						(void)packageBuildScript;
						std::string makeConfig = config;
						std::transform(makeConfig.begin(), makeConfig.end(), makeConfig.begin(),
							[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						const std::string root = ShellQuote(engineRoot.string());
						const std::string projectRoot = ShellQuote(projectRootDir);
						const std::string premake = ShellQuote(FunkeProject::GetPremakePath());
						const std::string make = ShellQuote(FunkeProject::GetMakeProgram());
						const std::string packageGenerator = ShellQuote((engineRoot / "premake" / "project-packages.lua").string());
						const std::string packageWorkspace = ShellQuote(projectRootDir + "/.funke/build/packages");
					#if defined(FNK_PLATFORM_MACOS)
						const char* const premakeSystem = "macosx";
					#else
						const char* const premakeSystem = "linux";
					#endif
						std::ostringstream sh;
						sh <<
							"#!/bin/sh\n"
							"fail() { echo; echo \"*** $1 Scroll up for errors. ***\"; echo 'Press Enter to close.'; read _; exit 1; }\n"
							"echo '===================================================='\n"
							"echo '  Rebuilding Funke Engine  (entityBits=" << newEntityBits << ")'\n"
							"echo '===================================================='\n"
							"echo 'Waiting for editor (PID " << editorPid << ") to exit...'\n"
							"waited=0\n"
							"while kill -0 " << editorPid << " 2>/dev/null && [ $waited -lt 30 ]; do sleep 1; waited=$((waited + 1)); done\n"
							"echo; echo '[1/3] Building user scripts (dotnet build)...'\n"
							"dotnet build " << ShellQuote(userCsprojPath) << " -c " << config <<
							" --nologo -v q -nodeReuse:false -p:UseSharedCompilation=false || fail 'User script build FAILED.'\n"
							"echo; echo '[2/3] Building the engine (this is the long step)...'\n"
							"cd " << root << " || fail 'Engine folder missing.'\n"
							<< make << " config=" << makeConfig << " -j\"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)\" "
							"Funke-Engine Funke-Runtime Funke-Editor || fail 'Engine build FAILED.'\n"
							"echo; echo '[3/3] Rebuilding enabled project packages...'\n"
							"if [ -d " << ShellQuote(projectRootDir + "/Packages") << " ]; then\n"
							// Newline-only splitting keeps paths with spaces intact in the loops below.
							"  IFS='\n'\n"
							"  " << premake << " --file=" << packageGenerator << " --os=" << premakeSystem << " vs2022 --funke-project=" << projectRoot
								<< " || fail 'Package workspace generation FAILED.'\n"
							"  " << premake << " --file=" << packageGenerator << " gmake --funke-project=" << projectRoot
								<< " || fail 'Package workspace generation FAILED.'\n"
							"  for project in $(find " << packageWorkspace << "/generated -name '*.vcxproj'); do\n"
							"    " << make << " -C " << packageWorkspace << " config=" << makeConfig << " \"$(basename \"$project\" .vcxproj)\" || fail 'Native package build FAILED.'\n"
							"  done\n"
							"  for project in $(find " << packageWorkspace << "/generated -name '*.csproj'); do\n"
							"    dotnet build \"$project\" -c " << config << " -p:Platform=x64 --nologo -v q || fail 'Managed package build FAILED.'\n"
							"  done\n"
							"fi\n"
							"echo; echo '[Relaunching editor...]'\n"
							"nohup " << ShellQuote(editorExe.string()) << " --project=" << projectRoot << " >/dev/null 2>&1 &\n"
							"exit 0\n";
						const std::string scriptText = sh.str();
						// .command: macOS Terminal runs it when opened; Linux terminals take any name.
						const char* const scriptName = "engine-rebuild.command";
					#endif

						std::error_code mkdirEc;
						const std::filesystem::path batDir = engineRoot / "bin-int";
						std::filesystem::create_directories(batDir, mkdirEc);
						const std::filesystem::path batPath = batDir / scriptName;
						std::ofstream batFile(batPath, std::ios::binary | std::ios::trunc);
						batFile << scriptText;
						const bool scriptWritten = static_cast<bool>(batFile);
						batFile.close();
					#ifndef FNK_PLATFORM_WINDOWS
						std::error_code permissionEc;
						std::filesystem::permissions(batPath, std::filesystem::perms::owner_exec,
							std::filesystem::perm_options::add, permissionEc);
					#endif

						if (scriptWritten) {
							m_PendingEngineRebuildWorkingDirectory = engineRoot.string();
							RequestEditorAction(PendingEditorAction::RebuildEngine, batPath.string());
							ImGui::CloseCurrentPopup();
						}
						else {
							FNK_ERROR_TAG("Editor",
								"Failed to write engine rebuild script at {}",
								batPath.string());
						}
					}
					}
				}
				else if (clicked == 1) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::Unindent(8);
		}
		}  // end SectionVisible("ECS Entity Bits")
	}

	void ImGuiEditorLayer::RenderSettings_Editor(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Editor, "Asset Browser", filterLower)) {
		if (SettingsSectionHeader("Asset Browser", filterLower)) {
			ImGui::Indent(8);
			constexpr const char* k_AssetSuffixLabels[] = {
				"Asset 1",
				"Asset (1)",
				"Asset-1",
				"Asset_1",
			};
			int assetSuffixIndex = static_cast<int>(project.EditorAssetDuplicateSuffix);
			if (assetSuffixIndex < 0 || assetSuffixIndex >= IM_ARRAYSIZE(k_AssetSuffixLabels)) {
				assetSuffixIndex = static_cast<int>(FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber);
			}
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::Combo("Duplicate suffix##AssetDuplicateSuffix", &assetSuffixIndex, k_AssetSuffixLabels, IM_ARRAYSIZE(k_AssetSuffixLabels))) {
				project.EditorAssetDuplicateSuffix =
					static_cast<FunkeProject::EditorEntityNameSuffixStyle>(assetSuffixIndex);
				changed = true;
			}
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Asset Browser")

		if (SectionVisible(SettingsCategory::Editor, "Entities", filterLower)) {
		if (SettingsSectionHeader("Entities", filterLower)) {
			ImGui::Indent(8);
			changed |= ImGuiUtils::Switch("Ensure unique editor-created names", &project.EditorEnsureUniqueEntityNames);
			EditorUI::Tooltip(
				"When on, entities created through editor UI actions,\n"
				"shortcuts, paste, duplicate, and asset drops get a\n"
				"non-conflicting name. Runtime/script-created entities\n"
				"skip this editor-only pass.");

			constexpr const char* k_StyleLabels[] = {
				"Entity 1",
				"Entity (1)",
				"Entity-1",
				"Entity_1",
			};
			int suffixIndex = static_cast<int>(project.EditorEntityNameSuffix);
			if (suffixIndex < 0 || suffixIndex >= IM_ARRAYSIZE(k_StyleLabels)) {
				suffixIndex = static_cast<int>(FunkeProject::EditorEntityNameSuffixStyle::ParenthesizedNumber);
			}
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::Combo("Duplicate suffix##EntityNameSuffix", &suffixIndex, k_StyleLabels, IM_ARRAYSIZE(k_StyleLabels))) {
				project.EditorEntityNameSuffix =
					static_cast<FunkeProject::EditorEntityNameSuffixStyle>(suffixIndex);
				changed = true;
			}
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Entities")

		if (SectionVisible(SettingsCategory::Editor, "Serialization", filterLower)) {
		if (SettingsSectionHeader("Serialization", filterLower)) {
			ImGui::Indent(8);
			constexpr const char* k_FormatLabels[] = { "JSON", "Binary" };
			int formatIndex = project.AssetSerializationFormat == FunkeProject::ProjectAssetSerializationFormat::Binary ? 1 : 0;
			ImGui::SetNextItemWidth(180.0f);
			// Disabled mid-conversion so the format can't be flipped again while the worker is running.
			ImGui::BeginDisabled(m_ReserializeState != 0);
			if (ImGui::Combo("Asset format", &formatIndex, k_FormatLabels, IM_ARRAYSIZE(k_FormatLabels))) {
				const auto nextFormat = formatIndex == 1
					? FunkeProject::ProjectAssetSerializationFormat::Binary
					: FunkeProject::ProjectAssetSerializationFormat::Json;
				if (nextFormat != project.AssetSerializationFormat && m_ReserializeState == 0) {
					// Persist the setting now; the actual file rewrite runs asynchronously (see the
					// m_ReserializeState machine in OnPreRender). A loader auto-detects either format,
					// so a mid-flight crash leaving mixed-format files on disk is still readable.
					project.AssetSerializationFormat = nextFormat;
					m_ReserializeState = 1;
					changed = true;
				}
			}
			ImGui::EndDisabled();
			EditorUI::Tooltip(
				"Controls how scene and prefab assets are written on disk.\n"
				"Changing this rewrites existing .scene and .prefab files\n"
				"only when the selected format actually changes.");
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Serialization")

		if (SectionVisible(SettingsCategory::Editor, "Scripting", filterLower)) {
		if (SettingsSectionHeader("Scripting", filterLower)) {
			ImGui::Indent(8);
			Scene* activeScene = SceneManager::GetActiveScene();
			ScriptSystem* scriptSys = (activeScene && activeScene->HasSystem<ScriptSystem>())
				? activeScene->GetSystem<ScriptSystem>()
				: nullptr;
			const bool canRecompile = scriptSys && !scriptSys->IsRebuilding();
			if (!canRecompile) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Recompile Scripts") && scriptSys) {
				scriptSys->RequestRebuildAndReloadAll();
			}
			if (!canRecompile) {
				ImGui::EndDisabled();
			}
			if (scriptSys && scriptSys->IsRebuilding()) {
				ImGui::SameLine();
				ImGui::TextDisabled("Compiling...");
			}
			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Scripting")
	}

	void ImGuiEditorLayer::RenderSettings_Systems(FunkeProject& project, bool& changed,
		bool& outGlobalSystemsChanged, const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Systems, "Global Systems", filterLower)) {
		if (SettingsSectionHeader("Global Systems", filterLower)) {
			ImGui::Indent(8);
			EditorUI::SearchField("##GlobalSystemSearch", m_GlobalSystemSearchBuffer,
				sizeof(m_GlobalSystemSearchBuffer), "Search global systems...");

			std::string filter(m_GlobalSystemSearchBuffer);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			std::vector<EditorScriptDiscovery::ScriptEntry> scriptEntries;
			EditorScriptDiscovery::CollectProjectScriptEntries(scriptEntries);

			std::unordered_set<std::string> discoveredGlobalSystems;
			for (const auto& scriptEntry : scriptEntries) {
				if (!scriptEntry.IsGlobalSystem) {
					continue;
				}

				discoveredGlobalSystems.insert(scriptEntry.ClassName);
				if (!filter.empty()) {
					std::string lowerClassName = EditorScriptDiscovery::ToLowerCopy(scriptEntry.ClassName);
					std::string lowerPath = EditorScriptDiscovery::ToLowerCopy(scriptEntry.Path.string());
					if (lowerClassName.find(filter) == std::string::npos
						&& lowerPath.find(filter) == std::string::npos) {
						continue;
					}
				}

				auto it = std::find_if(project.GlobalSystems.begin(), project.GlobalSystems.end(),
					[&](const FunkeProject::GlobalSystemRegistration& registration) {
						return registration.ClassName == scriptEntry.ClassName;
					});
				bool active = it != project.GlobalSystems.end() ? it->Active : false;
				if (ImGuiUtils::Switch(scriptEntry.ClassName.c_str(), &active)) {
					if (it == project.GlobalSystems.end()) {
						project.GlobalSystems.push_back({ scriptEntry.ClassName, active });
					}
					else {
						it->Active = active;
					}
					changed = true;
					outGlobalSystemsChanged = true;
				}
			}

			for (auto& registration : project.GlobalSystems) {
				if (discoveredGlobalSystems.contains(registration.ClassName)) {
					continue;
				}
				bool active = registration.Active;
				std::string label = registration.ClassName + " (missing)";
				if (ImGuiUtils::Switch(label.c_str(), &active)) {
					registration.Active = active;
					changed = true;
					outGlobalSystemsChanged = true;
				}
			}

			ImGui::Unindent(8);
		}
		}  // end SectionVisible("Global Systems")
	}

	void ImGuiEditorLayer::TickSplashPreview() {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			m_SplashPreviewRequest = false;
			m_SplashPreviewActive = false;
			return;
		}

		if (m_SplashPreviewRequest) {
			m_SplashPreviewRequest = false;
			m_SplashPreviewActive = true;
			m_SplashPreviewElapsed = 0.0f;

			std::string logoPath = SplashAssetResolve::Resolve(
				project->SplashScreen.ImagePath, project);
			if (logoPath.empty()) {
				logoPath = SplashAssetResolve::DefaultLogoPath();
			}
			m_SplashPreviewLogo = logoPath.empty()
				? TextureHandle::Invalid()
				: TextureManager::LoadTexture(logoPath);

			// Background
			const std::string bgPath = SplashAssetResolve::Resolve(
				project->SplashScreen.BackgroundImagePath, project);
			m_SplashPreviewBackground = bgPath.empty()
				? TextureHandle::Invalid()
				: TextureManager::LoadTexture(bgPath);

			// Keep both textures alive across PurgeUnreferenced for
			// the preview lifetime; release in the completion path
			// below.
			if (m_SplashPreviewTexRefToken == 0) {
				m_SplashPreviewTexRefToken = TextureManager::AddReferenceProvider(
					[this](const TextureManager::ReferenceEmitter& emit) {
						if (m_SplashPreviewLogo.IsValid()) emit(m_SplashPreviewLogo);
						if (m_SplashPreviewBackground.IsValid()) emit(m_SplashPreviewBackground);
					});
			}
		}

		if (!m_SplashPreviewActive) return;

		const float dt = Time::GetDeltaTimeUnscaled();
		m_SplashPreviewElapsed += dt;

		const float fadeIn = std::max(0.0f, project->SplashScreen.FadeInSeconds);
		const float hold   = std::max(0.0f, project->SplashScreen.DurationSeconds);
		const float fadeOut = std::max(0.0f, project->SplashScreen.FadeOutSeconds);
		const float total = fadeIn + hold + fadeOut;
		if (m_SplashPreviewElapsed >= total) {
			m_SplashPreviewActive = false;
			if (m_SplashPreviewTexRefToken != 0) {
				TextureManager::RemoveReferenceProvider(m_SplashPreviewTexRefToken);
				m_SplashPreviewTexRefToken = 0;
			}
			m_SplashPreviewLogo = TextureHandle::Invalid();
			m_SplashPreviewBackground = TextureHandle::Invalid();
			return;
		}

		float alpha = 1.0f;
		if (m_SplashPreviewElapsed < fadeIn && fadeIn > 0.0f) {
			alpha = m_SplashPreviewElapsed / fadeIn;
		}
		else if (m_SplashPreviewElapsed > fadeIn + hold && fadeOut > 0.0f) {
			alpha = 1.0f - (m_SplashPreviewElapsed - fadeIn - hold) / fadeOut;
		}
		alpha = std::clamp(alpha, 0.0f, 1.0f);

		// Use foreground draw list to float above all docks; avoids dockspace InvisibleButton/scissor issues.
		ImGuiViewport* vp = ImGui::GetMainViewport();
		ImDrawList* draw = ImGui::GetForegroundDrawList(vp);
		const ImVec2 wMin = vp->WorkPos;
		const ImVec2 wMax(wMin.x + vp->WorkSize.x, wMin.y + vp->WorkSize.y);
		const float width = vp->WorkSize.x;
		const float height = vp->WorkSize.y;

		auto packColor = [](float r, float g, float b, float a) {
			r = std::clamp(r, 0.0f, 1.0f);
			g = std::clamp(g, 0.0f, 1.0f);
			b = std::clamp(b, 0.0f, 1.0f);
			a = std::clamp(a, 0.0f, 1.0f);
			return IM_COL32(int(r * 255), int(g * 255), int(b * 255), int(a * 255));
		};

		draw->AddRectFilled(wMin, wMax, packColor(
			project->SplashScreen.BackgroundR,
			project->SplashScreen.BackgroundG,
			project->SplashScreen.BackgroundB,
			alpha));

		Texture2D* bg = TextureManager::GetTexture(m_SplashPreviewBackground);
		if (bg && bg->IsValid()) {
			const float bgW = static_cast<float>(bg->GetWidth());
			const float bgH = static_cast<float>(bg->GetHeight());
			if (bgW > 0.0f && bgH > 0.0f) {
				const float canvasAspect = width / height;
				const float bgAspect = bgW / bgH;
				float drawW = width, drawH = height;
				if (bgAspect > canvasAspect) drawW = drawH * bgAspect;
				else                          drawH = drawW / bgAspect;
				const ImVec2 bgMin(wMin.x + (width  - drawW) * 0.5f,
				                   wMin.y + (height - drawH) * 0.5f);
				const ImVec2 bgMax(bgMin.x + drawW, bgMin.y + drawH);
				ImGuiUtils::AddImageFiltered(draw,
					static_cast<ImTextureID>(static_cast<intptr_t>(bg->GetHandle())),
					bgMin, bgMax, bg->GetFilter(),
					ImVec2(0, 0), ImVec2(1, 1),
					packColor(1.0f, 1.0f, 1.0f, alpha));
			}
		}

		const float centerX = wMin.x + width * 0.5f;
		const float centerY = wMin.y + height * 0.5f;

		Texture2D* logo = TextureManager::GetTexture(m_SplashPreviewLogo);
		if (logo && logo->IsValid()) {
			const float maxLogoSide = std::min(width, height) * 0.35f;
			float logoW = static_cast<float>(logo->GetWidth());
			float logoH = static_cast<float>(logo->GetHeight());
			if (logoW > 0 && logoH > 0) {
				const float scale = std::min(maxLogoSide / logoW, maxLogoSide / logoH);
				logoW *= scale; logoH *= scale;
				const ImVec2 imgMin(centerX - logoW * 0.5f, centerY - logoH * 0.65f);
				const ImVec2 imgMax(imgMin.x + logoW, imgMin.y + logoH);
				ImGuiUtils::AddImageFiltered(draw,
					static_cast<ImTextureID>(static_cast<intptr_t>(logo->GetHandle())),
					imgMin, imgMax, logo->GetFilter(),
					ImVec2(0, 0), ImVec2(1, 1),
					packColor(1.0f, 1.0f, 1.0f, alpha));
			}
		}

		const std::string subtitle = project->SplashScreen.CustomText.empty()
			? SplashAssetResolve::DefaultSubtitleLine()
			: project->SplashScreen.CustomText;
		if (!subtitle.empty()) {
			ImFont* font = ImGui::GetFont();
			const float fontSize = (project->SplashScreen.FontSize > 0.0f)
				? project->SplashScreen.FontSize : ImGui::GetFontSize();
			const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, subtitle.c_str());
			const ImVec2 textPos(centerX - textSize.x * 0.5f,
				centerY + std::min(width, height) * 0.18f);
			draw->AddText(font, fontSize, textPos,
				packColor(project->SplashScreen.FontColorR,
					project->SplashScreen.FontColorG,
					project->SplashScreen.FontColorB,
					alpha * 0.85f),
				subtitle.c_str());
		}
	}

	namespace {
		// Every edit is written to the .material file at once and seen by all MeshRenderers using it.
		void DrawMaterialAssetFields(uint64_t guid) {
			if (guid == 0 || !MaterialLibrary::Get(guid)) {
				EditorUI::EmptyState("Material could not be read", "Check the file for JSON errors.");
				return;
			}
			if (MaterialShaderInspector::Draw(guid)) return;

			auto current = [guid]() -> Material {
				const Material* material = MaterialLibrary::Get(guid);
				return material ? *material : Material{};
			};
			auto edit = [guid](auto change) {
				const Material* material = MaterialLibrary::Get(guid);
				if (!material) return;
				Material updated = *material;
				change(updated);
				MaterialLibrary::Save(guid, updated);
			};

			const std::vector<PropertyDescriptor> properties = {
				Properties::MakeWith<bool>("Lit", "Lit",
					[current](const Entity&) { return current().Lit; },
					[edit](Entity&, bool v) { edit([v](Material& m) { m.Lit = v; }); },
					Properties::Meta::Tooltip("Unlit materials ignore lights, exposure and tone mapping.")),
				Properties::MakeWith<bool>("Fog", "Fog",
					[current](const Entity&) { return current().Fog; },
					[edit](Entity&, bool v) { edit([v](Material& m) { m.Fog = v; }); },
					Properties::Meta::Tooltip("Fades into the camera's fog with distance. Turn off for a sky dome or other backdrop.")),
				Properties::MakeWith<MaterialSurface>("Surface", "Surface",
					[current](const Entity&) { return current().Surface; },
					[edit](Entity&, MaterialSurface v) { edit([v](Material& m) { m.Surface = v; }); },
					Properties::Meta::Tooltip("Transparent blends by the alpha of Base Color, texture and the renderer's Color, "
						"sorted back to front per object; it does not write depth.")),
				Properties::MakeWith<Color>("BaseColor", "Base Color",
					[current](const Entity&) { return current().BaseColor; },
					[edit](Entity&, Color v) { edit([v](Material& m) { m.BaseColor = v; }); }),
				Properties::MakeTextureRef("BaseColorTexture", "Base Color Texture",
					[current](const Entity&) { return current().BaseColorTexture; },
					[edit](Entity&, uint64_t v) { edit([v](Material& m) { m.BaseColorTexture = v; }); },
					Properties::Meta::Tooltip("Multiplied into Base Color; sampled with repeat wrapping.")),
				Properties::MakeWith<float>("Metallic", "Metallic",
					[current](const Entity&) { return current().Metallic; },
					[edit](Entity&, float v) { edit([v](Material& m) { m.Metallic = v; }); },
					Properties::Meta::Clamp(0.0, 1.0, 0.005f)),
				Properties::MakeWith<float>("Roughness", "Roughness",
					[current](const Entity&) { return current().Roughness; },
					[edit](Entity&, float v) { edit([v](Material& m) { m.Roughness = v; }); },
					Properties::Meta::Clamp(0.0, 1.0, 0.005f)),
				Properties::MakeWith<Color>("EmissiveColor", "Emissive Color",
					[current](const Entity&) { return current().EmissiveColor; },
					[edit](Entity&, Color v) { edit([v](Material& m) { m.EmissiveColor = v; }); }),
				Properties::MakeWith<float>("EmissiveIntensity", "Emissive Intensity",
					[current](const Entity&) { return current().EmissiveIntensity; },
					[edit](Entity&, float v) { edit([v](Material& m) { m.EmissiveIntensity = v; }); },
					Properties::Meta::Clamp(0.0, 1000.0, 0.01f)),
				Properties::MakeWith<glm::vec2>("Tiling", "Tiling",
					[current](const Entity&) { return current().Tiling; },
					[edit](Entity&, glm::vec2 v) { edit([v](Material& m) { m.Tiling = v; }); },
					Properties::Meta::DragSpeed(0.01f)),
				Properties::MakeWith<glm::vec2>("Offset", "Offset",
					[current](const Entity&) { return current().Offset; },
					[edit](Entity&, glm::vec2 v) { edit([v](Material& m) { m.Offset = v; }); },
					Properties::Meta::DragSpeed(0.01f)),
			};

			const Entity placeholder = Entity::Null;
			const std::span<const Entity> entitySpan(&placeholder, 1);
			const std::string keyPrefix = "material:" + std::to_string(guid) + ".";
			for (const PropertyDescriptor& descriptor : properties) {
				PropertyDrawer::Draw(entitySpan, descriptor, keyPrefix + descriptor.Name);
			}
		}
	}

	void ImGuiEditorLayer::RenderAssetInspector() {
		const std::string& selectedPath = m_AssetBrowser.GetSelectedPath();
		if (selectedPath.empty()) {
			// Selection cleared. If the prefab inspector held a dirty prefab,
			// the save/discard prompt is driven from the dispatch above; here
			// just close cleanly.
			if (m_PrefabInspector.IsOpen() && !m_PrefabInspector.HasUnsavedChanges()) {
				m_PrefabInspector.Close();
				m_PrefabInspectorPath.clear();
			}
			EditorUI::EmptyState("No entity or asset selected", "Select an entity in the Hierarchy or an asset in the Project panel.");
			return;
		}

		std::filesystem::path path(selectedPath);
		if (!std::filesystem::exists(path)) {
			EditorUI::EmptyState("No entity or asset selected", "Select an entity in the Hierarchy or an asset in the Project panel.");
			return;
		}

		std::string name = path.filename().string();
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		// `.prefab` selection routes through PrefabInspector. Selection change
		// while a dirty prefab is open opens a save/discard modal — the new
		// selection is queued in m_PendingPrefabSwitchPath until the user picks.
		if (ext == ".prefab") {
			if (m_PrefabInspector.IsOpen() && m_PrefabInspectorPath != selectedPath) {
				if (m_PrefabInspector.HasUnsavedChanges()) {
					m_PendingPrefabSwitchPath = selectedPath;
					m_ShowPrefabSavePrompt = true;
				}
				else {
					if (m_PrefabInspector.Open(selectedPath)) {
						m_PrefabInspectorPath = selectedPath;
					}
				}
			}
			else if (!m_PrefabInspector.IsOpen()) {
				if (m_PrefabInspector.Open(selectedPath)) {
					m_PrefabInspectorPath = selectedPath;
				}
			}
			if (m_PrefabInspector.Render()) {
				SavePrefabInspectorChanges();
			}
			return;
		}

		// Non-prefab asset: clean up any open prefab inspector (no prompt — user
		// already navigated away; if they had unsaved work the prompt UX would
		// have caught it during the .prefab→.prefab switch above).
		if (m_PrefabInspector.IsOpen() && !m_PrefabInspector.HasUnsavedChanges()) {
			m_PrefabInspector.Close();
			m_PrefabInspectorPath.clear();
		}

		if (ext == k_MaterialExtension) {
			ImGui::TextDisabled("Material:");
			ImGui::SameLine();
			ImGuiUtils::TextEllipsis(name);
			ImGui::Separator();
			DrawMaterialAssetFields(AssetRegistry::GetOrCreateAssetUUID(selectedPath));
			return;
		}

		if (ext == k_AnimationClipExtension || ext == k_AnimatorControllerExtension) {
			const bool controller = ext == k_AnimatorControllerExtension;
			ImGui::TextDisabled(controller ? "Animator Controller:" : "Animation Clip:");
			ImGui::SameLine();
			ImGuiUtils::TextEllipsis(name);
			ImGui::Separator();
			const uint64_t guid = AssetRegistry::GetOrCreateAssetUUID(selectedPath);
			if (controller) {
				if (const auto asset = AnimatorControllerLibrary::Get(guid)) {
					ImGui::Text("%zu states, %zu transitions, %zu parameters", asset->States.size(), asset->Transitions.size(), asset->Parameters.size());
				}
				if (EditorUI::Button("Open in Animator", EditorUI::ButtonVariant::Primary)) {
					m_AnimatorWindow.OpenController(guid);
					m_ShowAnimatorWindow = true;
				}
			}
			else {
				if (const auto asset = AnimationClipLibrary::Get(guid)) {
					ImGui::Text("%.2f s%s, %zu tracks, %zu events", asset->Length, asset->Loop ? ", looping" : "", asset->Tracks.size(), asset->Events.size());
				}
				if (EditorUI::Button("Open in Animation", EditorUI::ButtonVariant::Primary)) {
					m_AnimationWindow.OpenClip(guid);
					m_ShowAnimationWindow = true;
				}
			}
			return;
		}

		if (ext == ".dataasset") {
			ImGui::TextDisabled("Data Asset:");
			ImGui::SameLine();
			ImGuiUtils::TextEllipsis(name);
			ImGui::Separator();
			DrawDataAssetFields(AssetRegistry::GetOrCreateAssetUUID(selectedPath));
			return;
		}

		ImGui::TextDisabled("Asset:");
		ImGui::SameLine();
		ImGuiUtils::TextEllipsis(name);
		ImGui::Separator();

		ImGui::TextDisabled("Path:");
		ImGui::SameLine();
		ImGuiUtils::TextDisabledEllipsis(selectedPath);

		try {
			auto fileSize = std::filesystem::file_size(path);
			if (fileSize >= 1024 * 1024) ImGui::TextDisabled("Size: %.2f MB", fileSize / (1024.0f * 1024.0f));
			else if (fileSize >= 1024) ImGui::TextDisabled("Size: %.1f KB", fileSize / 1024.0f);
			else ImGui::TextDisabled("Size: %llu bytes", fileSize);
		}
		catch (...) {
		}

		ImGui::TextDisabled("Type: %s", ext.c_str());
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
			ImGui::Spacing();
			const Texture2D* tex = GetPreviewTexture(path);
			if (tex && tex->IsValid()) {
				// Read the import meta first: the preview Texture2D is always
				// loaded with a fixed filter, so the authored FilterMode (not
				// tex.GetFilter()) is what the preview must honor — and reading
				// it every frame keeps the preview live as the combo changes.
				TextureMeta meta = AssetRegistry::ReadTextureMeta(selectedPath);
				if (!meta.HasImportBlock) {
					meta.Import.FilterMode = tex->GetFilter();
					meta.Import.WrapU      = tex->GetWrapU();
					meta.Import.WrapV      = tex->GetWrapV();
				}

				// Use the Texture2D overload so the canonical flip rule
				// applies — preview matches the asset-browser thumbnail
				// regardless of the texture's load-time flipVertical flag.
				ImGuiUtils::DrawTexturePreview(*tex, 128.0f, meta.Import.FilterMode);
				ImGui::Text("%.0f x %.0f", tex->GetWidth(), tex->GetHeight());

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				const auto persist = [&](TextureMeta updated) {
					updated.HasImportBlock = true;
					if (AssetRegistry::WriteTextureMeta(selectedPath, updated)) {
						TextureManager::ApplyMetaSamplerToLoaded(selectedPath);
						// The browser tile bakes its filter at thumbnail creation, so
						// drop it to re-pick up the new Filter Mode (engine slots above
						// re-sample live; the cache is separate).
						m_AssetBrowser.InvalidateThumbnail(selectedPath);
					}
				};

				ImGui::PushID("TextureImportSettings");
				ImGuiUtils::DrawEnumCombo<Filter>("Filter Mode", meta.Import.FilterMode,
					[&](Filter newFilter) {
						TextureMeta updated = meta;
						updated.Import.FilterMode = newFilter;
						persist(updated);
					});
				ImGuiUtils::DrawEnumCombo<Wrap>("Wrap U", meta.Import.WrapU,
					[&](Wrap newWrap) {
						TextureMeta updated = meta;
						updated.Import.WrapU = newWrap;
						persist(updated);
					});
				ImGuiUtils::DrawEnumCombo<Wrap>("Wrap V", meta.Import.WrapV,
					[&](Wrap newWrap) {
						TextureMeta updated = meta;
						updated.Import.WrapV = newWrap;
						persist(updated);
					});
				{
					// Order matches the .meta array; Images with all-zero borders of their own use it.
					int border[4] = { meta.Border.Left, meta.Border.Bottom, meta.Border.Right, meta.Border.Top };
					if (ImGui::InputInt4("Border (L B R T)", border)) {
						TextureMeta updated = meta;
						updated.Border = { std::max(border[0], 0), std::max(border[1], 0), std::max(border[2], 0), std::max(border[3], 0) };
						persist(updated);
						NotifySpriteSliceEpochBumped();
					}
					EditorUI::Tooltip("9-slice border of the whole texture in pixels. Sliced and Tiled Images keep it unstretched.");
				}
				ImGui::PopID();

				ImGui::Spacing();
				if (ImGui::Button("Open Sprite Editor", ImVec2(-FLT_MIN, 0.0f))) {
					m_PendingSpriteEditorPath    = selectedPath;
					m_PendingSpriteEditorAssetId = AssetRegistry::GetOrCreateAssetUUID(selectedPath);
					m_ShowSpriteEditor           = true;
				}
			}
		}
	}

	const Texture2D* ImGuiEditorLayer::GetPreviewTexture(const std::filesystem::path& path) {
		const std::string canonicalPath = NormalizePreviewTexturePath(path);
		if (canonicalPath.empty()) {
			return nullptr;
		}

		if (const auto it = m_PreviewTextureLookup.find(canonicalPath); it != m_PreviewTextureLookup.end()) {
			PreviewTextureEntry& entry = m_PreviewTextureCache[it->second];
			entry.LastTouchTick = ++m_PreviewTextureTick;
			return entry.Texture.get();
		}

		// TODO(perf): Synchronous decode on UI thread — large textures cause first-select hitches.
		// Convert to async-task pattern (see funke-add-editor-panel skill).
		auto texture = std::make_unique<Texture2D>(canonicalPath.c_str(), Filter::Point, Wrap::Clamp, Wrap::Clamp);
		if (!texture->IsValid()) {
			return nullptr;
		}

		PreviewTextureEntry entry;
		entry.CanonicalPath = canonicalPath;
		entry.Texture = std::move(texture);
		entry.LastTouchTick = ++m_PreviewTextureTick;

		const size_t index = m_PreviewTextureCache.size();
		m_PreviewTextureLookup[canonicalPath] = index;
		m_PreviewTextureCache.push_back(std::move(entry));
		TrimPreviewTextureCache();
		return m_PreviewTextureCache.back().Texture.get();
	}

	void ImGuiEditorLayer::TrimPreviewTextureCache() {
		while (m_PreviewTextureCache.size() > kMaxPreviewTextures) {
			auto victimIt = std::min_element(
				m_PreviewTextureCache.begin(),
				m_PreviewTextureCache.end(),
				[](const PreviewTextureEntry& a, const PreviewTextureEntry& b) {
					return a.LastTouchTick < b.LastTouchTick;
				});
			if (victimIt == m_PreviewTextureCache.end()) {
				break;
			}

			m_PreviewTextureCache.erase(victimIt);
			m_PreviewTextureLookup.clear();
			for (size_t i = 0; i < m_PreviewTextureCache.size(); ++i) {
				m_PreviewTextureLookup[m_PreviewTextureCache[i].CanonicalPath] = i;
			}
		}
	}

	void ImGuiEditorLayer::ClearPreviewTextureCache() {
		m_PreviewTextureLookup.clear();
		m_PreviewTextureCache.clear();
		m_PreviewTextureTick = 0;
	}

	void ImGuiEditorLayer::RenderPackageManagerPanel() {
		if (!m_ShowPackageManager) return;

		if (!m_PackageManagerInitialized) {
			m_PackageManager.Initialize();

			if (m_PackageManager.IsReady()) {
				m_PackageManager.AddSource(std::make_unique<NuGetSource>(m_PackageManager.GetToolPath()));
				m_PackageManager.AddSource(std::make_unique<GitHubSource>(
					m_PackageManager.GetToolPath(),
					"https://raw.githubusercontent.com/Ben-Scr/index-packages/main/index.json",
					"Engine Packages"));
			}

			m_PackageManagerPanel.Initialize(&m_PackageManager);
			m_PackageManagerInitialized = true;
		}

		ImGui::SetNextWindowSizeConstraints(ImVec2(420, 320), ImVec2(FLT_MAX, FLT_MAX));
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		ImGui::Begin("Package Manager", &m_ShowPackageManager);
		m_PackageManagerPanel.Render();
		ImGui::End();
	}

} // namespace Funke
