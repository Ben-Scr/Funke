#include "Gui/AssetBrowser.hpp"
#include "Gui/AssetBrowserInternal.hpp"
#include "Core/Platform/Win32BuildProgressWindow.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

#ifdef FNK_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <shlobj_core.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ole32.lib")
#endif
#endif

namespace Funke {
	using namespace AssetBrowserInternal;

	namespace AssetBrowserInternal {
		bool IsLeftMouseDragPastClickThreshold()
		{
			const ImGuiIO& io = ImGui::GetIO();
			const ImVec2 delta(io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x,
				io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y);
			return (delta.x * delta.x + delta.y * delta.y) > (io.MouseDragThreshold * io.MouseDragThreshold);
		}
	}

	namespace {
#ifdef FNK_PLATFORM_WINDOWS
		FORMATETC MakeHGlobalFormat(CLIPFORMAT format)
		{
			FORMATETC result{};
			result.cfFormat = format;
			result.dwAspect = DVASPECT_CONTENT;
			result.lindex = -1;
			result.tymed = TYMED_HGLOBAL;
			return result;
		}

		HGLOBAL CreateHDropMemory(const std::vector<std::wstring>& paths)
		{
			if (paths.empty()) {
				return nullptr;
			}

			std::size_t charCount = 1; // final double-null terminator
			for (const std::wstring& path : paths) {
				charCount += path.size() + 1;
			}

			const SIZE_T byteCount = sizeof(DROPFILES) + charCount * sizeof(wchar_t);
			HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byteCount);
			if (!memory) {
				return nullptr;
			}

			auto* dropFiles = static_cast<DROPFILES*>(GlobalLock(memory));
			if (!dropFiles) {
				GlobalFree(memory);
				return nullptr;
			}

			dropFiles->pFiles = sizeof(DROPFILES);
			dropFiles->fWide = TRUE;

			wchar_t* write = reinterpret_cast<wchar_t*>(
				reinterpret_cast<std::uint8_t*>(dropFiles) + sizeof(DROPFILES));
			for (const std::wstring& path : paths) {
				std::memcpy(write, path.c_str(), path.size() * sizeof(wchar_t));
				write += path.size();
				*write++ = L'\0';
			}
			*write = L'\0';

			GlobalUnlock(memory);
			return memory;
		}

		HGLOBAL CreateDropEffectMemory(DWORD effect)
		{
			HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
			if (!memory) {
				return nullptr;
			}

			if (auto* value = static_cast<DWORD*>(GlobalLock(memory))) {
				*value = effect;
				GlobalUnlock(memory);
				return memory;
			}

			GlobalFree(memory);
			return nullptr;
		}

		class FormatEtcEnumerator final : public IEnumFORMATETC {
		public:
			explicit FormatEtcEnumerator(std::vector<FORMATETC> formats)
				: m_Formats(std::move(formats))
			{
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
			{
				if (!out) return E_POINTER;
				if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
					*out = static_cast<IEnumFORMATETC*>(this);
					AddRef();
					return S_OK;
				}
				*out = nullptr;
				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override { return ++m_RefCount; }

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG count = --m_RefCount;
				if (count == 0) {
					delete this;
				}
				return count;
			}

			HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC* outFormats, ULONG* outFetched) override
			{
				if (!outFormats || (count > 1 && !outFetched)) {
					return E_POINTER;
				}

				ULONG fetched = 0;
				while (fetched < count && m_Index < m_Formats.size()) {
					outFormats[fetched++] = m_Formats[m_Index++];
				}

				if (outFetched) {
					*outFetched = fetched;
				}
				return fetched == count ? S_OK : S_FALSE;
			}

			HRESULT STDMETHODCALLTYPE Skip(ULONG count) override
			{
				m_Index = std::min(m_Index + static_cast<std::size_t>(count), m_Formats.size());
				return m_Index < m_Formats.size() ? S_OK : S_FALSE;
			}

			HRESULT STDMETHODCALLTYPE Reset() override
			{
				m_Index = 0;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override
			{
				if (!out) return E_POINTER;
				auto* clone = new FormatEtcEnumerator(m_Formats);
				clone->m_Index = m_Index;
				*out = clone;
				return S_OK;
			}

		private:
			ULONG m_RefCount = 1;
			std::vector<FORMATETC> m_Formats;
			std::size_t m_Index = 0;
		};

		class FileDropDataObject final : public IDataObject {
		public:
			explicit FileDropDataObject(std::vector<std::wstring> paths)
				: m_Paths(std::move(paths))
			{
				m_PreferredDropEffectFormat = static_cast<CLIPFORMAT>(
					RegisterClipboardFormatW(L"Preferred DropEffect"));
				m_Formats.push_back(MakeHGlobalFormat(static_cast<CLIPFORMAT>(CF_HDROP)));
				if (m_PreferredDropEffectFormat != 0) {
					m_Formats.push_back(MakeHGlobalFormat(m_PreferredDropEffectFormat));
				}
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
			{
				if (!out) return E_POINTER;
				if (riid == IID_IUnknown || riid == IID_IDataObject) {
					*out = static_cast<IDataObject*>(this);
					AddRef();
					return S_OK;
				}
				*out = nullptr;
				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override { return ++m_RefCount; }

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG count = --m_RefCount;
				if (count == 0) {
					delete this;
				}
				return count;
			}

			HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override
			{
				if (!format || !medium) return E_POINTER;
				std::memset(medium, 0, sizeof(STGMEDIUM));

				if (Matches(format, static_cast<CLIPFORMAT>(CF_HDROP))) {
					HGLOBAL hdrop = CreateHDropMemory(m_Paths);
					if (!hdrop) return E_OUTOFMEMORY;
					medium->tymed = TYMED_HGLOBAL;
					medium->hGlobal = hdrop;
					return S_OK;
				}

				if (m_PreferredDropEffectFormat != 0
					&& Matches(format, m_PreferredDropEffectFormat)) {
					HGLOBAL effect = CreateDropEffectMemory(DROPEFFECT_COPY);
					if (!effect) return E_OUTOFMEMORY;
					medium->tymed = TYMED_HGLOBAL;
					medium->hGlobal = effect;
					return S_OK;
				}

				return DV_E_FORMATETC;
			}

			HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }

			HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
			{
				if (!format) return E_POINTER;
				if (Matches(format, static_cast<CLIPFORMAT>(CF_HDROP))) return S_OK;
				if (m_PreferredDropEffectFormat != 0 && Matches(format, m_PreferredDropEffectFormat)) return S_OK;
				return DV_E_FORMATETC;
			}

			HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override
			{
				if (out) out->ptd = nullptr;
				return E_NOTIMPL;
			}

			HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }

			HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override
			{
				if (!out) return E_POINTER;
				if (direction != DATADIR_GET) {
					*out = nullptr;
					return E_NOTIMPL;
				}
				*out = new FormatEtcEnumerator(m_Formats);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
			{
				return OLE_E_ADVISENOTSUPPORTED;
			}

			HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }

			HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override
			{
				return OLE_E_ADVISENOTSUPPORTED;
			}

		private:
			static bool Matches(const FORMATETC* format, CLIPFORMAT expected)
			{
				return format->cfFormat == expected
					&& format->dwAspect == DVASPECT_CONTENT
					&& (format->tymed & TYMED_HGLOBAL) != 0;
			}

			ULONG m_RefCount = 1;
			std::vector<std::wstring> m_Paths;
			CLIPFORMAT m_PreferredDropEffectFormat = 0;
			std::vector<FORMATETC> m_Formats;
		};

		class FileDropSource final : public IDropSource {
		public:
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
			{
				if (!out) return E_POINTER;
				if (riid == IID_IUnknown || riid == IID_IDropSource) {
					*out = static_cast<IDropSource*>(this);
					AddRef();
					return S_OK;
				}
				*out = nullptr;
				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override { return ++m_RefCount; }

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG count = --m_RefCount;
				if (count == 0) {
					delete this;
				}
				return count;
			}

			HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
			{
				if (escapePressed) return DRAGDROP_S_CANCEL;
				if ((keyState & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
			{
				return DRAGDROP_S_USEDEFAULTCURSORS;
			}

		private:
			ULONG m_RefCount = 1;
		};

		bool IsCursorInsideAnyEditorWindow()
		{
			POINT point{};
			if (!GetCursorPos(&point)) {
				return true;
			}

			HWND hoveredWindow = WindowFromPoint(point);
			HWND hoveredRoot = hoveredWindow ? GetAncestor(hoveredWindow, GA_ROOT) : nullptr;
			bool foundEditorWindow = false;

			ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
			for (ImGuiViewport* viewport : platformIo.Viewports) {
				if (!viewport) continue;
				HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
				if (!hwnd || !IsWindow(hwnd)) continue;
				foundEditorWindow = true;

				HWND editorRoot = GetAncestor(hwnd, GA_ROOT);
				if (hoveredWindow == hwnd || hoveredRoot == hwnd || (editorRoot && hoveredRoot == editorRoot)) {
					return true;
				}
			}

			if (foundEditorWindow && hoveredWindow) {
				return false;
			}

			for (ImGuiViewport* viewport : platformIo.Viewports) {
				if (!viewport) continue;
				HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
				if (!hwnd || !IsWindow(hwnd)) continue;

				RECT rect{};
				if (GetWindowRect(hwnd, &rect) && PtInRect(&rect, point)) {
					return true;
				}
			}

			const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
			if (mainViewport) {
				RECT rect{
					static_cast<LONG>(mainViewport->Pos.x),
					static_cast<LONG>(mainViewport->Pos.y),
					static_cast<LONG>(mainViewport->Pos.x + mainViewport->Size.x),
					static_cast<LONG>(mainViewport->Pos.y + mainViewport->Size.y)
				};
				if (PtInRect(&rect, point)) {
					return true;
				}
			}

			return false;
		}

		HWND GetCurrentEditorViewportHwnd()
		{
			if (ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
				HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
				if (hwnd && IsWindow(hwnd)) {
					return hwnd;
				}
			}

			if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
				HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
				if (hwnd && IsWindow(hwnd)) {
					return hwnd;
				}
			}

			HWND hwnd = ::GetActiveWindow();
			return hwnd && IsWindow(hwnd) ? hwnd : nullptr;
		}

		bool IsScreenDragPastThreshold(int startX, int startY)
		{
			POINT point{};
			if (!GetCursorPos(&point)) {
				return IsLeftMouseDragPastClickThreshold();
			}

			const float threshold = ImGui::GetIO().MouseDragThreshold;
			const float dx = static_cast<float>(point.x - startX);
			const float dy = static_cast<float>(point.y - startY);
			return (dx * dx + dy * dy) > (threshold * threshold);
		}

		bool DoNativeFileDrag(std::vector<std::wstring> paths)
		{
			auto* dataObject = new FileDropDataObject(std::move(paths));
			auto* dropSource = new FileDropSource();
			DWORD effect = 0;
			const HRESULT dragResult = DoDragDrop(
				dataObject,
				dropSource,
				DROPEFFECT_COPY,
				&effect);
			dropSource->Release();
			dataObject->Release();

			const bool dropped = dragResult == DRAGDROP_S_DROP && (effect & DROPEFFECT_COPY) != 0;
			if (FAILED(dragResult)) {
				FNK_CORE_WARN_TAG("AssetBrowser", "DoDragDrop failed for native file drag: HRESULT 0x{:08X}",
					static_cast<unsigned int>(dragResult));
			}
			return dropped;
		}

		bool StartNativeFileDrag(const std::vector<std::string>& paths)
		{
			std::vector<std::wstring> widePaths;
			widePaths.reserve(paths.size());
			for (const std::string& pathString : paths) {
				std::filesystem::path path(pathString);
				std::error_code ec;
				if (!std::filesystem::exists(path, ec) || ec) {
					continue;
				}

				std::filesystem::path absolute = std::filesystem::weakly_canonical(path, ec);
				if (ec) {
					ec.clear();
					absolute = std::filesystem::absolute(path, ec);
				}
				if (ec) {
					absolute = path;
				}
				widePaths.push_back(absolute.wstring());
			}

			if (widePaths.empty()) {
				FNK_CORE_WARN_TAG("AssetBrowser", "Native file drag skipped because none of the selected paths exist.");
				return false;
			}

			// DoDragDrop runs a modal loop that parks the main thread for the whole
			// drag. The compile/build progress popup is an owned top-level window on
			// this thread; left up, its message queue can't be pumped and DoDragDrop
			// deadlocks against it → editor freezes forever. Drop it for the drag;
			// the chrome render re-Shows it next frame if the build is still running.
			Win32BuildProgressWindow::Hide();

			const HRESULT oleResult = OleInitialize(nullptr);
			if (oleResult == RPC_E_CHANGED_MODE) {
				// No worker-thread fallback: a DoDragDrop off the input thread never sees the button
				// release while the main thread is parked, so the editor hangs until Windows kills it.
				FNK_CORE_WARN_TAG("AssetBrowser", "Native file drag skipped: main thread is in a COM multithreaded apartment.");
				return false;
			}

			if (FAILED(oleResult)) {
				FNK_CORE_WARN_TAG("AssetBrowser", "OleInitialize failed for native file drag: HRESULT 0x{:08X}",
					static_cast<unsigned int>(oleResult));
				return false;
			}

			const bool dropped = DoNativeFileDrag(std::move(widePaths));
			OleUninitialize();
			return dropped;
		}
#endif
	}

#ifdef FNK_PLATFORM_WINDOWS
	void AssetBrowser::PrepareExternalFileDrag(const DirectoryEntry& entry) {
		m_ExternalDragPaths = IsPathSelected(entry.Path)
			? GetSelectedPaths()
			: std::vector<std::string>{ entry.Path };

		POINT point{};
		if (GetCursorPos(&point)) {
			m_ExternalDragStartScreenX = point.x;
			m_ExternalDragStartScreenY = point.y;
		}
		else {
			const ImGuiIO& io = ImGui::GetIO();
			m_ExternalDragStartScreenX = static_cast<int>(io.MousePos.x);
			m_ExternalDragStartScreenY = static_cast<int>(io.MousePos.y);
		}

		HWND hwnd = GetCurrentEditorViewportHwnd();
		if (hwnd) {
			SetCapture(hwnd);
			m_ExternalDragCaptureWindow = hwnd;
		}
	}

	void AssetBrowser::ClearExternalFileDrag() {
		if (m_ExternalDragCaptureWindow) {
			HWND captured = static_cast<HWND>(m_ExternalDragCaptureWindow);
			if (GetCapture() == captured) {
				ReleaseCapture();
			}
			m_ExternalDragCaptureWindow = nullptr;
		}
		m_ExternalDragPaths.clear();
	}

	void AssetBrowser::MaybeStartExternalFileDrag() {
		if (m_ExternalDragPaths.empty()) {
			return;
		}

		if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
			ClearExternalFileDrag();
			return;
		}

		if (!IsScreenDragPastThreshold(m_ExternalDragStartScreenX, m_ExternalDragStartScreenY)
			|| IsCursorInsideAnyEditorWindow()) {
			return;
		}

		std::vector<std::string> paths = std::move(m_ExternalDragPaths);
		m_ExternalDragPaths.clear();
		if (m_ExternalDragCaptureWindow) {
			HWND captured = static_cast<HWND>(m_ExternalDragCaptureWindow);
			if (GetCapture() == captured) {
				ReleaseCapture();
			}
			m_ExternalDragCaptureWindow = nullptr;
		}
		StartNativeFileDrag(paths);
	}
#endif
}
