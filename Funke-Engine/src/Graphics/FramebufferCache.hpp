#pragma once

#include "Graphics/Framebuffer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Funke {

	struct FramebufferDescription {
		int Width = 0;
		int Height = 0;
		TextureFormat Format = TextureFormat::RGBA16F;
		uint32_t Samples = 1;
		bool Depth = false;

		bool operator==(const FramebufferDescription&) const = default;
	};

	template<size_t Capacity = 2, typename TTarget = Framebuffer>
	class FramebufferCache {
		static_assert(Capacity > 0);
	public:
		TTarget* Acquire(const FramebufferDescription& description) {
			if (description.Width <= 0 || description.Height <= 0) return nullptr;
			for (auto& entry : m_Entries) {
				if (entry.Value.IsValid() && entry.Description == description) {
					entry.LastUse = ++m_UseCounter;
					return &entry.Value;
				}
			}
			auto& entry = *std::min_element(m_Entries.begin(), m_Entries.end(),
				[](const Entry& a, const Entry& b) { return a.LastUse < b.LastUse; });
			if (!entry.Value.Recreate(description.Width, description.Height, description.Format,
				TextureFilter::Linear, description.Samples, description.Depth)) {
				entry.LastUse = 0;
				return nullptr;
			}
			entry.Description = description;
			entry.LastUse = ++m_UseCounter;
			return &entry.Value;
		}

		void Clear() {
			for (auto& entry : m_Entries) {
				entry.Value.Destroy();
				entry.LastUse = 0;
			}
			m_UseCounter = 0;
		}

	private:
		struct Entry {
			TTarget Value;
			FramebufferDescription Description;
			uint64_t LastUse = 0;
		};
		std::array<Entry, Capacity> m_Entries;
		uint64_t m_UseCounter = 0;
	};

}
