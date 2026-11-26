#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

namespace Funke {
	using namespace AssetBrowserInternal;

	namespace {
		bool IsCppIdentifierChar(char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		}

		// Renames a C++ identifier in source text: replaces only whole-identifier matches in
		// code, leaving comments, string literals and char literals untouched. A plain
		// substring replace would also rewrite EnemySpawner, m_EnemyCount and "Enemy died"
		// when renaming Enemy -> Foe.
		std::string RenameCppIdentifier(const std::string& source, const std::string& oldName, const std::string& newName) {
			if (oldName.empty() || oldName == newName) {
				return source;
			}

			std::string out;
			out.reserve(source.size());
			const std::size_t n = source.size();
			std::size_t i = 0;
			while (i < n) {
				const char c = source[i];
				const char next = i + 1 < n ? source[i + 1] : '\0';

				if (c == '/' && next == '/') {
					const std::size_t end = source.find('\n', i);
					const std::size_t stop = end == std::string::npos ? n : end;
					out.append(source, i, stop - i);
					i = stop;
					continue;
				}
				if (c == '/' && next == '*') {
					const std::size_t end = source.find("*/", i + 2);
					const std::size_t stop = end == std::string::npos ? n : end + 2;
					out.append(source, i, stop - i);
					i = stop;
					continue;
				}
				if (c == '"' || c == '\'') {
					std::size_t j = i + 1;
					while (j < n && source[j] != c && source[j] != '\n') {
						j += (source[j] == '\\' && j + 1 < n) ? 2 : 1;
					}
					const std::size_t stop = j < n ? j + 1 : n;
					out.append(source, i, stop - i);
					i = stop;
					continue;
				}
				if (IsCppIdentifierChar(c) && (i == 0 || !IsCppIdentifierChar(source[i - 1]))) {
					std::size_t j = i;
					while (j < n && IsCppIdentifierChar(source[j])) ++j;
					if (j - i == oldName.size() && source.compare(i, j - i, oldName) == 0) {
						out += newName;
					}
					else {
						out.append(source, i, j - i);
					}
					i = j;
					continue;
				}

				out += c;
				++i;
			}
			return out;
		}
	}

	void AssetBrowser::BeginRename(const std::string& path, const std::string& currentName) {
		m_IsRenaming = true;
		m_RenamePath = path;
		m_PressedPath.clear();
		m_PendingScrollToPath = path;
		m_RenameInput.Reset();

		// Strip extension from display name when ShowFileExtensions is off; CommitRename re-appends it on commit.
		const bool showExt = EditorPreferences::GetShowFileExtensions();
		std::string display = currentName;
		if (!showExt) {
			std::filesystem::path p(currentName);
			std::string stem = p.stem().string();
			std::string ext = p.extension().string();
			// stem.empty() catches dotfiles like ".env" — leave them
			// alone so we don't render an empty rename buffer.
			if (!ext.empty() && !stem.empty()) {
				display = stem;
			}
		}
		std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", display.c_str());
	}

	void AssetBrowser::CommitRename() {
		std::string newName(m_RenameBuffer);
		std::string oldName = std::filesystem::path(m_RenamePath).filename().string();

		if (m_PendingScriptType == PendingScriptType::EntityPrefab) {
			// Strip a trailing ".prefab" the user typed so we don't end up with "Foo.prefab.prefab".
			std::string baseName = newName;
			if (baseName.size() > 7) {
				std::string lower = baseName;
				std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
				if (lower.substr(lower.size() - 7) == ".prefab") {
					baseName = baseName.substr(0, baseName.size() - 7);
				}
			}
			if (baseName.empty()) {
				baseName = "NewPrefab";
			}

			// Suffix " (N)" to dodge collisions with anything already in the folder.
			std::filesystem::path targetPath = std::filesystem::path(m_PendingScriptDir) / (baseName + ".prefab");
			std::error_code existsEc;
			for (int n = 1; targetPath.string() != m_RenamePath
				&& std::filesystem::exists(targetPath, existsEc) && n < 10000; ++n) {
				targetPath = std::filesystem::path(m_PendingScriptDir) / (FormatDuplicateAssetName(baseName, n) + ".prefab");
				existsEc.clear();
			}

			const std::string finalPath = targetPath.string();
			if (finalPath != m_RenamePath) RenameEntry(m_RenamePath, targetPath.filename().string());
			else AssetRegistry::ImportAssetPathIncremental(finalPath);

			m_PendingScriptType = PendingScriptType::None;
			m_PendingScriptDir.clear();
			m_PendingPrefabSourceEntity = entt::null;
			m_NeedsRefresh = true;
			CancelRename();
			return;
		}

		if (m_PendingScriptType != PendingScriptType::None) {
			std::string className = newName;
			const bool isCSharp = m_PendingScriptType == PendingScriptType::CSharp
				|| m_PendingScriptType == PendingScriptType::CSharpEmpty
				|| m_PendingScriptType == PendingScriptType::CSharpComponent
				|| m_PendingScriptType == PendingScriptType::CSharpNativeComponent
				|| m_PendingScriptType == PendingScriptType::CSharpSceneSystem
				|| m_PendingScriptType == PendingScriptType::CSharpGlobalSystem
				|| m_PendingScriptType == PendingScriptType::CSharpDataAsset;

			const bool isComponent = m_PendingScriptType == PendingScriptType::CSharpComponent;
			const bool isNativeComponent = m_PendingScriptType == PendingScriptType::CSharpNativeComponent;
			const bool isSceneSystem = m_PendingScriptType == PendingScriptType::CSharpSceneSystem;
			const bool isGlobalSystem = m_PendingScriptType == PendingScriptType::CSharpGlobalSystem;
			const bool isEmpty = m_PendingScriptType == PendingScriptType::CSharpEmpty;
			const bool isDataAsset = m_PendingScriptType == PendingScriptType::CSharpDataAsset;
			const bool isBolt = m_PendingScriptType == PendingScriptType::Bolt;
			const std::string ext = isBolt ? ".bolt" : (isCSharp ? ".cs" : ".cpp");

			if (!className.empty() && className.find('.') == std::string::npos) {
			}
			else if (className.size() > ext.size()) {
				std::string lowerName = className;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				if (lowerName.substr(lowerName.size() - ext.size()) == ext)
					className = className.substr(0, className.size() - ext.size());
			}

			const std::string classNameFallback = isComponent       ? "NewComponent"
			                                    : isNativeComponent ? "NewNativeComponent"
			                                    : isSceneSystem      ? "NewSceneSystem"
			                                    : isGlobalSystem    ? "NewGlobalSystem"
			                                    : isDataAsset       ? "NewDataAsset"
			                                                        : "NewScript";
			className = EditorScriptDiscovery::SanitizeIdentifier(className, classNameFallback);

			std::string finalFileName = className + ext;
			std::string finalPath = (std::filesystem::path(m_PendingScriptDir) / finalFileName).string();

			// Never overwrite an existing script with boilerplate. The collision prompt only
			// guards the placeholder name, not the name typed here. A target that resolves to
			// the placeholder itself (same name, or a case-only change) is fine.
			{
				std::error_code existsEc;
				if (std::filesystem::exists(finalPath, existsEc)) {
					std::error_code eqEc;
					const bool isPlaceholder = std::filesystem::exists(m_RenamePath, eqEc)
						&& std::filesystem::equivalent(finalPath, m_RenamePath, eqEc) && !eqEc;
					if (!isPlaceholder) {
						FNK_ERROR_TAG("AssetBrowser",
							"Can't create '{}': a file with that name already exists. Choose a different name.",
							finalFileName);
						CancelRename();
						return;
					}
				}
			}

			if (!std::filesystem::exists(finalPath) && !AssetFileHistory::DestinationAvailable(finalPath)) {
				FNK_ERROR_TAG("AssetBrowser", "Script metadata already exists: {}", finalPath);
				return;
			}
			auto edit = BeginFileEdit({ m_RenamePath, finalPath }, "Create Script");
			if (!edit) return;
			if (std::filesystem::exists(m_RenamePath)) {
				std::error_code ec;
				std::filesystem::remove(m_RenamePath, ec);
			}

			if (isCSharp) {
				std::string boilerplate;
				if (isComponent) {
					boilerplate =
						"using Funke;\n"
						"\n"
						"public class " + className + " : Component\n"
						"{\n"
						"     \n"
						"}\n";
				}
				else if (isNativeComponent) {
					boilerplate =
						"using System.Runtime.InteropServices;\n"
						"using Funke;\n"
						"using Funke.Components;\n"
						"\n"
						"[StructLayout(LayoutKind.Sequential)]\n"
						"public struct " + className + " : IComponent\n"
						"{\n"
						"     \n"
						"    public " + className + "() { }\n"
						"}\n";
				}
				else if (isSceneSystem) {
					boilerplate =
						"using Funke;\n"
						"\n"
						"public class " + className + " : SceneSystem\n"
						"{\n"
						"    public override void OnStart()\n"
						"    {\n"
						"    }\n"
						"\n"
						"    public override void OnUpdate()\n"
						"    {\n"
						"    }\n"
						"}\n";
				}
				else if (isGlobalSystem) {
					boilerplate =
						"using Funke;\n"
						"\n"
						"public class " + className + " : GlobalSystem\n"
						"{\n"
						"    public static " + className + " Instance { get; private set; } = null!;\n"
						"\n"
						"    public override void OnInitialize()\n"
						"    {\n"
						"        Instance = this;\n"
						"    }\n"
						"\n"
						"    public override void OnUpdate()\n"
						"    {\n"
						"    }\n"
						"}\n";
				}
				else if (isDataAsset) {
					boilerplate =
						"using Funke;\n"
						"\n"
						"[CreateDataAsset(\"" + className + "\")]\n"
						"public class " + className + " : DataAsset\n"
						"{\n"
						"     \n"
						"}\n";
				}
				else if (isEmpty) {
					boilerplate =
						"using Funke;\n"
						"\n"
						"public class " + className + "\n"
						"{\n"
						"     \n"
						"}\n";
				}
				else {
					boilerplate =
						"using Funke;\n"
						"\n"
						"public class " + className + " : EntityScript\n"
						"{\n"
						"    public override void OnStart()\n"
						"    {\n"
						"    }\n"
						"\n"
						"    public override void OnUpdate()\n"
						"    {\n"
						"    }\n"
						"}\n";
				}

				std::ofstream file(finalPath);
				if (file.is_open()) { file << boilerplate; file.close(); }

				FunkeProject* project = ProjectManager::GetCurrentProject();
				if (!project) {
					auto sandboxDir = std::filesystem::path(Path::ExecutableDir()) / ".." / ".." / ".." / "Funke-Sandbox" / "Source";
					if (std::filesystem::exists(sandboxDir)) {
						auto dst = sandboxDir / finalFileName;
						if (!std::filesystem::exists(dst)) {
							std::ofstream f(dst); if (f.is_open()) { f << boilerplate; f.close(); }
						}
					}
				}
			}
			else if (isBolt) {
				if (!File::WriteAllText(finalPath, EditorScriptDiscovery::BuildBoltScriptSource(className))) {
					FNK_ERROR_TAG("AssetBrowser", "Failed to write Bolt script '{}'", finalPath);
					CancelRename();
					return;
				}
			}
			else {
				const std::string boilerplate = EditorScriptDiscovery::BuildNativeScriptSource(className);

				std::ofstream file(finalPath);
				file << boilerplate;
				file.close();
				if (!file) {
					FNK_ERROR_TAG("AssetBrowser", "Failed to write C++ script '{}'", finalPath);
					CancelRename();
					return;
				}

				FunkeProject* project = ProjectManager::GetCurrentProject();
				if (project) {
					project->EnsureNativeScriptProjectFiles();
				}
				else if (std::filesystem::path nativeDir = GetNativeSourceDirectory(true); !nativeDir.empty()) {
					std::error_code ec;
					std::filesystem::create_directories(nativeDir, ec);
					auto dst = nativeDir / finalFileName;
					if (!std::filesystem::exists(dst)) {
						std::ofstream f(dst); if (f.is_open()) { f << boilerplate; f.close(); }
					}
				}
			}

			AssetRegistry::ImportAssetPathIncremental(finalPath);
			FinishFileEdit(edit);
			EditorScriptDiscovery::MarkDirty();
			m_SelectedPath = finalPath;
			m_PendingScriptType = PendingScriptType::None;
			m_PendingScriptDir.clear();
			m_NeedsRefresh = true;
			CancelRename();
			return;
		}

		if (!newName.empty() && newName.find('.') == std::string::npos) {
			std::string oldExt = std::filesystem::path(m_RenamePath).extension().string();
			if (!oldExt.empty()) {
				newName += oldExt;
			}
		}

		if (!newName.empty() && newName != oldName) {
			RenameEntry(m_RenamePath, newName);
		}
		CancelRename();
	}

	void AssetBrowser::CancelRename() {
		if (m_PendingScriptType != PendingScriptType::None) {
			if (!m_RenamePath.empty() && std::filesystem::exists(m_RenamePath)) {
				std::error_code ec;
				std::filesystem::remove(m_RenamePath, ec);
				if (!ec) {
					AssetRegistry::DeleteCompanionMetadata(m_RenamePath);
				}
			}
			m_PendingScriptType = PendingScriptType::None;
			m_PendingScriptDir.clear();
			m_PendingPrefabSourceEntity = entt::null;
			m_NeedsRefresh = true;
		}

		m_IsRenaming = false;
		m_RenamePath.clear();
		m_RenameInput.Reset();
	}

	bool AssetBrowser::IsRenamingEntry(const std::string& path) const {
		return m_IsRenaming && m_RenamePath == path;
	}

	bool AssetBrowser::BeginRenameSelected() {
		if (m_SelectedPath.empty() || m_IsRenaming) {
			return false;
		}

		std::error_code ec;
		if (!std::filesystem::exists(m_SelectedPath, ec) || ec) {
			return false;
		}

		BeginRename(m_SelectedPath, std::filesystem::path(m_SelectedPath).filename().string());
		return true;
	}

	void AssetBrowser::RenameEntry(const std::string& path, const std::string& newName) {
		// The name is joined onto the parent directory, so separators or ".." would move the asset (and its
		// .meta) elsewhere, even out of Assets/. Windows also rejects reserved characters and trailing dots/spaces.
		const bool invalidName = newName.empty() || newName == "." || newName == ".."
			|| newName.find_first_of("/\\:*?\"<>|") != std::string::npos
			|| newName.back() == '.' || newName.back() == ' '
			|| std::any_of(newName.begin(), newName.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
		if (invalidName) {
			FNK_WARN_TAG("AssetBrowser", "Cannot rename to '{}': names cannot be empty, contain / \\ : * ? \" < > |, or end with a dot or space",
				newName);
			return;
		}

		const std::filesystem::path oldPath(path);
		const std::filesystem::path requestedPath = oldPath.parent_path() / newName;
		if (NormalizeSceneStem(requestedPath.extension().string()) == ".scene"
			&& SceneStemExists(requestedPath.stem().string(), oldPath)) {
			FNK_WARN_TAG("AssetBrowser", "Cannot rename scene '{}': another scene already uses that name",
				requestedPath.stem().string());
			return;
		}

		if (oldPath == requestedPath) return;
		if (!AssetFileHistory::DestinationAvailable(requestedPath)) {
			FNK_WARN_TAG("AssetBrowser", "Rename target or metadata already exists: {}", requestedPath.string());
			return;
		}
		if (NormalizeSceneStem(requestedPath.extension().string()) == ".cs") {
			if (const auto* project = ProjectManager::GetCurrentProject(); project && !project->ScriptsDirectory.empty()) {
				const auto mirrorTarget = (std::filesystem::path(project->ScriptsDirectory) / newName).lexically_normal();
				if (mirrorTarget != requestedPath.lexically_normal() && !AssetFileHistory::DestinationAvailable(mirrorTarget)) {
					FNK_WARN_TAG("AssetBrowser", "Script mirror target already exists: {}", mirrorTarget.string());
					return;
				}
			}
		}
		auto edit = BeginFileEdit({ path, requestedPath.string() }, "Rename Asset");
		if (!edit) return;
		// Same dangling-WGPUTextureView risk as DeleteEntry: ImGui may have
		// already recorded a draw command for the old-key thumbnail this
		// frame. Queue the invalidate for next frame instead.
		m_PendingThumbnailInvalidates.push_back(path);

		std::string oldExt = std::filesystem::path(path).extension().string();
		std::string oldStem = std::filesystem::path(path).stem().string();
		std::vector<std::string> renamedAssetPaths;
		CollectRegistryAssetPaths(path, renamedAssetPaths);

		if (AssetFileHistory::MoveAsset(oldPath, requestedPath)) {
			EditorScriptDiscovery::MarkDirty();
			std::filesystem::path p(path);
			std::string newPath = (p.parent_path() / newName).string();
			ForgetRegistryAssetPaths(renamedAssetPaths);
			RegisterRegistryAssetTree(newPath);
			if (m_SelectedPath == path) {
				m_SelectedPath = newPath;
			}
			for (std::string& selectedPath : m_SelectedPaths) {
				if (selectedPath == path) {
					selectedPath = newPath;
				}
			}

			std::string ext = std::filesystem::path(newName).extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
			std::string newStem = std::filesystem::path(newName).stem().string();
			const bool oldIsNativeScript = IsNativeScriptSourceExtension(oldExt);
			const bool newIsNativeScript = IsNativeScriptSourceExtension(ext);

			std::filesystem::path projectSourceDir;
			FunkeProject* project = ProjectManager::GetCurrentProject();
			if ((oldIsNativeScript || newIsNativeScript) && !project)
			{
				projectSourceDir = GetNativeSourceDirectory(newIsNativeScript);
			}
			else if (ext == ".cs")
			{
				if (project)
					projectSourceDir = project->ScriptsDirectory;
				else
					projectSourceDir = std::filesystem::path(Path::ExecutableDir()) / ".." / ".." / ".." / "Funke-Sandbox" / "Source";
			}

			if (!projectSourceDir.empty() && (std::filesystem::exists(projectSourceDir) || newIsNativeScript))
			{
				if (newIsNativeScript) {
					std::error_code ec;
					std::filesystem::create_directories(projectSourceDir, ec);
				}

				auto oldProjectFile = projectSourceDir / (oldStem + oldExt);
				auto newProjectFile = projectSourceDir / newName;

				if (oldIsNativeScript && !newIsNativeScript)
				{
					std::error_code ec;
					std::filesystem::remove(oldProjectFile, ec);
				}
				else if (newIsNativeScript)
				{
					if (std::filesystem::exists(oldProjectFile))
					{
						std::error_code ec;
						std::filesystem::rename(oldProjectFile, newProjectFile, ec);
					}
					else if (std::filesystem::exists(newPath) && !std::filesystem::exists(newProjectFile))
					{
						std::error_code ec;
						std::filesystem::copy_file(newPath, newProjectFile, std::filesystem::copy_options::none, ec);
					}

					if (std::filesystem::exists(newProjectFile))
					{
						std::ifstream in(newProjectFile);
						std::string content((std::istreambuf_iterator<char>(in)),
							std::istreambuf_iterator<char>());
						in.close();

						const std::string renamed = RenameCppIdentifier(content, oldStem, newStem);
						if (renamed != content) {
							std::ofstream out(newProjectFile, std::ios::binary | std::ios::trunc);
							out << renamed;
							out.flush();
							if (!out) {
								FNK_ERROR_TAG("AssetBrowser",
									"Failed to update class name in '{}'; rename '{}' to '{}' there manually.",
									Path::ToUtf8(newProjectFile), oldStem, newStem);
							}
						}
					}
				}
			}

			// Scene files carry their displayed name inside the JSON. Without
			// this sync, renaming Foo.scene -> Bar.scene leaves the embedded
			// name as "Foo", and the hierarchy still shows "Foo" the next time
			// the file is opened.
			if (ext == ".scene")
			{
				SyncSceneEmbeddedNameToFilename(newPath, newStem);
				UpdateLoadedSceneNameAfterRename(oldStem, newStem);
				edit->Validate = [oldStem, newStem, oldPath, requestedPath](bool redo) {
					if (SceneStemExists(redo ? newStem : oldStem, redo ? oldPath : requestedPath)) {
						throw std::runtime_error("Another scene already uses the name '" + (redo ? newStem : oldStem)
							+ "' required by " + Path::ToUtf8(redo ? requestedPath : oldPath));
					}
				};
				edit->Applied = [oldStem, newStem](bool redo) {
					UpdateLoadedSceneNameAfterRename(redo ? oldStem : newStem, redo ? newStem : oldStem);
				};
			}

			m_NeedsRefresh = true;
		}
		FinishFileEdit(edit);
	}
}
