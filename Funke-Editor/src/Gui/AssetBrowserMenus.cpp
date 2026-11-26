#include "Gui/AssetBrowser.hpp"
#include "Core/EngineModules.hpp"
#include "Core/Platform.hpp"
#include "Graphics/CustomShader.hpp"
#include "Gui/EditorMenu.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Gui/Icons.hpp"
#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptEngine.hpp"
#include <imgui.h>
#include <filesystem>

namespace Funke {
	void AssetBrowser::RenderGridContextMenu() {
		using Icon = EditorMenu::Icon;
		if (!m_ItemRightClicked &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
			ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
		{
			ImGui::OpenPopup("##AssetGridCtx");
		}


		if (ImGui::BeginPopup("##AssetGridCtx")) {
			if (!m_AssetClipboardPaths.empty()) {
				if (EditorMenu::Item(Icon::Paste, "Paste Asset", EditorShortcuts::k_PasteLabel)) {
					PasteAssets();
				}
				ImGui::Separator();
			}

			if (EditorMenu::Item(Icon::Scene, "Entity Prefab")) {
				CreateEntityPrefab(m_CurrentDirectory);
			}

			if (EditorMenu::Begin(Icon::Add, "Create")) {

				ImGui::Separator();

				if (EditorMenu::Begin(Icon::Sprite, "Texture")) {
					if (ImGui::MenuItem("Square")) {
						CreateDefaultTexture(m_CurrentDirectory, "Square.png", "Square");
					}
					if (ImGui::MenuItem("Circle")) {
						CreateDefaultTexture(m_CurrentDirectory, "circle.png", "Circle");
					}
					if (ImGui::MenuItem("Capsule")) {
						CreateDefaultTexture(m_CurrentDirectory, "Capsule.png", "Capsule");
					}
					if (ImGui::MenuItem("9-Sliced")) {
						CreateDefaultTexture(m_CurrentDirectory, "9Sliced.png", "9Sliced");
					}
					if (ImGui::MenuItem("Hexagon (Flat-Top)")) {
						CreateDefaultTexture(m_CurrentDirectory, "HexagonFlatTop.png", "HexagonFlatTop");
					}
					if (ImGui::MenuItem("Hexagon (Pointed-Top)")) {
						CreateDefaultTexture(m_CurrentDirectory, "HexagonPointedTop.png", "HexagonPointedTop");
					}
					if (ImGui::MenuItem("Isometric Diamond")) {
						CreateDefaultTexture(m_CurrentDirectory, "IsometricDiamond.png", "IsometricDiamond");
					}
					if (ImGui::MenuItem("Pixel")) {
						CreateDefaultTexture(m_CurrentDirectory, "Pixel.png", "Pixel");
					}
					if (ImGui::MenuItem("Invisible")) {
						CreateDefaultTexture(m_CurrentDirectory, "Invisible.png", "Invisible");
					}
					ImGui::EndMenu();
				}

				if (EditorMenu::Begin(Icon::Script, "C# Scripting")) {
					if (EditorMenu::Item(Icon::Script, "EntityScript")) {
						CreateScript(m_CurrentDirectory);
					}
					if (EditorMenu::Item(Icon::Script, "SceneSystem")) {
						CreateSceneSystem(m_CurrentDirectory);
					}
					if (EditorMenu::Item(Icon::Script, "GlobalSystem")) {
						CreateGlobalSystem(m_CurrentDirectory);
					}
					if (EditorMenu::Item(Icon::Script, "Empty C# Script")) {
						CreateEmptyScript(m_CurrentDirectory);
					}
					ImGui::Separator();
					if (EditorMenu::Item(Icon::Script, "Component")) {
						CreateManagedCSharpComponent(m_CurrentDirectory);
					}
					if (EditorMenu::Item(Icon::Script, "Native Component")) {
						CreateNativeCSharpComponent(m_CurrentDirectory);
					}
					ImGui::Separator();
					if (EditorMenu::Item(Icon::Data, "DataAsset")) {
						CreateDataAssetScript(m_CurrentDirectory);
					}
					ImGui::EndMenu();
				}

				if (EditorMenu::Begin(Icon::Script, "C++ Scripting")) {
					const bool canCreateCppScript = ProjectManager::GetCurrentProject() != nullptr;
					if (EditorMenu::Item(Icon::Script, "C++ Script", nullptr, false, canCreateCppScript)) {
						CreateCppScript(m_CurrentDirectory);
					}
					if (!canCreateCppScript && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
						ImGui::SetTooltip("Open or create a project to add C++ scripts.");
					}
					ImGui::EndMenu();
				}

				if (EditorMenu::Item(Icon::Script, "Bolt Script", nullptr, false, ProjectManager::GetCurrentProject() != nullptr)) {
					CreateBoltScript(m_CurrentDirectory);
				}

				if (EditorMenu::Begin(Icon::Data, "Data Assets")) {
					std::vector<char> typesBuf(8192);
					int required = ScriptEngine::GetDataAssetTypes(typesBuf.data(), static_cast<int>(typesBuf.size()));
					if (required > static_cast<int>(typesBuf.size())) {
						typesBuf.resize(static_cast<std::size_t>(required));
						ScriptEngine::GetDataAssetTypes(typesBuf.data(), static_cast<int>(typesBuf.size()));
					}

					// Build a submenu tree from each type's '/'-separated menu path
					// ("Gameplay/Item Data" -> Gameplay > Item Data). The last segment
					// is the creatable leaf; the earlier ones are nested submenus.
					struct MenuNode {
						std::string Label;
						std::string TypeName; // non-empty => creatable leaf
						std::vector<MenuNode> Children;
						MenuNode* FindGroup(const std::string& label) {
							for (auto& c : Children)
								if (c.TypeName.empty() && c.Label == label) return &c;
							return nullptr;
						}
					};

					MenuNode root;
					bool anyType = false;
					Json::Value parsed;
					if (typesBuf[0] != '\0' && Json::TryParse(typesBuf.data(), parsed) && parsed.IsArray()) {
						for (const Json::Value& item : parsed.GetArray()) {
							if (!item.IsObject()) continue;
							const Json::Value* typeMember = item.FindMember("type");
							if (!typeMember) continue;
							const std::string typeName = typeMember->AsStringOr();
							if (typeName.empty()) continue;
							const Json::Value* menuMember = item.FindMember("menu");
							std::string menuPath = menuMember ? menuMember->AsStringOr(typeName) : typeName;
							if (menuPath.empty()) menuPath = typeName;
							anyType = true;

							MenuNode* cur = &root;
							std::size_t start = 0;
							while (true) {
								const std::size_t slash = menuPath.find('/', start);
								std::string seg = (slash == std::string::npos)
									? menuPath.substr(start)
									: menuPath.substr(start, slash - start);
								if (slash == std::string::npos) {
									if (seg.empty()) seg = typeName;
									cur->Children.push_back(MenuNode{ seg, typeName, {} });
									break;
								}
								if (!seg.empty()) {
									MenuNode* group = cur->FindGroup(seg);
									if (!group) {
										cur->Children.push_back(MenuNode{ seg, std::string(), {} });
										group = &cur->Children.back();
									}
									cur = group;
								}
								start = slash + 1;
							}
						}
					}

					if (!anyType) {
						ImGui::TextDisabled("No DataAsset types");
					}
					else {
						std::function<void(const MenuNode&)> renderNode = [&](const MenuNode& node) {
							for (const MenuNode& child : node.Children) {
								if (!child.TypeName.empty()) {
									if (EditorMenu::Item(Icon::Data, child.Label.c_str()))
										CreateDataAsset(m_CurrentDirectory, child.TypeName);
								}
								else if (EditorMenu::Begin(Icon::Project, child.Label.c_str())) {
									renderNode(child);
									ImGui::EndMenu();
								}
							}
							};
						renderNode(root);
					}
					ImGui::EndMenu();
				}
				if (EngineModules::IsActive(EngineModule::ThreeD) && EditorMenu::Item(Icon::Material, "Material")) {
					CreateMaterial(m_CurrentDirectory);
				}
				if (EditorMenu::Begin(Icon::Animation, "Animation")) {
					if (EditorMenu::Item(Icon::Animation, "Animation Clip")) CreateAnimationAsset(m_CurrentDirectory, false);
					if (EditorMenu::Item(Icon::Animator, "Animator Controller")) CreateAnimationAsset(m_CurrentDirectory, true);
					ImGui::EndMenu();
				}
				// Assign a material using one of these to a Camera 3D's Skybox Material.
				if (EngineModules::IsActive(EngineModule::ThreeD) && EditorMenu::Begin(Icon::Script, "Skybox Shader")) {
					for (const ShaderTemplate& skyTemplate : GetSkyboxShaderTemplates()) {
						if (ImGui::MenuItem(skyTemplate.MenuName)) {
							CreateFile(m_CurrentDirectory, skyTemplate.FileName, ".wgsl", skyTemplate.Source);
						}
					}
					ImGui::EndMenu();
				}
				if (EditorMenu::Begin(Icon::File, "File")) {
					if (EditorMenu::Item(Icon::File, "Empty File")) {
						CreateFile(m_CurrentDirectory, "NewFile", "", "");
					}
					if (EditorMenu::Item(Icon::TextFile, "Text File")) {
						CreateFile(m_CurrentDirectory, "NewFile", ".txt", "");
					}
					if (EditorMenu::Item(Icon::BinaryFile, "Binary File")) {
						CreateFile(m_CurrentDirectory, "NewFile", ".bin", "");
					}
					if (EditorMenu::Item(Icon::Json, "JSON File")) {
						CreateFile(m_CurrentDirectory, "NewFile", ".json", "{\n}\n");
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}

			if (EditorMenu::Item(Icon::Project, Platform::RevealInFileManagerLabel())) {
				RevealAssetInExplorer(m_CurrentDirectory);
			}


			ImGui::Separator();
			if (EditorMenu::Item(Icon::SceneFile, "New Scene")) {
				CreateScene(m_CurrentDirectory);
			}
			if (EditorMenu::Item(Icon::Project, "New Folder", "Shift + F")) {
				CreateFolder(m_CurrentDirectory);
			}
			ImGui::EndPopup();
		}
	}

	void AssetBrowser::RenderItemContextMenu(const DirectoryEntry& entry, int index) {
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
			ImGui::OpenPopup("##ItemCtx");
			m_ItemRightClicked = true;
			if (!IsPathSelected(entry.Path)) {
				SetSingleSelection(entry.Path, index);
			}
		}

		if (ImGui::BeginPopup("##ItemCtx")) {
			if (!IsPathSelected(entry.Path)) {
				SetSingleSelection(entry.Path, index);
			}

			if (ImGui::MenuItem("Copy", EditorShortcuts::k_CopyLabel)) {
				CopySelectedAssets(false);
			}
						if (ImGui::MenuItem("Copy Path")) {
				CopyPathToClipboard(entry.Path);
			}

			if (ImGui::MenuItem("Open", EditorShortcuts::k_OpenAssetLabel)) {
				OpenAssetPath(entry.Path);
				ImGui::EndPopup();
				return;
			}
			if (ImGui::MenuItem(Platform::RevealInFileManagerLabel())) {
				RevealAssetInExplorer(entry.Path);
			}
			ImGui::Separator();

			if (ImGui::MenuItem("Delete", EditorShortcuts::k_DeleteLabel)) {
				RequestDeleteSelectedAssets();
				ImGui::EndPopup();
				return;
			}
			if (ImGui::MenuItem("Rename", EditorShortcuts::k_RenameLabel, false, GetSelectedPaths().size() == 1)) {
				BeginRename(entry.Path, entry.Name);
			}
			if (!entry.IsDirectory && std::filesystem::path(entry.Path).extension() == ".prefab"
				&& ImGui::MenuItem("Create Variant")) CreatePrefabVariant(entry.Path);
			if (ImGui::MenuItem("Duplicate", EditorShortcuts::k_DuplicateLabel)) {
				DuplicateSelectedAssets();
			}

			ImGui::EndPopup();
		}
	}
}
