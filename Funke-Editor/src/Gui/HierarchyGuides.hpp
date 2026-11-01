#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace Funke {
	class HierarchyGuides {
	public:
		void Rebuild(std::span<const int> depths) {
			m_ContinueMasks.resize(depths.size());
			m_LastChildren.resize(depths.size());
			uint64_t siblingBelow = 0;
			for (size_t i = depths.size(); i-- > 0;) {
				const int depth = std::clamp(depths[i], 0, 63);
				const uint64_t level = uint64_t{ 1 } << depth;
				const uint64_t ancestors = level - 1;
				m_LastChildren[i] = (siblingBelow & level) == 0;
				m_ContinueMasks[i] = siblingBelow & ancestors & ~uint64_t{ 1 };
				siblingBelow = (siblingBelow & ancestors) | level;
			}
		}

		const auto& GetContinueMasks() const { return m_ContinueMasks; }
		const auto& GetLastChildren() const { return m_LastChildren; }

	private:
		std::vector<uint64_t> m_ContinueMasks;
		std::vector<bool> m_LastChildren;
	};
}
