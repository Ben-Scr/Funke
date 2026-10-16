#pragma once
#include "Assets/AssetRegistry.hpp"
#include "Serialization/FileStamp.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Index {

	// GUID -> shared immutable instance of a JSON asset. Failed loads are retried at most once per second; every
	// reload or store swaps the pointer so holders can detect changes by comparing it.
	template<typename T>
	class JsonAssetCache {
	public:
		using Loader = bool (*)(const std::string& path, T& out);

		explicit JsonAssetCache(Loader loader) : m_Loader(loader) {}

		std::shared_ptr<const T> Get(uint64_t guid) {
			if (guid == 0) return nullptr;
			auto it = m_Entries.find(guid);
			if (it == m_Entries.end()) {
				it = m_Entries.emplace(guid, Entry{}).first;
				Load(guid, it->second);
			}
			else if (!it->second.Value && std::chrono::steady_clock::now() - it->second.LastAttempt > std::chrono::seconds(1)) {
				Load(guid, it->second);
			}
			return it->second.Value;
		}

		void Store(uint64_t guid, const T& value, const std::string& path = {}) {
			Entry& entry = m_Entries[guid];
			entry.Value = std::make_shared<const T>(value);
			if (!path.empty()) entry.Path = path;
		}

		void Invalidate(uint64_t guid) { m_Entries.erase(guid); }

		size_t ReloadChangedFromDisk() {
			size_t reloaded = 0;
			for (auto& [guid, entry] : m_Entries) {
				if (entry.Path.empty() || !KnownFileStamps::HasChangedOnDisk(entry.Path)) continue;
				Load(guid, entry);
				++reloaded;
			}
			return reloaded;
		}

		void Clear() { m_Entries.clear(); }

	private:
		struct Entry {
			std::shared_ptr<const T> Value;
			std::string Path;
			std::chrono::steady_clock::time_point LastAttempt{};
		};

		void Load(uint64_t guid, Entry& entry) {
			entry.LastAttempt = std::chrono::steady_clock::now();
			entry.Path = AssetRegistry::ResolvePath(guid);
			T value{};
			if (!entry.Path.empty() && m_Loader(entry.Path, value)) entry.Value = std::make_shared<const T>(std::move(value));
			else entry.Value.reset();
		}

		Loader m_Loader;
		std::unordered_map<uint64_t, Entry> m_Entries;
	};

}
