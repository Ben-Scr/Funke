#include "Systems/ImGuiEditorLayer.hpp"
#include "Systems/ImGuiEditorLayerSettingsShared.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Graphics/ImageData.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/SplashAssetResolve.hpp"
#include "Serialization/Path.hpp"
#include <stb_image_write.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <span>

namespace Funke {
	using namespace EditorSettingsUI;

	namespace {
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
	}

	void ImGuiEditorLayer::RenderSettings_Branding(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Branding, "Splash Screen", filterLower)) {
		if (SettingsSection section{"Splash Screen", filterLower, project.SplashScreen.Enabled ? "Enabled" : "Disabled"}; section) {
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

			changed |= EditorUI::SettingsSwitch("Enabled##Splash", &project.SplashScreen.Enabled);
			EditorUI::Tooltip("When off, the runtime loads the startup scene immediately with no splash.");

			if (project.SplashScreen.Enabled) {
				ImGui::Spacing();
				changed |= SettingsSliderFloat("Duration (s)##Splash",
					&project.SplashScreen.DurationSeconds, 0.5f, 10.0f, "%.2f");
				changed |= SettingsSliderFloat("Fade In (s)##Splash",
					&project.SplashScreen.FadeInSeconds, 0.0f, 3.0f, "%.2f");
				changed |= SettingsSliderFloat("Fade Out (s)##Splash",
					&project.SplashScreen.FadeOutSeconds, 0.0f, 3.0f, "%.2f");

				ImGui::Spacing();
				float bg[3] = {
					project.SplashScreen.BackgroundR,
					project.SplashScreen.BackgroundG,
					project.SplashScreen.BackgroundB,
				};
				if (SettingsColor("Background##Splash", bg)) {
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
				if (SettingsColor("Font Color##Splash", fontColor)) {
					project.SplashScreen.FontColorR = fontColor[0];
					project.SplashScreen.FontColorG = fontColor[1];
					project.SplashScreen.FontColorB = fontColor[2];
					changed = true;
				}
				if (SettingsDragFloat("Font Size##Splash", &project.SplashScreen.FontSize,
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
		}
		}  // end SectionVisible("Splash Screen")

		if (SectionVisible(SettingsCategory::Branding, "App Icon", filterLower)) {
		if (SettingsSection section{"App Icon", filterLower, project.AppIconPath.empty() ? "Funke default" : "Custom icon"}; section) {

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

		}
		}  // end SectionVisible("App Icon")

		if (SectionVisible(SettingsCategory::Branding, "Cursors", filterLower)) {
		if (SettingsSection section{"Cursors", filterLower}; section) {

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

		}
		}  // end SectionVisible("Cursors")
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
}
