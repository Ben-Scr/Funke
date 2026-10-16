#include "Gui/ImGuiContextSetup.hpp"

#include "Core/Assert.hpp"
#include "Core/Window.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Packages/PackageImGuiBridge.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>

#include <algorithm>

namespace Funke::EditorRuntime::ImGuiContextSetup {

	namespace {
		int g_EventHookId = 0;
	}

	void PublishImGuiContextToPackages() {
		// Engine.dll has its own static-linked copy of ImGui (so it can
		// host the ImGui WebGPU backend alongside wgpu::Device). Publish
		// our context + allocators so engine.dll's ImGui state — and any
		// package that links ImGui statically (every engine_core package
		// today) — can sync to our context on every backend entry point.
		// Without this, package-side ImGui calls would crash on a null
		// GImGui. See Funke-Engine/src/Gui/ImGuiImplWebGPU.cpp
		// `SyncImGuiContextFromBridge`.
		ImGuiMemAllocFunc allocFn = nullptr;
		ImGuiMemFreeFunc  freeFn  = nullptr;
		void*             userData = nullptr;
		ImGui::GetAllocatorFunctions(&allocFn, &freeFn, &userData);
		PackageImGuiBridge::Publish(
			reinterpret_cast<void*>(ImGui::GetCurrentContext()),
			reinterpret_cast<void*>(allocFn),
			reinterpret_cast<void*>(freeFn),
			userData);
	}

	void ApplyCommonIoConfigFlags(ImGuiIO& io) {
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// Multi-viewport: undocked ImGui windows (and popups marked with
		// ImGuiViewportFlags_NoAutoMerge via a window class) spawn real OS
		// windows through imgui_impl_sdl3's platform handlers + the
		// engine's WebGPUBackend::ViewportSurface renderer handlers
		// (registered in InitBackends below). imgui_impl_wgpu doesn't
		// advertise renderer-side multi-viewport support on its own — we
		// bolt it on in ImGuiImplWebGPU. The matching BackendFlag tells
		// ImGui's per-frame UpdatePlatformWindows traversal that the
		// renderer side is ready to drive secondary viewports;
		// imgui_impl_sdl3 sets PlatformHasViewports in its own Init.
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
		// Tell ImGui that secondary viewports have OS-provided decoration,
		// so it should NOT draw its own titlebar/close-button on top of
		// the native one (the chrome our engine asserts via
		// ApplyViewportChrome). Without this, ImGui's default
		// ConfigViewportsNoDecoration=true makes it draw an ImGui titlebar
		// + X inside every detached window — producing the double-X
		// problem (one native, one ImGui-drawn). Docked windows are
		// unaffected: this flag only governs WHAT VIEWPORTS GET; docked
		// tab strips with close buttons are rendered by a separate path.
		io.ConfigViewportsNoDecoration = false;
		// Taskbar hiding is handled by the owner-window relationship
		// imgui_impl_sdl3 sets via SDL_SetWindowParent (Windows excludes
		// owned windows from the taskbar automatically). We deliberately
		// do NOT set io.ConfigViewportsNoTaskBarIcon = true here — that
		// would make every secondary viewport an SDL utility window
		// (WS_EX_TOOLWINDOW: no thick frame, the chunky "compact dialog"
		// close button instead of the normal app close button).
		// Edge-resize left at default (true). Forcing it false combined
		// with the transparent ResizeGrip colors in the theme made
		// undocked floating windows effectively non-resizable.
	}

	void InitBackends(SDL_Window* window) {
		// "Other": the window has no GL context. The WebGPU backend lives
		// inside engine.dll (Funke-Engine/src/Gui/ImGuiImplWebGPU.{hpp,cpp},
		// a thin wrapper around imgui_impl_wgpu), reachable via FUNKE_API.
		FNK_VERIFY(ImGui_ImplSDL3_InitForOther(window),
			"Failed to init SDL3 for imgui (WebGPU backend)!");
		// SDL has no callback chaining like GLFW; the engine's event pump forwards every event.
		g_EventHookId = Window::AddNativeEventHook([](const SDL_Event& event) {
			if (ImGui::GetCurrentContext() == nullptr) return;
			// A locked cursor's motion carries SDL's summed deltas, which wander across the whole window; ImGui would
			// hover and click editor panels with it. The editor pins ImGui's pointer to the game's lock point instead.
			if (event.type == SDL_EVENT_MOUSE_MOTION) {
				SDL_Window* window = SDL_GetWindowFromID(event.motion.windowID);
				if (window && SDL_GetWindowRelativeMouseMode(window)) return;
			}
			ImGui_ImplSDL3_ProcessEvent(&event);
		});
		FNK_VERIFY(ImGuiImplWebGPU::Init(),
			"Failed to init WebGPU imgui backend (device not ready?)");

		// Wire renderer-side multi-viewport handlers AFTER ImGui_ImplWGPU
		// is up. RegisterMultiViewportHandlers binds Renderer_RenderWindow
		// / SwapBuffers etc. to platform_io. imgui_impl_sdl3 registered its
		// platform-side handlers in Init when ViewportsEnable is set.
		ImGuiImplWebGPU::RegisterMultiViewportHandlers();
	}

	float CaptureDpiScale(SDL_Window* window) {
		// One-time HiDPI scale captured from the window's display.
		// Callers apply it to the UI font and to the theme via
		// ScaleAllSizes. Mid-session display moves are not handled —
		// react to SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED if that becomes
		// a real symptom. Pixel density is divided out: Retina's 2x is
		// already covered by ImGui's DisplayFramebufferScale.
		const float displayScale = SDL_GetWindowDisplayScale(window);
		const float pixelDensity = SDL_GetWindowPixelDensity(window);
		const float contentScale = pixelDensity > 0.0f ? displayScale / pixelDensity : displayScale;
		return std::max(1.0f, contentScale);
	}

	void ShutdownBackends() {
		// Secondary viewports must release their WebGPU surfaces before the renderer callback is removed.
		ImGui::DestroyPlatformWindows();
		ImGuiImplWebGPU::Shutdown();
		Window::RemoveNativeEventHook(g_EventHookId);
		g_EventHookId = 0;
		ImGui_ImplSDL3_Shutdown();
		PackageImGuiBridge::Clear();
		ImGui::DestroyContext();
	}

	void NewFrame() {
		ImGuiImplWebGPU::NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
	}

	void RenderAndPresent() {
		ImGui::Render();
		// viewId is vestigial on WebGPU (preserved in the API signature
		// for ABI stability); ImGuiImplWebGPU::RenderDrawData uses
		// whatever framebuffer RenderApi::BindFramebuffer last bound.
		ImGuiImplWebGPU::RenderDrawData(ImGui::GetDrawData(), /*viewId*/ 0xFFFFu);

		// Pump secondary OS windows when multi-viewport is enabled. No-op
		// otherwise. The engine.dll-side helper internally flushes the
		// main encoder before walking viewports so per-frame WriteBuffer
		// ordering across the shared ImGui_ImplWGPU FrameResources slot
		// is correct — see ImGuiImplWebGPU.hpp for the full rationale.
		ImGuiImplWebGPU::RenderPlatformWindowsDefault();
	}

}
