#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Core/Platform/Win32BuildProgressWindow.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Serialization/Path.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>

namespace Funke {
	using namespace AssetBrowserInternal;

	namespace {
		std::uintmax_t SafeFileSize(const std::filesystem::path& path) {
			std::error_code ec;
			const std::uintmax_t size = std::filesystem::file_size(path, ec);
			return ec ? 0 : size;
		}

		void CollectImportByteSize(const std::filesystem::path& path, std::uintmax_t& totalBytes) {
			std::error_code ec;
			if (std::filesystem::is_regular_file(path, ec) && !ec) {
				totalBytes += SafeFileSize(path);
				return;
			}

			ec.clear();
			if (!std::filesystem::is_directory(path, ec) || ec) {
				return;
			}

			for (std::filesystem::recursive_directory_iterator it(
				 path,
				 std::filesystem::directory_options::skip_permission_denied,
				 ec), end;
				 it != end;
				 it.increment(ec)) {
				if (ec) {
					ec.clear();
					continue;
				}

				std::error_code fileEc;
				if (it->is_regular_file(fileEc) && !fileEc) {
					totalBytes += SafeFileSize(it->path());
				}
			}
		}

		std::filesystem::path MakeUniqueImportFilePath(
			const std::filesystem::path& source,
			const std::filesystem::path& destinationDirectory)
		{
			std::filesystem::path dest = destinationDirectory / source.filename();
			if (NormalizeSceneStem(dest.extension().string()) == ".scene") {
				return MakeGloballyUniqueScenePath(dest);
			}
			std::error_code ec;
			if (AssetFileHistory::DestinationAvailable(dest)) {
				return dest;
			}

			const std::string stem = dest.stem().string();
			const std::string ext = dest.extension().string();
			for (int counter = 1; counter < 10000; ++counter) {
				dest = destinationDirectory / (stem + " (" + std::to_string(counter) + ")" + ext);
				ec.clear();
				if (AssetFileHistory::DestinationAvailable(dest)) {
					return dest;
				}
			}

			return destinationDirectory / (stem + " (" + std::to_string(std::time(nullptr)) + ")" + ext);
		}

		bool CopyFileChunked(
			const std::filesystem::path& source,
			const std::filesystem::path& destination,
			bool skipExisting,
			const std::atomic_bool& keepRunning,
			const std::function<void(std::uintmax_t)>& onCopiedBytes)
		{
			(void)skipExisting;
			if (!keepRunning.load(std::memory_order_acquire)) return false;
			std::ifstream input(source, std::ios::binary);
			if (!input) return false;
			std::FILE* output = nullptr;
#ifdef FNK_PLATFORM_WINDOWS
			if (_wfopen_s(&output, destination.c_str(), L"wbx") != 0) return false;
#else
			output = std::fopen(destination.c_str(), "wbx");
			if (!output) return false;
#endif
			std::unique_ptr<std::FILE, decltype(&std::fclose)> file(output, &std::fclose);
			std::vector<char> buffer(1024 * 1024);
			bool succeeded = true;
			while (keepRunning.load(std::memory_order_acquire) && input) {
				input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				const auto count = input.gcount();
				if (count <= 0) break;
				if (std::fwrite(buffer.data(), 1, static_cast<size_t>(count), file.get()) != static_cast<size_t>(count)) {
					succeeded = false;
					break;
				}
				onCopiedBytes(static_cast<std::uintmax_t>(count));
			}
			succeeded = succeeded && !input.bad() && keepRunning.load(std::memory_order_acquire);
			if (std::fclose(file.release()) != 0) succeeded = false;
			if (!succeeded) {
				std::error_code error;
				std::filesystem::remove(destination, error);
			}
			return succeeded;
		}
	}

	void AssetBrowser::StartExternalAssetImport(std::vector<std::string> paths, std::string destinationDirectory) {
		if (paths.empty() || destinationDirectory.empty()) {
			return;
		}

		// Import and delete share the single Win32 progress popup and the
		// (unsynchronized) AssetRegistry, so they must not overlap. A delete already in
		// flight wins; the drop is ignored (same behaviour as dropping mid-import).
		if (m_DeleteActive.load(std::memory_order_acquire)) {
			FNK_CORE_WARN_TAG("AssetBrowser", "Asset deletion is in progress; ignoring {} dropped path(s).", paths.size());
			return;
		}

		if (m_ImportActive.exchange(true, std::memory_order_acq_rel)) {
			FNK_CORE_WARN_TAG("AssetBrowser", "Asset import is already running; ignoring {} dropped path(s).", paths.size());
			return;
		}

		if (m_ImportThread.joinable()) {
			m_ImportThread.join();
		}

		{
			std::scoped_lock lock(m_ImportMutex);
			m_ImportWorkerFinished = false;
			m_ImportResultsConsumed = true;
			m_ImportDestinationDirectory = destinationDirectory;
			m_ImportCurrentItem = "Preparing...";
			m_ImportTotalBytes = 0;
			m_ImportCopiedBytes = 0;
			m_ImportTotalRootItems = paths.size();
			m_ImportProcessedRootItems = 0;
			m_ImportImportedRootCount = 0;
			m_ImportImportedRootPaths.clear();
			m_ImportImportedAssetPaths.clear();
			m_ImportErrors.clear();
		}

		try {
			m_ImportThread = std::thread(
				&AssetBrowser::RunExternalAssetImport,
				this,
				std::move(paths),
				std::move(destinationDirectory));
		}
		catch (const std::exception& e) {
			m_ImportActive.store(false, std::memory_order_release);
			FNK_CORE_ERROR_TAG("AssetBrowser", "Failed to start asset import worker: {}", e.what());
		}
	}

	void AssetBrowser::RunExternalAssetImport(std::vector<std::string> paths, std::string destinationDirectory) {
		namespace fs = std::filesystem;

		// Boundary guard: a throw escaping this std::thread body calls std::terminate and kills the
		// editor. Most fs calls here use error_code, but allocation (the copy buffer, path strings) can
		// still throw — catch and publish a finished+error state so the import UI recovers instead.
		try {

		std::vector<std::string> importedRootPaths;
		std::vector<std::string> importedAssetPaths;
		std::vector<std::string> errors;
		std::size_t importedRootCount = 0;

		auto publishCurrentItem = [this](const std::string& item) {
			std::scoped_lock lock(m_ImportMutex);
			m_ImportCurrentItem = item;
		};

		auto publishCopiedBytes = [this](std::uintmax_t bytes) {
			std::scoped_lock lock(m_ImportMutex);
			m_ImportCopiedBytes += bytes;
		};

		auto publishProcessedRoot = [this](std::size_t processed) {
			std::scoped_lock lock(m_ImportMutex);
			m_ImportProcessedRootItems = processed;
		};

		auto appendError = [&errors](const fs::path& path, const std::string& message) {
			errors.push_back("Failed to import '" + path.string() + "': " + message);
		};

		publishCurrentItem("Scanning...");
		std::uintmax_t totalBytes = 0;
		for (const std::string& pathString : paths) {
			if (!m_ImportActive.load(std::memory_order_acquire)) {
				break;
			}

			std::error_code ec;
			const fs::path path(pathString);
			if (fs::exists(path, ec) && !ec) {
				CollectImportByteSize(path, totalBytes);
			}
		}

		{
			std::scoped_lock lock(m_ImportMutex);
			m_ImportTotalBytes = totalBytes;
		}

		const fs::path destDir(destinationDirectory);
		for (std::size_t i = 0; i < paths.size(); ++i) {
			if (!m_ImportActive.load(std::memory_order_acquire)) {
				break;
			}

			const fs::path src(paths[i]);
			publishCurrentItem(src.filename().empty() ? src.string() : src.filename().string());

			std::error_code ec;
			if (!fs::exists(src, ec) || ec) {
				appendError(src, ec ? ec.message() : "source path does not exist");
				publishProcessedRoot(i + 1);
				continue;
			}

			bool importedThisRoot = false;
			fs::path importedRootPath;

			if (fs::is_directory(src, ec) && !ec) {
				const fs::path destRoot = MakeUniqueImportFilePath(src, destDir);
				const bool created = !destRoot.empty() && AssetFileHistory::DestinationAvailable(destRoot) && fs::create_directory(destRoot, ec);
				if (!created || ec) {
					appendError(src, ec.message());
					publishProcessedRoot(i + 1);
					continue;
				}

				importedThisRoot = true;
				importedRootPath = destRoot;

				for (fs::recursive_directory_iterator it(
					 src,
					 fs::directory_options::skip_permission_denied,
					 ec), end;
					 it != end && m_ImportActive.load(std::memory_order_acquire);
					 it.increment(ec)) {
					if (ec) {
						ec.clear();
						continue;
					}

					std::error_code relEc;
					const fs::path rel = fs::relative(it->path(), src, relEc);
					if (relEc) {
						continue;
					}

					fs::path dest = destRoot / rel;
					std::error_code entryEc;
					if (it->is_directory(entryEc) && !entryEc) {
						fs::create_directories(dest, entryEc);
						if (entryEc) {
							appendError(it->path(), entryEc.message());
						}
						continue;
					}

					entryEc.clear();
					if (!it->is_regular_file(entryEc) || entryEc) {
						continue;
					}
					if (NormalizeSceneStem(dest.extension().string()) == ".scene") {
						dest = MakeGloballyUniqueScenePath(dest);
						if (dest.empty()) {
							appendError(it->path(), "could not choose a globally unique scene name");
							continue;
						}
					}

					if (fs::exists(dest, entryEc) && !entryEc) {
						publishCopiedBytes(SafeFileSize(it->path()));
						continue;
					}

					if (CopyFileChunked(it->path(), dest, false, m_ImportActive, publishCopiedBytes)) {
						AddImportedAssetPath(importedAssetPaths, dest);
					}
					else if (m_ImportActive.load(std::memory_order_acquire)) {
						appendError(it->path(), "copy failed");
					}
				}
			}
			else if (fs::is_regular_file(src, ec) && !ec) {
				if (AssetRegistry::IsMetaFilePath(src.string())) continue;
				const fs::path dest = MakeUniqueImportFilePath(src, destDir);
				if (dest.empty() || !AssetFileHistory::DestinationAvailable(dest)) {
					appendError(src, "could not choose a globally unique scene name");
					publishProcessedRoot(i + 1);
					continue;
				}
				if (CopyFileChunked(src, dest, false, m_ImportActive, publishCopiedBytes)) {
					importedThisRoot = true;
					importedRootPath = dest;
					AddImportedAssetPath(importedAssetPaths, dest);
					auto sourceMeta = src; sourceMeta += ".meta";
					auto destinationMeta = dest; destinationMeta += ".meta";
					if (fs::is_regular_file(sourceMeta, ec) && !ec) {
						CopyFileChunked(sourceMeta, destinationMeta, false, m_ImportActive, publishCopiedBytes);
					}
				}
				else if (m_ImportActive.load(std::memory_order_acquire)) {
					appendError(src, "copy failed");
				}
			}

			if (importedThisRoot) {
				++importedRootCount;
				importedRootPaths.push_back(importedRootPath.string());
			}

			publishProcessedRoot(i + 1);
		}

		{
			std::scoped_lock lock(m_ImportMutex);
			m_ImportCurrentItem = m_ImportActive.load(std::memory_order_acquire) ? "Copy complete" : "Cancelled";
			m_ImportImportedRootCount = importedRootCount;
			m_ImportImportedRootPaths = std::move(importedRootPaths);
			m_ImportImportedAssetPaths = std::move(importedAssetPaths);
			m_ImportErrors = std::move(errors);
			m_ImportWorkerFinished = true;
			m_ImportResultsConsumed = false;
		}
		}
		catch (const std::exception& e) {
			FNK_CORE_ERROR_TAG("AssetBrowser", "Asset import worker crashed: {}", e.what());
			std::scoped_lock lock(m_ImportMutex);
			m_ImportCurrentItem = "Import failed";
			m_ImportErrors.push_back(std::string("Import failed: ") + e.what());
			m_ImportWorkerFinished = true;
			m_ImportResultsConsumed = false;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("AssetBrowser", "Asset import worker crashed: unknown exception");
			std::scoped_lock lock(m_ImportMutex);
			m_ImportCurrentItem = "Import failed";
			m_ImportErrors.push_back("Import failed: unknown error");
			m_ImportWorkerFinished = true;
			m_ImportResultsConsumed = false;
		}
	}

	void AssetBrowser::ConsumeFinishedExternalAssetImport() {
		bool shouldConsume = false;
		{
			std::scoped_lock lock(m_ImportMutex);
			shouldConsume = m_ImportWorkerFinished && !m_ImportResultsConsumed;
		}

		if (!shouldConsume) {
			return;
		}

		if (m_ImportThread.joinable()) {
			m_ImportThread.join();
		}

		std::vector<std::string> errors;
		{
			std::scoped_lock lock(m_ImportMutex);
			m_PendingImportedAssetRegistrations = std::move(m_ImportImportedAssetPaths);
			m_PendingImportRootPaths = std::move(m_ImportImportedRootPaths);
			m_PendingImportRootCount = m_ImportImportedRootCount;
			m_PendingImportDestinationDirectory = m_ImportDestinationDirectory;
			m_PendingImportedAssetRegistrationIndex = 0;
			errors = std::move(m_ImportErrors);
			m_ImportResultsConsumed = true;
		}

		for (const std::string& error : errors) {
			FNK_CORE_WARN_TAG("AssetBrowser", "{}", error);
		}
	}

	void AssetBrowser::ProcessPendingImportedAssetRegistrations() {
		if (!m_ImportActive.load(std::memory_order_acquire)) {
			return;
		}

		const bool workerDone = [&]() {
			std::scoped_lock lock(m_ImportMutex);
			return m_ImportWorkerFinished && m_ImportResultsConsumed;
		}();

		if (!workerDone) {
			return;
		}

		const auto start = std::chrono::steady_clock::now();
		constexpr auto kBudget = std::chrono::milliseconds(4);
		std::size_t processedThisFrame = 0;

		while (m_PendingImportedAssetRegistrationIndex < m_PendingImportedAssetRegistrations.size()) {
			const std::string& path = m_PendingImportedAssetRegistrations[m_PendingImportedAssetRegistrationIndex];
			SyncSceneEmbeddedNameIfNeeded(path);
			AssetRegistry::ImportAssetPathIncremental(path);
			m_Thumbnails.Invalidate(path);
			++m_PendingImportedAssetRegistrationIndex;
			++processedThisFrame;

			if (processedThisFrame >= 1 && std::chrono::steady_clock::now() - start >= kBudget) {
				return;
			}
		}

		if (m_PendingImportRootCount > 0) {
			if (m_CurrentDirectory == m_PendingImportDestinationDirectory) {
				m_SelectedPaths = m_PendingImportRootPaths;
				m_SelectedPath = m_PendingImportRootPaths.empty() ? std::string{} : m_PendingImportRootPaths.back();
				m_LastSelectionIndex = -1;
			}
			m_SkipRegistrySyncOnNextRefresh = true;
			m_NeedsRefresh = true;
			m_SuppressNextAssetWatcherRefresh.store(true, std::memory_order_release);
			FNK_CORE_INFO_TAG("AssetBrowser",
				"Imported {} item(s) into {}",
				m_PendingImportRootCount,
				m_PendingImportDestinationDirectory);
		}

		if (!m_PendingImportRootPaths.empty()) {
			try {
				m_FileHistory.RecordCreated(m_PendingImportRootPaths, "Import Assets");
				m_AssetHistoryContext = true;
			}
			catch (const std::exception& error) { FNK_ERROR_TAG("AssetUndo", "Cannot record import: {}", error.what()); }
		}
		m_PendingImportedAssetRegistrations.clear();
		m_PendingImportRootPaths.clear();
		m_PendingImportedAssetRegistrationIndex = 0;
		m_PendingImportRootCount = 0;
		m_PendingImportDestinationDirectory.clear();
		m_ImportActive.store(false, std::memory_order_release);
	}

	void AssetBrowser::UpdateExternalAssetImportProgressWindow() {
		if (!m_ImportActive.load(std::memory_order_acquire)) {
			if (m_ImportProgressWindowVisible) {
				Win32BuildProgressWindow::Hide();
				m_ImportProgressWindowVisible = false;
			}
			return;
		}

		if (!m_ImportProgressWindowVisible) {
			Win32BuildProgressWindow::Show("Importing Assets...");
			m_ImportProgressWindowVisible = true;
		}

		float progress = 0.0f;
		std::string stage = "Preparing...";

		const bool workerDone = [&]() {
			std::scoped_lock lock(m_ImportMutex);
			return m_ImportWorkerFinished && m_ImportResultsConsumed;
		}();

		if (workerDone) {
			const std::size_t total = m_PendingImportedAssetRegistrations.size();
			if (total > 0) {
				progress = 0.90f + 0.10f * (
					static_cast<float>(m_PendingImportedAssetRegistrationIndex) /
					static_cast<float>(total));
				stage = "Indexing assets...";
			}
			else {
				progress = 1.0f;
				stage = "Done";
			}
		}
		else {
			std::uintmax_t totalBytes = 0;
			std::uintmax_t copiedBytes = 0;
			std::size_t totalRoots = 0;
			std::size_t processedRoots = 0;
			{
				std::scoped_lock lock(m_ImportMutex);
				totalBytes = m_ImportTotalBytes;
				copiedBytes = m_ImportCopiedBytes;
				totalRoots = m_ImportTotalRootItems;
				processedRoots = m_ImportProcessedRootItems;
				stage = m_ImportCurrentItem.empty() ? "Copying..." : m_ImportCurrentItem;
			}

			if (totalBytes > 0) {
				progress = 0.90f * std::min(
					1.0f,
					static_cast<float>(copiedBytes) / static_cast<float>(totalBytes));
			}
			else if (totalRoots > 0) {
				progress = 0.10f * (
					static_cast<float>(processedRoots) / static_cast<float>(totalRoots));
			}
		}

		Win32BuildProgressWindow::Update(progress, stage);
	}

	void AssetBrowser::PumpExternalAssetImport() {
		ConsumeFinishedExternalAssetImport();
		ProcessPendingImportedAssetRegistrations();
		UpdateExternalAssetImportProgressWindow();
	}

	void AssetBrowser::OnExternalFileDrop(const std::vector<std::string>& paths) {
		if (m_CurrentDirectory.empty()) return;
		if (paths.empty()) return;

		StartExternalAssetImport(paths, m_CurrentDirectory);
	}
}
