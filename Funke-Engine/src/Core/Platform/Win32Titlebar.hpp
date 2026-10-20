#pragma once
#include "Collections/Color.hpp"
#include "Core/Export.hpp"

namespace Funke { class Window; }

// Windows keeps a WndProc subclass for the custom titlebar: SDL's hit-test callback has
// no answer for DWM's maximized-frame inset, the work-area clamp, or the 1px shadow margin.
namespace Funke::Win32Titlebar {

#ifdef FNK_PLATFORM_WINDOWS

    // hwnd is the window's HWND (Window::GetPlatformHandle()).
    void Install(void* hwnd, Window* owner);
    // MUST run before the window is destroyed — Windows can dispatch teardown messages into the subclass after state is gone.
    void Uninstall(void* hwnd);

    // Physical-pixel height for the given window (logical * DPI / 96).
    int  GetTitlebarHeightPhysical(void* hwnd);

    void SetDarkMode(void* hwnd, bool enabled);
    // Alpha 0 restores the DWM default for that slot. Exported for the editor's secondary viewport windows.
    FUNKE_API void SetColors(void* hwnd, Color caption, Color text, Color border);

#else

    inline void Install(void*, Window*) {}
    inline void Uninstall(void*) {}
    inline int  GetTitlebarHeightPhysical(void*) { return 0; }
    inline void SetDarkMode(void*, bool) {}
    inline void SetColors(void*, Color, Color, Color) {}

#endif

}
