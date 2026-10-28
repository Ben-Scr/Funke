#pragma once
#include "Scene/EntityHandle.hpp"

#include <cmath>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <utility>

namespace Funke {
	class DelayedEntityDestruction {
	public:
		DelayedEntityDestruction() = default;
		DelayedEntityDestruction(const DelayedEntityDestruction&) = delete;
		DelayedEntityDestruction& operator=(const DelayedEntityDestruction&) = delete;

		void Schedule(EntityHandle entity, float delay) {
			if (std::isnan(delay)) return;
			const double deadline = m_Time + delay;
			const auto found = m_ByEntity.find(entity);
			if (found != m_ByEntity.end()) {
				if (found->second->first <= deadline) return;
				auto entry = m_Deadlines.extract(found->second);
				entry.value().first = deadline;
				found->second = m_Deadlines.insert(std::move(entry)).position;
				return;
			}
			const auto entry = m_Deadlines.emplace(deadline, entity).first;
			try {
				m_ByEntity.emplace(entity, entry);
			}
			catch (...) {
				m_Deadlines.erase(entry);
				throw;
			}
		}

		void Cancel(EntityHandle entity) {
			const auto found = m_ByEntity.find(entity);
			if (found == m_ByEntity.end()) return;
			m_Deadlines.erase(found->second);
			m_ByEntity.erase(found);
		}

		void Advance(float deltaTime) {
			if (std::isfinite(deltaTime) && deltaTime > 0.0f) m_Time += deltaTime;
		}

		bool PopDue(EntityHandle& entity) {
			if (m_Deadlines.empty() || m_Deadlines.begin()->first > m_Time) return false;
			const auto first = m_Deadlines.begin();
			entity = first->second;
			m_ByEntity.erase(entity);
			m_Deadlines.erase(first);
			return true;
		}

		void Clear() {
			m_ByEntity.clear();
			m_Deadlines.clear();
			m_Time = 0.0;
		}

		size_t Size() const { return m_ByEntity.size(); }

	private:
		using Deadlines = std::set<std::pair<double, EntityHandle>>;
		Deadlines m_Deadlines;
		std::unordered_map<EntityHandle, Deadlines::iterator> m_ByEntity;
		double m_Time = 0.0;
	};
}
