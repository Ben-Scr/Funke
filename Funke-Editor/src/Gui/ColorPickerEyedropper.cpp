#include "Gui/ColorPickerEyedropper.hpp"

#include "Gui/EditorUI.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace Funke::ColorPickerEyedropper {

	namespace {
		struct PickerState {
			ImGuiID Owner = 0;
			int LastFrame = -1;
			bool Finishing = false;
			bool Commit = false;
			bool Valid = false;
			ImVec4 Sample{ 0.0f, 0.0f, 0.0f, 1.0f };
#ifdef FNK_PLATFORM_WINDOWS
			HWND CaptureWindow = nullptr;
#endif
		};

		PickerState g_Picker;
		ImGuiID g_FrameHook = 0;
		ImGuiContext* g_Context = nullptr;

		void StopPicking() {
#ifdef FNK_PLATFORM_WINDOWS
			if (g_Picker.CaptureWindow && GetCapture() == g_Picker.CaptureWindow) {
				ReleaseCapture();
			}
			if (g_Picker.Owner) SetCursor(LoadCursor(nullptr, IDC_ARROW));
#endif
			if (g_Picker.Owner && ImGui::GetActiveID() == g_Picker.Owner) {
				ImGui::ClearActiveID();
			}
			g_Picker = {};
		}

		void BeforeNewFrame(ImGuiContext* context, ImGuiContextHook*) {
#ifdef FNK_PLATFORM_WINDOWS
			if (!g_Picker.Owner) return;
			if (g_Picker.LastFrame != context->FrameCount) {
				StopPicking();
				return;
			}

			bool accept = false;
			bool cancel = false;
			ImVec2 mousePosition = context->IO.MousePos;
			ImVec2 clickPosition = mousePosition;
			// Consume the sampling gesture before ImGui can close the popup or activate another field.
			for (int i = 0; i < context->InputEventsQueue.Size;) {
				const ImGuiInputEvent& event = context->InputEventsQueue[i];
				if (event.Type == ImGuiInputEventType_MousePos) {
					mousePosition = ImVec2(event.MousePos.PosX, event.MousePos.PosY);
				}
				if (event.Type == ImGuiInputEventType_MouseButton) {
					if (!accept && event.MouseButton.Button == ImGuiMouseButton_Left && event.MouseButton.Down) {
						accept = true;
						clickPosition = mousePosition;
					}
					cancel |= event.MouseButton.Button == ImGuiMouseButton_Right && event.MouseButton.Down;
				}
				if (event.Type == ImGuiInputEventType_Key) {
					cancel |= event.Key.Key == ImGuiKey_Escape && event.Key.Down;
				}
				if (event.Type == ImGuiInputEventType_Focus && !event.AppFocused.Focused) cancel = true;
				if (event.Type == ImGuiInputEventType_MouseButton || event.Type == ImGuiInputEventType_MouseWheel
					|| event.Type == ImGuiInputEventType_Key || event.Type == ImGuiInputEventType_Text) {
					context->InputEventsQueue.erase(context->InputEventsQueue.Data + i);
				}
				else ++i;
			}
			context->IO.ClearInputKeys();

			const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			const bool rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
			DWORD foregroundProcess = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
			cancel |= foregroundProcess != GetCurrentProcessId() || rightDown || escapeDown;

			SetCapture(g_Picker.CaptureWindow);
			if (g_Picker.Finishing) {
				if (!g_Picker.Commit && !leftDown && !rightDown && !escapeDown) StopPicking();
				return;
			}
			if (cancel) {
				g_Picker.Finishing = true;
				return;
			}

			SetCursor(LoadCursor(nullptr, IDC_CROSS));
			POINT point{};
			g_Picker.Valid = false;
			if (GetCursorPos(&point)) {
				if (accept && ImGui::IsMousePosValid(&clickPosition)) {
					point = { static_cast<LONG>(clickPosition.x), static_cast<LONG>(clickPosition.y) };
					if (!(context->IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
						ClientToScreen(g_Picker.CaptureWindow, &point);
					}
				}
				if (HDC desktop = GetDC(nullptr)) {
					const COLORREF pixel = GetPixel(desktop, point.x, point.y);
					ReleaseDC(nullptr, desktop);
					if (pixel != CLR_INVALID) {
						g_Picker.Sample = ImVec4(GetRValue(pixel) / 255.0f, GetGValue(pixel) / 255.0f,
							GetBValue(pixel) / 255.0f, 1.0f);
						g_Picker.Valid = true;
					}
				}
			}
			if ((accept || leftDown) && g_Picker.Valid) {
				g_Picker.Commit = true;
				g_Picker.Finishing = true;
			}
#else
			(void)context;
#endif
		}

		void DrawPipette(const ImVec2& origin, float size) {
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
			const float stroke = size / 15.0f;
			auto point = [&](float x, float y) { return ImVec2(origin.x + x * size, origin.y + y * size); };
			const ImVec2 tube[] = { point(0.25f, 0.76f), point(0.27f, 0.60f), point(0.57f, 0.30f),
				point(0.70f, 0.43f), point(0.40f, 0.73f) };
			draw->AddPolyline(tube, IM_ARRAYSIZE(tube), color, ImDrawFlags_Closed, stroke);
			draw->AddLine(point(0.50f, 0.27f), point(0.73f, 0.50f), color, stroke);
			draw->AddLine(point(0.62f, 0.35f), point(0.77f, 0.20f), color, stroke * 2.5f);
		}

		bool DrawFooter(float* color, ImGuiColorEditFlags flags) {
			if (ImGui::GetCurrentContext() != g_Context) return false;
			const ImGuiID id = ImGui::GetID("##Eyedropper");
			const bool active = g_Picker.Owner == id;
			const float size = ImGui::GetFrameHeight();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
#ifndef FNK_PLATFORM_WINDOWS
			ImGui::BeginDisabled();
#endif
			const auto variant = active ? EditorUI::ButtonVariant::Primary : EditorUI::ButtonVariant::Default;
			EditorUI::PushButtonColors(variant);
			const bool pressed = ImGui::Button("##Eyedropper", ImVec2(size, size));
			DrawPipette(origin, size);
			EditorUI::PopButtonColors(variant);
#ifdef FNK_PLATFORM_WINDOWS
			EditorUI::Tooltip("Pick a color from anywhere on screen. Alpha is kept.\nClick to apply; Escape or right-click to cancel.");
			if (pressed && !g_Picker.Owner) {
				HWND window = static_cast<HWND>(ImGui::GetWindowViewport()->PlatformHandleRaw);
				if (window) {
					g_Picker.Owner = id;
					g_Picker.CaptureWindow = window;
					SetCapture(window);
				}
			}
#else
			(void)pressed;
			EditorUI::Tooltip("Screen color picking is currently available on Windows.");
			ImGui::EndDisabled();
#endif
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(active ? "Click a pixel; Esc to cancel" : "Pick screen color");

			bool changed = false;
			if (g_Picker.Owner == id) {
				g_Picker.LastFrame = ImGui::GetFrameCount();
				if (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) {
					StopPicking();
					return false;
				}
				ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
				ImGui::KeepAliveID(id);
				ImGui::SetNextFrameWantCaptureMouse(true);
				ImGui::SetNextFrameWantCaptureKeyboard(true);
				if (g_Picker.Commit) {
					// Callers often pass stack arrays; commit only through this frame's picker value.
					color[0] = g_Picker.Sample.x;
					color[1] = g_Picker.Sample.y;
					color[2] = g_Picker.Sample.z;
					if (flags & ImGuiColorEditFlags_InputHSV) {
						ImGui::ColorConvertRGBtoHSV(color[0], color[1], color[2], color[0], color[1], color[2]);
					}
					g_Picker.Commit = false;
					changed = true;
				}
			}

			if (active && g_Picker.Valid) {
				ImGui::ColorButton("##ScreenSample", g_Picker.Sample,
					ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(size, size));
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("#%02X%02X%02X", static_cast<int>(g_Picker.Sample.x * 255.0f + 0.5f),
					static_cast<int>(g_Picker.Sample.y * 255.0f + 0.5f), static_cast<int>(g_Picker.Sample.z * 255.0f + 0.5f));
			}
			else if (active) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("Pixel unavailable");
			}
			else {
				ImGui::Dummy(ImVec2(0.0f, size));
			}
			return changed;
		}
	}

	void Initialize() {
		g_Picker = {};
		g_Context = ImGui::GetCurrentContext();
		ImGui::SetColorPickerFooterHook(&DrawFooter);
		ImGuiContextHook hook;
		hook.Type = ImGuiContextHookType_NewFramePre;
		hook.Callback = &BeforeNewFrame;
		g_FrameHook = ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
	}

	void Shutdown() {
		StopPicking();
		ImGui::SetColorPickerFooterHook(nullptr);
		ImGui::RemoveContextHook(ImGui::GetCurrentContext(), g_FrameHook);
		g_FrameHook = 0;
		g_Context = nullptr;
	}
}
