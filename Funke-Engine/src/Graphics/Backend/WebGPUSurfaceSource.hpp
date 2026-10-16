#pragma once

#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

// Builds the platform-specific WGPUSurfaceSource* chain for an SDL window:
// HWND (Windows), Xlib / Wayland (Linux), CAMetalLayer (macOS / iOS),
// ANativeWindow (Android) and the canvas selector (Web).
namespace Funke::WebGPUSurfaceSource {

	// Null on failure (logged).
	wgpu::Surface Create(const wgpu::Instance& instance, SDL_Window* window);

	// Frees per-window helpers Create made (the Metal view on Apple). MUST run before the window is destroyed.
	void Release(SDL_Window* window);

	// The OS object a surface is built on when it can be swapped under the window: the ANativeWindow on
	// Android (null while paused). Elsewhere the window itself, which never changes.
	void* CurrentNativeHandle(SDL_Window* window);

}
