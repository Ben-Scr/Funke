#pragma once

#include "Core/Layer.hpp"

#include <cstdint>

namespace Funke {

	// Owns the ImGui context for a Funke application. Push this Layer first (before
	// any other ImGui-using Layer) so its OnPreRender / OnPostRender wrap the per-frame
	// NewFrame / Render calls around all other Layers' UI work.
	class ImGuiContextLayer : public Layer {
	public:
		enum class Theme : std::uint8_t {
			Dark = 0,
			Light = 1,
		};

		using Layer::Layer;

		void OnAttach(Application& app) override;
		void OnDetach(Application& app) override;
		void OnPreRender(Application& app) override;
		void OnPostRender(Application& app) override;

		static Theme GetSystemTheme();
		static void ApplyFunkeTheme(Theme theme);

	private:
		bool m_IsInitialized = false;
	};

}
