#include "pch.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Core/Platform.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/FunkeProject.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#ifdef FNK_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <oleauto.h>
#endif

namespace Funke {

#ifdef FNK_PLATFORM_WINDOWS
	namespace {
		// Tracked (not detached) so editor shutdown can join them; join may block ~4s on the cold-start VS thread.
		std::mutex s_LaunchMutex;
		std::vector<std::thread> s_LaunchThreads;

		struct VisualStudioCandidate {
			std::string DisplayName;
			std::string ExecutablePath;
			std::vector<int> Version;
		};

		std::vector<int> ParseVersion(std::string_view text) {
			std::vector<int> parts;
			size_t start = 0;
			while (start < text.size()) {
				size_t end = text.find('.', start);
				if (end == std::string_view::npos) end = text.size();
				int value = 0;
				for (size_t i = start; i < end && std::isdigit(static_cast<unsigned char>(text[i])); ++i)
					value = value * 10 + (text[i] - '0');
				parts.push_back(value);
				start = end + 1;
			}
			return parts;
		}

		bool SamePath(const std::string& lhs, const std::string& rhs) {
			if (lhs.empty() || rhs.empty()) return false;
			std::error_code lhsError;
			std::error_code rhsError;
			const auto lhsPath = std::filesystem::weakly_canonical(Path::FromUtf8(lhs), lhsError);
			const auto rhsPath = std::filesystem::weakly_canonical(Path::FromUtf8(rhs), rhsError);
			const std::wstring lhsValue = (lhsError ? Path::FromUtf8(lhs).lexically_normal() : lhsPath).wstring();
			const std::wstring rhsValue = (rhsError ? Path::FromUtf8(rhs).lexically_normal() : rhsPath).wstring();
			return _wcsicmp(lhsValue.c_str(), rhsValue.c_str()) == 0;
		}

		void AppendVisualStudioCandidate(std::vector<VisualStudioCandidate>& candidates,
			std::string displayName, const std::filesystem::path& executablePath,
			std::string_view version)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(executablePath, error) || error) return;
			const std::string executable = Path::ToUtf8(executablePath);
			if (std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
				return SamePath(candidate.ExecutablePath, executable);
			})) return;
			if (displayName.empty()) displayName = "Visual Studio";
			candidates.push_back({ std::move(displayName), executable, ParseVersion(version) });
		}

		std::vector<VisualStudioCandidate> DetectVisualStudioInstallations() {
			std::vector<VisualStudioCandidate> candidates;
			const std::filesystem::path vswhere =
				Path::FromUtf8("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe");
			std::error_code error;
			if (std::filesystem::is_regular_file(vswhere, error) && !error) {
				const Process::Result result = Process::Run({
					Path::ToUtf8(vswhere), "-all", "-products", "*",
					"-requires", "Microsoft.Component.MSBuild", "-format", "json", "-utf8"
				}, {}, std::chrono::seconds(15));
				std::string json = result.Output;
				if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF
					&& static_cast<unsigned char>(json[1]) == 0xBB
					&& static_cast<unsigned char>(json[2]) == 0xBF)
					json.erase(0, 3);
				Json::Value installations;
				if (result.Succeeded() && Json::TryParse(json, installations)
					&& installations.IsArray())
				{
					for (const Json::Value& installation : installations.GetArray()) {
						if (!installation.IsObject()) continue;
						const Json::Value* installPathValue = installation.FindMember("installationPath");
						if (!installPathValue) continue;
						const std::string installPath = installPathValue->AsStringOr();
						const std::filesystem::path executable = Path::FromUtf8(installPath)
							/ "Common7" / "IDE" / "devenv.exe";
						const Json::Value* displayNameValue = installation.FindMember("displayName");
						const Json::Value* versionValue = installation.FindMember("installationVersion");
						const std::string version = versionValue ? versionValue->AsStringOr() : std::string{};
						std::string displayName = displayNameValue
							? displayNameValue->AsStringOr() : "Visual Studio";
						if (!version.empty()) displayName += " (" + version + ")";
						AppendVisualStudioCandidate(candidates,
							std::move(displayName), executable, version);
					}
				}
			}

			for (const auto& [folder, label, version] : {
				std::tuple{ "18", "Visual Studio 2026", "18" },
				std::tuple{ "2026", "Visual Studio 2026", "18" },
				std::tuple{ "2022", "Visual Studio 2022", "17" } })
			{
				for (const char* edition : { "Community", "Professional", "Enterprise" }) {
					AppendVisualStudioCandidate(candidates, std::string(label) + " " + edition,
						std::filesystem::path("C:/Program Files/Microsoft Visual Studio") / folder
							/ edition / "Common7" / "IDE" / "devenv.exe", version);
				}
			}

			std::stable_sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
				return lhs.Version > rhs.Version;
			});
			return candidates;
		}

		struct JetBrainsProduct {
			ExternalEditorType Type;
			const char* FallbackName;
			const char* Executable;
			std::vector<std::string> FolderPrefixes;   // Program Files\JetBrains\<prefix>*, %LOCALAPPDATA%\Programs\<prefix>*
			std::vector<std::string> ToolboxApps;      // %LOCALAPPDATA%\JetBrains\Toolbox\apps\<app>
			std::vector<std::wstring> RegistryKeys;    // Software\JetBrains\<key>\<build> = install dir
		};

		struct JetBrainsCandidate {
			std::string DisplayName;
			std::string ExecutablePath;
			std::vector<int> Version;
		};

		bool StartsWithInsensitive(const std::string& text, const std::string& prefix) {
			return text.size() >= prefix.size()
				&& _strnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
		}

		void AddJetBrainsInstall(std::vector<JetBrainsCandidate>& out, const JetBrainsProduct& product,
			const std::filesystem::path& installRoot)
		{
			std::error_code error;
			const std::filesystem::path executable = installRoot / "bin" / product.Executable;
			if (!std::filesystem::is_regular_file(executable, error) || error) return;
			const std::string executablePath = Path::ToUtf8(executable);
			if (std::any_of(out.begin(), out.end(), [&](const auto& c) { return SamePath(c.ExecutablePath, executablePath); })) return;

			std::string name = product.FallbackName;
			std::string version;
			Json::Value info;
			const std::filesystem::path productInfo = installRoot / "product-info.json";
			if (std::filesystem::is_regular_file(productInfo, error) && Json::TryParse(File::ReadAllText(Path::ToUtf8(productInfo)), info)
				&& info.IsObject()) {
				if (const Json::Value* v = info.FindMember("name")) name = v->AsStringOr(name);
				if (const Json::Value* v = info.FindMember("version")) version = v->AsStringOr();
			}
			out.push_back({ version.empty() ? name : name + " " + version, executablePath, ParseVersion(version) });
		}

		std::string LocalAppData() {
			std::string value;
			char* env = nullptr;
			size_t len = 0;
			if (_dupenv_s(&env, &len, "LOCALAPPDATA") == 0 && env) {
				value = env;
				free(env);
			}
			return value;
		}

		// Rider and IntelliJ IDEA: standalone installer, Toolbox 1.x/2.x and the registry entries the installer writes.
		std::vector<JetBrainsCandidate> DetectJetBrainsInstallations(const JetBrainsProduct& product) {
			std::vector<JetBrainsCandidate> out;
			std::error_code error;
			auto scanPrefixed = [&](const std::filesystem::path& parent) {
				if (!std::filesystem::is_directory(parent, error)) return;
				for (const auto& entry : std::filesystem::directory_iterator(parent, error)) {
					if (!entry.is_directory(error)) continue;
					const std::string folder = Path::ToUtf8(entry.path().filename());
					for (const std::string& prefix : product.FolderPrefixes) {
						if (StartsWithInsensitive(folder, prefix)) {
							AddJetBrainsInstall(out, product, entry.path());
							break;
						}
					}
				}
			};
			scanPrefixed("C:/Program Files/JetBrains");
			const std::string localAppData = LocalAppData();
			if (!localAppData.empty()) {
				scanPrefixed(Path::FromUtf8(localAppData) / "Programs");
				// Toolbox 1.x: apps\<app>\ch-0\<build>\bin\<exe> (older layouts drop the channel folder).
				for (const std::string& app : product.ToolboxApps) {
					const std::filesystem::path appDir = Path::FromUtf8(localAppData) / "JetBrains" / "Toolbox" / "apps" / app;
					if (!std::filesystem::is_directory(appDir, error)) continue;
					for (auto it = std::filesystem::recursive_directory_iterator(appDir, error);
						it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
						if (error) break;
						if (it.depth() >= 2) it.disable_recursion_pending();
						if (it->is_directory(error)) AddJetBrainsInstall(out, product, it->path());
					}
				}
			}
			for (HKEY hive : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
				for (const std::wstring& key : product.RegistryKeys) {
					HKEY productKey = nullptr;
					const std::wstring keyPath = L"Software\\JetBrains\\" + key;
					if (RegOpenKeyExW(hive, keyPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &productKey) != ERROR_SUCCESS) continue;
					wchar_t build[256];
					for (DWORD index = 0;; ++index) {
						DWORD buildLength = static_cast<DWORD>(std::size(build));
						if (RegEnumKeyExW(productKey, index, build, &buildLength, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
						wchar_t installDir[MAX_PATH * 2]{};
						DWORD bytes = sizeof(installDir);
						if (RegGetValueW(productKey, build, nullptr, RRF_RT_REG_SZ, nullptr, installDir, &bytes) == ERROR_SUCCESS) {
							AddJetBrainsInstall(out, product, std::filesystem::path(installDir));
						}
					}
					RegCloseKey(productKey);
				}
			}
			std::stable_sort(out.begin(), out.end(), [](const auto& lhs, const auto& rhs) { return lhs.Version > rhs.Version; });
			return out;
		}
	}

	static void TrackLaunchThread(std::thread t) {
		std::scoped_lock lock(s_LaunchMutex);
		// Join and drop already-finished threads so the vector doesn't grow without bound during a long
		// editor session. A finished std::thread stays joinable until joined, so test the OS handle instead.
		s_LaunchThreads.erase(
			std::remove_if(s_LaunchThreads.begin(), s_LaunchThreads.end(),
				[](std::thread& th) {
					if (!th.joinable()) return true;
					if (WaitForSingleObject(th.native_handle(), 0) != WAIT_OBJECT_0) return false;
					th.join();
					return true;
				}),
			s_LaunchThreads.end());
		s_LaunchThreads.push_back(std::move(t));
	}
#endif

#ifndef FNK_PLATFORM_WINDOWS
	namespace {
		// First executable named `name` on $PATH; empty when absent.
		std::string FindOnPath(const char* name) {
			const char* pathEnv = std::getenv("PATH");
			if (!pathEnv) return {};
			std::string_view remaining(pathEnv);
			while (!remaining.empty()) {
				const size_t separator = remaining.find(':');
				const std::string_view directory = remaining.substr(0, separator);
				if (!directory.empty()) {
					const std::filesystem::path candidate = std::filesystem::path(std::string(directory)) / name;
					std::error_code ec;
					if (std::filesystem::is_regular_file(candidate, ec)) return candidate.string();
				}
				if (separator == std::string_view::npos) break;
				remaining.remove_prefix(separator + 1);
			}
			return {};
		}

		std::string HomeRelative(const char* relative) {
			const std::filesystem::path home = Platform::GetUserFolder(Platform::UserFolder::Home);
			return home.empty() ? std::string() : (home / relative).string();
		}

		bool SamePath(const std::string& lhs, const std::string& rhs) {
			if (lhs.empty() || rhs.empty()) return false;
			std::error_code lhsError;
			std::error_code rhsError;
			const auto lhsPath = std::filesystem::weakly_canonical(Path::FromUtf8(lhs), lhsError);
			const auto rhsPath = std::filesystem::weakly_canonical(Path::FromUtf8(rhs), rhsError);
			return (lhsError ? Path::FromUtf8(lhs).lexically_normal() : lhsPath)
				== (rhsError ? Path::FromUtf8(rhs).lexically_normal() : rhsPath);
		}
	}
#endif

	void ExternalEditor::JoinPendingLaunchThreads() {
#ifdef FNK_PLATFORM_WINDOWS
		std::vector<std::thread> drained;
		{
			std::scoped_lock lock(s_LaunchMutex);
			drained.swap(s_LaunchThreads);
		}
		for (auto& t : drained) {
			if (t.joinable()) t.join();
		}
#endif
	}

#ifdef FNK_PLATFORM_WINDOWS
	// Proper UTF-8→UTF-16 via MultiByteToWideChar; naive wstring(begin,end) corrupted non-ASCII paths and silently failed to launch.
	static std::wstring Utf8ToWide(const std::string& utf8) {
		if (utf8.empty()) return {};
		const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
			static_cast<int>(utf8.size()), nullptr, 0);
		if (needed <= 0) return {};
		std::wstring out(static_cast<size_t>(needed), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
			out.data(), needed);
		return out;
	}
#endif

	std::vector<ExternalEditorInfo> ExternalEditor::s_Editors;
	int ExternalEditor::s_SelectedIndex = 0;
	bool ExternalEditor::s_Detected = false;

	bool ExternalEditor::IsScriptExtension(const std::string& ext) {
		return ext == ".cs" || ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp"
			|| ext == ".lua" || ext == ".py" || ext == ".js" || ext == ".ts"
			|| ext == ".json" || ext == ".xml" || ext == ".yaml" || ext == ".yml"
			|| ext == ".txt" || ext == ".md" || ext == ".cfg" || ext == ".ini"
			|| ext == ".glsl" || ext == ".hlsl" || ext == ".shader";
	}

	void ExternalEditor::TryDetect(ExternalEditorType type, const std::string& displayName,
		const std::vector<std::string>& candidates) {
		for (const auto& path : candidates) {
			if (std::filesystem::exists(path)) {
				ExternalEditorInfo info;
				info.Type = type;
				info.DisplayName = displayName;
				info.ExecutablePath = path;
				info.Available = true;
				s_Editors.push_back(std::move(info));
				return;
			}
		}
	}

	void ExternalEditor::DetectEditors() {
		if (s_Detected) return;
		s_Detected = true;
		s_Editors.clear();
		s_SelectedIndex = 0;

#ifdef FNK_PLATFORM_WINDOWS
		for (auto& candidate : DetectVisualStudioInstallations()) {
			s_Editors.push_back({ ExternalEditorType::VisualStudio,
				std::move(candidate.DisplayName), std::move(candidate.ExecutablePath), true });
		}

		// VS Code
		{
			std::string localAppData;
			char* env = nullptr;
			size_t len = 0;
			if (_dupenv_s(&env, &len, "LOCALAPPDATA") == 0 && env) {
				localAppData = env;
				free(env);
			}

			std::vector<std::string> vscodePaths = {
				localAppData + "\\Programs\\Microsoft VS Code\\Code.exe",
				"C:\\Program Files\\Microsoft VS Code\\Code.exe",
			};

			// Also check PATH
			char pathBuf[MAX_PATH]{};
			DWORD found = SearchPathA(nullptr, "code.cmd", nullptr, MAX_PATH, pathBuf, nullptr);
			if (found > 0)
				vscodePaths.insert(vscodePaths.begin(), std::string(pathBuf));

			TryDetect(ExternalEditorType::VSCode, "Visual Studio Code", vscodePaths);
		}

		// JetBrains: every installed Rider / IntelliJ IDEA version is listed on its own.
		{
			const JetBrainsProduct products[] = {
				{ ExternalEditorType::Rider, "JetBrains Rider", "rider64.exe",
					{ "JetBrains Rider", "Rider" }, { "Rider" }, { L"JetBrains Rider", L"Rider" } },
				{ ExternalEditorType::IntelliJ, "IntelliJ IDEA", "idea64.exe",
					{ "IntelliJ IDEA" }, { "IDEA-U", "IDEA-C", "IntelliJ IDEA Ultimate", "IntelliJ IDEA Community Edition" },
					{ L"IntelliJ IDEA", L"IntelliJ IDEA Community Edition" } },
			};
			for (const JetBrainsProduct& product : products) {
				for (JetBrainsCandidate& candidate : DetectJetBrainsInstallations(product)) {
					s_Editors.push_back({ product.Type, std::move(candidate.DisplayName), std::move(candidate.ExecutablePath), true });
				}
			}
		}
#else
		{
			std::vector<std::string> vscodePaths;
			// VSCodium and Code - OSS take the same command line.
			for (const char* command : { "code", "codium", "code-oss" }) {
				if (std::string onPath = FindOnPath(command); !onPath.empty()) vscodePaths.push_back(std::move(onPath));
			}
#if defined(FNK_PLATFORM_MACOS)
			vscodePaths.push_back("/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code");
			vscodePaths.push_back(HomeRelative("Applications/Visual Studio Code.app/Contents/Resources/app/bin/code"));
			vscodePaths.push_back("/opt/homebrew/bin/code");
			vscodePaths.push_back("/usr/local/bin/code");
			vscodePaths.push_back("/Applications/VSCodium.app/Contents/Resources/app/bin/codium");
#else
			vscodePaths.push_back("/usr/bin/code");
			vscodePaths.push_back("/snap/bin/code");
			vscodePaths.push_back("/var/lib/flatpak/exports/bin/com.visualstudio.code");
			vscodePaths.push_back(HomeRelative(".local/share/flatpak/exports/bin/com.visualstudio.code"));
			vscodePaths.push_back("/usr/bin/codium");
			vscodePaths.push_back("/var/lib/flatpak/exports/bin/com.vscodium.codium");
#endif
			TryDetect(ExternalEditorType::VSCode, "Visual Studio Code", vscodePaths);
		}
		{
			std::vector<std::string> riderPaths;
			if (std::string onPath = FindOnPath("rider"); !onPath.empty()) riderPaths.push_back(std::move(onPath));
#if defined(FNK_PLATFORM_MACOS)
			riderPaths.push_back("/Applications/Rider.app/Contents/MacOS/rider");
			riderPaths.push_back(HomeRelative("Applications/Rider.app/Contents/MacOS/rider"));
			riderPaths.push_back(HomeRelative("Library/Application Support/JetBrains/Toolbox/scripts/rider"));
#else
			riderPaths.push_back(HomeRelative(".local/share/JetBrains/Toolbox/scripts/rider"));
			riderPaths.push_back("/snap/bin/rider");
			riderPaths.push_back("/opt/rider/bin/rider.sh");
#endif
			TryDetect(ExternalEditorType::Rider, "JetBrains Rider", riderPaths);
		}
		{
			std::vector<std::string> ideaPaths;
			if (std::string onPath = FindOnPath("idea"); !onPath.empty()) ideaPaths.push_back(std::move(onPath));
#if defined(FNK_PLATFORM_MACOS)
			ideaPaths.push_back("/Applications/IntelliJ IDEA.app/Contents/MacOS/idea");
			ideaPaths.push_back("/Applications/IntelliJ IDEA CE.app/Contents/MacOS/idea");
			ideaPaths.push_back(HomeRelative("Applications/IntelliJ IDEA.app/Contents/MacOS/idea"));
			ideaPaths.push_back(HomeRelative("Library/Application Support/JetBrains/Toolbox/scripts/idea"));
#else
			ideaPaths.push_back(HomeRelative(".local/share/JetBrains/Toolbox/scripts/idea"));
			ideaPaths.push_back("/snap/bin/intellij-idea-ultimate");
			ideaPaths.push_back("/snap/bin/intellij-idea-community");
			ideaPaths.push_back("/opt/idea/bin/idea.sh");
#endif
			TryDetect(ExternalEditorType::IntelliJ, "IntelliJ IDEA", ideaPaths);
		}
#endif

		if (s_Editors.empty()) {
			FNK_CORE_WARN_TAG("ExternalEditor", "No code editors detected on this system");
		}
		else {
			FNK_CORE_INFO_TAG("ExternalEditor", "Detected {} code editor(s):", s_Editors.size());
			for (size_t i = 0; i < s_Editors.size(); i++)
				FNK_CORE_INFO_TAG("ExternalEditor", "  [{}] {} ({})", i, s_Editors[i].DisplayName, s_Editors[i].ExecutablePath);
		}

		LoadPreferences();
	}

	void ExternalEditor::RefreshEditors() {
		std::string selectedPath;
		if (s_SelectedIndex >= 0 && s_SelectedIndex < static_cast<int>(s_Editors.size()))
			selectedPath = s_Editors[s_SelectedIndex].ExecutablePath;
		s_Detected = false;
		DetectEditors();
		if (selectedPath.empty()) return;
		for (int i = 0; i < static_cast<int>(s_Editors.size()); ++i) {
			if (SamePath(s_Editors[i].ExecutablePath, selectedPath)) {
				s_SelectedIndex = i;
				return;
			}
		}
	}

	static std::string GetPreferencesPath() {
		return Path::Combine(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData), "Funke", "editor-preferences.json");
	}

	void ExternalEditor::SavePreferences() {
		std::string path = GetPreferencesPath();
		auto dir = std::filesystem::path(path).parent_path();
		if (!std::filesystem::exists(dir))
			std::filesystem::create_directories(dir);

		const auto& selected = (s_SelectedIndex >= 0 && s_SelectedIndex < static_cast<int>(s_Editors.size()))
			? s_Editors[s_SelectedIndex].DisplayName : "";

		Json::Value root = Json::Value::MakeObject();
		root.AddMember("selectedEditor", selected);
		if (s_SelectedIndex >= 0 && s_SelectedIndex < static_cast<int>(s_Editors.size()))
			root.AddMember("selectedEditorPath", s_Editors[s_SelectedIndex].ExecutablePath);
		(void)File::WriteAllText(path, Json::Stringify(root, true));
	}

	void ExternalEditor::LoadPreferences() {
		std::string path = GetPreferencesPath();
		if (!std::filesystem::exists(path)) return;

		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(File::ReadAllText(path), root, &parseError) || !root.IsObject()) {
			FNK_CORE_WARN_TAG("ExternalEditor", "Failed to parse editor preferences '{}': {}", path, parseError);
			return;
		}

		if (const Json::Value* selectedPathValue = root.FindMember("selectedEditorPath")) {
			const std::string selectedPath = selectedPathValue->AsStringOr();
			for (int i = 0; i < static_cast<int>(s_Editors.size()); ++i) {
				if (SamePath(s_Editors[i].ExecutablePath, selectedPath)) {
					s_SelectedIndex = i;
					return;
				}
			}
		}

		const Json::Value* selectedEditorValue = root.FindMember("selectedEditor");
		if (!selectedEditorValue) return;
		std::string savedName = selectedEditorValue->AsStringOr();

		for (int i = 0; i < static_cast<int>(s_Editors.size()); i++) {
			if (s_Editors[i].DisplayName == savedName) {
				s_SelectedIndex = i;
				return;
			}
		}
		if (savedName.rfind("Visual Studio", 0) == 0) {
			for (int i = 0; i < static_cast<int>(s_Editors.size()); ++i) {
				if (s_Editors[i].Type == ExternalEditorType::VisualStudio) {
					s_SelectedIndex = i;
					return;
				}
			}
		}
	}

	void ExternalEditor::SetSelectedIndex(int index) {
		s_SelectedIndex = index;
		SavePreferences();
	}

	const ExternalEditorInfo& ExternalEditor::GetSelected() {
		DetectEditors();
		if (s_SelectedIndex >= 0 && s_SelectedIndex < static_cast<int>(s_Editors.size()))
			return s_Editors[s_SelectedIndex];

		static ExternalEditorInfo fallback;
		return fallback;
	}

	std::string ExternalEditor::ResolveProjectContext(const std::string& filePath, const std::string& ext) {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) return "";

		// C# scripts → project .sln
		if (ext == ".cs") {
			if (std::filesystem::exists(project->SlnPath))
				return std::filesystem::canonical(project->SlnPath).string();
		}

		// C++ native scripts → NativeScripts .sln
		if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp") {
			auto nativeSln = std::filesystem::path(project->NativeScriptsDir)
				/ "build" / (project->Name + "-NativeScripts.sln");
			if (std::filesystem::exists(nativeSln))
				return std::filesystem::canonical(nativeSln).string();
		}

		// Fallback: project .sln if available
		if (std::filesystem::exists(project->SlnPath))
			return std::filesystem::canonical(project->SlnPath).string();

		return "";
	}

#ifdef FNK_PLATFORM_WINDOWS
	// Check if Visual Studio specifically has a window open containing the search string.
	// Requires BOTH the search title AND "Visual Studio" to be in the window title,
	// to avoid false positives from the Funke editor or other windows.
	[[maybe_unused]] static bool IsVisualStudioOpen(const std::string& searchTitle) {
		if (searchTitle.empty()) return false;
		std::wstring wSearch = Utf8ToWide(searchTitle);
		std::wstring wVS = L"Visual Studio";
		bool found = false;

		struct Ctx { bool* found; std::wstring* search; std::wstring* vs; };
		Ctx ctx = { &found, &wSearch, &wVS };

		EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
			auto* c = reinterpret_cast<Ctx*>(lParam);
			wchar_t title[512]{};
			GetWindowTextW(hwnd, title, 512);
			if (wcsstr(title, c->search->c_str()) != nullptr &&
				wcsstr(title, c->vs->c_str()) != nullptr) {
				*c->found = true;
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&ctx));
		return found;
	}

	// Late-bound IDispatch: no #import of EnvDTE to avoid .tlh pull; DISPPARAMS::rgvarg is in REVERSE order.
	static HRESULT InvokeByName(IDispatch* disp, const wchar_t* name, WORD flags,
		VARIANT* args, UINT cArgs, VARIANT* result)
	{
		if (!disp || !name) return E_POINTER;
		LPOLESTR n = const_cast<LPOLESTR>(name);
		DISPID dispid = 0;
		HRESULT hr = disp->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &dispid);
		if (FAILED(hr)) return hr;
		DISPPARAMS params{};
		params.rgvarg = args;
		params.cArgs = cArgs;
		if (flags == DISPATCH_PROPERTYPUT) {
			static DISPID putDispid = DISPID_PROPERTYPUT;
			params.rgdispidNamedArgs = &putDispid;
			params.cNamedArgs = 1;
		}
		return disp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags,
			&params, result, nullptr, nullptr);
	}

	// Walks the ROT for !VisualStudio.DTE monikers; returns AddRef'd IDispatch* of the instance whose Solution.FullName matches slnPath, or nullptr.
	// Also writes the devenv PID to *outPid for AllowSetForegroundWindow (without the grant, DTE.MainWindow.Activate silently taskbar-flashes).
	static IDispatch* FindVSDTEForSolution(const std::wstring& slnPath, DWORD* outPid) {
		if (outPid) *outPid = 0;
		if (slnPath.empty()) return nullptr;

		std::error_code ec;
		std::wstring canonTarget = std::filesystem::weakly_canonical(slnPath, ec).wstring();
		if (canonTarget.empty()) canonTarget = slnPath;

		IRunningObjectTable* rot = nullptr;
		if (FAILED(GetRunningObjectTable(0, &rot)) || !rot) return nullptr;

		IBindCtx* bindCtx = nullptr;
		if (FAILED(CreateBindCtx(0, &bindCtx)) || !bindCtx) {
			rot->Release();
			return nullptr;
		}

		IEnumMoniker* enumMoniker = nullptr;
		if (FAILED(rot->EnumRunning(&enumMoniker)) || !enumMoniker) {
			bindCtx->Release();
			rot->Release();
			return nullptr;
		}

		IDispatch* matched = nullptr;
		IMoniker* moniker = nullptr;
		while (!matched && enumMoniker->Next(1, &moniker, nullptr) == S_OK) {
			LPOLESTR displayName = nullptr;
			if (SUCCEEDED(moniker->GetDisplayName(bindCtx, nullptr, &displayName)) && displayName) {
				std::wstring name(displayName);
				CoTaskMemFree(displayName);

				if (name.rfind(L"!VisualStudio.DTE.", 0) == 0) {
					IUnknown* unk = nullptr;
					if (SUCCEEDED(rot->GetObject(moniker, &unk)) && unk) {
						IDispatch* dte = nullptr;
						if (SUCCEEDED(unk->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&dte))) && dte) {
							VARIANT solVar; VariantInit(&solVar);
							HRESULT hr = InvokeByName(dte, L"Solution", DISPATCH_PROPERTYGET, nullptr, 0, &solVar);
							if (SUCCEEDED(hr) && solVar.vt == VT_DISPATCH && solVar.pdispVal) {
								VARIANT nameVar; VariantInit(&nameVar);
								hr = InvokeByName(solVar.pdispVal, L"FullName", DISPATCH_PROPERTYGET, nullptr, 0, &nameVar);
								if (SUCCEEDED(hr) && nameVar.vt == VT_BSTR && nameVar.bstrVal) {
									std::wstring full(nameVar.bstrVal, SysStringLen(nameVar.bstrVal));
									if (!full.empty()) {
										std::error_code ec2;
										std::wstring canonFull = std::filesystem::weakly_canonical(full, ec2).wstring();
										if (canonFull.empty()) canonFull = full;
										if (_wcsicmp(canonFull.c_str(), canonTarget.c_str()) == 0) {
											matched = dte;   // transfer ownership (already AddRef'd by QI)
											dte = nullptr;
											if (outPid) {
												// Moniker name is "!VisualStudio.DTE.<ver>:<pid>".
												// Parse the PID suffix; leave 0 on parse failure.
												auto colon = name.find_last_of(L':');
												if (colon != std::wstring::npos && colon + 1 < name.size()) {
													wchar_t* end = nullptr;
													unsigned long pid = wcstoul(name.c_str() + colon + 1, &end, 10);
													if (end && *end == L'\0') *outPid = static_cast<DWORD>(pid);
												}
											}
										}
									}
								}
								VariantClear(&nameVar);
							}
							VariantClear(&solVar);
							if (dte) dte->Release();
						}
						unk->Release();
					}
				}
			}
			moniker->Release();
			moniker = nullptr;
		}

		enumMoniker->Release();
		bindCtx->Release();
		rot->Release();
		return matched;
	}

	// Open file in the given DTE instance; retries on RPC_E_CALL_REJECTED to survive VS-busy-with-solution-load; returns false → caller cold-launches.
	static bool OpenFileViaDTE(IDispatch* dte, const std::wstring& file, int line) {
		if (!dte) return false;
		// vsViewKindPrimary — opens whichever editor is registered as primary
		// for the file extension (avoids forcing the XML/code view).
		const wchar_t* kViewKindPrimary = L"{00000000-0000-0000-0000-000000000000}";

		for (int attempt = 0; attempt < 5; ++attempt) {
			VARIANT itemOpsVar; VariantInit(&itemOpsVar);
			HRESULT hr = InvokeByName(dte, L"ItemOperations", DISPATCH_PROPERTYGET,
				nullptr, 0, &itemOpsVar);
			if (hr == RPC_E_CALL_REJECTED) {
				VariantClear(&itemOpsVar);
				Sleep(500);
				continue;
			}
			if (FAILED(hr) || itemOpsVar.vt != VT_DISPATCH || !itemOpsVar.pdispVal) {
				VariantClear(&itemOpsVar);
				return false;
			}

			BSTR fileBstr = SysAllocStringLen(file.c_str(), static_cast<UINT>(file.size()));
			BSTR viewBstr = SysAllocString(kViewKindPrimary);

			// DISPPARAMS::rgvarg is in REVERSE order:
			// args[0] = 2nd parameter (ViewKind), args[1] = 1st parameter (File).
			VARIANT args[2];
			VariantInit(&args[0]); args[0].vt = VT_BSTR; args[0].bstrVal = viewBstr;
			VariantInit(&args[1]); args[1].vt = VT_BSTR; args[1].bstrVal = fileBstr;

			VARIANT openResult; VariantInit(&openResult);
			hr = InvokeByName(itemOpsVar.pdispVal, L"OpenFile", DISPATCH_METHOD,
				args, 2, &openResult);

			SysFreeString(fileBstr);
			SysFreeString(viewBstr);
			VariantClear(&openResult);
			VariantClear(&itemOpsVar);

			if (hr == RPC_E_CALL_REJECTED) {
				Sleep(500);
				continue;
			}
			if (FAILED(hr)) return false;

			VARIANT mainWinVar; VariantInit(&mainWinVar);
			if (SUCCEEDED(InvokeByName(dte, L"MainWindow", DISPATCH_PROPERTYGET,
				nullptr, 0, &mainWinVar)) && mainWinVar.vt == VT_DISPATCH && mainWinVar.pdispVal)
			{
				VARIANT trueVar; VariantInit(&trueVar);
				trueVar.vt = VT_BOOL; trueVar.boolVal = VARIANT_TRUE;
				InvokeByName(mainWinVar.pdispVal, L"Visible", DISPATCH_PROPERTYPUT,
					&trueVar, 1, nullptr);
				VARIANT actResult; VariantInit(&actResult);
				InvokeByName(mainWinVar.pdispVal, L"Activate", DISPATCH_METHOD,
					nullptr, 0, &actResult);
				VariantClear(&actResult);

				VARIANT hwndVar; VariantInit(&hwndVar);
				if (SUCCEEDED(InvokeByName(mainWinVar.pdispVal, L"HWnd", DISPATCH_PROPERTYGET,
					nullptr, 0, &hwndVar)))
				{
					LONG_PTR raw = 0;
					if (hwndVar.vt == VT_I4)      raw = hwndVar.lVal;
					else if (hwndVar.vt == VT_I8) raw = static_cast<LONG_PTR>(hwndVar.llVal);
					HWND hwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(raw));
					if (hwnd && IsWindow(hwnd)) {
						if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
						BringWindowToTop(hwnd);
						SetForegroundWindow(hwnd);
					}
				}
				VariantClear(&hwndVar);
			}
			VariantClear(&mainWinVar);

			// Jump to line via ActiveDocument.Selection.GotoLine(line, Select=false)
			if (line > 0) {
				VARIANT docVar; VariantInit(&docVar);
				if (SUCCEEDED(InvokeByName(dte, L"ActiveDocument", DISPATCH_PROPERTYGET,
					nullptr, 0, &docVar)) && docVar.vt == VT_DISPATCH && docVar.pdispVal)
				{
					VARIANT selVar; VariantInit(&selVar);
					if (SUCCEEDED(InvokeByName(docVar.pdispVal, L"Selection", DISPATCH_PROPERTYGET,
						nullptr, 0, &selVar)) && selVar.vt == VT_DISPATCH && selVar.pdispVal)
					{
						VARIANT gotoArgs[2];
						VariantInit(&gotoArgs[0]); gotoArgs[0].vt = VT_BOOL; gotoArgs[0].boolVal = VARIANT_FALSE;
						VariantInit(&gotoArgs[1]); gotoArgs[1].vt = VT_I4;   gotoArgs[1].lVal    = line;
						VARIANT gotoResult; VariantInit(&gotoResult);
						InvokeByName(selVar.pdispVal, L"GotoLine", DISPATCH_METHOD,
							gotoArgs, 2, &gotoResult);
						VariantClear(&gotoResult);
					}
					VariantClear(&selVar);
				}
				VariantClear(&docVar);
			}

			return true;
		}
		return false;
	}
#endif

	void ExternalEditor::OpenFile(const std::string& filePath, int line) {
		DetectEditors();

		if (s_Editors.empty()) {
			FNK_CORE_WARN_TAG("ExternalEditor", "No code editor available, falling back to OS default");
			Platform::OpenPath(Path::FromUtf8(filePath));
			return;
		}

		const auto& editor = GetSelected();
		std::string absPath = std::filesystem::absolute(filePath).string();

		std::string ext = std::filesystem::path(filePath).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		// Resolve .sln path for VS/Rider, folder path for VS Code
		std::string slnPath = ResolveProjectContext(filePath, ext);
		std::string projectFolder;
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (project) projectFolder = project->RootDirectory;

		FNK_CORE_INFO_TAG("ExternalEditor", "Opening '{}' with {} (sln: {})",
			absPath, editor.DisplayName, slnPath.empty() ? "none" : slnPath);

#ifdef FNK_PLATFORM_WINDOWS
		std::string fullCmd;

		switch (editor.Type) {
		case ExternalEditorType::VSCode: {
			// VS Code needs the FOLDER (not .sln) as primary argument for IntelliSense.
			// code "projectFolder" --reuse-window --goto "file:line:col"
			// If already open, --reuse-window makes VS Code focus the existing window.
			fullCmd = "\"" + editor.ExecutablePath + "\"";
			if (!projectFolder.empty())
				fullCmd += " \"" + projectFolder + "\"";
			fullCmd += " --reuse-window";
			if (line > 0)
				fullCmd += " --goto \"" + absPath + ":" + std::to_string(line) + ":1\"";
			else
				fullCmd += " --goto \"" + absPath + ":1:1\"";
			break;
		}

		case ExternalEditorType::VisualStudio: {
			std::string devenvPath = editor.ExecutablePath;
			std::string sln = slnPath;
			std::string file = absPath;
			int lineNum = line;

			std::thread vsLauncher([devenvPath, sln, file, lineNum]() {
				HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
				const bool needUninit = SUCCEEDED(comHr);

				std::wstring wsln = Utf8ToWide(sln);
				std::wstring wfile = Utf8ToWide(file);

				DWORD matchedPid = 0;
				IDispatch* matchedDTE = sln.empty()
					? nullptr
					: FindVSDTEForSolution(wsln, &matchedPid);
				bool handled = false;
				if (matchedDTE) {
					FNK_CORE_INFO_TAG("ExternalEditor",
						"Matched running VS instance (pid={}) for sln '{}' — opening file via DTE",
						matchedPid, sln);
					if (matchedPid != 0) AllowSetForegroundWindow(matchedPid);
					handled = OpenFileViaDTE(matchedDTE, wfile, lineNum);
					if (!handled) {
						FNK_CORE_WARN_TAG("ExternalEditor",
							"DTE.OpenFile failed on matched instance, falling back to cold launch");
					}
					matchedDTE->Release();
				}

				if (!handled) {
					std::string cmd;
					if (!sln.empty()) {
						cmd = "\"" + devenvPath + "\" \"" + sln + "\" \"" + file + "\"";
						if (lineNum > 0) {
							cmd += " /command \"Edit.GoTo " + std::to_string(lineNum) + "\"";
						}
						FNK_CORE_INFO_TAG("ExternalEditor",
							"No running VS matched sln '{}' — cold launching devenv with sln + file",
							sln);
					}
					else {
						cmd = "\"" + devenvPath + "\" /edit \"" + file + "\"";
						FNK_CORE_WARN_TAG("ExternalEditor",
							"No .sln context — falling back to devenv /edit '{}'", file);
					}

					std::wstring wcmd = Utf8ToWide(cmd);
					std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
					buf.push_back(L'\0');

					STARTUPINFOW si{}; si.cb = sizeof(si);
					PROCESS_INFORMATION pi{};
					if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
						FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi))
					{
						CloseHandle(pi.hProcess);
						CloseHandle(pi.hThread);
					}
				}

				if (needUninit) CoUninitialize();
			});
			TrackLaunchThread(std::move(vsLauncher));

			// Return early — the thread handles everything (matching the
			// original cold-start path's contract).
			return;
		}

		case ExternalEditorType::Rider: {
			fullCmd = "\"" + editor.ExecutablePath + "\"";
			if (!slnPath.empty())
				fullCmd += " \"" + slnPath + "\"";
			if (line > 0)
				fullCmd += " --line " + std::to_string(line);
			fullCmd += " \"" + absPath + "\"";
			break;
		}

		case ExternalEditorType::IntelliJ: {
			// The project folder opens (or reuses) the project window; a running IDE takes the request over.
			fullCmd = "\"" + editor.ExecutablePath + "\"";
			if (!projectFolder.empty())
				fullCmd += " \"" + projectFolder + "\"";
			if (line > 0)
				fullCmd += " --line " + std::to_string(line);
			fullCmd += " \"" + absPath + "\"";
			break;
		}

		default: {
			fullCmd = "\"" + editor.ExecutablePath + "\" \"" + absPath + "\"";
			break;
		}
		}

		// Launch async so we don't block the engine editor.
		// M29: tracked instead of detached so editor shutdown can join.
		std::thread launcher([fullCmd]() {
			std::wstring wcmd = Utf8ToWide(fullCmd);
			std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
			buf.push_back(L'\0');

			STARTUPINFOW si{};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi{};

			if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
				FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi))
			{
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
			}
		});
		TrackLaunchThread(std::move(launcher));
#else
		std::vector<std::string> command{ editor.ExecutablePath };
		switch (editor.Type) {
		case ExternalEditorType::VSCode:
			// The project folder first so IntelliSense sees the whole workspace; --goto jumps to the line.
			if (!projectFolder.empty()) command.push_back(projectFolder);
			command.push_back("--reuse-window");
			command.push_back("--goto");
			command.push_back(line > 0 ? absPath + ":" + std::to_string(line) : absPath);
			break;
		case ExternalEditorType::Rider:
			if (!slnPath.empty()) command.push_back(slnPath);
			if (line > 0) {
				command.push_back("--line");
				command.push_back(std::to_string(line));
			}
			command.push_back(absPath);
			break;
		case ExternalEditorType::IntelliJ:
			if (!projectFolder.empty()) command.push_back(projectFolder);
			if (line > 0) {
				command.push_back("--line");
				command.push_back(std::to_string(line));
			}
			command.push_back(absPath);
			break;
		default:
			command.push_back(absPath);
			break;
		}
		if (!Process::LaunchDetached(command)) {
			FNK_CORE_WARN_TAG("ExternalEditor", "Failed to launch {}; opening with the OS default", editor.DisplayName);
			Platform::OpenPath(Path::FromUtf8(absPath));
		}
#endif
	}

}
