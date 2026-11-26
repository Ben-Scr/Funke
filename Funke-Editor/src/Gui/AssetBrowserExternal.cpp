#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Core/Platform.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Serialization/Path.hpp"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

#ifdef FNK_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

namespace Funke {
	using namespace AssetBrowserInternal;

#ifdef FNK_PLATFORM_WINDOWS
	namespace {
		// Tracked (not detached) so AssetBrowser::Shutdown can join them before global destructors run.
		std::mutex s_ShellLaunchMutex;
		std::vector<std::thread> s_ShellLaunchThreads;

		void ReapFinishedShellThreads_Locked() {
			s_ShellLaunchThreads.erase(
				std::remove_if(s_ShellLaunchThreads.begin(), s_ShellLaunchThreads.end(),
					[](std::thread& t) {
						if (!t.joinable()) return true;
						// A finished std::thread stays joinable until joined; test the OS handle and join it.
						if (WaitForSingleObject(t.native_handle(), 0) != WAIT_OBJECT_0) return false;
						t.join();
						return true;
					}),
				s_ShellLaunchThreads.end());
		}
	}

	void TrackShellLaunchThread(std::thread t) {
		std::scoped_lock lock(s_ShellLaunchMutex);
		ReapFinishedShellThreads_Locked();
		s_ShellLaunchThreads.push_back(std::move(t));
	}

	void JoinAllShellLaunchThreads() {
		std::vector<std::thread> drained;
		{
			std::scoped_lock lock(s_ShellLaunchMutex);
			drained.swap(s_ShellLaunchThreads);
		}
		for (auto& t : drained) {
			if (t.joinable()) t.join();
		}
	}
#endif // FNK_PLATFORM_WINDOWS
	void AssetBrowser::RevealAssetInExplorer(const std::string& path) {
		Platform::RevealInFileManager(Path::FromUtf8(path));
	}

	void AssetBrowser::CopyPathToClipboard(const std::string& path) {
		ImGui::SetClipboardText(path.c_str());
	}

	void AssetBrowser::OpenAssetExternal(const DirectoryEntry& entry) {
		try
		{
			std::string ext = std::filesystem::path(entry.Path).extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

			if (ext == ".scene")
			{
				m_PendingSceneLoad = entry.Path;
				return;
			}

			if (ext == ".anim" || ext == ".animctrl")
			{
				m_PendingAnimationOpen = entry.Path;
				return;
			}

			if (ext == ".prefab")
			{
				// Defer: ImGuiEditorLayer drains this in OnPreRender to enter prefab-edit mode; routing here keeps prefab and scene double-click behaviour in the same place.
				m_PendingPrefabEdit = entry.Path;
				return;
			}

			if (IsMeshFileExtension(ext) || ext == k_MaterialExtension) {
				m_Pending3DPreview = entry.Path;
				return;
			}

			if (ExternalEditor::IsScriptExtension(ext))
			{
				std::filesystem::path scriptPath = entry.Path;
				const std::filesystem::path nativeMirrorPath = ResolveNativeScriptMirrorPath(entry.Path);
				if (!nativeMirrorPath.empty() && std::filesystem::exists(nativeMirrorPath)) {
					scriptPath = nativeMirrorPath;
				}
				ExternalEditor::OpenFile(scriptPath.string());
				return;
			}

#ifdef FNK_PLATFORM_WINDOWS
			// ShellExecuteW is needed for OS file-association handling; threads are tracked (not detached) so Shutdown can join them.
			static constexpr int k_MaxConcurrentOpens = 8;
			static std::atomic<int> s_ActiveOpens{ 0 };
			if (s_ActiveOpens.load(std::memory_order_acquire) >= k_MaxConcurrentOpens) {
				FNK_WARN_TAG("AssetBrowser",
					"Skipping external open for '{}' — too many concurrent shell launches.",
					entry.Path);
				return;
			}
			s_ActiveOpens.fetch_add(1, std::memory_order_acq_rel);

			std::wstring wpath = std::filesystem::absolute(entry.Path).wstring();
			std::thread worker([wpath]() {
				CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
				ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				CoUninitialize();
				s_ActiveOpens.fetch_sub(1, std::memory_order_acq_rel);
			});
			TrackShellLaunchThread(std::move(worker));
#else
			Platform::OpenPath(std::filesystem::absolute(entry.Path));
#endif
		}
		catch (...) {}
	}
}
