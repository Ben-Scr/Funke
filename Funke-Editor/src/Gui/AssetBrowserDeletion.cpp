#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Core/Platform/Win32BuildProgressWindow.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Serialization/Path.hpp"
#include <imgui.h>
#include <algorithm>
#include <stdexcept>

namespace Funke {
	using namespace AssetBrowserInternal;

	void AssetBrowser::DeleteEntry(const std::string& path) {
		auto edit = BeginFileEdit({ path }, "Delete Assets");
		if (!edit) return;
		// Defer thumbnail invalidation: see m_PendingThumbnailInvalidates docs.
		// The file itself is deleted now so on-disk state is consistent before
		// Render() returns; only the GPU-bound Texture2D destruction waits.
		m_PendingThumbnailInvalidates.push_back(path);
		const std::filesystem::path nativeMirrorPath = ResolveNativeScriptMirrorPath(path);
		const std::filesystem::path pathFs(path);
		const bool wasSceneFile = pathFs.extension() == ".scene";
		const std::string sceneStem = wasSceneFile ? pathFs.stem().string() : std::string{};
		std::vector<std::string> deletedAssetPaths;
		CollectRegistryAssetPaths(pathFs, deletedAssetPaths);

		if (Directory::Delete(path)) {
			AssetRegistry::DeleteCompanionMetadata(path);
			ForgetRegistryAssetPaths(deletedAssetPaths);
			EditorScriptDiscovery::MarkDirty();
			if (!nativeMirrorPath.empty()) {
				std::error_code ec;
				std::filesystem::remove(nativeMirrorPath, ec);
			}

			if (m_SelectedPath == path) {
				m_SelectedPath.clear();
			}
			m_SelectedPaths.erase(
				std::remove(m_SelectedPaths.begin(), m_SelectedPaths.end(), path),
				m_SelectedPaths.end());
			if (m_PressedPath == path) {
				m_PressedPath.clear();
			}
			CancelRename();
			m_NeedsRefresh = true;

			// Drop the SceneDefinition so a script-side LoadScene of the
			// deleted name can't resurrect an empty Scene from the registered
			// definition.
			if (wasSceneFile && !sceneStem.empty()) {
				SceneManager::UnregisterScene(sceneStem);
			}
		}
		FinishFileEdit(edit);
	}

	void AssetBrowser::RequestDeleteSelectedAssets() {
		const std::vector<std::string> paths = GetSelectedPaths();
		if (paths.empty()) {
			return;
		}

		// Confirmation disabled in preferences: delete straight away.
		if (!EditorPreferences::GetConfirmOnDeleteAsset()) {
			DeleteSelectedAssets();
			return;
		}

		m_PendingDeletePaths = paths;
		m_OpenDeletePromptThisFrame = true;
	}

	void AssetBrowser::RenderDeleteConfirmationPrompt() {
		constexpr const char* k_DeleteId = "Delete Asset###AssetBrowserDelete";

		if (m_OpenDeletePromptThisFrame) {
			ImGui::OpenPopup(k_DeleteId);
			m_OpenDeletePromptThisFrame = false;
		}

		if (m_PendingDeletePaths.empty()) return;

		// Same native-dialog centering as RenderCreationCollisionPrompt so the
		// prompt isn't trapped inside the editor window.
		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();

		if (ImGuiDialog::BeginModal(k_DeleteId, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			if (m_PendingDeletePaths.size() == 1) {
				const std::string name = std::filesystem::path(m_PendingDeletePaths.front()).filename().string();
				ImGuiDialog::Message("##DeleteAsset", ("Delete \"" + name + "\"?").c_str());
			}
			else {
				ImGui::TextWrapped("Delete %zu selected items?", m_PendingDeletePaths.size());
			}
			ImGui::Spacing();
			ImGui::TextDisabled("You can undo this while the project remains open.");

			bool decided = false;
			// Cancel is the accent default so the safe choice reads first; Delete keeps the destructive style.
			const int choice = EditorUI::DialogFooter({ "Delete", "Cancel" }, /*primary=*/1, /*danger=*/0);
			if (choice == 0) {
				StartAssetDeletion(m_PendingDeletePaths);
				decided = true;
			}
			else if (choice == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				decided = true;
			}

			if (decided) {
				m_PendingDeletePaths.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void AssetBrowser::StartAssetDeletion(std::vector<std::string> paths) {
		if (paths.empty()) {
			return;
		}

		// Registry publication and the progress window have a single owner.

		if (IsImportingAssets() || IsDeleting()) return;

		// A finished-but-not-yet-reaped worker can linger if the editor closed the
		// progress flow early; drain it before reusing the thread handle.
		if (m_DeleteThread.joinable()) {
			m_DeleteThread.join();
		}

		// Main-thread, pre-spawn bookkeeping. The actual filesystem removal is the slow
		// part and runs on the worker; everything that needs the files to still exist
		// (native-mirror resolution) or touches render/UI state happens here, and the
		// registry/scene/selection cleanup is deferred to the pump once the worker
		// reports which roots actually got deleted.
		std::vector<DeleteRootInfo> roots;
		roots.reserve(paths.size());
		for (const std::string& path : paths) {
			// Defer thumbnail invalidation to next frame's Render() exactly as the sync
			// DeleteEntry did — destroying the GPU texture now would dangle a
			// WGPUTextureView mid-frame (see m_PendingThumbnailInvalidates docs).
			m_PendingThumbnailInvalidates.push_back(path);

			DeleteRootInfo info;
			info.Path = path;
			std::error_code ec;
			info.IsDirectory = std::filesystem::is_directory(path, ec) && !ec;

			const std::filesystem::path mirror = ResolveNativeScriptMirrorPath(path);
			info.NativeMirrorPath = mirror.empty() ? std::string{} : mirror.string();

			const std::filesystem::path pathFs(path);
			if (!info.IsDirectory && pathFs.extension() == ".scene") {
				info.SceneStem = pathFs.stem().string();
			}

			roots.push_back(std::move(info));
		}

		// A pending rename targeting one of these paths would otherwise resolve against
		// a vanished file; cancel it now (matches the old sync DeleteEntry).
		CancelRename();

		{
			std::scoped_lock lock(m_DeleteMutex);
			m_DeleteWorkerFinished = false;
			m_DeleteResultsConsumed = true;
			m_DeleteCurrentItem = "Preparing...";
			m_DeleteForgetAssetPaths.clear();
			m_DeleteSucceededRoots.clear();
			m_DeleteErrors.clear();
		}
		// m_DeleteRoots / m_DeleteStartTime need no atomics or lock: the std::thread
		// constructor below establishes a happens-before edge, so every write here is
		// visible to the worker, and the worker is join()ed before they are touched
		// again (pump / next Start). m_DeleteStartTime is then only ever read on the
		// main thread.
		m_DeleteFileEdit.reset();
		m_DeleteRoots = std::move(roots);
		m_DeleteTotalEntries.store(0, std::memory_order_release);
		m_DeleteProcessedEntries.store(0, std::memory_order_release);
		m_DeleteStartTime = std::chrono::steady_clock::now();
		m_DeleteActive.store(true, std::memory_order_release);

		try {
			m_DeleteThread = std::thread(&AssetBrowser::RunAssetDeletion, this);
		}
		catch (const std::exception& e) {
			m_DeleteActive.store(false, std::memory_order_release);
			FNK_CORE_ERROR_TAG("AssetBrowser", "Failed to start asset deletion worker: {}", e.what());
			// Worker never launched — complete the user's action synchronously.
			std::vector<DeleteRootInfo> fallback = std::move(m_DeleteRoots);
			m_DeleteRoots.clear();
			for (const DeleteRootInfo& info : fallback) {
				DeleteEntry(info.Path);
			}
		}
	}

	void AssetBrowser::RunAssetDeletion() {
		namespace fs = std::filesystem;

		// Boundary guard: a throw escaping this std::thread body calls std::terminate
		// and kills the editor. fs calls use error_code, but allocation (the path
		// vectors) can still throw — catch and publish a finished+error state so the
		// deletion UI recovers instead.
		try {

		std::vector<std::string> undoPaths;
		for (const auto& root : m_DeleteRoots) {
			undoPaths.push_back(root.Path);
			if (!root.NativeMirrorPath.empty()) undoPaths.push_back(root.NativeMirrorPath);
		}
		m_DeleteFileEdit = m_FileHistory.Begin(undoPaths, "Delete Assets");

		auto publishCurrentItem = [this](const std::string& item) {
			std::scoped_lock lock(m_DeleteMutex);
			m_DeleteCurrentItem = item;
		};

		// --- Pass 1: enumerate every entry up front so the progress bar is determinate. ---
		struct RootPlan {
			const DeleteRootInfo* Info = nullptr;
			std::vector<fs::path> Removal;        // ordered children-before-parents
			std::vector<std::string> AssetPaths;  // non-.meta assets to forget on success
		};

		publishCurrentItem("Scanning...");
		std::vector<RootPlan> plans;
		plans.reserve(m_DeleteRoots.size());
		std::size_t totalEntries = 0;

		for (const DeleteRootInfo& info : m_DeleteRoots) {
			if (!m_DeleteActive.load(std::memory_order_acquire)) {
				break;
			}

			RootPlan plan;
			plan.Info = &info;

			std::error_code ec;
			if (info.IsDirectory) {
				std::vector<fs::path> descendants;
				for (fs::recursive_directory_iterator it(
					 info.Path, fs::directory_options::skip_permission_denied, ec), end;
					 it != end; it.increment(ec)) {
					// Bail a huge scan on shutdown (Shutdown sets m_DeleteActive=false).
					if (!m_DeleteActive.load(std::memory_order_acquire)) {
						break;
					}
					if (ec) { ec.clear(); continue; }

					// Never descend INTO a symlink/junction directory: it can point back
					// up the tree and spin the iterator into unbounded growth. The link
					// entry itself is still recorded below so the link is removed.
					std::error_code symEc;
					if (it->is_symlink(symEc)) {
						it.disable_recursion_pending();
					}

					descendants.push_back(it->path());

					std::error_code fileEc;
					if (it->is_regular_file(fileEc) && !fileEc
						&& !AssetRegistry::IsMetaFilePath(it->path().string())) {
						plan.AssetPaths.push_back(it->path().string());
					}
				}
				// recursive_directory_iterator is pre-order (parent before children);
				// reversing it removes children before their parent. The root directory
				// itself is appended last so it is empty by the time we remove it.
				plan.Removal.assign(descendants.rbegin(), descendants.rend());
				plan.Removal.emplace_back(info.Path);
			}
			else {
				plan.Removal.emplace_back(info.Path);
				if (!AssetRegistry::IsMetaFilePath(info.Path)) {
					plan.AssetPaths.push_back(info.Path);
				}
			}

			totalEntries += plan.Removal.size();
			plans.push_back(std::move(plan));
		}

		m_DeleteTotalEntries.store(totalEntries, std::memory_order_release);

		// --- Pass 2: remove entries bottom-up, publishing progress per entry. ---
		std::vector<std::string> forgetAssetPaths;
		std::vector<std::string> succeededRoots;
		std::vector<std::string> errors;

		for (const RootPlan& plan : plans) {
			if (!m_DeleteActive.load(std::memory_order_acquire)) {
				break;
			}

			const fs::path rootPath(plan.Info->Path);
			publishCurrentItem(rootPath.filename().empty()
				? plan.Info->Path : rootPath.filename().string());

			for (const fs::path& entry : plan.Removal) {
				if (!m_DeleteActive.load(std::memory_order_acquire)) {
					break;
				}
				std::error_code ec;
				fs::remove(entry, ec);
				m_DeleteProcessedEntries.fetch_add(1, std::memory_order_acq_rel);
			}

			if (!plan.Info->NativeMirrorPath.empty()) {
				std::error_code ec;
				fs::remove(plan.Info->NativeMirrorPath, ec);
			}

			// Treat the root as deleted only if it is provably gone: fs::exists returns
			// false on error too, so without the !existsEc guard a stat failure on a
			// still-present folder would wrongly forget its assets from the registry.
			std::error_code existsEc;
			const bool stillExists = fs::exists(plan.Info->Path, existsEc);
			if (!stillExists && !existsEc) {
				succeededRoots.push_back(plan.Info->Path);
				forgetAssetPaths.insert(forgetAssetPaths.end(),
					plan.AssetPaths.begin(), plan.AssetPaths.end());
			}
			else {
				errors.push_back("Failed to fully delete '" + plan.Info->Path + "'");
			}
		}

		{
			std::scoped_lock lock(m_DeleteMutex);
			m_DeleteCurrentItem = m_DeleteActive.load(std::memory_order_acquire) ? "Delete complete" : "Cancelled";
			m_DeleteForgetAssetPaths = std::move(forgetAssetPaths);
			m_DeleteSucceededRoots = std::move(succeededRoots);
			m_DeleteErrors = std::move(errors);
			m_DeleteWorkerFinished = true;
			m_DeleteResultsConsumed = false;
		}

		}
		catch (const std::exception& e) {
			FNK_CORE_ERROR_TAG("AssetBrowser", "Asset deletion worker crashed: {}", e.what());
			std::scoped_lock lock(m_DeleteMutex);
			m_DeleteCurrentItem = "Delete failed";
			m_DeleteErrors.push_back(std::string("Delete failed: ") + e.what());
			m_DeleteWorkerFinished = true;
			m_DeleteResultsConsumed = false;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("AssetBrowser", "Asset deletion worker crashed: unknown exception");
			std::scoped_lock lock(m_DeleteMutex);
			m_DeleteCurrentItem = "Delete failed";
			m_DeleteErrors.push_back("Delete failed: unknown error");
			m_DeleteWorkerFinished = true;
			m_DeleteResultsConsumed = false;
		}
	}

	void AssetBrowser::ConsumeAndApplyFinishedAssetDeletion() {
		bool shouldApply = false;
		{
			std::scoped_lock lock(m_DeleteMutex);
			shouldApply = m_DeleteWorkerFinished && !m_DeleteResultsConsumed;
		}
		if (!shouldApply) {
			return;
		}

		if (m_DeleteThread.joinable()) {
			m_DeleteThread.join();
		}

		std::vector<std::string> forgetAssetPaths;
		std::vector<std::string> succeededRoots;
		std::vector<std::string> errors;
		{
			std::scoped_lock lock(m_DeleteMutex);
			forgetAssetPaths = std::move(m_DeleteForgetAssetPaths);
			succeededRoots = std::move(m_DeleteSucceededRoots);
			errors = std::move(m_DeleteErrors);
			m_DeleteResultsConsumed = true;
		}

		const std::unordered_set<std::string> succeeded(succeededRoots.begin(), succeededRoots.end());

		// All of this is main-thread-only: AssetRegistry and SceneManager keep
		// unsynchronized global state, and selection fields are read by Render().
		for (const DeleteRootInfo& info : m_DeleteRoots) {
			if (succeeded.find(info.Path) == succeeded.end()) {
				continue;
			}

			// Root sidecars are siblings, so the worker does not traverse them.

			AssetRegistry::DeleteCompanionMetadata(info.Path);

			// Drop the SceneDefinition so a script-side LoadScene of the deleted name
			// can't resurrect an empty Scene from the stale definition.
			if (!info.SceneStem.empty()) {
				SceneManager::UnregisterScene(info.SceneStem);
			}

			if (m_SelectedPath == info.Path) {
				m_SelectedPath.clear();
			}
			m_SelectedPaths.erase(
				std::remove(m_SelectedPaths.begin(), m_SelectedPaths.end(), info.Path),
				m_SelectedPaths.end());
			if (m_PressedPath == info.Path) {
				m_PressedPath.clear();
			}
		}

		ForgetRegistryAssetPaths(forgetAssetPaths);
		EditorScriptDiscovery::MarkDirty();

		for (const std::string& error : errors) {
			FNK_CORE_WARN_TAG("AssetBrowser", "{}", error);
		}

		FinishFileEdit(m_DeleteFileEdit);
		m_DeleteFileEdit.reset();
		m_DeleteRoots.clear();
		m_NeedsRefresh = true;
		// Mirror the import path: suppress the single asset-watcher refresh that would
		// otherwise fire right after we mutate the tree. Import and delete never overlap
		// (StartAssetDeletion serializes them), so reusing the import flag is safe.
		m_SuppressNextAssetWatcherRefresh.store(true, std::memory_order_release);
		m_DeleteActive.store(false, std::memory_order_release);
	}

	void AssetBrowser::UpdateAssetDeletionProgressWindow() {
		if (!m_DeleteActive.load(std::memory_order_acquire)) {
			if (m_DeleteProgressWindowVisible) {
				Win32BuildProgressWindow::Hide();
				m_DeleteProgressWindowVisible = false;
			}
			return;
		}

		if (!m_DeleteProgressWindowVisible) {
			// Grace period: a fast delete (a single small file) finishes before this
			// elapses, so the popup never flashes. Only genuinely slow deletes — big
			// folders, large or Defender-throttled removals — surface the window.
			constexpr auto kGrace = std::chrono::milliseconds(200);
			if (std::chrono::steady_clock::now() - m_DeleteStartTime < kGrace) {
				return;
			}
			Win32BuildProgressWindow::Show("Deleting Assets...");
			m_DeleteProgressWindowVisible = true;
		}

		const std::size_t total = m_DeleteTotalEntries.load(std::memory_order_acquire);
		const std::size_t processed = m_DeleteProcessedEntries.load(std::memory_order_acquire);
		const float progress = total > 0
			? std::min(1.0f, static_cast<float>(processed) / static_cast<float>(total))
			: 0.0f;

		std::string stage;
		{
			std::scoped_lock lock(m_DeleteMutex);
			stage = m_DeleteCurrentItem.empty() ? "Deleting..." : m_DeleteCurrentItem;
		}

		Win32BuildProgressWindow::Update(progress, stage);
	}

	void AssetBrowser::PumpAssetDeletion() {
		ConsumeAndApplyFinishedAssetDeletion();
		UpdateAssetDeletionProgressWindow();
	}
}
