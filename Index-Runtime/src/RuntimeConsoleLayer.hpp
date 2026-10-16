#pragma once

#include "Core/Layer.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace Index {

	// Dev console window of Debug/Development builds: ` (the key left of 1) or a three-finger tap opens it,
	// Esc closes it; F8 toggles the physics debug overlay. Commands come from DevConsole.
	class RuntimeConsoleLayer : public Layer {
	public:
		using Layer::Layer;

		void OnAttach(Application& app) override;
		void OnDetach(Application& app) override;
		void OnUpdate(Application& app, float dt) override;
		void OnPreRender(Application& app) override;
		void OnPostRender(Application& app) override;

	private:
		void Toggle();
		void Submit();
		bool DetectThreeFingerTap() const;
		static int InputCallback(ImGuiInputTextCallbackData* data);

		bool m_ImGuiAcquired = false;
		bool m_FocusInput = false;
		bool m_CloseRequested = false;
		uint64_t m_ShownRevision = 0;
		char m_Buffer[512]{};
		std::vector<std::string> m_InputHistory;
		int m_HistoryCursor = -1;
		std::vector<std::string> m_CommandNames;
	};

} // namespace Index
