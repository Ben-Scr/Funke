#pragma once
#include "Core/Export.hpp"
#include <string>

// Build / import progress popup. Windows shows a real OS window (it stays responsive while the editor is busy);
// elsewhere these calls only record the state and BuildProgressOverlay (Index-EditorRuntime) draws it with ImGui.
namespace Index::Win32BuildProgressWindow {

    INDEX_API void Show(const std::string& title);

    // Pushes the latest progress (0..1) and stage label into the popup.
    // Cheap to call every frame — only invalidates the window if either
    // value actually changed.
    INDEX_API void Update(float progress, const std::string& stage);

    // Destroys the popup. Safe to call when not shown.
    INDEX_API void Hide();

    INDEX_API bool IsVisible();

#ifndef IDX_PLATFORM_WINDOWS
    struct State {
        bool Visible = false;
        std::string Title;
        std::string Stage;
        float Progress = 0.0f;
    };
    INDEX_API State GetState();
#endif

}
