// Browser entry point. The browser owns the frame loop and WebGPU objects arrive through callbacks,
// so this requests adapter and device asynchronously, hands them to the engine, then drives the
// runtime one frame per requestAnimationFrame.
//
// Native-only builds start from main(). C# builds (INDEX_WEB_MANAGED) are started by the .NET host
// program: dotnet.js boots Mono, the generated Program.Main P/Invokes IndexWeb_Start, and the engine
// then attaches to that running runtime.
//
// Saved data (PlayerPrefs, SaveData, files under Application.PersistentDataPath) lives below SDL's pref
// path /libsdl. That folder is backed by IndexedDB, and the engine starts only once the stored files are
// back, so saves survive reloads like they survive restarts on desktop.

#include <emscripten/emscripten.h>
#include <webgpu/webgpu_cpp.h>

#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Serialization/Path.hpp"

#if INDEX_WEB_MANAGED
#include "MonoScriptRuntime.hpp"
#endif

#include <cstdio>
#include <string>

namespace Index { class Application; }

Index::Application* IndexRuntimeBegin(int argc, char** argv);
bool IndexRuntimeFrame();
int IndexRuntimeEnd();

namespace {
	wgpu::Instance g_Instance;
	wgpu::Adapter g_Adapter;

	void ReportFatal(const std::string& message) {
		std::fprintf(stderr, "Index: %s\n", message.c_str());
		EM_ASM({ if (typeof Module.indexReportError === 'function') Module.indexReportError(UTF8ToString($0)); }, message.c_str());
	}

	std::string ToString(wgpu::StringView view) {
		return view.data ? std::string(view.data, view.length == WGPU_STRLEN ? std::char_traits<char>::length(view.data) : view.length) : std::string();
	}

	void Frame() {
		if (IndexRuntimeFrame()) return;
		emscripten_cancel_main_loop();
		IndexRuntimeEnd();
		// Application.Quit: a browser tab cannot close itself, so say why the picture stopped.
		EM_ASM({ if (typeof Module.indexGameEnded === 'function') Module.indexGameEnded(); });
	}

	void StartEngine(wgpu::Device device) {
		Index::WebGPUBackend::ProvideDevice(g_Instance, g_Adapter, std::move(device));
		static char program[] = "IndexGame";
		static char* argv[] = { program, nullptr };
		if (!IndexRuntimeBegin(1, argv)) {
			ReportFatal("The game failed to start; see the browser console for details.");
			return;
		}
		emscripten_set_main_loop(&Frame, 0, false);
	}

	void RequestDevice() {
		wgpu::DeviceDescriptor desc{};
		desc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) {
			std::fprintf(stderr, "WebGPU error %d: %s\n", static_cast<int>(type), ToString(message).c_str());
		});
		g_Adapter.RequestDevice(&desc, wgpu::CallbackMode::AllowSpontaneous,
			[](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
				if (status != wgpu::RequestDeviceStatus::Success) {
					ReportFatal("WebGPU device request failed: " + ToString(message));
					return;
				}
				StartEngine(std::move(device));
			});
	}
}

extern "C" EMSCRIPTEN_KEEPALIVE void IndexWeb_StorageReady();

// autoPersist writes changes back to IndexedDB after every file operation; pagehide catches the rest.
EM_JS(void, IndexWeb_MountStorage, (), {
	try { FS.mkdir('/libsdl'); } catch (e) {}
	try {
		FS.mount(IDBFS, { autoPersist: true }, '/libsdl');
	}
	catch (e) {
		console.warn('Index: saved data will not persist in this browser', e);
		_IndexWeb_StorageReady();
		return;
	}
	FS.syncfs(true, (error) => {
		if (error) console.warn('Index: could not restore saved data', error);
		addEventListener('pagehide', () => FS.syncfs(false, () => {}));
		_IndexWeb_StorageReady();
	});
});

namespace {
	void RequestAdapter() {
		g_Instance = wgpu::CreateInstance(nullptr);
		if (!g_Instance) {
			ReportFatal("This browser does not support WebGPU.");
			return;
		}
		wgpu::RequestAdapterOptions options{};
		options.powerPreference = wgpu::PowerPreference::HighPerformance;
		g_Instance.RequestAdapter(&options, wgpu::CallbackMode::AllowSpontaneous,
			[](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
				if (status != wgpu::RequestAdapterStatus::Success) {
					ReportFatal("No WebGPU adapter: " + ToString(message));
					return;
				}
				g_Adapter = std::move(adapter);
				RequestDevice();
			});
	}

	int StartWeb() {
		// The builder places the game content into the virtual filesystem at /game.
		Index::Path::SetExecutableDirOverride("/game");
#if INDEX_WEB_MANAGED
		IndexMonoConfig mono{};
		mono.AssembliesDir = "/game";
		mono.RuntimeIdentifier = "browser-wasm";
		mono.InterpreterOnly = true;
		mono.RuntimeAlreadyStarted = true;
		IndexScriptRuntimeMono_Register(mono);
#endif
		IndexWeb_MountStorage();
		return 0;
	}
}

extern "C" EMSCRIPTEN_KEEPALIVE void IndexWeb_StorageReady() {
	RequestAdapter();
}

#if INDEX_WEB_MANAGED
extern "C" EMSCRIPTEN_KEEPALIVE int IndexWeb_Start() {
	return StartWeb();
}
#else
int main() {
	return StartWeb();
}
#endif
