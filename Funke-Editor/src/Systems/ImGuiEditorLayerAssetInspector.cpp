#include "Systems/ImGuiEditorLayer.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Core/Platform.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/SpriteUVResolver.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/MaterialShaderInspector.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Serialization/Path.hpp"
#include <algorithm>

namespace Funke {
	namespace {
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
						"sorted back to front per object; it does not write depth. Cutout discards alpha below Alpha Cutoff in color and shadows.")),
				Properties::MakeWith<Color>("BaseColor", "Base Color",
					[current](const Entity&) { return current().BaseColor; },
					[edit](Entity&, Color v) { edit([v](Material& m) { m.BaseColor = v; }); }),
				Properties::MakeTextureRef("BaseColorTexture", "Base Color Texture",
					[current](const Entity&) { return current().BaseColorTexture; },
					[edit](Entity&, uint64_t v) { edit([v](Material& m) { m.BaseColorTexture = v; }); },
					Properties::Meta::Tooltip("Multiplied into Base Color; uses the texture sampling settings.")),
                Properties::MakeTextureRef("NormalTexture", "Normal Texture",
                    [current](const Entity&) { return current().NormalTexture; },
                    [edit](Entity&, uint64_t v) { edit([v](Material& m) { m.NormalTexture = v; }); },
                    Properties::Meta::Tooltip("Tangent-space normal map; RGB stores linear direction data.")),
                Properties::MakeTextureRef("MetallicRoughnessTexture", "Metallic / Roughness Texture",
                    [current](const Entity&) { return current().MetallicRoughnessTexture; },
                    [edit](Entity&, uint64_t v) { edit([v](Material& m) { m.MetallicRoughnessTexture = v; }); },
                    Properties::Meta::Tooltip("Linear packed map: green = roughness, blue = metallic. Multiplied by the scalar factors.")),
                Properties::MakeTextureRef("EmissiveTexture", "Emissive Texture",
                    [current](const Entity&) { return current().EmissiveTexture; },
                    [edit](Entity&, uint64_t v) { edit([v](Material& m) { m.EmissiveTexture = v; }); },
                    Properties::Meta::Tooltip("sRGB color multiplied by Emissive Color and Intensity.")),
                Properties::MakeTextureRef("OcclusionTexture", "Occlusion Texture",
                    [current](const Entity&) { return current().OcclusionTexture; },
                    [edit](Entity&, uint64_t v) { edit([v](Material& m) { m.OcclusionTexture = v; }); },
                    Properties::Meta::Tooltip("Red channel reduces indirect lighting; sampled as linear data.")),
                Properties::MakeWith<float>("NormalScale", "Normal Strength",
                    [current](const Entity&) { return current().NormalScale; },
                    [edit](Entity&, float v) { edit([v](Material& m) { m.NormalScale = v; }); },
                    Properties::Meta::Clamp(0.0, 10.0, 0.005f)),
                Properties::MakeWith<float>("OcclusionStrength", "Occlusion Strength",
                    [current](const Entity&) { return current().OcclusionStrength; },
                    [edit](Entity&, float v) { edit([v](Material& m) { m.OcclusionStrength = v; }); },
                    Properties::Meta::Clamp(0.0, 1.0, 0.005f)),
                Properties::MakeWith<float>("AlphaCutoff", "Alpha Cutoff",
                    [current](const Entity&) { return current().AlphaCutoff; },
                    [edit](Entity&, float v) { edit([v](Material& m) { m.AlphaCutoff = v; }); },
                    Properties::Meta::Clamp(0.0, 1.0, 0.005f)),
                Properties::MakeWith<bool>("DoubleSided", "Double Sided",
                    [current](const Entity&) { return current().DoubleSided; },
                    [edit](Entity&, bool v) { edit([v](Material& m) { m.DoubleSided = v; }); }),
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
		// Opening a folder clears the browser selection; keep the action's path alive.
		const std::string selectedPath = m_AssetBrowser.GetSelectedPath();
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

		// Shared actions precede every specialized inspector, including prefabs.
		const char* fileManagerLabel = Platform::OpenInFileManagerLabel();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float actionsWidth = ImGui::CalcTextSize("Open").x + ImGui::CalcTextSize(fileManagerLabel).x
			+ style.FramePadding.x * 4.0f + style.ItemSpacing.x;
		const bool actionsFit = actionsWidth <= ImGui::GetContentRegionAvail().x;
		ImGui::PushID("AssetInspectorActions");
		if (EditorUI::Button("Open", EditorUI::ButtonVariant::Primary)) {
			m_AssetBrowser.OpenAssetPath(selectedPath);
			ImGui::PopID();
			return;
		}
		EditorUI::Tooltip("Open this asset using the Asset Browser's default action.");
		if (actionsFit) ImGui::SameLine();
		if (EditorUI::Button(fileManagerLabel)) {
			if (!Platform::RevealInFileManager(Path::FromUtf8(selectedPath))) {
				FNK_WARN_TAG("Inspector", "Could not show asset '{}' in the file manager.", selectedPath);
			}
		}
		EditorUI::Tooltip("Show this asset in the system file manager.");
		ImGui::PopID();
		ImGui::Separator();

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
			}
			else {
				if (const auto asset = AnimationClipLibrary::Get(guid)) {
					ImGui::Text("%.2f s%s, %zu tracks, %zu events", asset->Length, asset->Loop ? ", looping" : "", asset->Tracks.size(), asset->Events.size());
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
		if (ext == ".md" || ext == ".markdown") {
			m_MarkdownPreview.Render(path, [this](const std::filesystem::path& imagePath) {
				MarkdownPreview::ImageInfo image;
				if (const auto* texture = GetPreviewTexture(imagePath); texture && texture->IsValid()) {
					image.Texture = texture->GetHandle();
					image.Width = texture->GetWidth();
					image.Height = texture->GetHeight();
					image.FlipY = texture->IsFlippedY();
				}
				else {
					const auto entry = m_PreviewTextureLookup.find(NormalizePreviewTexturePath(imagePath));
					image.Failed = entry != m_PreviewTextureLookup.end()
						&& m_PreviewTextureCache[entry->second].Preview->State == TexturePreview::Status::Failed;
				}
				return image;
			});
			return;
		}
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
			ImGui::Spacing();
			const Texture2D* tex = GetPreviewTexture(path);
			if (!tex) {
				const auto it = m_PreviewTextureLookup.find(NormalizePreviewTexturePath(path));
				const bool failed = it != m_PreviewTextureLookup.end()
					&& m_PreviewTextureCache[it->second].Preview->State == TexturePreview::Status::Failed;
				ImGui::TextDisabled(failed ? "Texture preview could not be loaded." : "Loading texture preview...");
				if (failed && ImGui::Button("Retry preview")) ClearPreviewTextureCache();
			}

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
			return entry.Preview->GetTexture();
		}

		PreviewTextureEntry entry;
		entry.CanonicalPath = canonicalPath;
		entry.Preview = AsyncTexturePreviews::Get().Request(canonicalPath);
		entry.LastTouchTick = ++m_PreviewTextureTick;

		const size_t index = m_PreviewTextureCache.size();
		m_PreviewTextureLookup[canonicalPath] = index;
		m_PreviewTextureCache.push_back(std::move(entry));
		TrimPreviewTextureCache();
		return m_PreviewTextureCache.back().Preview->GetTexture();
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

	void ImGuiEditorLayer::InvalidatePreviewTexture(const std::filesystem::path& path) {
		const auto found = m_PreviewTextureLookup.find(NormalizePreviewTexturePath(path));
		if (found == m_PreviewTextureLookup.end()) return;
		const size_t index = found->second;
		AsyncTexturePreviews::Get().Retire(std::move(m_PreviewTextureCache[index].Preview));
		m_PreviewTextureLookup.erase(found);
		if (index != m_PreviewTextureCache.size() - 1) {
			m_PreviewTextureCache[index] = std::move(m_PreviewTextureCache.back());
			m_PreviewTextureLookup[m_PreviewTextureCache[index].CanonicalPath] = index;
		}
		m_PreviewTextureCache.pop_back();
	}
}
