#pragma once

#include "Core/Layer.hpp"
#include "Core/PlatformDetection.hpp"
#include "Diagnostics/StatsOverlay.hpp"

namespace Funke {

	// Requires showRuntimeStats; keyboard defaults live in PlatformShortcuts.
	class RuntimeStatsLayer : public Layer {
	public:
		using Layer::Layer; // inherit "explicit Layer(const std::string& name)"

		void OnAttach(Application& app) override;
		void OnDetach(Application& app) override;
		void OnUpdate(Application& app, float dt) override;
		void OnPreRender(Application& app) override;
		void OnPostRender(Application& app) override;

		bool IsVisible() const { return m_Show; }
		void SetVisible(bool v) { m_Show = v; }

	private:
		// Mobile has no F-keys to toggle, so the overlay starts visible there.
		bool m_Show = FNK_PLATFORM_MOBILE;
		bool m_ImGuiAcquired = false;
		Diagnostics::StatsOverlay m_Overlay;
	};

} // namespace Funke
