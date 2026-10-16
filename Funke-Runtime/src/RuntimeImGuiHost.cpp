#include "RuntimeImGuiHost.hpp"

#include "Core/Window.hpp"
#include "Gui/ImGuiFonts.hpp"
#include "Packages/PackageImGuiBridge.hpp"

#include <algorithm>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
// Must use engine.dll's WebGPU backend (the window has no GL context).
#include "Gui/ImGuiImplWebGPU.hpp"

namespace Funke {

	namespace {
		int  s_RefCount        = 0; // Acquire/Release nesting depth
		bool s_Initialized     = false;
		int  s_FrameOpenCount  = 0; // BeginFrame/EndFrame nesting in this frame
		int  s_EventHookId     = 0;
	}

	bool RuntimeImGuiHost::Acquire(Window* window) {
		if (s_Initialized) {
			++s_RefCount;
			return true;
		}
		if (!window) return false;
		SDL_Window* sdlWindow = window->GetNativeWindow();
		if (!sdlWindow) return false;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		// Publish context+allocators so engine.dll's static ImGui copy syncs to our context on every backend entry point.
		{
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

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		const float pixelDensity = SDL_GetWindowPixelDensity(sdlWindow);
		const float contentScale = SDL_GetWindowDisplayScale(sdlWindow) / (pixelDensity > 0.0f ? pixelDensity : 1.0f);
		LoadFunkeImGuiFont(io, std::max(1.0f, contentScale));

		if (!ImGui_ImplSDL3_InitForOther(sdlWindow)) {
			ImGui::DestroyContext();
			return false;
		}
		if (!ImGuiImplWebGPU::Init()) {
			ImGui_ImplSDL3_Shutdown();
			ImGui::DestroyContext();
			return false;
		}
		s_EventHookId = Window::AddNativeEventHook([](const SDL_Event& event) {
			if (ImGui::GetCurrentContext() != nullptr) {
				ImGui_ImplSDL3_ProcessEvent(&event);
			}
		});

		s_Initialized = true;
		++s_RefCount;
		return true;
	}

	void RuntimeImGuiHost::Release() {
		if (s_RefCount == 0) return;
		--s_RefCount;
		if (s_RefCount > 0) return;
		if (!s_Initialized) return;

		ImGuiImplWebGPU::Shutdown();
		Window::RemoveNativeEventHook(s_EventHookId);
		s_EventHookId = 0;
		ImGui_ImplSDL3_Shutdown();
		PackageImGuiBridge::Clear();
		ImGui::DestroyContext();
		s_Initialized = false;
		s_FrameOpenCount = 0;
	}

	bool RuntimeImGuiHost::IsInitialized() {
		return s_Initialized;
	}

	void RuntimeImGuiHost::BeginFrame() {
		if (!s_Initialized) return;
		if (s_FrameOpenCount == 0) {
			ImGuiImplWebGPU::NewFrame();
			ImGui_ImplSDL3_NewFrame();
			ImGui::NewFrame();
		}
		++s_FrameOpenCount;
	}

	void RuntimeImGuiHost::EndFrame() {
		if (!s_Initialized) return;
		if (s_FrameOpenCount == 0) return;
		--s_FrameOpenCount;
		if (s_FrameOpenCount == 0) {
			ImGui::Render();
			ImGuiImplWebGPU::RenderDrawData(ImGui::GetDrawData(), /*viewId*/ 0xFFFFu);
		}
	}

} // namespace Funke
