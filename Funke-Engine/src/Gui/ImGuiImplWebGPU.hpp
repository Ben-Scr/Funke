#pragma once

#include "Core/Export.hpp"

// Dear ImGui WebGPU backend (Dawn). Owned by engine.dll to share the same wgpu::Device as WebGPUApi.cpp; PackageImGuiBridge syncs the consumer ImGuiContext on each call.

struct ImDrawData;

namespace Funke::ImGuiImplWebGPU {

	FUNKE_API bool Init();

	// Per-frame setup. Forwards to ImGui_ImplWGPU_NewFrame, which inspects
	// the font atlas for changes and re-uploads if needed. Cheap when
	// nothing changed.
	FUNKE_API void NewFrame();

	// viewId is ignored (target is determined by the last RenderApi::BindFramebuffer). Opens a wgpu::RenderPassEncoder with LoadOp::Load to preserve the prior clear.
	FUNKE_API void RenderDrawData(ImDrawData* drawData, unsigned short viewId);

	// Releases ImGui's wgpu resources (pipeline, bind groups, font atlas
	// texture). Runs before ImGui::DestroyContext.
	FUNKE_API void Shutdown();

	// Wire ImGui secondary viewport renderer handlers (Renderer_CreateWindow/Destroy/Resize/Render/SwapBuffers). Idempotent; call after Init().
	FUNKE_API void RegisterMultiViewportHandlers();

	FUNKE_API void UnregisterMultiViewportHandlers();

	// Configures the next ImGui window as a native OS dialog: NoAutoMerge (own viewport), clears NoDecoration (OS titlebar), clears TopMost (owner-window relationship handles Z-order), NoTaskBarIcon.
	// MUST call immediately before BeginPopupModal/Begin. Overrides ImGui's hardcoded NoDecoration+TopMost on popup viewports.
	FUNKE_API void SetNextWindowAsNativeDialog();

	// MUST be called after the main RenderDrawData: flushes the frame encoder first so the main window WriteBuffer uploads land before per-viewport renders consume the same FrameResources slot.
	FUNKE_API void RenderPlatformWindowsDefault();

}  // namespace Funke::ImGuiImplWebGPU
