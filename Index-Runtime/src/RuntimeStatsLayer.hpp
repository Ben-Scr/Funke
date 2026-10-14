#pragma once

#include "Core/Layer.hpp"
#include "Core/PlatformDetection.hpp"
#include "Diagnostics/StatsOverlay.hpp"

namespace Index {

	// Runtime stats overlay layer (showRuntimeStats=true). F6 toggles visibility; distinct from Ctrl+F6 (profiler) so both can be open simultaneously.
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
		bool m_Show = IDX_PLATFORM_MOBILE;
		bool m_ImGuiAcquired = false;
		Diagnostics::StatsOverlay m_Overlay;
	};

} // namespace Index
