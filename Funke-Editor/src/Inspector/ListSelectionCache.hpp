#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Funke::PropertyDrawer {

	struct ListSelectionState {
		std::unordered_set<int> Selected;
		int LastClicked = -1;
	};

	class ListSelectionCache {
	public:
		void BeginFrame() {
			for (auto it = m_Entries.begin(); it != m_Entries.end();) {
				if (!it->second.Used) it = m_Entries.erase(it);
				else {
					it->second.Used = false;
					++it;
				}
			}
		}

		ListSelectionState& Get(const std::string& key) {
			Entry& entry = m_Entries[key];
			entry.Used = true;
			return entry.Selection;
		}

		void Clear() { decltype(m_Entries){}.swap(m_Entries); }
		std::size_t Size() const { return m_Entries.size(); }

	private:
		struct Entry {
			ListSelectionState Selection;
			bool Used = false;
		};
		std::unordered_map<std::string, Entry> m_Entries;
	};
}
