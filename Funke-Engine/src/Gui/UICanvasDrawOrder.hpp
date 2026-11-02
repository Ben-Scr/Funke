#pragma once
#include "Gui/UIDrawOrder.hpp"

#include <map>

namespace Funke::UIDrawOrder {

	class CanvasOrders {
	public:
		void Rebuild(entt::registry& registry) {
			m_Order.clear();
			Build(registry, m_Order);
			for (auto& [root, order] : m_Canvases) order.clear();
			for (const auto& entry : m_Order)
				m_Canvases[WorldUI::FindRoot(registry, entry.first)].push_back(entry);
			std::erase_if(m_Canvases, [](const auto& canvas) { return canvas.second.empty(); });
		}

		const auto& GetCanvases() const { return m_Canvases; }

	private:
		std::vector<std::pair<EntityHandle, int>> m_Order;
		// Keep each canvas's allocation while preserving the renderer's root ordering.
		std::map<EntityHandle, std::vector<std::pair<EntityHandle, int>>> m_Canvases;
	};
}
