#pragma once

struct SDL_Window;
struct ImGuiIO;

namespace Index::EditorRuntime::ImGuiContextSetup {

	// Free helpers used by both Index-Launcher's and Index-Editor's
	// ImGuiContextLayer::OnAttach. Each captures one of the byte-identical
	// blocks the two layers shared before extraction. Kept as free
	// functions (not a base class) because the rest of each layer
	// genuinely diverges — see plan notes for the design rationale.

	// Publish the active ImGui context + allocator to engine.dll and any
	// loaded packages via PackageImGuiBridge. Must be called AFTER
	// ImGui::CreateContext.
	void PublishImGuiContextToPackages();

	// Apply the docking + multi-viewport ConfigFlags / BackendFlags both
	// binaries set. Does NOT touch IniSavingRate or IniFilename — those
	// are caller-specific.
	void ApplyCommonIoConfigFlags(ImGuiIO& io);

	// Initialize the SDL3 + WebGPU ImGui backends, route SDL events into
	// ImGui, and register the multi-viewport renderer handlers. Asserts
	// (IDX_VERIFY) on failure.
	void InitBackends(SDL_Window* window);

	// Return the UI content scale of the window's display (≥ 1.0).
	// Caller stores it for ScaleAllSizes / font sizing.
	float CaptureDpiScale(SDL_Window* window);

	// Shut down the backends in the order both binaries use: WebGPU →
	// SDL3 → PackageImGuiBridge → ImGui::DestroyContext.
	void ShutdownBackends();

	// Drive a new ImGui frame: WebGPU NewFrame → SDL3 NewFrame →
	// ImGui::NewFrame.
	void NewFrame();

	// Finalize and render the current ImGui frame, then pump secondary
	// OS windows for multi-viewport.
	void RenderAndPresent();

}
