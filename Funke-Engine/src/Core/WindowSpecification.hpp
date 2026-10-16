#pragma once

#include "Collections/Color.hpp"

#include <cstdint>
#include <string>

namespace Funke {

	// Values are stable on disk (mapped in project.json) — do not reorder, append only.
	enum class FullscreenMode : uint8_t {
		// True exclusive fullscreen at the desktop mode, lowest latency, expensive Alt-Tab restore. Platforms without exclusive modes (Wayland, mobile, Web) get desktop fullscreen.
		Exclusive = 0,
		// Decorated window sized to the monitor's video mode. Same look as
		// Exclusive to the player but Alt-Tab is instant and the OS
		// compositor stays in charge. Common default for desktop games.
		BorderlessWindowed = 1,
		Maximized = 2,
	};

	// Which way up a phone or tablet game may be held. Left/Right name the side the top of the device
	// points to, the same on Android and iOS. Values are stable on disk — do not reorder, append only.
	enum class ScreenOrientation : uint8_t {
		Auto = 0,                  // follows the window size: landscape when wider than tall
		Landscape = 1,             // landscape, either way round (turns with the device)
		LandscapeLeft = 2,         // device turned counter-clockwise, top on the left
		LandscapeRight = 3,        // device turned clockwise, top on the right
		Portrait = 4,              // upright only
		PortraitUpsideDown = 5,    // upside down only
		PortraitAny = 6,           // upright or upside down
		AnyExceptUpsideDown = 7,   // landscape either way or upright portrait
		Any = 8,                   // all four
	};

	struct WindowSpecification {
		int Width{ 800 }, Height{ 800 };
		// Resize limits; 0 means "no constraint".
		int MinWidth{ 0 }, MinHeight{ 0 };
		int MaxWidth{ 0 }, MaxHeight{ 0 };
		// >0: start size is this fraction of the primary monitor's work area, so the window
		// scales from 1080p to 8K. Width:Height then only sets the aspect (and the fallback size).
		float MonitorFraction{ 0.0f };
		// >0: min size as a fraction of the work area at the same aspect; overrides MinWidth/MinHeight.
		float MinMonitorFraction{ 0.0f };
		std::string Title{ "Funke" };
		bool Resizeable{ true };
		bool Decorated{ true };
		bool Fullscreen{ false };
		// Legacy: superseded by FullscreenMode. Windowed=true with FullscreenMode=Exclusive still routes to the Maximized path for backwards compat.
		bool Windowed{ false };
		// Selected only when Fullscreen=true. Ignored otherwise.
		Funke::FullscreenMode FullscreenMode{ Funke::FullscreenMode::Exclusive };
		// Phones and tablets only; desktop and Web windows ignore it.
		Funke::ScreenOrientation Orientation{ Funke::ScreenOrientation::Auto };
		Color ClearColor;

		bool CustomTitlebar{ false };
		// Logical-pixel height of the custom titlebar row; DPI-scaled at
		// query time. Ignored when CustomTitlebar is false.
		int  TitlebarHeight{ 32 };
		Color TitlebarColor          { 0.0f, 0.0f, 0.0f, 0.0f };
		Color TitlebarTextColor      { 0.0f, 0.0f, 0.0f, 0.0f };
		Color TitlebarActiveColor    { 0.0f, 0.0f, 0.0f, 0.0f };
		Color TitlebarInactiveColor  { 0.0f, 0.0f, 0.0f, 0.0f };
		// Opt into the Windows immersive dark-mode titlebar controls via
		// DWMWA_USE_IMMERSIVE_DARK_MODE. Win10 1809+ only - silently no-ops
		// on older builds.
		bool TitlebarDarkMode{ true };

		float AspectLock{ 0.0f };

		WindowSpecification() = default;

		WindowSpecification(int width, int height, const std::string& title)
			: Width{ width }, Height{ height }, Title{ title } {
		}

		WindowSpecification(int width, int height, const std::string& title, bool resizeable, bool decorated, bool fullscreen)
			: Width{ width }, Height{ height }, Title{ title }, Resizeable{ resizeable }, Decorated{ decorated }, Fullscreen{ fullscreen } {
		}

		WindowSpecification(int width, int height, const std::string& title, bool resizeable, bool decorated, bool fullscreen, bool windowed)
			: Width{ width }, Height{ height }, Title{ title }, Resizeable{ resizeable }, Decorated{ decorated }, Fullscreen{ fullscreen }, Windowed{windowed} {
		}
	};

} // namespace Funke
