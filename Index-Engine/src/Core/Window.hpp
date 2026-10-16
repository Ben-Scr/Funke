#pragma once
#include "Collections/Color.hpp"
#include "Collections/Vec2.hpp"
#include "Collections/Viewport.hpp"
#include "Core/Export.hpp"
#include "Core/WindowSpecification.hpp"
#include "Events/IndexEvent.hpp"

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <optional>
#include <vector>

struct SDL_Window;
struct SDL_Cursor;
union SDL_Event;

namespace Index {
	class Texture2D;
	struct ImageData;

	// Name/version/reverse-DNS id the OS uses to group windows and pick the taskbar/dock entry (Wayland app_id, macOS).
	struct AppIdentity {
		std::string Name = "Index";
		std::string Version;
		std::string Identifier = "dev.index.engine";
	};

	struct DisplayMode {
		int Width = 0;
		int Height = 0;
		float RefreshRate = 0.0f;
	};

	class INDEX_API Window {
		friend class Application;

	public:
		Window(int width, int height, const std::string& title);
		Window(const WindowSpecification& props);

		// Initializes SDL's video, event and gamepad subsystems.
		static void Initialize(const AppIdentity& identity = {});
		static void Shutdown();
		static bool IsInitialized() { return s_IsInitialized; }

		// Drains the SDL event queue and routes events to the owning Window and the native hooks.
		static void PollEvents();

		// Raw SDL events for consumer-side backends (ImGui's SDL3 platform backend) that live outside engine.dll.
		using NativeEventHook = std::function<void(const SDL_Event&)>;
		static int AddNativeEventHook(NativeEventHook hook);
		static void RemoveNativeEventHook(int hookId);

		static void SetVsync(bool enabled);

		void Destroy();

		void SetClipboardString(const std::string& s);
		void SetWindowResizeable(bool enabled) { SetResizeable(enabled); }
		void SetWindowMoveable(bool enabled) { SetDecorated(enabled); }
		void SetPosition(Vec2Int pos);
		void SetSize(Vec2Int size);

		// Components <= 0 mean "no limit".
		void SetMinSize(Vec2Int size);
		void SetMaxSize(Vec2Int size);
		Vec2Int GetMinSize() const { return m_MinSize; }
		Vec2Int GetMaxSize() const { return m_MaxSize; }
		void SetCursorPosition(Vec2 position);

		void SetCursorLocked(bool enabled);
		void SetCursorHidden(bool enabled);
		// The mode the game asked for; it reads back unchanged while suspended.
		void SetCursorMode(int mode);
		int GetCursorMode() const { return m_CursorMode; }
		// Editor-only: while suspended the cursor is the editor's (visible, free, not fed to the game) and the game's mode
		// waits; lifting the suspension applies it again.
		void SetCursorModeSuspended(bool suspended);
		bool IsCursorModeSuspended() const { return m_CursorModeSuspended; }
		int GetAppliedCursorMode() const { return m_CursorModeSuspended ? 0 : m_CursorMode; }
		// Where a locked or hidden cursor sits, in window coordinates: the centre of the game area (the Game View in the editor).
		Vec2 GetCursorLockPoint() const;

		// Hotspot = click point in source-image pixels from the top-left. No-op on touch platforms.
		void SetUICursorImage(const Texture2D* tex2D, Vec2Int hotspot = {});
		void SetCursorOverUI(bool overUI);

		void SetCursorImage(const Texture2D* tex2D, Vec2Int hotspot = {});
		// Editor-only gate. When false, SetCursorImage / SetUICursorImage /
		// SetCursorOverUI keep their state but DON'T touch the OS cursor, so the
		// project's game cursor stays scoped to the Game View instead of leaking
		// editor-wide. Always true in standalone builds; the editor flips it true
		// only while the Game View is hovered/focused.
		void SetGameCursorEnabled(bool enabled);
		bool HasGameCursor() const { return m_DefaultCursor != nullptr; }
		uint64_t GetCursorTextureAsset() const { return m_CursorTextureAssetId; }
		void SetCursorTextureAsset(uint64_t assetId) { m_CursorTextureAssetId = assetId; }
		void SetWindowIcon(const Texture2D* tex2D);
		void SetWindowIcon(const ImageData& image);
		// Windows: the executable's embedded icon at this window's DPI. No-op elsewhere.
		void SetEmbeddedWindowIcon();

		// Info: If enabled equals true the window will become fullscreen else windowed
		void SetFullScreen(bool enabled);
		void SetDecorated(bool enabled);
		void SetVisible(bool enabled);
		void SetResizeable(bool enabled);
		void SetTitle(const std::string& title);

		void SetAspectLock(float aspect);
		float GetAspectLock() const { return m_AspectLock; }

		// MUST be called once after RenderApi::Init: no resize event fires on initial window creation, so frame 0 would miss the aspect-lock sub-rect without this.
		void ResyncViewportAfterRenderApiInit();

		std::string GetTitle() const;
		int GetWidth()  const { return s_MainViewport->GetWidth(); }
		int GetHeight() const { return s_MainViewport->GetHeight(); }
		// Desktop mode of the display the window is on; empty when no display is available.
		std::optional<DisplayMode> GetDisplayMode() const;
		SDL_Window* GetNativeWindow() const { return m_Window; }
		// HWND on Windows, NSWindow* on macOS, null elsewhere.
		void* GetPlatformHandle() const;
		float GetAspect() const { return s_MainViewport->GetAspect(); }
		Vec2Int GetSize() const { return s_MainViewport->GetSize(); }
		Vec2Int GetPosition() const;
		// Content scale of the window's display (1 = 96 DPI on Windows, 2 on Retina).
		float GetDisplayScale() const;

		std::string GetClipboardString() const;
		// Window coordinates; gameplay uses Input::GetMousePosition().
		Vec2 GetCursorPosition() const;

		// Desktop keeps text input on; touch platforms show the on-screen keyboard only while a field wants it.
		void SetTextInputActive(bool active);
		bool IsTextInputActive() const { return m_TextInputWanted; }

		enum class TextInputKind : uint8_t { Text, Integer, Decimal, Password };
		struct TextEditRequest {
			TextInputKind Kind = TextInputKind::Text;
			bool Multiline = false;
			// The edited field in UI-surface pixels (top-left origin); the OS keeps it clear of the keyboard.
			float Left = 0.0f, Top = 0.0f, Width = 0.0f, Height = 0.0f;
		};
		// Phones and tablets show the on-screen keyboard while a field is edited; no-ops elsewhere. owner
		// identifies the requester so another scene's EndTextEditing can't close a keyboard it didn't open.
		void BeginTextEditing(const TextEditRequest& request, const void* owner);
		void EndTextEditing(const void* owner);
		// The user closed the keyboard (back gesture, hide key) while editing was still requested.
		bool WasTextEditingDismissed(const void* owner) const;
		// Cut / Copy / Paste toolbar above a rect given like TextEditRequest (Platform::TextActionFlags). False on desktop.
		bool ShowTextActions(const TextEditRequest& area, int flags) const;

		bool IsMaximized() const;
		bool IsMinimized() const;
		bool IsFullScreen() const;
		bool IsVisible() const;
		bool IsDecorated() const;
		bool IsResizeable() const;
		bool IsFocused() const;
		bool ShouldClose() const { return m_ShouldClose; }
		void SetShouldClose(bool shouldClose) { m_ShouldClose = shouldClose; }

		bool IsCustomTitlebarEnabled() const { return m_CustomTitlebar; }
		int GetTitlebarHeight() const;
		void SetTitlebarHeight(int logicalPx);

		// Alpha = 0 means "no override"; applied via DWM on native chrome or read by the custom ImGui titlebar.
		Color GetTitlebarColor() const { return m_TitlebarColor; }
		void  SetTitlebarColor(Color color) { m_TitlebarColor = color; ApplyTitlebarAppearance(); }
		Color GetTitlebarTextColor() const { return m_TitlebarTextColor; }
		void  SetTitlebarTextColor(Color color) { m_TitlebarTextColor = color; ApplyTitlebarAppearance(); }
		Color GetTitlebarActiveColor() const { return m_TitlebarActiveColor; }
		void  SetTitlebarActiveColor(Color color) { m_TitlebarActiveColor = color; ApplyTitlebarAppearance(); }
		Color GetTitlebarInactiveColor() const { return m_TitlebarInactiveColor; }
		void  SetTitlebarInactiveColor(Color color) { m_TitlebarInactiveColor = color; ApplyTitlebarAppearance(); }
		bool IsTitlebarDarkMode() const { return m_TitlebarDarkMode; }
		void SetTitlebarDarkMode(bool enabled);

		static bool IsVsync() { return s_IsVsync; }

		// Info: If reset equals true the window will automatically be restored if it's already maximized
		void MaximizeWindow(bool reset = false);
		void MinimizeWindow();
		void RestoreWindow();

		void CenterWindow();
		void Focus();

		void SetEventCallback(const EventCallbackFn& callback) { m_EventCallback = callback; }

		static Window* GetActiveWindow() { return s_ActiveWindow; }
		static Viewport* GetMainViewport() { return s_MainViewport.get(); }

		// Ownership prevents runtime aspect resyncs from erasing the editor Game View region.
		enum class UIRegionSource : uint8_t {
			None,
			Editor,
			AspectLock
		};

		struct UIRegion {
			int OffsetX = 0;
			int OffsetY = 0;
			int Width = 0;
			int Height = 0;
			Vec2 WindowToRegionScale{ 1.0f, 1.0f };
			UIRegionSource Source = UIRegionSource::None;
			bool IsActive() const { return Width > 0 && Height > 0; }
			Vec2 ToLocalPosition(Vec2 windowPosition) const {
				return {
					windowPosition.x * WindowToRegionScale.x - static_cast<float>(OffsetX),
					windowPosition.y * WindowToRegionScale.y - static_cast<float>(OffsetY)
				};
			}
		};

		static UIRegion GetUIRegion() { return s_UIRegion; }
		// Bumped whenever the OS may take the native surface away (app sent to the background); a renderer
		// that sees a new value rebuilds its surface even if the OS reused the old window's address.
		static uint32_t GetNativeSurfaceGeneration() { return s_NativeSurfaceGeneration; }

		// Framebuffer-pixel distances from the edges of the UI surface that UI keeps clear of on phones
		// and tablets: notch / camera cutout, rounded corners, home indicator. Zero on desktop, Web and in
		// the editor's game view.
		struct SafeAreaInsets {
			float Left = 0.0f, Top = 0.0f, Right = 0.0f, Bottom = 0.0f;
			bool IsZero() const { return Left <= 0.0f && Top <= 0.0f && Right <= 0.0f && Bottom <= 0.0f; }
		};
		static SafeAreaInsets GetSafeAreaInsets();
		static void SetUIRegion(int x, int y, int w, int h, Vec2 windowToRegionScale = { 1.0f, 1.0f }) {
			s_UIRegion = UIRegion{ x, y, w, h, windowToRegionScale, UIRegionSource::Editor };
		}
		static void ClearUIRegion() { s_UIRegion = UIRegion{}; }
		static UIRegion ResolveAspectLockUIRegion(float aspect, UIRegion current, UIRegion candidate) {
			if (aspect > 0.0f) {
				candidate.Source = UIRegionSource::AspectLock;
				return candidate;
			}
			if (current.Source == UIRegionSource::AspectLock) {
				return {};
			}
			return current;
		}
		static void PublishAspectLockUIRegion(float aspect, UIRegion region) {
			s_UIRegion = ResolveAspectLockUIRegion(aspect, s_UIRegion, region);
		}
		static Vec2 CalculateWindowToFramebufferScale(Vec2Int windowSize, Vec2Int framebufferSize) {
			return {
				windowSize.x > 0 && framebufferSize.x > 0
					? static_cast<float>(framebufferSize.x) / static_cast<float>(windowSize.x) : 1.0f,
				windowSize.y > 0 && framebufferSize.y > 0
					? static_cast<float>(framebufferSize.y) / static_cast<float>(windowSize.y) : 1.0f
			};
		}
		static Vec2 MapToViewportLocalPosition(Vec2 windowPosition, const UIRegion& region,
			Vec2 windowToFramebufferScale) {
			if (region.IsActive()) {
				return region.ToLocalPosition(windowPosition);
			}
			return {
				windowPosition.x * windowToFramebufferScale.x,
				windowPosition.y * windowToFramebufferScale.y
			};
		}
		static Vec2 ToViewportLocalPosition(Vec2 windowPosition);

		static void SetTitlebarCaptionRect(int x, int y, int w, int h);
		static void AddTitlebarNonClientRect(int x, int y, int w, int h);
		static void ResetTitlebarNonClientRects();

	private:
		void Create(const WindowSpecification& props);
		void ApplyTitlebarAppearance();
		void SyncViewportFromFramebuffer();
		void UpdateViewport();
		// The surface is owned by the render backend; this presents through RenderApi::Present().
		void SwapBuffers() const;

		Vec2Int GetScreenCenter() const;
		Vec2 GetWindowCenter() const;
		void ApplyCursor(SDL_Cursor* cursor) const;
		void ApplyCursorMode();

		void HandleEvent(const SDL_Event& event);
		void OnFocusChanged(bool focused);
		void OnKey(int key, bool pressed, bool repeat);
		void OnMouseButton(int button, bool pressed);
		void OnCursorMoved(float x, float y);
		void OnFramebufferResized();
		static bool OnEventWatch(void* userData, SDL_Event* event);

		static Window* s_ActiveWindow;

		SDL_Window* m_Window = nullptr;
		SDL_Cursor* m_Cursor = nullptr;
		SDL_Cursor* m_DefaultCursor = nullptr;
		SDL_Cursor* m_UICursor = nullptr;
		bool m_CursorOverUI = false;
		// Runtime: always true. Editor: false except while over the Game View.
		bool m_GameCursorEnabled = true;
		int m_CursorMode = 0;
		bool m_CursorModeSuspended = false;
		uint64_t m_CursorTextureAssetId = 0;
		// Win32 HICONs at the exact big/small system sizes; SDL would hand Windows one image to scale.
		void* m_BigIcon = nullptr;
		void* m_SmallIcon = nullptr;
		void ApplyWin32Icons(void* bigHandle, void* smallHandle);
		bool m_ShouldClose = false;
		bool m_TextInputWanted = false;
		const void* m_TextEditOwner = nullptr;
		TextInputKind m_TextEditKind = TextInputKind::Text;
		bool m_TextEditMultiline = false;
		int m_TextEditArea[4] = { 0, 0, 0, 0 };
		bool m_TextEditKeyboardShown = false;
		bool m_TextEditDismissed = false;
		// Relative mouse mode reports deltas only; integrating them keeps Input's absolute-position model.
		Vec2 m_VirtualCursor{ 0.0f, 0.0f };
		std::vector<std::string> m_PendingDrops;

		Vec2Int m_MinSize;
		Vec2Int m_MaxSize;
		bool m_CustomTitlebar = false;
		float m_AspectLock = 0.0f;
		Color m_TitlebarColor         { 0.0f, 0.0f, 0.0f, 0.0f };
		Color m_TitlebarTextColor     { 0.0f, 0.0f, 0.0f, 0.0f };
		Color m_TitlebarActiveColor   { 0.0f, 0.0f, 0.0f, 0.0f };
		Color m_TitlebarInactiveColor { 0.0f, 0.0f, 0.0f, 0.0f };
		bool m_TitlebarDarkMode = true;

		static std::unique_ptr<Viewport> s_MainViewport;
		static UIRegion s_UIRegion;
		static Vec2 s_WindowToFramebufferScale;
		static bool s_IsVsync;
		static bool s_IsInitialized;
		static uint32_t s_NativeSurfaceGeneration;

		EventCallbackFn m_EventCallback;
	};
}
