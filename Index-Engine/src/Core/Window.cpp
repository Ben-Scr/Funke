#include "pch.hpp"
#include "Window.hpp"
#include "Application.hpp"
#include "Engine.hpp"
#include "Core/Platform.hpp"
#include "Core/Platform/SDLInputMapping.hpp"
#include "Core/Platform/TitlebarRegions.hpp"
#include "Core/Platform/Win32Titlebar.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/RenderApi.hpp"
#include "Events/WindowEvents.hpp"
#include "Events/KeyEvents.hpp"
#include "Events/MouseEvents.hpp"
#include "Scripting/ScriptEngine.hpp"
#include <Utils/StringHelper.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace Index {
	Window* Window::s_ActiveWindow = nullptr;
	bool Window::s_IsVsync = true;
	std::unique_ptr<Viewport> Window::s_MainViewport = nullptr;
	Window::UIRegion Window::s_UIRegion{};
	Vec2 Window::s_WindowToFramebufferScale{ 1.0f, 1.0f };
	bool Window::s_IsInitialized = false;
	uint32_t Window::s_NativeSurfaceGeneration = 0;

	namespace {
		// SDL_HINT_ORIENTATIONS value. SDL's LandscapeLeft means "top points left" on Android but follows
		// UIKit (top points right) on iOS, so iOS swaps the names. Auto is resolved here from the requested
		// window size: a fullscreen window takes the display's size, and SDL would otherwise pick the
		// orientation from that (portrait on phones) and turn a landscape game sideways.
		[[maybe_unused]] const char* OrientationHint(ScreenOrientation orientation, int width, int height) {
			if (orientation == ScreenOrientation::Auto) {
				orientation = width > height ? ScreenOrientation::Landscape : ScreenOrientation::PortraitAny;
			}
#if defined(IDX_PLATFORM_IOS)
			constexpr const char* k_TopLeft = "LandscapeRight";
			constexpr const char* k_TopRight = "LandscapeLeft";
			constexpr const char* k_BothLandscape = "LandscapeRight LandscapeLeft";
			constexpr const char* k_AllButUpsideDown = "LandscapeRight LandscapeLeft Portrait";
			constexpr const char* k_All = "LandscapeRight LandscapeLeft Portrait PortraitUpsideDown";
#else
			constexpr const char* k_TopLeft = "LandscapeLeft";
			constexpr const char* k_TopRight = "LandscapeRight";
			constexpr const char* k_BothLandscape = "LandscapeLeft LandscapeRight";
			constexpr const char* k_AllButUpsideDown = "LandscapeLeft LandscapeRight Portrait";
			constexpr const char* k_All = "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown";
#endif
			switch (orientation) {
				case ScreenOrientation::Auto:                return nullptr;  // resolved above
				case ScreenOrientation::Landscape:           return k_BothLandscape;
				case ScreenOrientation::LandscapeLeft:       return k_TopLeft;
				case ScreenOrientation::LandscapeRight:      return k_TopRight;
				case ScreenOrientation::Portrait:            return "Portrait";
				case ScreenOrientation::PortraitUpsideDown:  return "PortraitUpsideDown";
				case ScreenOrientation::PortraitAny:         return "Portrait PortraitUpsideDown";
				case ScreenOrientation::AnyExceptUpsideDown: return k_AllButUpsideDown;
				case ScreenOrientation::Any:                 return k_All;
			}
			return nullptr;
		}

		struct NativeHookEntry {
			int Id = 0;
			Window::NativeEventHook Hook;
		};
		std::vector<NativeHookEntry> g_NativeHooks;
		int g_NextNativeHookId = 1;

		constexpr bool k_TextInputByDefault = !IDX_PLATFORM_MOBILE;

		SDL_DisplayID DisplayForWindow(SDL_Window* window) {
			SDL_DisplayID display = window ? SDL_GetDisplayForWindow(window) : 0;
			return display != 0 ? display : SDL_GetPrimaryDisplay();
		}

		void AppendUtf8Codepoints(const char* text, std::vector<uint32_t>& out) {
			const char* cursor = text;
			while (cursor && *cursor) {
				const Uint32 cp = SDL_StepUTF8(&cursor, nullptr);
				if (cp == 0) break;
				if (cp != SDL_INVALID_UNICODE_CODEPOINT) out.push_back(cp);
			}
		}

		// Non-Windows custom titlebar: SDL's hit test drives OS-level move/resize from the ImGui-drawn caption.
		SDL_HitTestResult SDLCALL CustomTitlebarHitTest(SDL_Window* window, const SDL_Point* point, void*) {
			int w = 0, h = 0;
			SDL_GetWindowSize(window, &w, &h);
			const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
			using TitlebarRegions::Hit;
			switch (TitlebarRegions::HitTest(point->x, point->y, w, h, 6,
				(flags & SDL_WINDOW_RESIZABLE) != 0, (flags & SDL_WINDOW_MAXIMIZED) != 0)) {
				case Hit::Caption:           return SDL_HITTEST_DRAGGABLE;
				case Hit::ResizeTopLeft:     return SDL_HITTEST_RESIZE_TOPLEFT;
				case Hit::ResizeTop:         return SDL_HITTEST_RESIZE_TOP;
				case Hit::ResizeTopRight:    return SDL_HITTEST_RESIZE_TOPRIGHT;
				case Hit::ResizeRight:       return SDL_HITTEST_RESIZE_RIGHT;
				case Hit::ResizeBottomRight: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
				case Hit::ResizeBottom:      return SDL_HITTEST_RESIZE_BOTTOM;
				case Hit::ResizeBottomLeft:  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
				case Hit::ResizeLeft:        return SDL_HITTEST_RESIZE_LEFT;
				case Hit::Client:            break;
			}
			return SDL_HITTEST_NORMAL;
		}
	}

	Window::Window(int width, int height, const std::string& title)
	{
		Create(WindowSpecification{ width , height, title });
	}

	Window::Window(const WindowSpecification& props)
	{
		Create(props);
	}

	void Window::Initialize(const AppIdentity& identity) {
		if (s_IsInitialized) {
			IDX_CORE_WARN_TAG("Window", "SDL video is already initialized");
			return;
		}

		// Window close is a per-window request the editor may veto (unsaved changes); SDL_EVENT_QUIT stays for OS-level quits.
		SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
		// Gamepads keep working while the editor's Game View is in a detached viewport window.
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		// Windows only scales custom cursors with the display when asked; must precede cursor creation.
		SDL_SetHint(SDL_HINT_MOUSE_DPI_SCALE_CURSORS, "1");
		// Android would otherwise freeze the main loop in the background, and AudioManager::SetPlayInBackground needs
		// scripts to keep running there (a music player starting the next track). Paused frames only cost ~10 Hz of polling.
		SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
		const std::string version = identity.Version.empty() ? std::string(Engine::GetVersionLong()) : identity.Version;
		SDL_SetAppMetadata(identity.Name.c_str(), version.c_str(), identity.Identifier.c_str());

		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
			IDX_CORE_ERROR_TAG("Window", "SDL_Init failed: {}", SDL_GetError());
			return;
		}

		// Headless / CI / detached-session: there can be no display at all.
		if (SDL_GetPrimaryDisplay() == 0) {
			IDX_CORE_ERROR_TAG("Window", "No primary display available: {}", SDL_GetError());
			SDL_Quit();
			return;
		}

		SDL_AddEventWatch(&Window::OnEventWatch, nullptr);
		IDX_CORE_INFO_TAG("Window", "SDL {}.{}.{} initialized (video driver: {})",
			SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
			SDL_VERSIONNUM_MICRO(SDL_GetVersion()),
			SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
		s_IsInitialized = true;
	}

	void Window::Shutdown() {
		if (!s_IsInitialized) {
			return;
		}

		SDL_RemoveEventWatch(&Window::OnEventWatch, nullptr);
		s_MainViewport.reset();
		s_ActiveWindow = nullptr;
		ClearUIRegion();
		s_WindowToFramebufferScale = { 1.0f, 1.0f };
		// Avoid carrying the previous window's VSync state into a later recreation.
		s_IsVsync = true;
		s_IsInitialized = false;
		SDL_Quit();
	}

	void Window::SetVsync(bool enabled) {
		// Vsync is owned by the WebGPU swap-chain; see RenderApi::SetVsync.
		s_IsVsync = enabled;
		RenderApi::SetVsync(enabled);
	}

	int Window::AddNativeEventHook(NativeEventHook hook) {
		const int id = g_NextNativeHookId++;
		g_NativeHooks.push_back({ id, std::move(hook) });
		return id;
	}

	void Window::RemoveNativeEventHook(int hookId) {
		std::erase_if(g_NativeHooks, [hookId](const NativeHookEntry& entry) { return entry.Id == hookId; });
	}

	void Window::PollEvents() {
		if (!s_IsInitialized) return;

		// ImGui's SDL3 backend stops text input when its own fields lose focus; desktop games expect it always on.
		if (Window* active = s_ActiveWindow; active && active->m_Window && active->m_TextInputWanted
			&& !SDL_TextInputActive(active->m_Window)) {
			SDL_StartTextInput(active->m_Window);
		}

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (!g_NativeHooks.empty()) {
				// Copy: a hook may unregister itself (ImGui shutdown on a close request).
				const std::vector<NativeHookEntry> hooks = g_NativeHooks;
				for (const NativeHookEntry& entry : hooks) {
					entry.Hook(event);
				}
			}

			Application* app = Application::GetInstance();
			switch (event.type) {
			case SDL_EVENT_GAMEPAD_ADDED:
				if (app) app->m_Input.OnGamepadAdded(event.gdevice.which);
				continue;
			case SDL_EVENT_GAMEPAD_REMOVED:
				if (app) app->m_Input.OnGamepadRemoved(event.gdevice.which);
				continue;
			case SDL_EVENT_WILL_ENTER_BACKGROUND:
			case SDL_EVENT_DID_ENTER_FOREGROUND:
				// Handled in OnEventWatch the moment they're queued.
				continue;
			case SDL_EVENT_SCREEN_KEYBOARD_SHOWN:
				if (Window* active = s_ActiveWindow; active && active->m_TextEditOwner) active->m_TextEditKeyboardShown = true;
				continue;
			case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
				// The back gesture closes the keyboard without SDL stopping text input; without this the field
				// stayed focused and tapping it could not bring the keyboard back.
				if (Window* active = s_ActiveWindow; active && active->m_TextEditOwner && active->m_TextEditKeyboardShown) {
					active->m_TextEditDismissed = true;
				}
				continue;
			case SDL_EVENT_QUIT:
				// OS-level quit (Cmd+Q, SIGINT, mobile termination) behaves like closing the main window.
				if (s_ActiveWindow) {
					SDL_Event close{};
					close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
					close.window.windowID = SDL_GetWindowID(s_ActiveWindow->m_Window);
					s_ActiveWindow->HandleEvent(close);
				}
				continue;
			default:
				break;
			}

			if (s_ActiveWindow) {
				s_ActiveWindow->HandleEvent(event);
			}
		}
	}

	bool Window::OnEventWatch(void*, SDL_Event* event) {
		// Android blocks the main thread inside SDL_PumpEvents right after queueing these, so PollEvents
		// would only see them on return; watchers run synchronously on the main thread while they're queued.
		if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND && SDL_IsMainThread()) {
			++s_NativeSurfaceGeneration;
			Application::EnterBackground();
			return true;
		}
		if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND && SDL_IsMainThread()) {
			Application::EnterForeground();
			return true;
		}

		// Windows' modal move/size loop blocks PollEvents; SDL forwards live-resize exposes here so the frame keeps up with the drag.
		if (event->type != SDL_EVENT_WINDOW_EXPOSED || event->window.data1 != 1 || !SDL_IsMainThread()) {
			return true;
		}
		Window* active = s_ActiveWindow;
		if (active && active->m_Window && event->window.windowID == SDL_GetWindowID(active->m_Window)) {
			active->SyncViewportFromFramebuffer();
			active->UpdateViewport();
		}
		if (Application* app = Application::GetInstance()) {
			app->RenderOnceForRefresh();
		}
		return true;
	}

	void Window::HandleEvent(const SDL_Event& event) {
		if (!m_Window) return;
		const SDL_WindowID ownId = SDL_GetWindowID(m_Window);

		if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
			if (event.window.windowID != ownId) return;
			switch (event.type) {
			case SDL_EVENT_WINDOW_FOCUS_GAINED: OnFocusChanged(true); break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:   OnFocusChanged(false); break;
			case SDL_EVENT_WINDOW_MINIMIZED:    Application::SetWindowMinimized(true); break;
			case SDL_EVENT_WINDOW_RESTORED:
			case SDL_EVENT_WINDOW_MAXIMIZED:    Application::SetWindowMinimized(false); break;
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: OnFramebufferResized(); break;
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
				// The app's close handler clears this when it takes over the quit flow.
				m_ShouldClose = true;
				if (m_EventCallback) {
					WindowCloseEvent e;
					m_EventCallback(e);
				}
				break;
			}
			default: break;
			}
			return;
		}

		switch (event.type) {
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP: {
			if (event.key.windowID != ownId) return;
			const int key = SDLInputMapping::KeyCodeFromScancode(static_cast<int>(event.key.scancode));
			if (key < 0) return;
			OnKey(key, event.type == SDL_EVENT_KEY_DOWN, event.key.repeat);
			break;
		}
		case SDL_EVENT_TEXT_INPUT: {
			if (event.text.windowID != ownId) return;
			Application* app = Application::GetInstance();
			if (!app) return;
			std::vector<uint32_t> codepoints;
			AppendUtf8Codepoints(event.text.text, codepoints);
			for (uint32_t cp : codepoints) {
				app->m_Input.OnChar(cp);
				ScriptEngine::RaiseEnterChar(cp);
			}
			break;
		}
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP: {
			if (event.button.windowID != ownId) return;
			const int button = SDLInputMapping::MouseButtonFromSDL(event.button.button);
			if (button < 0) return;
			OnMouseButton(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
			break;
		}
		case SDL_EVENT_MOUSE_MOTION: {
			if (event.motion.windowID != ownId) return;
			if (SDL_GetWindowRelativeMouseMode(m_Window)) {
				m_VirtualCursor.x += event.motion.xrel;
				m_VirtualCursor.y += event.motion.yrel;
			}
			else {
				m_VirtualCursor = { event.motion.x, event.motion.y };
			}
			OnCursorMoved(m_VirtualCursor.x, m_VirtualCursor.y);
			break;
		}
		case SDL_EVENT_MOUSE_WHEEL: {
			if (event.wheel.windowID != ownId) return;
			const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
			const float x = event.wheel.x * sign;
			const float y = event.wheel.y * sign;
			Application* app = Application::GetInstance();
			if (!app) return;
			app->m_Input.OnScroll(y);
			ScriptEngine::RaiseMouseScroll(y);
			if (m_EventCallback) {
				MouseScrolledEvent e(x, y);
				m_EventCallback(e);
			}
			break;
		}
		case SDL_EVENT_DROP_BEGIN:
			if (event.drop.windowID == ownId) m_PendingDrops.clear();
			break;
		case SDL_EVENT_DROP_FILE:
			if (event.drop.windowID == ownId && event.drop.data) m_PendingDrops.emplace_back(event.drop.data);
			break;
		case SDL_EVENT_DROP_COMPLETE: {
			if (event.drop.windowID != ownId || m_PendingDrops.empty()) break;
			std::vector<std::string> paths = std::move(m_PendingDrops);
			m_PendingDrops.clear();
			if (m_EventCallback) {
				FileDropEvent e(std::move(paths));
				m_EventCallback(e);
			}
			break;
		}
		default:
			break;
		}
	}

	void Window::OnFocusChanged(bool focused) {
		Application::SetWindowFocused(focused);
		ApplyTitlebarAppearance();
		if (Application::IsShuttingDown()) return;

		ScriptEngine::RaiseFocusChanged(focused);
		if (!m_EventCallback) return;
		if (focused) {
			WindowFocusEvent e;
			m_EventCallback(e);
		}
		else {
			WindowLostFocusEvent e;
			m_EventCallback(e);
		}
	}

	void Window::OnKey(int key, bool pressed, bool repeat) {
		Application* app = Application::GetInstance();
		if (!app) return;

		if (pressed) {
			app->m_Input.OnKeyDown(key);
			if (!repeat) ScriptEngine::RaiseKeyDown(key);
			if (m_EventCallback) {
				KeyPressedEvent e(key, repeat);
				m_EventCallback(e);
			}
		}
		else {
			app->m_Input.OnKeyUp(key);
			ScriptEngine::RaiseKeyUp(key);
			if (m_EventCallback) {
				KeyReleasedEvent e(key);
				m_EventCallback(e);
			}
		}
	}

	void Window::OnMouseButton(int button, bool pressed) {
		Application* app = Application::GetInstance();
		if (!app) return;

		if (pressed) {
			app->m_Input.OnMouseDown(button);
			ScriptEngine::RaiseMouseDown(button);
			if (m_EventCallback) {
				MouseButtonPressedEvent e(button);
				m_EventCallback(e);
			}
		}
		else {
			app->m_Input.OnMouseUp(button);
			ScriptEngine::RaiseMouseUp(button);
			if (m_EventCallback) {
				MouseButtonReleasedEvent e(button);
				m_EventCallback(e);
			}
		}
	}

	void Window::OnCursorMoved(float x, float y) {
		Application* app = Application::GetInstance();
		if (!app) return;
		app->m_Input.OnMouseMove(x, y);
		const Vec2 viewportPosition = ToViewportLocalPosition({ x, y });
		ScriptEngine::RaiseMouseMove(viewportPosition.x, viewportPosition.y);

		if (m_EventCallback) {
			MouseMovedEvent e(viewportPosition.x, viewportPosition.y);
			m_EventCallback(e);
		}
	}

	void Window::OnFramebufferResized() {
		SyncViewportFromFramebuffer();
		UpdateViewport();
		if (s_MainViewport) {
			IDX_CORE_INFO_TAG("Window", "Framebuffer {}x{}, viewport {}x{} at {},{}, window-to-framebuffer scale {}x{}",
				s_MainViewport->GetFramebufferWidth(), s_MainViewport->GetFramebufferHeight(),
				s_MainViewport->GetWidth(), s_MainViewport->GetHeight(),
				s_MainViewport->GetOffsetX(), s_MainViewport->GetOffsetY(), s_WindowToFramebufferScale.x, s_WindowToFramebufferScale.y);
		}

		if (m_EventCallback) {
			int fbW = 0, fbH = 0;
			SDL_GetWindowSizeInPixels(m_Window, &fbW, &fbH);
			WindowResizeEvent e(fbW, fbH);
			m_EventCallback(e);
		}
	}

	void Window::Create(const WindowSpecification& props) {
		IDX_ASSERT(s_IsInitialized, IndexErrorCode::NotInitialized, "The Window isn't initialized");

		m_AspectLock = props.AspectLock > 0.0f ? props.AspectLock : 0.0f;

		int width = props.Width, height = props.Height;
		int minWidth = props.MinWidth, minHeight = props.MinHeight;
		if (props.MonitorFraction > 0.0f || props.MinMonitorFraction > 0.0f) {
			SDL_Rect area{};
			SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area);
			const int areaW = area.w, areaH = area.h;
			if (areaW > 0 && areaH > 0) {
				const float aspect = (props.Width > 0 && props.Height > 0)
					? static_cast<float>(props.Width) / static_cast<float>(props.Height) : 0.0f;
				// Largest rect of the spec's aspect that fits in fraction * work area.
				auto fit = [&](float fraction, int& outW, int& outH) {
					float w = areaW * fraction;
					float h = areaH * fraction;
					if (aspect > 0.0f) {
						if (w / aspect > h) w = h * aspect;
						else h = w / aspect;
					}
					outW = static_cast<int>(w);
					outH = static_cast<int>(h);
				};
				if (props.MinMonitorFraction > 0.0f) fit(props.MinMonitorFraction, minWidth, minHeight);
				if (props.MonitorFraction > 0.0f) fit(props.MonitorFraction, width, height);
				width  = std::clamp(width,  std::min(minWidth,  areaW), areaW);
				height = std::clamp(height, std::min(minHeight, areaH), areaH);
			}
		}

		const SDL_DisplayMode* desktopMode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
		const bool useMonitorSize = props.Fullscreen && desktopMode;
		s_MainViewport = std::make_unique<Viewport>(
			useMonitorSize ? desktopMode->w : width,
			useMonitorSize ? desktopMode->h : height
		);

		IDX_INFO_TAG("Window",
			"Create: spec={}x{}, fullscreen={}, display={}x{} -> viewport={}x{}",
			width, height, props.Fullscreen,
			desktopMode ? desktopMode->w : 0,
			desktopMode ? desktopMode->h : 0,
			s_MainViewport->GetWidth(), s_MainViewport->GetHeight());

#if IDX_PLATFORM_MOBILE
		if (const char* orientations = OrientationHint(props.Orientation, props.Width, props.Height)) {
			SDL_SetHint(SDL_HINT_ORIENTATIONS, orientations);
		}
#endif
		SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
		if (props.Resizeable) flags |= SDL_WINDOW_RESIZABLE;
		if (!props.Decorated || props.CustomTitlebar) flags |= SDL_WINDOW_BORDERLESS;
#if defined(IDX_PLATFORM_MACOS)
		flags |= SDL_WINDOW_METAL;
#elif IDX_PLATFORM_MOBILE
		// Fullscreen hides the status and navigation bars (Android immersive mode, iOS status bar);
		// otherwise they cover the top and bottom of the game.
		flags |= SDL_WINDOW_FULLSCREEN;
#endif

		m_Window = SDL_CreateWindow(props.Title.c_str(), s_MainViewport->GetWidth(), s_MainViewport->GetHeight(), flags);
		IDX_ASSERT(m_Window, IndexErrorCode::Undefined, std::string("Failed to create window: ") + SDL_GetError());

		m_MinSize = { minWidth > 0 ? minWidth : 0, minHeight > 0 ? minHeight : 0 };
		m_MaxSize = { props.MaxWidth > 0 ? props.MaxWidth : 0, props.MaxHeight > 0 ? props.MaxHeight : 0 };
		SDL_SetWindowMinimumSize(m_Window, m_MinSize.x, m_MinSize.y);
		SDL_SetWindowMaximumSize(m_Window, m_MaxSize.x, m_MaxSize.y);

		m_TitlebarColor         = props.TitlebarColor;
		m_TitlebarTextColor     = props.TitlebarTextColor;
		m_TitlebarActiveColor   = props.TitlebarActiveColor;
		m_TitlebarInactiveColor = props.TitlebarInactiveColor;
		m_TitlebarDarkMode      = props.TitlebarDarkMode;
		if (props.CustomTitlebar) {
			TitlebarRegions::SetHeight(props.TitlebarHeight);
#ifdef IDX_PLATFORM_WINDOWS
			Win32Titlebar::Install(GetPlatformHandle(), this);
#else
			SDL_SetWindowHitTest(m_Window, &CustomTitlebarHitTest, nullptr);
#endif
			m_CustomTitlebar = true;
		}
		ApplyTitlebarAppearance();

#if defined(IDX_PLATFORM_WEB) || IDX_PLATFORM_MOBILE
		// Browser and mobile windows always cover their page / screen.
		if (false) {
#else
		if (props.Fullscreen) {
#endif
			// Legacy compat: Fullscreen+Windowed=true maps to Maximized so old projects don't silently switch to Exclusive.
			FullscreenMode mode = props.FullscreenMode;
			if (mode == FullscreenMode::Exclusive && props.Windowed) {
				mode = FullscreenMode::Maximized;
			}
			switch (mode) {
				case FullscreenMode::Exclusive:
					SetFullScreen(true);
					break;
				case FullscreenMode::BorderlessWindowed: {
					SDL_SetWindowBordered(m_Window, false);
					SDL_Rect bounds{};
					if (SDL_GetDisplayBounds(DisplayForWindow(m_Window), &bounds)) {
						SDL_SetWindowSize(m_Window, bounds.w, bounds.h);
						SDL_SetWindowPosition(m_Window, bounds.x, bounds.y);
					}
					break;
				}
				case FullscreenMode::Maximized:
					SDL_MaximizeWindow(m_Window);
					break;
			}
		}
		else if (IDX_PLATFORM_DESKTOP) {
			CenterWindow();
		}

		SDL_ShowWindow(m_Window);
#if defined(IDX_PLATFORM_WEB)
		// The canvas fills the page and follows browser resizes; real fullscreen needs a user gesture. Set
		// after creation: as a creation flag the canvas kept the requested size.
		SDL_SetWindowFillDocument(m_Window, true);
#endif
		SDL_SyncWindow(m_Window);

		m_TextInputWanted = k_TextInputByDefault;
		if (m_TextInputWanted) SDL_StartTextInput(m_Window);

		{
			int fbW = 0, fbH = 0;
			SDL_GetWindowSizeInPixels(m_Window, &fbW, &fbH);
			IDX_INFO_TAG("Window", "After fullscreen/center: framebuffer={}x{}, display scale={}",
				fbW, fbH, GetDisplayScale());
		}

		float mouseX = 0.0f, mouseY = 0.0f;
		SDL_GetMouseState(&mouseX, &mouseY);
		m_VirtualCursor = { mouseX, mouseY };

		// Vsync is handled by the render backend.
		SyncViewportFromFramebuffer();

		s_ActiveWindow = this;
	}

	void Window::Destroy() {
		if (!m_Window) {
			return;
		}

		m_Cursor = nullptr;
		m_CursorTextureAssetId = 0;
		if (m_DefaultCursor || m_UICursor) {
			SDL_SetCursor(SDL_GetDefaultCursor());
		}
		if (m_DefaultCursor) {
			SDL_DestroyCursor(m_DefaultCursor);
			m_DefaultCursor = nullptr;
		}
		if (m_UICursor) {
			SDL_DestroyCursor(m_UICursor);
			m_UICursor = nullptr;
		}
#ifdef IDX_PLATFORM_WINDOWS
		if (m_BigIcon) ::DestroyIcon(static_cast<HICON>(m_BigIcon));
		if (m_SmallIcon) ::DestroyIcon(static_cast<HICON>(m_SmallIcon));
#endif
		m_BigIcon = nullptr;
		m_SmallIcon = nullptr;
		m_EventCallback = {};
#ifdef IDX_PLATFORM_WINDOWS
		// MUST uninstall before SDL_DestroyWindow: teardown messages can arrive after HWND destruction, dangling the subclass state map entry.
		if (m_CustomTitlebar) {
			Win32Titlebar::Uninstall(GetPlatformHandle());
		}
#endif
		m_CustomTitlebar = false;
		SDL_DestroyWindow(m_Window);
		m_Window = nullptr;
		if (s_ActiveWindow == this) {
			s_ActiveWindow = nullptr;
			ClearUIRegion();
			s_WindowToFramebufferScale = { 1.0f, 1.0f };
		}
	}

	void Window::SwapBuffers() const {
		RenderApi::Present();
	}

	void* Window::GetPlatformHandle() const {
		if (!m_Window) return nullptr;
		const SDL_PropertiesID props = SDL_GetWindowProperties(m_Window);
#if defined(IDX_PLATFORM_WINDOWS)
		return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(IDX_PLATFORM_MACOS)
		return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(IDX_PLATFORM_IOS)
		return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
#elif defined(IDX_PLATFORM_ANDROID)
		return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#else
		(void)props;
		return nullptr;
#endif
	}

	void Window::SetClipboardString(const std::string& s) {
		SDL_SetClipboardText(s.c_str());
	}

	std::string Window::GetClipboardString() const {
		char* text = SDL_GetClipboardText();
		std::string result = text ? std::string(text) : std::string{};
		SDL_free(text);
		return result;
	}

	void Window::SetPosition(Vec2Int pos) {
		SDL_SetWindowPosition(m_Window, pos.x, pos.y);
	}

	Vec2Int Window::GetPosition() const {
		Vec2Int pos;
		SDL_GetWindowPosition(m_Window, &pos.x, &pos.y);
		return pos;
	}

	void Window::SetSize(Vec2Int size) {
		SDL_SetWindowSize(m_Window, size.x, size.y);
	}

	void Window::SetTitle(const std::string& title) {
		SDL_SetWindowTitle(m_Window, title.c_str());
	}

	std::string Window::GetTitle() const {
		const char* title = m_Window ? SDL_GetWindowTitle(m_Window) : nullptr;
		return title ? std::string(title) : std::string{};
	}

	float Window::GetDisplayScale() const {
		const float scale = m_Window ? SDL_GetWindowDisplayScale(m_Window) : 0.0f;
		return scale > 0.0f ? scale : 1.0f;
	}

	void Window::SetTextInputActive(bool active) {
		m_TextInputWanted = active;
		if (!m_Window) return;
		if (active) SDL_StartTextInput(m_Window);
		else SDL_StopTextInput(m_Window);
	}

	void Window::BeginTextEditing(const TextEditRequest& request, const void* owner) {
#if IDX_PLATFORM_MOBILE
		if (!m_Window || !owner) return;

		const bool region = s_UIRegion.IsActive();
		const float uiLeft = static_cast<float>(region ? s_UIRegion.OffsetX : (s_MainViewport ? s_MainViewport->GetOffsetX() : 0));
		const float uiTop  = static_cast<float>(region ? s_UIRegion.OffsetY : (s_MainViewport ? s_MainViewport->GetOffsetY() : 0));
		const Vec2 scale = s_WindowToFramebufferScale;
		const SDL_Rect area{
			static_cast<int>((uiLeft + request.Left) / scale.x),
			static_cast<int>((uiTop + request.Top) / scale.y),
			static_cast<int>(std::ceil(request.Width / scale.x)),
			static_cast<int>(std::ceil(request.Height / scale.y)) };
		// Every change is a JNI / UIKit round trip; the field rarely moves while edited.
		if (area.x != m_TextEditArea[0] || area.y != m_TextEditArea[1] || area.w != m_TextEditArea[2] || area.h != m_TextEditArea[3]) {
			SDL_SetTextInputArea(m_Window, &area, 0);
			m_TextEditArea[0] = area.x; m_TextEditArea[1] = area.y; m_TextEditArea[2] = area.w; m_TextEditArea[3] = area.h;
		}

		const bool sameKeyboard = m_TextEditOwner && request.Kind == m_TextEditKind && request.Multiline == m_TextEditMultiline;
		m_TextEditOwner = owner;
		if (sameKeyboard) return;

		m_TextEditKeyboardShown = false;
		m_TextEditDismissed = false;
		// A different keyboard layout only applies when the keyboard is shown again.
		if (SDL_TextInputActive(m_Window)) SDL_StopTextInput(m_Window);
		m_TextEditKind = request.Kind;
		m_TextEditMultiline = request.Multiline;

		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_TextInputType type = SDL_TEXTINPUT_TYPE_TEXT;
		switch (request.Kind) {
			case TextInputKind::Text:     type = SDL_TEXTINPUT_TYPE_TEXT; break;
			case TextInputKind::Integer:
			case TextInputKind::Decimal:  type = SDL_TEXTINPUT_TYPE_NUMBER; break;
			case TextInputKind::Password: type = SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN; break;
		}
		SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, type);
		SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, request.Multiline);
		// SDL's number keyboard has no minus sign or decimal point on Android; the raw InputType adds them
		// (TYPE_CLASS_NUMBER | TYPE_NUMBER_FLAG_SIGNED [| TYPE_NUMBER_FLAG_DECIMAL]).
		if (request.Kind == TextInputKind::Integer) SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 0x1002);
		if (request.Kind == TextInputKind::Decimal) SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 0x3002);
		SDL_StartTextInputWithProperties(m_Window, props);
		SDL_DestroyProperties(props);
#else
		(void)request;
		(void)owner;
#endif
	}

	bool Window::ShowTextActions(const TextEditRequest& area, int flags) const {
#if IDX_PLATFORM_MOBILE
		const bool region = s_UIRegion.IsActive();
		const float uiLeft = static_cast<float>(region ? s_UIRegion.OffsetX : (s_MainViewport ? s_MainViewport->GetOffsetX() : 0));
		const float uiTop  = static_cast<float>(region ? s_UIRegion.OffsetY : (s_MainViewport ? s_MainViewport->GetOffsetY() : 0));
		const Vec2 scale = s_WindowToFramebufferScale;
		return Platform::ShowTextActions(
			static_cast<int>((uiLeft + area.Left) / scale.x), static_cast<int>((uiTop + area.Top) / scale.y),
			static_cast<int>(std::ceil(area.Width / scale.x)), static_cast<int>(std::ceil(area.Height / scale.y)), flags);
#else
		(void)area;
		(void)flags;
		return false;
#endif
	}

	void Window::EndTextEditing(const void* owner) {
#if IDX_PLATFORM_MOBILE
		if (!m_Window || !owner || owner != m_TextEditOwner) return;
		m_TextEditOwner = nullptr;
		m_TextEditKeyboardShown = false;
		m_TextEditDismissed = false;
		m_TextEditArea[0] = m_TextEditArea[1] = m_TextEditArea[2] = m_TextEditArea[3] = 0;
		if (!m_TextInputWanted && SDL_TextInputActive(m_Window)) SDL_StopTextInput(m_Window);
#else
		(void)owner;
#endif
	}

	bool Window::WasTextEditingDismissed(const void* owner) const {
#if IDX_PLATFORM_MOBILE
		// SDL stops text input itself when the user hides the keyboard.
		return m_Window && owner && owner == m_TextEditOwner && (m_TextEditDismissed || !SDL_TextInputActive(m_Window));
#else
		(void)owner;
		return false;
#endif
	}

	void Window::CenterWindow() {
		const SDL_DisplayID display = DisplayForWindow(m_Window);
		SDL_SetWindowPosition(m_Window,
			static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)),
			static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)));
	}
	void Window::Focus() {
		SDL_RaiseWindow(m_Window);
	}
	void Window::MaximizeWindow(bool reset) {
		if (reset && IsMaximized()) {
			SDL_RestoreWindow(m_Window);
		}
		else {
			SDL_MaximizeWindow(m_Window);
		}
		SDL_SyncWindow(m_Window);

		SyncViewportFromFramebuffer();
		UpdateViewport();
	}
	void Window::MinimizeWindow() {
		SDL_MinimizeWindow(m_Window);
	}
	void Window::RestoreWindow() {
		SDL_RestoreWindow(m_Window);
		SDL_SyncWindow(m_Window);
		SyncViewportFromFramebuffer();
		UpdateViewport();
	}

	void Window::SetFullScreen(bool enabled) {
		// Phones and tablets have no windowed mode; leaving fullscreen would only bring the system bars back.
		if (IDX_PLATFORM_MOBILE) return;
		if (IsFullScreen() == enabled)
			return;

		if (enabled) {
			const SDL_DisplayID display = DisplayForWindow(m_Window);
			const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
			if (!desktop) {
				IDX_CORE_WARN_TAG("Window", "Cannot enter fullscreen: no display mode ({})", SDL_GetError());
				return;
			}
			// Exclusive mode at the desktop resolution; SDL falls back to borderless desktop fullscreen where exclusive modes don't exist (Wayland, mobile, Web).
			SDL_DisplayMode closest{};
			if (SDL_GetClosestFullscreenDisplayMode(display, desktop->w, desktop->h, desktop->refresh_rate, true, &closest)) {
				SDL_SetWindowFullscreenMode(m_Window, &closest);
			}
			else {
				SDL_SetWindowFullscreenMode(m_Window, nullptr);
			}
		}

		if (!SDL_SetWindowFullscreen(m_Window, enabled)) {
			IDX_CORE_WARN_TAG("Window", "SDL_SetWindowFullscreen({}) failed: {}", enabled, SDL_GetError());
		}
		SDL_SyncWindow(m_Window);
		SyncViewportFromFramebuffer();
		UpdateViewport();
	}

	void Window::SetDecorated(bool enabled) {
		SDL_SetWindowBordered(m_Window, enabled);
	}
	void Window::SetVisible(bool enabled) {
		if (enabled)
			SDL_ShowWindow(m_Window);
		else
			SDL_HideWindow(m_Window);
	}

	void Window::SetResizeable(bool enabled) {
		SDL_SetWindowResizable(m_Window, enabled);
	}

	void Window::SetMinSize(Vec2Int size) {
		m_MinSize = { size.x > 0 ? size.x : 0, size.y > 0 ? size.y : 0 };
		SDL_SetWindowMinimumSize(m_Window, m_MinSize.x, m_MinSize.y);
	}

	void Window::SetMaxSize(Vec2Int size) {
		m_MaxSize = { size.x > 0 ? size.x : 0, size.y > 0 ? size.y : 0 };
		SDL_SetWindowMaximumSize(m_Window, m_MaxSize.x, m_MaxSize.y);
	}

	void Window::SetCursorLocked(bool enabled) {
		SetCursorMode(enabled ? 1 : 0);
	}
	void Window::SetCursorHidden(bool enabled) {
		SetCursorMode(enabled ? 3 : 0);
	}
	void Window::SetCursorMode(int mode) {
		const int normalized = (mode >= 1 && mode <= 3) ? mode : 0;
		// 1 = locked, 2 = confined-and-hidden (both relative mode), 3 = hidden but free.
		const bool relative = normalized == 1 || normalized == 2;
		if (relative && !SDL_GetWindowRelativeMouseMode(m_Window)) {
			m_VirtualCursor = GetCursorPosition();
		}
		SDL_SetWindowRelativeMouseMode(m_Window, relative);
		if (normalized == 0) SDL_ShowCursor();
		else SDL_HideCursor();
		m_CursorMode = normalized;
	}

	void Window::SetCursorPosition(Vec2 position) {
		SDL_WarpMouseInWindow(m_Window, position.x, position.y);
		m_VirtualCursor = position;
	}

	Vec2 Window::GetCursorPosition() const {
		if (!m_Window) return {};
		if (SDL_GetWindowRelativeMouseMode(m_Window)) return m_VirtualCursor;
		float x = 0.0f, y = 0.0f;
		if (SDL_GetMouseFocus() == m_Window) {
			SDL_GetMouseState(&x, &y);
			return { x, y };
		}
		// Mouse is over another window (an ImGui viewport): derive window-local coordinates from the global position.
		SDL_GetGlobalMouseState(&x, &y);
		const Vec2Int pos = GetPosition();
		return { x - static_cast<float>(pos.x), y - static_cast<float>(pos.y) };
	}

	namespace {
		// 32 logical px matches the classic Windows cursor; every project cursor is fitted to it so source resolution doesn't change on-screen size.
		constexpr int k_CursorTargetSize = 32;
		// Exact per-scale images: SDL's own rescale for in-between DPIs drops the hotspot on Windows.
		constexpr float k_CursorScales[] = { 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 3.0f, 3.5f, 4.0f };
		constexpr int k_WindowIconBaseSize = 256;
		constexpr int k_WindowIconAlternateSizes[] = { 16, 24, 32, 48, 64, 128, 512 };

		// Copies the pixels: SDL keeps cursor/icon surfaces alive past this call (Windows rebuilds cursors per DPI).
		SDL_Surface* CreateOwnedSurface(const ImageData& image) {
			SDL_Surface* surface = SDL_CreateSurface(image.Width, image.Height, SDL_PIXELFORMAT_RGBA32);
			if (!surface) return nullptr;
			for (int y = 0; y < image.Height; ++y) {
				std::memcpy(static_cast<unsigned char*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch,
					image.Pixels + static_cast<size_t>(y) * image.Width * 4, static_cast<size_t>(image.Width) * 4);
			}
			return surface;
		}

		std::unique_ptr<ImageData> ReadTextureImage(const Texture2D* tex2D) {
			std::unique_ptr<ImageData> image = tex2D ? tex2D->GetImageData() : nullptr;
			if (!image || !image->Pixels || image->Width <= 0 || image->Height <= 0) return nullptr;
			// GetImageData decodes with the texture's own flip; undo it so row 0 is the top again.
			if (tex2D->IsFlippedY()) image->FlipVerticalRGBA();
			return image;
		}

		Vec2Int ScaleHotspot(Vec2Int hotspot, const ImageData& source, int width, int height) {
			const auto scaleAxis = [](int value, int from, int to) {
				return std::clamp(static_cast<int>(static_cast<long long>(value) * to / std::max(from, 1)), 0, to - 1);
			};
			return { scaleAxis(hotspot.x, source.Width, width), scaleAxis(hotspot.y, source.Height, height) };
		}

		SDL_Cursor* MakeCursorFromTexture(const Texture2D* tex2D, Vec2Int hotspot) {
#if IDX_PLATFORM_MOBILE
			(void)tex2D; (void)hotspot;
			return nullptr;
#else
			std::unique_ptr<ImageData> source = ReadTextureImage(tex2D);
			if (!source) {
				IDX_WARN_TAG("Window", "Cannot set cursor image from invalid texture data");
				return nullptr;
			}

			// Keep the aspect ratio: the longest side becomes the target size.
			const float baseScale = static_cast<float>(k_CursorTargetSize) / static_cast<float>(std::max(source->Width, source->Height));
			const int baseW = std::max(1, static_cast<int>(std::lround(source->Width * baseScale)));
			const int baseH = std::max(1, static_cast<int>(std::lround(source->Height * baseScale)));

			std::unique_ptr<ImageData> baseImage = ImageProcessing::Resize(*source, baseW, baseH);
			SDL_Surface* surface = baseImage ? CreateOwnedSurface(*baseImage) : nullptr;
			if (!surface) {
				IDX_WARN_TAG("Window", "Cursor surface creation failed: {}", SDL_GetError());
				return nullptr;
			}

			for (float scale : k_CursorScales) {
				// Must equal SDL_GetSurfaceImage's rounding so the exact image (and its hotspot) is picked.
				const int w = static_cast<int>(SDL_round(baseW * scale));
				const int h = static_cast<int>(SDL_round(baseH * scale));
				std::unique_ptr<ImageData> scaled = ImageProcessing::Resize(*source, w, h);
				SDL_Surface* alternate = scaled ? CreateOwnedSurface(*scaled) : nullptr;
				if (!alternate) continue;
				const Vec2Int scaledHotspot = ScaleHotspot(hotspot, *source, w, h);
				const SDL_PropertiesID props = SDL_GetSurfaceProperties(alternate);
				SDL_SetNumberProperty(props, SDL_PROP_SURFACE_HOTSPOT_X_NUMBER, scaledHotspot.x);
				SDL_SetNumberProperty(props, SDL_PROP_SURFACE_HOTSPOT_Y_NUMBER, scaledHotspot.y);
				SDL_AddSurfaceAlternateImage(surface, alternate);
				SDL_DestroySurface(alternate);
			}

			const Vec2Int baseHotspot = ScaleHotspot(hotspot, *source, baseW, baseH);
			SDL_Cursor* cursor = SDL_CreateColorCursor(surface, baseHotspot.x, baseHotspot.y);
			SDL_DestroySurface(surface);
			if (!cursor) {
				IDX_WARN_TAG("Window", "SDL_CreateColorCursor failed: {}", SDL_GetError());
			}
			return cursor;
#endif
		}

#ifdef IDX_PLATFORM_WINDOWS
		HICON CreateWin32Icon(const ImageData& source, int size) {
			std::unique_ptr<ImageData> image = ImageProcessing::FitSquare(source, size);
			if (!image) return nullptr;

			BITMAPV5HEADER header{};
			header.bV5Size = sizeof(header);
			header.bV5Width = size;
			header.bV5Height = -size; // top-down
			header.bV5Planes = 1;
			header.bV5BitCount = 32;
			header.bV5Compression = BI_BITFIELDS;
			header.bV5RedMask = 0x00FF0000;
			header.bV5GreenMask = 0x0000FF00;
			header.bV5BlueMask = 0x000000FF;
			header.bV5AlphaMask = 0xFF000000;

			void* bits = nullptr;
			HDC dc = ::GetDC(nullptr);
			HBITMAP color = ::CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
			::ReleaseDC(nullptr, dc);
			if (!color || !bits) {
				if (color) ::DeleteObject(color);
				return nullptr;
			}
			auto* dst = static_cast<std::uint8_t*>(bits);
			for (size_t i = 0; i < static_cast<size_t>(size) * size; ++i) {
				dst[i * 4 + 0] = image->Pixels[i * 4 + 2];
				dst[i * 4 + 1] = image->Pixels[i * 4 + 1];
				dst[i * 4 + 2] = image->Pixels[i * 4 + 0];
				dst[i * 4 + 3] = image->Pixels[i * 4 + 3];
			}
			HBITMAP mask = ::CreateBitmap(size, size, 1, 1, nullptr);
			ICONINFO info{};
			info.fIcon = TRUE;
			info.hbmColor = color;
			info.hbmMask = mask;
			HICON icon = ::CreateIconIndirect(&info);
			::DeleteObject(color);
			if (mask) ::DeleteObject(mask);
			return icon;
		}
#endif
	}

	void Window::ApplyCursor(SDL_Cursor* cursor) const {
		SDL_SetCursor(cursor ? cursor : SDL_GetDefaultCursor());
	}

	void Window::SetCursorImage(const Texture2D* tex2D, Vec2Int hotspot) {
		SDL_Cursor* fresh = tex2D ? MakeCursorFromTexture(tex2D, hotspot) : nullptr;
		if (tex2D && !fresh) return;

		m_CursorTextureAssetId = 0;
		const bool wasUsingDefault = (m_Cursor == m_DefaultCursor);
		SDL_Cursor* previous = m_DefaultCursor;
		m_DefaultCursor = fresh;

		if (wasUsingDefault) {
			m_Cursor = m_DefaultCursor;
			// Track the cursor but only push it to the OS when enabled — the
			// editor keeps it off so the game cursor doesn't leak outside the Game View.
			if (m_GameCursorEnabled) ApplyCursor(m_DefaultCursor);
		}
		// Destroy after the switch: SDL must never be left pointing at a freed active cursor.
		if (previous) {
			if (SDL_GetCursor() == previous) SDL_SetCursor(SDL_GetDefaultCursor());
			SDL_DestroyCursor(previous);
		}
	}

	void Window::SetUICursorImage(const Texture2D* tex2D, Vec2Int hotspot) {
		SDL_Cursor* fresh = tex2D ? MakeCursorFromTexture(tex2D, hotspot) : nullptr;
		if (tex2D && !fresh) return;

		const bool wasUsingUI = (m_Cursor == m_UICursor);
		SDL_Cursor* previous = m_UICursor;
		m_UICursor = fresh;

		if (wasUsingUI) {
			m_Cursor = m_UICursor;
			if (m_GameCursorEnabled) ApplyCursor(m_UICursor);
		}
		if (previous) {
			if (SDL_GetCursor() == previous) SDL_SetCursor(SDL_GetDefaultCursor());
			SDL_DestroyCursor(previous);
		}
	}

	void Window::SetCursorOverUI(bool overUI) {
		if (overUI == m_CursorOverUI) return;
		m_CursorOverUI = overUI;

		SDL_Cursor* desired = overUI && m_UICursor ? m_UICursor : m_DefaultCursor;
		if (desired == m_Cursor) return;
		m_Cursor = desired;
		if (m_GameCursorEnabled) ApplyCursor(desired);
	}

	void Window::SetGameCursorEnabled(bool enabled) {
		if (enabled == m_GameCursorEnabled) return;
		m_GameCursorEnabled = enabled;
		// On enable, re-assert the game's current cursor (the editor pairs this
		// with ImGui's NoMouseCursorChange so ImGui doesn't overwrite it). On
		// disable, leave the OS cursor and let the caller hand control back to ImGui.
		if (enabled) {
			ApplyCursor(m_Cursor);
		}
	}

	void Window::SetWindowIcon(const Texture2D* tex2D) {
		std::unique_ptr<ImageData> image = ReadTextureImage(tex2D);
		if (!image) {
			IDX_WARN_TAG("Window", "Cannot set window icon from invalid texture data");
			return;
		}
		SetWindowIcon(*image);
	}

	void Window::SetWindowIcon(const ImageData& image) {
		if (!m_Window || !image.Pixels || image.Width <= 0 || image.Height <= 0) return;
#if defined(IDX_PLATFORM_WINDOWS)
		HWND hwnd = static_cast<HWND>(GetPlatformHandle());
		if (!hwnd) return;
		const UINT dpi = ::GetDpiForWindow(hwnd);
		HICON bigIcon = CreateWin32Icon(image, ::GetSystemMetricsForDpi(SM_CXICON, dpi));
		HICON smallIcon = CreateWin32Icon(image, ::GetSystemMetricsForDpi(SM_CXSMICON, dpi));
		ApplyWin32Icons(bigIcon, smallIcon);
#elif IDX_PLATFORM_MOBILE || defined(IDX_PLATFORM_WEB)
		// The OS takes the icon from the app bundle / APK / web manifest.
#else
		// X11 uses the base image; Wayland (xdg-toplevel-icon) and the macOS dock also read the alternates.
		const int largest = std::max(image.Width, image.Height);
		const int baseSize = std::min(k_WindowIconBaseSize, largest);
		std::unique_ptr<ImageData> base = ImageProcessing::FitSquare(image, baseSize);
		SDL_Surface* surface = base ? CreateOwnedSurface(*base) : nullptr;
		if (!surface) {
			IDX_WARN_TAG("Window", "Window icon surface creation failed: {}", SDL_GetError());
			return;
		}
		for (int size : k_WindowIconAlternateSizes) {
			if (size == baseSize || size > largest) continue;
			std::unique_ptr<ImageData> scaled = ImageProcessing::FitSquare(image, size);
			if (SDL_Surface* alternate = scaled ? CreateOwnedSurface(*scaled) : nullptr) {
				SDL_AddSurfaceAlternateImage(surface, alternate);
				SDL_DestroySurface(alternate);
			}
		}
		if (!SDL_SetWindowIcon(m_Window, surface)) {
			IDX_WARN_TAG("Window", "SDL_SetWindowIcon failed: {}", SDL_GetError());
		}
		SDL_DestroySurface(surface);
#endif
	}

	void Window::SetEmbeddedWindowIcon() {
#if defined(IDX_PLATFORM_WINDOWS)
		HWND hwnd = m_Window ? static_cast<HWND>(GetPlatformHandle()) : nullptr;
		if (!hwnd) return;
		// SDL's class icon comes from ExtractIconEx at 32 px, which the taskbar upscales on HiDPI.
		const HINSTANCE module = ::GetModuleHandleW(nullptr);
		const UINT dpi = ::GetDpiForWindow(hwnd);
		// LoadIconWithScaleDown would need the comctl32 v6 manifest in every host exe.
		const auto load = [&](int metric) {
			const int size = ::GetSystemMetricsForDpi(metric, dpi);
			return static_cast<HICON>(::LoadImageW(module, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
		};
		ApplyWin32Icons(load(SM_CXICON), load(SM_CXSMICON));
#endif
	}

#if defined(IDX_PLATFORM_WINDOWS)
	void Window::ApplyWin32Icons(void* bigHandle, void* smallHandle) {
		HICON bigIcon = static_cast<HICON>(bigHandle);
		HICON smallIcon = static_cast<HICON>(smallHandle);
		if (!bigIcon || !smallIcon) {
			if (bigIcon) ::DestroyIcon(bigIcon);
			if (smallIcon) ::DestroyIcon(smallIcon);
			IDX_WARN_TAG("Window", "Window icon creation failed (error {})", ::GetLastError());
			return;
		}
		HWND hwnd = static_cast<HWND>(GetPlatformHandle());
		::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
		::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
		if (m_BigIcon) ::DestroyIcon(static_cast<HICON>(m_BigIcon));
		if (m_SmallIcon) ::DestroyIcon(static_cast<HICON>(m_SmallIcon));
		m_BigIcon = bigIcon;
		m_SmallIcon = smallIcon;
	}
#endif

	Window::SafeAreaInsets Window::GetSafeAreaInsets() {
		SafeAreaInsets insets;
		if (s_UIRegion.Source == UIRegionSource::Editor || !s_ActiveWindow || !s_ActiveWindow->m_Window || !s_MainViewport) {
			return insets;
		}
		SDL_Rect safe{};
		if (!SDL_GetWindowSafeArea(s_ActiveWindow->m_Window, &safe) || safe.w <= 0 || safe.h <= 0) {
			return insets;
		}
		// The UI surface: the aspect-locked sub-rect when letterboxed, otherwise the viewport.
		const bool region = s_UIRegion.IsActive();
		const float uiLeft   = static_cast<float>(region ? s_UIRegion.OffsetX : s_MainViewport->GetOffsetX());
		const float uiTop    = static_cast<float>(region ? s_UIRegion.OffsetY : s_MainViewport->GetOffsetY());
		const float uiWidth  = static_cast<float>(region ? s_UIRegion.Width : s_MainViewport->GetWidth());
		const float uiHeight = static_cast<float>(region ? s_UIRegion.Height : s_MainViewport->GetHeight());

		const Vec2 scale = s_WindowToFramebufferScale;
		const float safeLeft   = static_cast<float>(safe.x) * scale.x;
		const float safeTop    = static_cast<float>(safe.y) * scale.y;
		const float safeRight  = static_cast<float>(safe.x + safe.w) * scale.x;
		const float safeBottom = static_cast<float>(safe.y + safe.h) * scale.y;
		// Clamped so a stale or odd report can never collapse the UI.
		insets.Left   = std::clamp(safeLeft - uiLeft, 0.0f, uiWidth * 0.5f);
		insets.Top    = std::clamp(safeTop - uiTop, 0.0f, uiHeight * 0.5f);
		insets.Right  = std::clamp(uiLeft + uiWidth - safeRight, 0.0f, uiWidth * 0.5f);
		insets.Bottom = std::clamp(uiTop + uiHeight - safeBottom, 0.0f, uiHeight * 0.5f);
		return insets;
	}

	Vec2 Window::ToViewportLocalPosition(Vec2 windowPosition) {
		return MapToViewportLocalPosition(windowPosition, s_UIRegion, s_WindowToFramebufferScale);
	}

	std::optional<DisplayMode> Window::GetDisplayMode() const {
		const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(DisplayForWindow(m_Window));
		if (!mode) return std::nullopt;
		return DisplayMode{ mode->w, mode->h, mode->refresh_rate };
	}
	Vec2Int Window::GetScreenCenter() const {
		const std::optional<DisplayMode> mode = GetDisplayMode();
		if (!mode) return Vec2Int(0, 0);
		return Vec2Int(mode->Width / 2, mode->Height / 2);
	}
	Vec2 Window::GetWindowCenter() const {
		Vec2Int size = GetSize();
		return Vec2(size.x / 2.f, size.y / 2.f);
	}

	bool Window::IsMaximized() const {
		return (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_MAXIMIZED) != 0;
	}
	bool Window::IsMinimized() const {
		return (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_MINIMIZED) != 0;
	}
	bool Window::IsFullScreen() const {
		return (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) != 0;
	}
	bool Window::IsVisible() const {
		return (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_HIDDEN) == 0;
	}
	bool Window::IsDecorated() const {
		return (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_BORDERLESS) == 0;
	}
	bool Window::IsResizeable() const {
		return (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_RESIZABLE) != 0;
	}
	bool Window::IsFocused() const {
		return m_Window && (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_INPUT_FOCUS) != 0;
	}

	void Window::UpdateViewport() {
		if (!RenderApi::IsInitialized() || !s_MainViewport) {
			return;
		}
		const int fbW = s_MainViewport->GetFramebufferWidth();
		const int fbH = s_MainViewport->GetFramebufferHeight();
		const int x   = s_MainViewport->GetOffsetX();
		const int y   = s_MainViewport->GetOffsetY();
		const int w   = s_MainViewport->GetWidth();
		const int h   = s_MainViewport->GetHeight();
		// OnWindowResize resets the swap chain; SetViewport-only would leave it stale when an FBO is bound mid-frame.
		RenderApi::OnWindowResize(fbW, fbH);
		RenderApi::SetViewport(x, y, w, h);
	}

	void Window::SyncViewportFromFramebuffer() {
		if (!m_Window) {
			return;
		}

		int fbW = 0;
		int fbH = 0;
		SDL_GetWindowSizeInPixels(m_Window, &fbW, &fbH);
		int windowW = 0;
		int windowH = 0;
		SDL_GetWindowSize(m_Window, &windowW, &windowH);
		s_WindowToFramebufferScale = CalculateWindowToFramebufferScale(
			{ windowW, windowH }, { fbW, fbH });

		int subW = fbW > 0 ? fbW : 1;
		int subH = fbH > 0 ? fbH : 1;
		int offsetX = 0;
		int offsetY = 0;
		if (m_AspectLock > 0.0f && fbW > 0 && fbH > 0) {
			const float fbAspect = static_cast<float>(fbW) / static_cast<float>(fbH);
			if (fbAspect > m_AspectLock) {
				// Pillarbox: shrink width, keep full height.
				subW = static_cast<int>(std::round(static_cast<float>(fbH) * m_AspectLock));
				subH = fbH;
			} else {
				// Letterbox: keep full width, shrink height.
				subW = fbW;
				subH = static_cast<int>(std::round(static_cast<float>(fbW) / m_AspectLock));
			}
			if (subW < 1) subW = 1;
			if (subH < 1) subH = 1;
			offsetX = (fbW - subW) / 2;
			offsetY = (fbH - subH) / 2;
		}

		if (!s_MainViewport) {
			s_MainViewport = std::make_unique<Viewport>(subW, subH);
		}
		s_MainViewport->SetLetterboxedSubRect(fbW, fbH, offsetX, offsetY, subW, subH);

		PublishAspectLockUIRegion(m_AspectLock,
			UIRegion{ offsetX, offsetY, subW, subH, s_WindowToFramebufferScale });
	}

	void Window::SetAspectLock(float aspect) {
		m_AspectLock = aspect > 0.0f ? aspect : 0.0f;
		PublishAspectLockUIRegion(m_AspectLock, s_UIRegion);
		SyncViewportFromFramebuffer();
		UpdateViewport();
	}

	void Window::ResyncViewportAfterRenderApiInit() {
		SyncViewportFromFramebuffer();
		UpdateViewport();
	}

	void Window::SetTitlebarCaptionRect(int x, int y, int w, int h) {
		TitlebarRegions::SetCaptionRect(x, y, w, h);
	}

	void Window::AddTitlebarNonClientRect(int x, int y, int w, int h) {
		TitlebarRegions::AddNonClientRect(x, y, w, h);
	}

	void Window::ResetTitlebarNonClientRects() {
		TitlebarRegions::ResetNonClientRects();
	}

	void Window::SetTitlebarDarkMode(bool enabled) {
		if (m_TitlebarDarkMode == enabled) return;
		m_TitlebarDarkMode = enabled;
		ApplyTitlebarAppearance();
	}

	void Window::ApplyTitlebarAppearance() {
#ifdef IDX_PLATFORM_WINDOWS
		if (!m_Window) return;

		const bool focused = IsFocused();
		Color caption = m_TitlebarColor;
		if (focused && m_TitlebarActiveColor.a > 0.0f) {
			caption = m_TitlebarActiveColor;
		}
		else if (!focused && m_TitlebarInactiveColor.a > 0.0f) {
			caption = m_TitlebarInactiveColor;
		}

		void* hwnd = GetPlatformHandle();
		Win32Titlebar::SetDarkMode(hwnd, m_TitlebarDarkMode);
		Win32Titlebar::SetColors(hwnd, caption, m_TitlebarTextColor, caption);
#endif
	}

	int Window::GetTitlebarHeight() const {
		return TitlebarRegions::GetHeight();
	}

	void Window::SetTitlebarHeight(int logicalPx) {
		TitlebarRegions::SetHeight(logicalPx);
	}
}
