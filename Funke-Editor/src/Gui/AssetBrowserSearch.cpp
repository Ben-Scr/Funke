#include "Gui/AssetBrowser.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Gui/EditorUI.hpp"
#include "Serialization/Path.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

namespace Funke {
	void AssetBrowser::RebuildSearchResults() {
		QueueSearchRebuild();
	}

	void AssetBrowser::QueueSearchRebuild() {
		ResetBoxSelection();
		m_SearchResults.clear();
		m_GridDirty = true;
		m_SearchTruncated = false;
		if (m_SearchBuffer[0] == '\0') {
			std::scoped_lock lock(m_SearchMutex);
			m_QueuedSearchQuery.clear();
			return;
		}

		std::string query(m_SearchBuffer);
		{
			std::scoped_lock lock(m_SearchMutex);
			m_QueuedSearchQuery = query;
		}
		if (!m_SearchWorkerActive.load(std::memory_order_acquire)) {
			StartSearchWorker(std::move(query));
		}
	}

	void AssetBrowser::StartSearchWorker(std::string query) {
		if (query.empty() || m_RootDirectory.empty()) {
			return;
		}
		if (m_SearchWorkerActive.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		if (m_SearchThread.joinable()) {
			m_SearchThread.join();
		}

		{
			std::scoped_lock lock(m_SearchMutex);
			m_SearchWorkerFinished = false;
			m_SearchWorkerTruncated = false;
			m_SearchWorkerQuery = query;
			m_SearchWorkerResults.clear();
		}

		try {
#if FNK_PLATFORM_HAS_THREADS
			m_SearchThread = std::thread(
				&AssetBrowser::RunSearchWorker,
				this,
				m_RootDirectory,
				std::move(query));
#else
			RunSearchWorker(m_RootDirectory, std::move(query));
#endif
		}
		catch (const std::exception& e) {
			m_SearchWorkerActive.store(false, std::memory_order_release);
			FNK_CORE_WARN_TAG("AssetBrowser", "Failed to start asset search worker: {}", e.what());
		}
	}

	void AssetBrowser::PumpSearchRebuild() {
		if (!m_SearchWorkerActive.load(std::memory_order_acquire)) {
			return;
		}

		bool finished = false;
		{
			std::scoped_lock lock(m_SearchMutex);
			finished = m_SearchWorkerFinished;
		}
		if (!finished) {
			return;
		}

		if (m_SearchThread.joinable()) {
			m_SearchThread.join();
		}

		std::string completedQuery;
		std::string queuedQuery;
		std::vector<DirectoryEntry> results;
		bool truncated = false;
		{
			std::scoped_lock lock(m_SearchMutex);
			completedQuery = m_SearchWorkerQuery;
			queuedQuery = m_QueuedSearchQuery;
			results = std::move(m_SearchWorkerResults);
			truncated = m_SearchWorkerTruncated;
			m_SearchWorkerFinished = false;
			m_SearchWorkerQuery.clear();
		}
		m_SearchWorkerActive.store(false, std::memory_order_release);

		if (std::string(m_SearchBuffer) == completedQuery) {
			ResetBoxSelection();
			m_SearchResults = std::move(results);
			m_GridDirty = true;
			m_SearchTruncated = truncated;
		}
		if (!queuedQuery.empty() && queuedQuery != completedQuery) {
			StartSearchWorker(std::move(queuedQuery));
		}
	}

	// Function-try-block: an exception escaping a std::thread body calls std::terminate.
	void AssetBrowser::RunSearchWorker(std::string rootDirectory, std::string queryText) try {
		std::vector<DirectoryEntry> results;
		bool truncated = false;

		constexpr std::size_t kMaxSearchResults = 1000;
		constexpr std::size_t kMaxEntriesExamined = 200000;

		const auto toLower = [](char c) -> char {
			return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
		};
		std::string query;
		for (char ch : queryText)
			query.push_back(toLower(ch));

		const auto matches = [&](const std::string& name) {
			if (query.size() > name.size()) return false;
			for (std::size_t i = 0; i + query.size() <= name.size(); ++i) {
				std::size_t j = 0;
				for (; j < query.size(); ++j)
					if (toLower(name[i + j]) != query[j]) break;
				if (j == query.size()) return true;
			}
			return false;
		};

		std::error_code ec;
		const std::filesystem::path root = Path::FromUtf8(rootDirectory);
		if (rootDirectory.empty() || !std::filesystem::exists(root, ec) || ec) {
			std::scoped_lock lock(m_SearchMutex);
			m_SearchWorkerResults.clear();
			m_SearchWorkerTruncated = false;
			m_SearchWorkerFinished = true;
			return;
		}

		std::size_t examined = 0;
		for (std::filesystem::recursive_directory_iterator
				it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
			it != end && !ec; it.increment(ec)) {
			if (!m_SearchWorkerActive.load(std::memory_order_acquire)) {
				return;
			}
			if (++examined > kMaxEntriesExamined) {
				truncated = true;
				break;
			}
			const std::filesystem::directory_entry& de = *it;

			DirectoryEntry e;
			e.Path = Path::ToUtf8(de.path());
			e.Name = Path::ToUtf8(de.path().filename());
			std::error_code dirEc;
			e.IsDirectory = de.is_directory(dirEc) && !dirEc;

			if (!e.IsDirectory && AssetRegistry::IsMetaFilePath(e.Path))
				continue;
			if (!matches(e.Name))
				continue;

			results.push_back(std::move(e));
			if (results.size() >= kMaxSearchResults) {
				truncated = true;
				break;
			}
		}

		std::sort(results.begin(), results.end(),
			[](const DirectoryEntry& a, const DirectoryEntry& b) {
				if (a.IsDirectory != b.IsDirectory) return a.IsDirectory > b.IsDirectory;
				return a.Name < b.Name;
			});

		{
			std::scoped_lock lock(m_SearchMutex);
			m_SearchWorkerResults = std::move(results);
			m_SearchWorkerTruncated = truncated;
			m_SearchWorkerFinished = true;
		}
	}
	catch (const std::exception& e) {
		FNK_CORE_ERROR_TAG("AssetBrowser", "Asset search failed: {}", e.what());
		std::scoped_lock lock(m_SearchMutex);
		m_SearchWorkerResults.clear();
		m_SearchWorkerTruncated = false;
		m_SearchWorkerFinished = true;
	}


	// Full-width row above the breadcrumb/refresh row.
	void AssetBrowser::RenderSearchBar() {
		if (EditorUI::SearchField("##AssetBrowserSearch", m_SearchBuffer, sizeof(m_SearchBuffer), "Search assets...")) {
			// A changed query reshuffles the visible list; drop the range-select
			// anchor so a later shift-click doesn't span stale indices, then
			// rebuild the recursive match set from the whole Assets/ tree.
			m_LastSelectionIndex = -1;
			RebuildSearchResults();
		}
	}
}
