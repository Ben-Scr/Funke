#include "pch.hpp"
#include "Graphics/Backend/WebGPUSurfaceSource.hpp"

#include "Core/Log.hpp"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_properties.h>
#if defined(IDX_PLATFORM_MACOS) || defined(IDX_PLATFORM_IOS)
	#include <SDL3/SDL_metal.h>
#endif

#include <cstring>
#include <string>
#include <unordered_map>

namespace Index::WebGPUSurfaceSource {

	namespace {
#if defined(IDX_PLATFORM_MACOS) || defined(IDX_PLATFORM_IOS)
		std::unordered_map<SDL_Window*, SDL_MetalView>& MetalViews() {
			static std::unordered_map<SDL_Window*, SDL_MetalView> s_Views;
			return s_Views;
		}
#endif

		wgpu::Surface CreateFromChain(const wgpu::Instance& instance, const wgpu::ChainedStruct* source) {
			wgpu::SurfaceDescriptor desc{};
			desc.nextInChain = source;
			return instance.CreateSurface(&desc);
		}
	}

	wgpu::Surface Create(const wgpu::Instance& instance, SDL_Window* window) {
		if (!instance || !window) return nullptr;
		const SDL_PropertiesID props = SDL_GetWindowProperties(window);

#if defined(IDX_PLATFORM_WINDOWS)
		wgpu::SurfaceSourceWindowsHWND source{};
		source.hwnd      = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
		source.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
		if (!source.hwnd) {
			IDX_CORE_ERROR_TAG("WebGPUSurface", "SDL window has no HWND");
			return nullptr;
		}
		return CreateFromChain(instance, &source);

#elif defined(IDX_PLATFORM_MACOS) || defined(IDX_PLATFORM_IOS)
		SDL_MetalView view = nullptr;
		auto& views = MetalViews();
		if (auto it = views.find(window); it != views.end()) {
			view = it->second;
		}
		else {
			view = SDL_Metal_CreateView(window);
			if (!view) {
				IDX_CORE_ERROR_TAG("WebGPUSurface", "SDL_Metal_CreateView failed: {}", SDL_GetError());
				return nullptr;
			}
			views.emplace(window, view);
		}
		wgpu::SurfaceSourceMetalLayer source{};
		source.layer = SDL_Metal_GetLayer(view);
		return CreateFromChain(instance, &source);

#elif defined(IDX_PLATFORM_ANDROID)
		wgpu::SurfaceSourceAndroidNativeWindow source{};
		source.window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
		if (!source.window) {
			IDX_CORE_ERROR_TAG("WebGPUSurface", "SDL window has no ANativeWindow (app paused?)");
			return nullptr;
		}
		return CreateFromChain(instance, &source);

#elif defined(IDX_PLATFORM_WEB)
		const char* canvasId = SDL_GetStringProperty(props, SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "canvas");
		std::string selector = canvasId ? canvasId : "canvas";
		if (selector.empty() || selector.front() != '#') selector.insert(selector.begin(), '#');
		wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector source{};
		source.selector = wgpu::StringView(selector.c_str(), selector.size());
		return CreateFromChain(instance, &source);

#elif defined(IDX_PLATFORM_LINUX)
		const char* driver = SDL_GetCurrentVideoDriver();
		if (driver && std::strcmp(driver, "wayland") == 0) {
			wgpu::SurfaceSourceWaylandSurface source{};
			source.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
			source.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
			if (!source.display || !source.surface) {
				IDX_CORE_ERROR_TAG("WebGPUSurface", "SDL Wayland window has no wl_display / wl_surface");
				return nullptr;
			}
			return CreateFromChain(instance, &source);
		}
		if (driver && std::strcmp(driver, "x11") == 0) {
			wgpu::SurfaceSourceXlibWindow source{};
			source.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
			source.window  = static_cast<uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
			if (!source.display || source.window == 0) {
				IDX_CORE_ERROR_TAG("WebGPUSurface", "SDL X11 window has no Display / Window");
				return nullptr;
			}
			return CreateFromChain(instance, &source);
		}
		IDX_CORE_ERROR_TAG("WebGPUSurface", "Unsupported SDL video driver '{}' (need x11 or wayland)", driver ? driver : "none");
		return nullptr;

#else
		(void)props;
		IDX_CORE_ERROR_TAG("WebGPUSurface", "No WebGPU surface source for this platform");
		return nullptr;
#endif
	}

	void Release(SDL_Window* window) {
#if defined(IDX_PLATFORM_MACOS) || defined(IDX_PLATFORM_IOS)
		auto& views = MetalViews();
		if (auto it = views.find(window); it != views.end()) {
			SDL_Metal_DestroyView(it->second);
			views.erase(it);
		}
#else
		(void)window;
#endif
	}

	void* CurrentNativeHandle(SDL_Window* window) {
#if defined(IDX_PLATFORM_ANDROID)
		return window ? SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr) : nullptr;
#else
		return window;
#endif
	}

}
