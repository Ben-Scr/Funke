#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/NameComponent.hpp"
#include "Graphics/Material.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/DataAssetManager.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include <imgui.h>
#include <array>
#include <cstdio>
#include <fstream>

namespace Funke {
	using namespace AssetBrowserInternal;

	void AssetBrowser::CreateAssetWithCollisionCheck(
		std::filesystem::path preferredPath,
		std::function<void(const std::filesystem::path&)> doCreate)
	{
		doCreate = [this, create = std::move(doCreate)](const std::filesystem::path& path) {
			if (!AssetFileHistory::DestinationAvailable(path)) {
				FNK_WARN_TAG("AssetBrowser", "Creation target or metadata already exists: {}", path.string());
				return;
			}
			auto edit = BeginFileEdit({ path.string() }, "Create Asset");
			if (!edit) return;
			create(path);
			RegisterRegistryAssetTree(path);
			FinishFileEdit(edit);
		};
		std::error_code ec;
		const bool exists = !AssetFileHistory::DestinationAvailable(preferredPath);
		if (!exists) {
			doCreate(preferredPath);
			return;
		}

		auto prompt = std::make_unique<PendingCreationPrompt>();
		prompt->PreferredPath = std::move(preferredPath);
		prompt->DisplayName = prompt->PreferredPath.filename().string();
		prompt->DoCreate = std::move(doCreate);
		m_PendingCreationPrompt = std::move(prompt);
		m_OpenCreationPromptThisFrame = true;
	}

	void AssetBrowser::RenderCreationCollisionPrompt() {
		constexpr const char* k_PromptId = "Asset already exists###AssetBrowserCollision";

		if (m_OpenCreationPromptThisFrame) {
			ImGui::OpenPopup(k_PromptId);
			m_OpenCreationPromptThisFrame = false;
		}

		if (!m_PendingCreationPrompt) return;

		// Center on the main viewport (= editor window). Promoted to a real OS
		// window via SetNextWindowAsNativeDialog so the prompt isn't trapped
		// inside the editor and still works while the editor is unfocused.
		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();

		if (ImGuiDialog::BeginModal(k_PromptId, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			ImGuiDialog::Message("##Collision", ("An asset named \"" + m_PendingCreationPrompt->DisplayName
				+ "\" already exists in this folder.").c_str());
			ImGui::Spacing();
			// The footer has no per-button tooltips, so the choices are explained inline.
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Make Copy keeps the existing asset and creates the new one with a numbered suffix.");
			ImGui::PopStyleColor();

			bool decided = false;
			// Move the prompt out before invoking DoCreate so the callback can
			// re-enter CreateAssetWithCollisionCheck without trampling state.
			auto finishWith = [&](const std::filesystem::path& finalPath) {
				auto local = std::move(m_PendingCreationPrompt);
				local->DoCreate(finalPath);
				decided = true;
			};

			const int choice = EditorUI::DialogFooter({ "Make Copy", "Cancel" }, /*primary=*/0);
			if (choice == 0) {
				const std::filesystem::path parent = m_PendingCreationPrompt->PreferredPath.parent_path();
				const std::string stem = m_PendingCreationPrompt->PreferredPath.stem().string();
				const std::string ext = m_PendingCreationPrompt->PreferredPath.extension().string();
				std::filesystem::path candidate = m_PendingCreationPrompt->PreferredPath;
				std::error_code ec;
				for (int n = 1; !AssetFileHistory::DestinationAvailable(candidate) && n < 10000; ++n) {
					candidate = parent / (FormatDuplicateAssetName(stem, n) + ext);
					ec.clear();
				}
				finishWith(candidate);
			}
			else if (choice == 1) {
				m_PendingCreationPrompt.reset();
				decided = true;
			}

			if (!decided && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				m_PendingCreationPrompt.reset();
				decided = true;
			}

			if (decided) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void AssetBrowser::CreateFolder(const std::string& parentDir) {
		std::string baseName = "New Folder";
		std::string folderPath = (std::filesystem::path(parentDir) / baseName).string();
		int counter = 1;
		while (!AssetFileHistory::DestinationAvailable(Path::FromUtf8(folderPath))) {
			folderPath = (std::filesystem::path(parentDir) / (baseName + " " + std::to_string(counter))).string();
			counter++;
		}

		auto edit = BeginFileEdit({ folderPath }, "Create Folder");
		if (!edit) return;
		Directory::Create(folderPath, false);
		FinishFileEdit(edit);
		m_NeedsRefresh = true;

		Refresh();

		m_SelectedPath = folderPath;
		std::string name = std::filesystem::path(folderPath).filename().string();
		BeginRename(folderPath, name);
	}

	void AssetBrowser::CreateScript(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewEntityScript.cs";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::CSharp;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateBoltScript(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewBoltScript.bolt";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::Bolt;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateCppScript(const std::string& parentDir) {
		if (!ProjectManager::GetCurrentProject()) {
			FNK_WARN_TAG("AssetBrowser", "Open or create a project to add C++ scripts.");
			return;
		}
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewNativeScript.cpp";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::Cpp;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateManagedCSharpComponent(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewComponent.cs";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::CSharpComponent;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateNativeCSharpComponent(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewNativeComponent.cs";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::CSharpNativeComponent;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateSceneSystem(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewSceneSystem.cs";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::CSharpSceneSystem;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateGlobalSystem(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewGlobalSystem.cs";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::CSharpGlobalSystem;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateEmptyScript(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewScript.cs";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::CSharpEmpty;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateDataAssetScript(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / "NewDataAsset.cs";
		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir](const std::filesystem::path& finalPath) {
				m_PendingScriptType = PendingScriptType::CSharpDataAsset;
				m_PendingScriptDir = parentDir;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPath.string();
				BeginRename(finalPath.string(), finalPath.stem().string());
			});
	}


	void AssetBrowser::CreateDefaultTexture(const std::string& parentDir,
		const std::string& sourceFile, const std::string& displayName)
	{
		const std::string sourceDir = Path::ResolveFunkeAssets("Textures/Default");
		if (sourceDir.empty()) {
			FNK_WARN_TAG("AssetBrowser",
				"Default textures directory not found (FunkeAssets/Textures/Default). "
				"Cannot create '{}'.", displayName);
			return;
		}

		const std::filesystem::path source =
			std::filesystem::path(sourceDir) / sourceFile;
		if (!std::filesystem::exists(source)) {
			FNK_WARN_TAG("AssetBrowser",
				"Default texture '{}' not found at {}.",
				sourceFile, source.string());
			return;
		}

		const std::string ext = source.extension().string();
		const std::filesystem::path preferred =
			std::filesystem::path(parentDir) / (displayName + ext);

		CreateAssetWithCollisionCheck(preferred,
			[this, source](const std::filesystem::path& finalPath) {
				try {
					std::filesystem::copy_file(source, finalPath,
						std::filesystem::copy_options::overwrite_existing);
				}
				catch (const std::exception& e) {
					FNK_ERROR_TAG("AssetBrowser",
						"Failed to copy default texture '{}' to '{}': {}",
						source.string(), finalPath.string(), e.what());
					return;
				}

				const std::string finalPathStr = finalPath.string();
				AssetRegistry::ImportAssetPathIncremental(finalPathStr);
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPathStr;
				BeginRename(finalPathStr, finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateScene(const std::string& parentDir) {
		const std::filesystem::path preferred = MakeGloballyUniqueScenePath(
			std::filesystem::path(parentDir) / "NewScene.scene");
		if (preferred.empty()) {
			FNK_WARN_TAG("AssetBrowser", "Could not choose a globally unique scene name");
			return;
		}

		CreateAssetWithCollisionCheck(preferred,
			[this](const std::filesystem::path& finalPath) {
				const std::string sceneName = finalPath.stem().string();
				const std::string content =
					"{\n"
					"  \"version\": 1,\n"
					"  \"name\": \"" + sceneName + "\",\n"
					"  \"systems\": [],\n"
					"  \"entities\": [\n"
					"    {\n"
					"      \"name\": \"Camera\",\n"
					"      \"Transform2D\": { \"posX\": 0, \"posY\": 0, \"rotation\": 0, \"scaleX\": 1, \"scaleY\": 1 },\n"
					"      \"Camera2D\": { \"orthoSize\": 5, \"zoom\": 1 }\n"
					"    }\n"
					"  ]\n"
					"}\n";

				const std::string finalPathStr = finalPath.string();
				if (!File::WriteAllText(finalPathStr, content)) {
					FNK_WARN_TAG("AssetBrowser", "Failed to create scene '{}'", finalPathStr);
					return;
				}

				AssetRegistry::ImportAssetPathIncremental(finalPathStr);
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPathStr;
				BeginRename(finalPathStr, finalPath.filename().string());
			});
	}

	void AssetBrowser::CreateDataAsset(const std::string& parentDir, const std::string& typeName) {
		const std::string fileName = typeName.starts_with("type:") ? typeName.substr(5) : typeName;
		const std::filesystem::path preferred =
			std::filesystem::path(parentDir) / (fileName + ".dataasset");

		CreateAssetWithCollisionCheck(preferred,
			[this, typeName](const std::filesystem::path& finalPath) {
				const std::string finalPathStr = finalPath.string();
				// DataAssetManager::Create writes the {version,type,fields} stub and registers the GUID.
				if (DataAssetManager::Create(finalPathStr, typeName) == 0)
					return;

				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPathStr;
				BeginRename(finalPathStr, finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateMaterial(const std::string& parentDir) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir) / (std::string("NewMaterial") + k_MaterialExtension);

		CreateAssetWithCollisionCheck(preferred,
			[this](const std::filesystem::path& finalPath) {
				const std::string finalPathStr = finalPath.string();
				if (!MaterialLibrary::SaveToFile(finalPathStr, Material{})) {
					FNK_WARN_TAG("AssetBrowser", "Failed to create material '{}'", finalPathStr);
					return;
				}

				AssetRegistry::ImportAssetPathIncremental(finalPathStr);
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPathStr;
				BeginRename(finalPathStr, finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateAnimationAsset(const std::string& parentDir, bool controller) {
		const std::filesystem::path preferred = std::filesystem::path(parentDir)
			/ (controller ? std::string("NewAnimator") + k_AnimatorControllerExtension : std::string("NewAnimation") + k_AnimationClipExtension);

		CreateAssetWithCollisionCheck(preferred,
			[this, controller](const std::filesystem::path& finalPath) {
				const std::string finalPathStr = finalPath.string();
				const bool saved = controller
					? AnimatorControllerLibrary::SaveToFile(finalPathStr, AnimatorController{})
					: AnimationClipLibrary::SaveToFile(finalPathStr, AnimationClip{});
				if (!saved) {
					FNK_WARN_TAG("AssetBrowser", "Failed to create '{}'", finalPathStr);
					return;
				}

				AssetRegistry::ImportAssetPathIncremental(finalPathStr);
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPathStr;
				BeginRename(finalPathStr, finalPath.stem().string());
			});
	}

	void AssetBrowser::CreateFile(const std::string& parentDir, const std::string& baseName,
		const std::string& extension, const std::string& defaultContent) {
		// Normalize: extension may or may not include the leading dot.
		const std::string ext = (extension.empty() || extension[0] == '.') ? extension : ("." + extension);

		const std::filesystem::path preferred =
			std::filesystem::path(parentDir) / (baseName + ext);

		CreateAssetWithCollisionCheck(preferred,
			[this, defaultContent](const std::filesystem::path& finalPath) {
				const std::string finalPathStr = finalPath.string();
				std::ofstream file(finalPathStr, std::ios::binary);
				if (file.is_open()) {
					if (!defaultContent.empty()) {
						file.write(defaultContent.data(),
							static_cast<std::streamsize>(defaultContent.size()));
					}
					file.close();
				}

				AssetRegistry::ImportAssetPathIncremental(finalPathStr);
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPathStr;
				BeginRename(finalPathStr,
					std::filesystem::path(finalPathStr).stem().string());
			});
	}

	void AssetBrowser::CreatePrefabVariant(const std::string& basePath) {
		const uint64_t baseGuid = AssetRegistry::GetOrCreateAssetUUID(basePath);
		Json::Value expanded;
		if (!baseGuid || !SceneSerializer::ResolvePrefabSource(baseGuid, expanded)) return;
		const auto source = Path::FromUtf8(basePath);
		auto target = source.parent_path() / (Path::ToUtf8(source.stem()) + " Variant.prefab");
		for (int index = 1; index < 10000 && !AssetFileHistory::DestinationAvailable(target); ++index)
			target = source.parent_path() / (Path::ToUtf8(source.stem()) + " Variant (" + std::to_string(index) + ").prefab");
		if (!AssetFileHistory::DestinationAvailable(target)) return;
		auto history = BeginFileEdit({Path::ToUtf8(target)}, "Create Prefab Variant");
		if (!history) return;
		Json::Value value = Json::Value::MakeObject();
		value.AddMember("version", Json::Value(SceneSerializerInternal::k_SceneFormatVersion));
		value.AddMember("type", Json::Value("Prefab"));
		value.AddMember("PrefabRef", Json::Value(std::to_string(baseGuid)));
		value.AddMember("Overrides", Json::Value::MakeObject());
		if (!SceneSerializerStorage::WriteRootToFile(Path::ToUtf8(target), value, SceneSerializationFormat::Json)) return;
		AssetRegistry::ImportAssetPathIncremental(Path::ToUtf8(target));
		FinishFileEdit(history);
		m_NeedsRefresh = true; Refresh();
		m_SelectedPaths = {Path::ToUtf8(target)}; m_SelectedPath = Path::ToUtf8(target);
		BeginRename(Path::ToUtf8(target), Path::ToUtf8(target.filename()));
	}

	void AssetBrowser::CreateEntityPrefab(const std::string& parentDir, EntityHandle sourceEntity) {
		const std::filesystem::path preferred =
			std::filesystem::path(parentDir) / "NewPrefab.prefab";

		CreateAssetWithCollisionCheck(preferred,
			[this, parentDir, sourceEntity](const std::filesystem::path& finalPath) {
				const std::string finalPathStr = finalPath.string();

				if (sourceEntity != entt::null) {
					Scene* activeScene = SceneManager::GetActiveScene();
					if (activeScene && activeScene->IsValid(sourceEntity)) {
						SceneSerializer::SaveEntityToFile(*activeScene, sourceEntity, finalPathStr);
					}
				}
				else {
					const std::string content =
						"{\n"
						"  \"version\": 1,\n"
						"  \"type\": \"Prefab\",\n"
						"  \"Entity\": {\n"
						"    \"Transform2D\": { \"posX\": 0, \"posY\": 0, \"rotation\": 0, \"scaleX\": 1, \"scaleY\": 1 }\n"
						"  },\n"
						"  \"prefab\": {\n"
						"    \"Transform2D\": { \"posX\": 0, \"posY\": 0, \"rotation\": 0, \"scaleX\": 1, \"scaleY\": 1 }\n"
						"  }\n"
						"}\n";
					std::ofstream file(finalPathStr);
					if (file.is_open()) {
						file << content;
						file.close();
					}
				}

				AssetRegistry::ImportAssetPathIncremental(finalPathStr);
				m_PendingScriptType = PendingScriptType::EntityPrefab;
				m_PendingScriptDir = parentDir;
				m_PendingPrefabSourceEntity = sourceEntity;
				m_NeedsRefresh = true;
				Refresh();
				m_SelectedPath = finalPathStr;
				BeginRename(finalPathStr, finalPath.filename().string());
			});
	}
}
