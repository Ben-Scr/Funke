#include "RuntimeLogLayer.hpp"
#include "RuntimeImGuiHost.hpp"
#include "RuntimeStatsLayer.hpp"

#include "Core/Application.hpp"
#include "Core/PlatformShortcuts.hpp"
#include "Core/Window.hpp"
#include "Collections/Viewport.hpp"

#include <imgui.h>

namespace Funke {

	namespace {
		float s_LastStatsRenderedHeight = 0.0f;
	}

	void Internal_SetStatsRenderedHeight(float h) { s_LastStatsRenderedHeight = h; }

	void RuntimeLogLayer::OnAttach(Application& app) {
		m_ImGuiAcquired = RuntimeImGuiHost::Acquire(app.GetWindow());
		if (m_ImGuiAcquired) {
			m_Overlay = std::make_unique<Diagnostics::LogOverlay>();
		}
	}

	void RuntimeLogLayer::OnDetach(Application&) {
		m_Overlay.reset();
		if (m_ImGuiAcquired) {
			RuntimeImGuiHost::Release();
			m_ImGuiAcquired = false;
		}
	}

	void RuntimeLogLayer::OnUpdate(Application&, float) {
		if (!m_ImGuiAcquired || !RuntimeImGuiHost::IsInitialized()) return;
		if (PlatformShortcuts::IsPressed(PlatformShortcuts::k_Logs)) {
			m_Show = !m_Show;
		}
	}

	void RuntimeLogLayer::OnPreRender(Application&) {
		if (!m_ImGuiAcquired || !RuntimeImGuiHost::IsInitialized()) return;
		RuntimeImGuiHost::BeginFrame();

		if (!m_Show || !m_Overlay) return;

		float yOffset = 0.0f;
		if (s_LastStatsRenderedHeight > 0.0f) {
			yOffset = s_LastStatsRenderedHeight + 8.0f;
		}

		m_Overlay->RenderInMainViewport(yOffset);
	}

	void RuntimeLogLayer::OnPostRender(Application&) {
		if (!m_ImGuiAcquired || !RuntimeImGuiHost::IsInitialized()) return;
		RuntimeImGuiHost::EndFrame();
	}

} // namespace Funke
