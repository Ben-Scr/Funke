#include "Systems/ImGuiEditorLayer.hpp"
#include "Systems/ImGuiEditorLayerSettingsShared.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Collections/AspectRatio.hpp"
#include "Core/Application.hpp"
#include "Core/EngineModules.hpp"
#include "Core/Window.hpp"
#include "Graphics/RenderSettings.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/ReferencePicker.hpp"
#include <algorithm>

namespace Funke {
	using namespace EditorSettingsUI;

	namespace {
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
	}

	void ImGuiEditorLayer::RenderSettings_Display(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Display, "Window", filterLower)) {
			if (SettingsSection section{"Window", filterLower,
				std::to_string(project.BuildWidth) + " x " + std::to_string(project.BuildHeight)
					+ (project.BuildFullscreen ? " · Fullscreen" : " · Windowed")}; section) {

				ImGui::TextDisabled("Startup");
				ImGui::Separator();

				changed |= SettingsInt("Width", &project.BuildWidth);
				changed |= SettingsInt("Height", &project.BuildHeight);
				if (project.BuildWidth < 320) project.BuildWidth = 320;
				if (project.BuildHeight < 240) project.BuildHeight = 240;

				changed |= EditorUI::SettingsSwitch("Fullscreen", &project.BuildFullscreen);

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
				EditorUI::BeginSettingsRow("Fullscreen Mode");
				if (EditorUI::BeginCombo("##Value", currentLabel)) {
					for (const auto& opt : k_FullscreenModeOptions) {
						const bool selected = (opt.Mode == project.BuildFullscreenMode);
						if (ImGui::Selectable(opt.Label, selected)) {
							project.BuildFullscreenMode = opt.Mode;
							changed = true;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					EditorUI::EndCombo();
				}
				EditorUI::EndSettingsRow();
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

				changed |= EditorUI::SettingsSwitch("VSync", &project.BuildVsync);
				EditorUI::Tooltip("Enable vertical synchronization when the built application starts.");

				ImGui::Spacing();
				ImGui::TextDisabled("Runtime");
				ImGui::Separator();

				changed |= EditorUI::SettingsSwitch("Resizable", &project.BuildResizable);
				changed |= EditorUI::SettingsSwitch("Run in background", &project.BuildRunInBackground);

				ImGui::Spacing();
				ImGui::TextDisabled("Resize Limits (0 = unbounded)");
				changed |= SettingsInt("Min Width", &project.BuildMinWidth);
				changed |= SettingsInt("Min Height", &project.BuildMinHeight);
				changed |= SettingsInt("Max Width", &project.BuildMaxWidth);
				changed |= SettingsInt("Max Height", &project.BuildMaxHeight);
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
				EditorUI::BeginSettingsRow("Aspect Ratio");
				if (EditorUI::BeginCombo("##Value", k_AspectRatioPresets[aspectIndex].Label)) {
					for (int i = 0; i < static_cast<int>(k_AspectRatioPresets.size()); ++i) {
						const bool selected = (i == aspectIndex);
						if (ImGui::Selectable(k_AspectRatioPresets[i].Label, selected)) {
							project.BuildAspect = k_AspectRatioPresets[i].Label;
							changed = true;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					EditorUI::EndCombo();
				}
				EditorUI::EndSettingsRow();
				EditorUI::Tooltip(
					"Locks the shipped game to this aspect ratio.\n"
					"When the window's aspect doesn't match, the renderer\n"
					"adds black bars (letterbox/pillarbox) so the game\n"
					"always appears at the chosen aspect.\n"
					"\n"
					"Independent of the editor's Game View aspect.\n"
					"Select 'Free Aspect' to disable and fill the full window.");
			}
		}

		if (SectionVisible(SettingsCategory::Display, "Mobile", filterLower)) {
			if (SettingsSection section{"Mobile", filterLower}; section) {
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
				EditorUI::BeginSettingsRow("Orientation");
				if (EditorUI::BeginCombo("##Value", currentLabel)) {
					for (const auto& option : k_OrientationOptions) {
						const bool selected = option.Value == project.MobileOrientation;
						if (ImGui::Selectable(option.Label, selected)) {
							project.MobileOrientation = option.Value;
							changed = true;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					EditorUI::EndCombo();
				}
				EditorUI::EndSettingsRow();
				EditorUI::Tooltip(
					"Which way up the game may be held on Android and iOS.\n"
					"Left/Right name the side the top of the device points to.\n"
					"Options with more than one direction turn with the device.\n"
					"Auto uses landscape when the window is wider than tall.\n"
					"Desktop and Web builds ignore this.");
			}
		}

		if (SectionVisible(SettingsCategory::Display, "UI Scaling", filterLower)) {
			if (SettingsSection section{"UI Scaling", filterLower,
				std::to_string(project.UIReferenceWidth) + " x " + std::to_string(project.UIReferenceHeight)}; section) {
				changed |= SettingsInt("Reference Width##UIRef", &project.UIReferenceWidth);
				changed |= SettingsInt("Reference Height##UIRef", &project.UIReferenceHeight);
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
				EditorUI::BeginSettingsRow("Scale Mode##UIRef");
				if (EditorUI::BeginCombo("##Value", currentScaleLabel)) {
					for (const auto& option : k_ScaleModeOptions) {
						const bool selected = option.Value == project.UIScaling;
						if (ImGui::Selectable(option.Label, selected)) {
							project.UIScaling = option.Value;
							changed = true;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					EditorUI::EndCombo();
				}
				EditorUI::EndSettingsRow();
				EditorUI::Tooltip(
					"How the reference resolution is scaled to the screen.\n"
					"Fit: the whole reference area stays visible on every aspect ratio\n"
					"(wide phones, tall windows); extra space appears around it.\n"
					"Fill: the reference area covers the screen; edges may be cut off.\n"
					"Match Width or Height: blend set by the slider below.");

				if (project.UIScaling == UIScaleMode::MatchWidthOrHeight) {
					changed |= SettingsSliderFloat("Match Width / Height", &project.UIScaleMatch, 0.0f, 1.0f, "%.2f");
					EditorUI::Tooltip(
						"0 = scale UI by window WIDTH only.\n"
						"1 = scale UI by window HEIGHT only.\n"
						"0.5 = balanced (geometric mean).\n"
						"\n"
						"Move toward 1 if your UI hugs the top/bottom edges,\n"
						"toward 0 if it hugs the left/right edges.");
				}
			}
		}
	}

	void ImGuiEditorLayer::RenderSettings_Graphics(FunkeProject& project, bool& changed,
		const std::string& filterLower)
	{
		if (SectionVisible(SettingsCategory::Graphics, "Render Backend", filterLower)) {
		if (SettingsSection section{"Render Backend", filterLower, RenderBackendLabel(project.ActiveRenderBackend)}; section) {

			EditorUI::BeginSettingsRow("Rendering API");
			if (EditorUI::BeginCombo("##Value", RenderBackendLabel(project.ActiveRenderBackend))) {
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
				EditorUI::EndCombo();
			}
			EditorUI::EndSettingsRow();
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

		}
		}  // end SectionVisible("Render Backend")

		if (SectionVisible(SettingsCategory::Graphics, "Runtime Quality", filterLower)) {
		if (SettingsSection section{"Runtime Quality", filterLower}; section) {
			bool graphicsChanged = false;
			graphicsChanged |= SettingsSliderFloat(
				"Render Scale", &project.Graphics.RenderScale,
				GraphicsSettings::MinRenderScale, GraphicsSettings::MaxRenderScale, "%.2f",
				ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("Resolution used for the 2D scene; UI and gizmos stay at native resolution.");

			const char* upscaleLabels[] = { "Point", "Linear" };
			int upscale = static_cast<int>(project.Graphics.UpscaleFilter);
			if (SettingsCombo("Upscale Filter", &upscale, upscaleLabels, IM_ARRAYSIZE(upscaleLabels))) {
				project.Graphics.UpscaleFilter = static_cast<UpscaleFilterMode>(upscale);
				graphicsChanged = true;
			}

			graphicsChanged |= EditorUI::SettingsSwitch(
				"Enable Post-Processing", &project.Graphics.PostProcessingEnabled);

			const char* postQualityLabels[] = { "Low", "Medium", "High" };
			int postQuality = static_cast<int>(project.Graphics.PostProcessingQuality);
			if (SettingsCombo("Post-Processing Quality", &postQuality,
				postQualityLabels, IM_ARRAYSIZE(postQualityLabels))) {
				project.Graphics.PostProcessingQuality =
					static_cast<PostProcessingQualityLevel>(postQuality);
				graphicsChanged = true;
			}

			const char* particleQualityLabels[] = { "Off", "Low", "Medium", "High" };
			int particleQuality = static_cast<int>(project.Graphics.ParticleQuality);
			if (SettingsCombo("Particle Quality", &particleQuality,
				particleQualityLabels, IM_ARRAYSIZE(particleQualityLabels))) {
				project.Graphics.ParticleQuality = static_cast<ParticleQualityLevel>(particleQuality);
				graphicsChanged = true;
			}

			const char* textureQualityLabels[] = { "Full", "Half", "Quarter" };
			int textureQuality = static_cast<int>(project.Graphics.TextureQuality);
			if (SettingsCombo("Texture Quality", &textureQuality,
				textureQualityLabels, IM_ARRAYSIZE(textureQualityLabels))) {
				project.Graphics.TextureQuality = static_cast<TextureQualityLevel>(textureQuality);
				graphicsChanged = true;
			}
			EditorUI::Tooltip("Half and Quarter skip the top mip levels of scene textures.\n"
				"Point-filtered textures (pixel art) and UI always stay at full resolution.");

			const char* anisotropyLabels[] = { "Off", "2x", "4x", "8x", "16x" };
			int anisotropy = static_cast<int>(project.Graphics.AnisotropicFiltering);
			if (SettingsCombo("Anisotropic Filtering", &anisotropy,
				anisotropyLabels, IM_ARRAYSIZE(anisotropyLabels))) {
				project.Graphics.AnisotropicFiltering = static_cast<AnisotropicFilteringLevel>(anisotropy);
				graphicsChanged = true;
			}
			EditorUI::Tooltip("Keeps textures sharp on surfaces seen at a grazing angle.\n"
				"Applies to textures whose filter is Trilinear or Anisotropic.");

			const char* antiAliasingLabels[] = { "Off", "FXAA", "MSAA 4x" };
			int antiAliasing = static_cast<int>(project.Graphics.AntiAliasing);
			if (SettingsCombo("Anti-Aliasing", &antiAliasing,
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
			if (SettingsCombo("Anti-Aliasing Quality", &antiAliasingQuality,
				antiAliasingQualityLabels, IM_ARRAYSIZE(antiAliasingQualityLabels))) {
				project.Graphics.AntiAliasingQuality = static_cast<AntiAliasingQualityLevel>(antiAliasingQuality);
				graphicsChanged = true;
			}
			ImGui::EndDisabled();

			if (graphicsChanged) {
				changed = true;
				RenderSettings::Apply(project.Graphics);
			}
		}
		}  // end SectionVisible("Runtime Quality")

		if (EngineModules::IsActive(EngineModule::ThreeD) && SectionVisible(SettingsCategory::Graphics, "3D Rendering", filterLower)) {
		if (SettingsSection section{"3D Rendering", filterLower}; section) {
			bool graphicsChanged = false;

			const char* shadowLabels[] = { "Off", "Low (1024)", "Medium (2048)", "High (4096)" };
			int shadowQuality = static_cast<int>(project.Graphics.ShadowQuality);
			if (SettingsCombo("Shadow Quality", &shadowQuality, shadowLabels, IM_ARRAYSIZE(shadowLabels))) {
				project.Graphics.ShadowQuality = static_cast<ShadowQualityLevel>(shadowQuality);
				graphicsChanged = true;
			}
			EditorUI::Tooltip("Shadow map resolution of the main Directional Light.");

			graphicsChanged |= SettingsDragFloat("Shadow Distance", &project.Graphics.ShadowDistance, 0.5f,
				GraphicsSettings::MinShadowDistance, GraphicsSettings::MaxShadowDistance, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("World units from the camera that receive directional shadows; smaller is sharper.");

			graphicsChanged |= SettingsSliderInt("Max Lights", &project.Graphics.MaxLights,
				GraphicsSettings::MinMaxLights, GraphicsSettings::MaxMaxLights, "%d", ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("Lights per frame: directional lights first, then the point and spot lights nearest the camera.");

			graphicsChanged |= SettingsDragFloat("Exposure", &project.Graphics.Exposure, 0.01f,
				GraphicsSettings::MinExposure, GraphicsSettings::MaxExposure, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			const char* toneMappingLabels[] = { "None", "Reinhard", "ACES" };
			int toneMapping = static_cast<int>(project.Graphics.ToneMapping);
			if (SettingsCombo("Tone Mapping", &toneMapping, toneMappingLabels, IM_ARRAYSIZE(toneMappingLabels))) {
				project.Graphics.ToneMapping = static_cast<ToneMappingMode>(toneMapping);
				graphicsChanged = true;
			}

			if (graphicsChanged) {
				changed = true;
				RenderSettings::Apply(project.Graphics);
			}
		}
		}  // end SectionVisible("3D Rendering")

		if (SectionVisible(SettingsCategory::Graphics, "Animation", filterLower)) {
		if (SettingsSection section{"Animation", filterLower,
			project.Graphics.AnimationDistanceRateEnabled ? "Distance rate enabled" : "Every frame"}; section) {
			bool graphicsChanged = EditorUI::SettingsSwitch("Distance Rate", &project.Graphics.AnimationDistanceRateEnabled);
			EditorUI::Tooltip("Animators and Skinned Mesh Renderers whose Distance Rate is Project Setting update less often far "
				"from the game camera; the skipped time is added to their next update.");
			ImGui::BeginDisabled(!project.Graphics.AnimationDistanceRateEnabled);
			graphicsChanged |= SettingsDragFloat("Half Rate From", &project.Graphics.AnimationHalfRateDistance, 0.5f,
				0.0f, GraphicsSettings::MaxAnimationRateDistance, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("World units from the camera from which animations update every 2nd frame (30 of 60 fps).");
			graphicsChanged |= SettingsDragFloat("Quarter Rate From", &project.Graphics.AnimationQuarterRateDistance, 0.5f,
				0.0f, GraphicsSettings::MaxAnimationRateDistance, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			EditorUI::Tooltip("World units from the camera from which animations update every 4th frame.");
			ImGui::EndDisabled();
			if (graphicsChanged) {
				changed = true;
				RenderSettings::Apply(project.Graphics);
			}
		}
		}  // end SectionVisible("Animation")

		if (SectionVisible(SettingsCategory::Graphics, "Default Font", filterLower)) {
		if (SettingsSection section{"Default Font", filterLower}; section) {
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
			if (EditorUI::IconButton("##ProjectDefaultFontReset", "reset", "Restore the default project font.", false, false, 0.0f, "Reset")) {
				project.DefaultFontAssetId = k_DefaultFontAssetId;
				changed = true;
			}

		}
		}  // end SectionVisible("Default Font")

        if (SectionVisible(SettingsCategory::Graphics, "Font Fallbacks", filterLower)) {
        if (SettingsSection section{"Font Fallbacks", filterLower}; section) {
            constexpr const char* picker = "ProjectSettings.FontFallback";
            bool fallbackChanged = false;
            if (auto pending = ReferencePicker::ConsumeSelection(picker); pending) {
                const auto id = ParsePickerAssetId(*pending);
                if (id && project.FontFallbackAssets.size() < 16
                    && std::find(project.FontFallbackAssets.begin(), project.FontFallbackAssets.end(), id) == project.FontFallbackAssets.end()) {
                    project.FontFallbackAssets.push_back(id); fallbackChanged = true;
                }
            }
            ImGui::TextWrapped("Missing glyphs use these fonts in order. Applies to text rendering, layout and input fields.");
            for (size_t i = 0; i < project.FontFallbackAssets.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                bool missing = false;
                const auto name = ReferencePicker::ResolveAssetDisplay(project.FontFallbackAssets[i], AssetKind::Font, missing, nullptr);
                ImGui::Text("%zu. %s", i + 1, name.c_str()); ImGui::SameLine();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::SmallButton("Up")) { std::swap(project.FontFallbackAssets[i], project.FontFallbackAssets[i - 1]); fallbackChanged = true; }
                ImGui::EndDisabled(); ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) { project.FontFallbackAssets.erase(project.FontFallbackAssets.begin() + i); fallbackChanged = true; ImGui::PopID(); break; }
                ImGui::PopID();
            }
            ImGui::BeginDisabled(project.FontFallbackAssets.size() >= 16);
            if (EditorUI::Button("Add fallback font")) ReferencePicker::OpenForFieldKey(picker, "Select Fallback Font",
                ReferencePicker::CollectAssetsByKind(AssetKind::Font), ReferencePicker::Style::Plain);
            ImGui::EndDisabled();
            if (fallbackChanged) { FontManager::SetFallbackFonts(project.FontFallbackAssets); changed = true; }
        }
        }

		if (SectionVisible(SettingsCategory::Graphics, "Font Baking", filterLower)) {
		if (SettingsSection section{"Font Baking", filterLower}; section) {

			// Bake-step: 0 = adaptive ladder, N = snap every N px. Applied live so
			// the change takes effect without a restart.
			int bakeStep = project.FontBakeStep;
			if (SettingsSliderInt("Bake size step", &bakeStep, 0, 32,
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
			if (SettingsSliderInt("Atlas memory budget", &budgetMB, 0, 512,
				budgetMB == 0 ? "Unlimited" : "%d MB", ImGuiSliderFlags_AlwaysClamp)) {
				project.FontAtlasBudgetMB = budgetMB;
				FontManager::SetAtlasMemoryBudgetMB(budgetMB);
				changed = true;
			}
			EditorUI::Tooltip(
				"Cap on resident font-atlas memory. When exceeded, the\n"
				"least-recently-used baked sizes are evicted (and re-baked on demand).\n"
				"0 = unlimited (atlases accumulate until the scene reloads).");

		}
		}  // end SectionVisible("Font Baking")
	}
}
