#include "pch.hpp"
#include "Win32Titlebar.hpp"
#include "TitlebarRegions.hpp"

#ifdef FNK_PLATFORM_WINDOWS

#include <windowsx.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>

namespace Funke::Win32Titlebar {

    namespace {

        struct State {
            WNDPROC OriginalWndProc = nullptr;
            Window* Owner = nullptr;
        };

        std::unordered_map<HWND, State>& StateMap() {
            static std::unordered_map<HWND, State> s_Map;
            return s_Map;
        }

        int DpiScale(int logical, UINT dpi) {
            return MulDiv(logical, static_cast<int>(dpi), 96);
        }

        COLORREF ToColorRef(Color color) {
            const Color c = color.Clamped();
            const auto toByte = [](float value) -> BYTE {
                return static_cast<BYTE>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            };
            return RGB(toByte(c.r), toByte(c.g), toByte(c.b));
        }

        void SetColorAttribute(HWND hwnd, DWORD attribute, Color color) {
            constexpr COLORREF k_DwmDefaultColor = 0xFFFFFFFF;
            COLORREF value = color.a > 0.0f ? ToColorRef(color) : k_DwmDefaultColor;
            DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
        }

        LRESULT ToNcHit(TitlebarRegions::Hit hit) {
            using TitlebarRegions::Hit;
            switch (hit) {
                case Hit::Caption:           return HTCAPTION;
                case Hit::ResizeTopLeft:     return HTTOPLEFT;
                case Hit::ResizeTop:         return HTTOP;
                case Hit::ResizeTopRight:    return HTTOPRIGHT;
                case Hit::ResizeRight:       return HTRIGHT;
                case Hit::ResizeBottomRight: return HTBOTTOMRIGHT;
                case Hit::ResizeBottom:      return HTBOTTOM;
                case Hit::ResizeBottomLeft:  return HTBOTTOMLEFT;
                case Hit::ResizeLeft:        return HTLEFT;
                case Hit::Client:            break;
            }
            return HTCLIENT;
        }

        LRESULT CALLBACK TitlebarWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
            auto& map = StateMap();
            auto it = map.find(hwnd);
            if (it == map.end()) {
                // State already torn down (Uninstall raced with a queued
                // message). Defer to the default handler — better than a
                // null-deref.
                return DefWindowProcW(hwnd, msg, wparam, lparam);
            }
            WNDPROC originalProc = it->second.OriginalWndProc;

            switch (msg) {
            case WM_NCCALCSIZE: {
                if (wparam == TRUE) {
                    // Maximized: inset by frame+padding to keep window inside the monitor work area.
                    if (IsZoomed(hwnd)) {
                        NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                        const UINT dpi = GetDpiForWindow(hwnd);
                        const int frameX = GetSystemMetricsForDpi(SM_CXFRAME, dpi);
                        const int frameY = GetSystemMetricsForDpi(SM_CYFRAME, dpi);
                        const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                        params->rgrc[0].left   += frameX + padding;
                        params->rgrc[0].top    += frameY + padding;
                        params->rgrc[0].right  -= frameX + padding;
                        params->rgrc[0].bottom -= frameY + padding;
                    }
                    return 0;
                }
                break;
            }
            case WM_NCHITTEST: {
                const POINT cursor{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                RECT window;
                GetWindowRect(hwnd, &window);
                const UINT dpi = GetDpiForWindow(hwnd);
                const bool resizable = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0;
                return ToNcHit(TitlebarRegions::HitTest(
                    cursor.x - window.left, cursor.y - window.top,
                    window.right - window.left, window.bottom - window.top,
                    MulDiv(8, static_cast<int>(dpi), 96), resizable, IsZoomed(hwnd) != FALSE));
            }
            case WM_GETMINMAXINFO: {
                // SDL first, for the window's min/max size limits.
                CallWindowProcW(originalProc, hwnd, msg, wparam, lparam);
                // Without WS_CAPTION the default sizing covers the full monitor; constrain to rcWork so maximize respects the taskbar.
                HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(monitor, &mi)) {
                    MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
                    mmi->ptMaxPosition.x = mi.rcWork.left   - mi.rcMonitor.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top    - mi.rcMonitor.top;
                    mmi->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
                    mmi->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
                }
                return 0;
            }
            case WM_NCPAINT:
                // We own all painting — never let the default frame paint.
                return 0;
            case WM_NCACTIVATE:
                // Returning TRUE suppresses the inactive-frame repaint
                // flicker that DWM otherwise does on focus change.
                return TRUE;
            case WM_SIZE: {
                // Re-apply DWM margins on restore; some Windows builds drop them across minimize/restore.
                if (wparam == SIZE_RESTORED) {
                    MARGINS m{ 0, 0, 0, 1 };
                    DwmExtendFrameIntoClientArea(hwnd, &m);
                }
                break;
            }
            default:
                break;
            }

            return CallWindowProcW(originalProc, hwnd, msg, wparam, lparam);
        }

    }

    void Install(void* nativeHandle, Window* owner) {
        HWND hwnd = static_cast<HWND>(nativeHandle);
        if (!hwnd) return;

        auto& map = StateMap();
        if (map.find(hwnd) != map.end()) {
            FNK_CORE_WARN_TAG("Win32Titlebar", "Install called twice for HWND {}", static_cast<void*>(hwnd));
            return;
        }

        State state;
        state.Owner = owner;
        state.OriginalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&TitlebarWndProc)));
        if (!state.OriginalWndProc) {
            FNK_CORE_ERROR_TAG("Win32Titlebar",
                "SetWindowLongPtrW failed (GetLastError={})", GetLastError());
            return;
        }
        map.emplace(hwnd, state);

        MARGINS margins{ 0, 0, 0, 1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        // SWP_FRAMECHANGED forces WM_NCCALCSIZE immediately; without it the NC strip is deferred to the next user resize.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Uninstall(void* nativeHandle) {
        HWND hwnd = static_cast<HWND>(nativeHandle);
        if (!hwnd) return;

        auto& map = StateMap();
        auto it = map.find(hwnd);
        if (it == map.end()) return;

        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(it->second.OriginalWndProc));
        map.erase(it);
    }

    int GetTitlebarHeightPhysical(void* nativeHandle) {
        HWND hwnd = static_cast<HWND>(nativeHandle);
        if (!hwnd) return TitlebarRegions::GetHeight();
        return DpiScale(TitlebarRegions::GetHeight(), GetDpiForWindow(hwnd));
    }

    void SetDarkMode(void* nativeHandle, bool enabled) {
        HWND hwnd = static_cast<HWND>(nativeHandle);
        if (!hwnd) return;
        constexpr DWORD k_DwmwaUseImmersiveDarkMode = 20;
        BOOL useDark = enabled ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, k_DwmwaUseImmersiveDarkMode, &useDark, sizeof(useDark));
    }

    void SetColors(void* nativeHandle, Color caption, Color text, Color border) {
        HWND hwnd = static_cast<HWND>(nativeHandle);
        if (!hwnd) return;

        constexpr DWORD k_DwmwaBorderColor = 34;
        constexpr DWORD k_DwmwaCaptionColor = 35;
        constexpr DWORD k_DwmwaTextColor = 36;
        SetColorAttribute(hwnd, k_DwmwaCaptionColor, caption);
        SetColorAttribute(hwnd, k_DwmwaTextColor, text);
        SetColorAttribute(hwnd, k_DwmwaBorderColor, border);
    }

}

#endif
