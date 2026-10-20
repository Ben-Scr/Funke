#include "pch.hpp"
#include "Graphics/RenderApi.hpp"

#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "Core/Log.hpp"
#include "Core/Window.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Graphics/Backend/WebGPUSurfaceSource.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Graphics/GLInitSpecifications.hpp"
#include "Graphics/Texture2D.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(FNK_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif


namespace Funke {

	namespace {
		// ── Backend lifecycle state ─────────────────────────────────────────
		bool        g_Initialized = false;
		std::string g_VersionString;
		std::string g_VendorString;
		std::string g_RendererString;
		GraphicsApi g_GraphicsApi = GraphicsApi::Unknown;
		GpuVendor   g_GpuVendor = GpuVendor::Unknown;

		Color    g_ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		uint32_t g_BackbufferWidth = 0;
		uint32_t g_BackbufferHeight = 0;
		bool     g_VsyncEnabled = true;

		// ── WebGPU objects ──────────────────────────────────────────────────
		wgpu::Instance      g_Instance;
		wgpu::Adapter       g_Adapter;
		wgpu::Device        g_Device;
		wgpu::Queue         g_Queue;
		wgpu::Surface       g_Surface;
		SDL_Window*         g_SurfaceWindow = nullptr;
		// What g_Surface was built on; Android hands out a new ANativeWindow after the app was backgrounded.
		void*               g_SurfaceNativeHandle = nullptr;
		uint32_t            g_SurfaceGeneration = 0;
		wgpu::TextureFormat g_SurfaceFormat = wgpu::TextureFormat::Undefined;
		bool g_SurfaceCopySrc = false;

		bool g_HasTimestampQuery = false;

		// Set by hosts that cannot block on WaitAny (the browser): adapter and device were requested
		// asynchronously before the engine started.
		wgpu::Instance g_ProvidedInstance;
		wgpu::Adapter  g_ProvidedAdapter;
		wgpu::Device   g_ProvidedDevice;

		// Always false until Dawn stabilises a portable bindless feature name; gate kept ready for when it does.
		bool g_HasBindlessTextures = false;

		// ── Per-frame transient state ───────────────────────────────────────
		struct FrameState {
			wgpu::CommandEncoder    Encoder;
			wgpu::Texture           SurfaceTexture;
			wgpu::TextureView       SurfaceView;
			wgpu::RenderPassEncoder ActivePass;
			bool HasEncoder = false;
			bool HasSurfaceTexture = false;
			bool HasActivePass = false;
			bool PresentedSwapChain = false;
		};
		FrameState g_Frame;
		uint64_t   g_FrameSubmitSerial = 0;

		struct TargetState {
			wgpu::TextureView ColorView;       // null -> use swap chain's per-frame view
			wgpu::TextureView DepthView;       // null -> no depth attachment
			wgpu::TextureFormat ColorFormat = wgpu::TextureFormat::Undefined;
			bool IsSwapChain = true;
			uint32_t Width  = 0;
			uint32_t Height = 0;
			uint32_t SampleCount = 1;
		};
		TargetState g_CurrentTarget;

		uint32_t g_ViewportX = 0, g_ViewportY = 0, g_ViewportW = 0, g_ViewportH = 0;
		uint32_t g_ScissorX  = 0, g_ScissorY  = 0, g_ScissorW  = 0, g_ScissorH  = 0;
		bool     g_ScissorActive = false;
		PolygonMode g_PolygonMode = PolygonMode::Filled;

		// ── Helpers ─────────────────────────────────────────────────────────

		const char* AdapterTypeName(wgpu::AdapterType t) {
			switch (t) {
				case wgpu::AdapterType::DiscreteGPU:   return "DiscreteGPU";
				case wgpu::AdapterType::IntegratedGPU: return "IntegratedGPU";
				case wgpu::AdapterType::CPU:           return "CPU";
				default:                               return "Unknown";
			}
		}

		const char* BackendTypeName(wgpu::BackendType t) {
			switch (t) {
				case wgpu::BackendType::WebGPU:  return "WebGPU";
				case wgpu::BackendType::D3D11:   return "Direct3D11";
				case wgpu::BackendType::D3D12:   return "Direct3D12";
				case wgpu::BackendType::Vulkan:  return "Vulkan";
				case wgpu::BackendType::Metal:   return "Metal";
				case wgpu::BackendType::OpenGL:  return "OpenGL";
				case wgpu::BackendType::OpenGLES:return "OpenGLES";
				case wgpu::BackendType::Null:    return "Null";
				default:                         return "Unknown";
			}
		}

		GraphicsApi MapGraphicsApi(wgpu::BackendType type) noexcept {
			switch (type) {
				case wgpu::BackendType::WebGPU:  return GraphicsApi::WebGPU;
				case wgpu::BackendType::D3D11:   return GraphicsApi::Direct3D11;
				case wgpu::BackendType::D3D12:   return GraphicsApi::Direct3D12;
				case wgpu::BackendType::Vulkan:  return GraphicsApi::Vulkan;
				case wgpu::BackendType::Metal:   return GraphicsApi::Metal;
				case wgpu::BackendType::OpenGL:  return GraphicsApi::OpenGL;
				case wgpu::BackendType::OpenGLES:return GraphicsApi::OpenGLES;
				default:                         return GraphicsApi::Unknown;
			}
		}

		constexpr char ToAsciiLower(char value) noexcept {
			return value >= 'A' && value <= 'Z'
				? static_cast<char>(value + ('a' - 'A'))
				: value;
		}

		bool ContainsAsciiCaseInsensitive(std::string_view value, std::string_view token) noexcept {
			if (token.empty()) return true;
			if (token.size() > value.size()) return false;

			for (size_t start = 0; start <= value.size() - token.size(); ++start) {
				bool matches = true;
				for (size_t offset = 0; offset < token.size(); ++offset) {
					if (ToAsciiLower(value[start + offset]) != ToAsciiLower(token[offset])) {
						matches = false;
						break;
					}
				}
				if (matches) return true;
			}

			return false;
		}

		GpuVendor MapGpuVendor(
			uint32_t vendorId,
			std::string_view vendorName,
			std::string_view deviceName) noexcept
		{
			switch (vendorId) {
				case 0x10DEu: return GpuVendor::NVIDIA;
				case 0x1002u: return GpuVendor::AMD;
				case 0x8086u: return GpuVendor::Intel;
				case 0x106Bu: return GpuVendor::Apple;
				case 0x13B5u: return GpuVendor::ARM;
				case 0x1414u: return GpuVendor::Microsoft;
				case 0x17CBu:
				case 0x5143u:
				case 0x4D4F4351u: return GpuVendor::Qualcomm;
				default: break;
			}

			auto contains = [vendorName, deviceName](std::string_view token) noexcept {
				return ContainsAsciiCaseInsensitive(vendorName, token)
					|| ContainsAsciiCaseInsensitive(deviceName, token);
			};

			if (contains("NVIDIA")) return GpuVendor::NVIDIA;
			if (contains("Intel")) return GpuVendor::Intel;
			if (contains("AMD") || contains("Advanced Micro Devices") || contains("Radeon")) return GpuVendor::AMD;
			if (vendorName == "ATI" || ContainsAsciiCaseInsensitive(deviceName, "ATI Technologies")) return GpuVendor::AMD;
			if (contains("Apple")) return GpuVendor::Apple;
			if (contains("Qualcomm") || contains("Adreno")) return GpuVendor::Qualcomm;
			if (contains("ARM") || contains("Mali")) return GpuVendor::ARM;
			if (contains("Microsoft") || contains("WARP")) return GpuVendor::Microsoft;
			return GpuVendor::Unknown;
		}

		bool SupportsPresentMode(const wgpu::SurfaceCapabilities& caps, wgpu::PresentMode mode) {
			for (size_t i = 0; i < caps.presentModeCount; ++i) {
				if (caps.presentModes[i] == mode) return true;
			}
			return false;
		}

		const char* PresentModeName(wgpu::PresentMode mode) {
			switch (mode) {
			case wgpu::PresentMode::Fifo:        return "Fifo";
			case wgpu::PresentMode::FifoRelaxed: return "FifoRelaxed";
			case wgpu::PresentMode::Immediate:   return "Immediate";
			case wgpu::PresentMode::Mailbox:     return "Mailbox";
			default:                             return "Undefined";
			}
		}

		void LogPresentModeChoice(const char* surfaceName, const wgpu::SurfaceCapabilities& caps, wgpu::PresentMode chosen) {
			std::string supported;
			for (size_t i = 0; i < caps.presentModeCount; ++i) {
				if (!supported.empty()) supported += ", ";
				supported += PresentModeName(caps.presentModes[i]);
			}
			FNK_CORE_INFO_TAG("WebGPUApi", "{} surface: vsync={}, supported present modes [{}] -> {}",
				surfaceName, g_VsyncEnabled, supported, PresentModeName(chosen));
		}

		wgpu::PresentMode ChoosePresentMode(const wgpu::SurfaceCapabilities& caps) {
			if (g_VsyncEnabled) {
				if (SupportsPresentMode(caps, wgpu::PresentMode::Fifo)) {
					return wgpu::PresentMode::Fifo;
				}
				return caps.presentModeCount > 0 ? caps.presentModes[0] : wgpu::PresentMode::Fifo;
			}

			if (SupportsPresentMode(caps, wgpu::PresentMode::Immediate)) {
				return wgpu::PresentMode::Immediate;
			}
			if (SupportsPresentMode(caps, wgpu::PresentMode::Mailbox)) {
				return wgpu::PresentMode::Mailbox;
			}
			return caps.presentModeCount > 0 ? caps.presentModes[0] : wgpu::PresentMode::Fifo;
		}

		std::string FromStringView(wgpu::StringView sv) {
			if (sv.data == nullptr || sv.length == 0) return {};
			return std::string(sv.data, sv.length);
		}

		void PumpEvents() {
			if (g_Instance) g_Instance.ProcessEvents();
		}

		bool CreateSurface() {
			Window* win = Application::GetWindow();
			if (!win) {
				FNK_CORE_ERROR_TAG("WebGPUApi", "No engine window available for surface creation");
				return false;
			}
			g_SurfaceWindow = win->GetNativeWindow();
			if (!g_SurfaceWindow) {
				FNK_CORE_ERROR_TAG("WebGPUApi", "SDL window handle is null");
				return false;
			}

			g_Surface = WebGPUSurfaceSource::Create(g_Instance, g_SurfaceWindow);
			if (!g_Surface) {
				FNK_CORE_ERROR_TAG("WebGPUApi", "wgpu::Instance::CreateSurface returned null");
				return false;
			}
			g_SurfaceNativeHandle = WebGPUSurfaceSource::CurrentNativeHandle(g_SurfaceWindow);
			g_SurfaceGeneration = Window::GetNativeSurfaceGeneration();
			return true;
		}

		// False while there is nothing to draw to (Android paused). Touching a surface whose native window
		// is gone fails with VK_ERROR_SURFACE_LOST, which Dawn escalates to device loss: black until restart.
		bool RefreshSurfaceIfReplaced(bool& replaced) {
			replaced = false;
			void* handle = WebGPUSurfaceSource::CurrentNativeHandle(g_SurfaceWindow);
			if (!handle) return false;
			const uint32_t generation = Window::GetNativeSurfaceGeneration();
			if (handle == g_SurfaceNativeHandle && generation == g_SurfaceGeneration) return true;
			wgpu::Surface replacement = WebGPUSurfaceSource::Create(g_Instance, g_SurfaceWindow);
			if (!replacement) return false;
			g_Surface = std::move(replacement);
			g_SurfaceNativeHandle = handle;
			g_SurfaceGeneration = generation;
			replaced = true;
			return true;
		}

		wgpu::BackendType PreferredBackendType() {
			FunkeProject* project = ProjectManager::GetCurrentProject();
			if (!project) return wgpu::BackendType::Undefined;
			switch (project->ActiveRenderBackend) {
				case FunkeProject::RenderBackend::Auto:       return wgpu::BackendType::Undefined;
				case FunkeProject::RenderBackend::Vulkan:     return wgpu::BackendType::Vulkan;
				case FunkeProject::RenderBackend::Direct3D11: return wgpu::BackendType::D3D11;
				case FunkeProject::RenderBackend::Direct3D12: return wgpu::BackendType::D3D12;
				case FunkeProject::RenderBackend::OpenGL:     return wgpu::BackendType::OpenGL;
				case FunkeProject::RenderBackend::Metal:      return wgpu::BackendType::Metal;
				case FunkeProject::RenderBackend::OpenGLES:   return wgpu::BackendType::OpenGLES;
			}
			return wgpu::BackendType::Undefined;
		}

		void AppendBackend(std::vector<wgpu::BackendType>& candidates, wgpu::BackendType type) {
			if (type == wgpu::BackendType::Undefined) return;
			if (std::find(candidates.begin(), candidates.end(), type) == candidates.end()) {
				candidates.push_back(type);
			}
		}

		std::vector<wgpu::BackendType> BackendCandidates() {
			std::vector<wgpu::BackendType> candidates;
			AppendBackend(candidates, PreferredBackendType());

#if defined(FNK_PLATFORM_WINDOWS)
			AppendBackend(candidates, wgpu::BackendType::D3D12);
			AppendBackend(candidates, wgpu::BackendType::Vulkan);
			AppendBackend(candidates, wgpu::BackendType::D3D11);
			AppendBackend(candidates, wgpu::BackendType::OpenGL);
#elif defined(FNK_PLATFORM_MACOS) || defined(FNK_PLATFORM_IOS)
			AppendBackend(candidates, wgpu::BackendType::Metal);
#elif defined(FNK_PLATFORM_ANDROID)
			AppendBackend(candidates, wgpu::BackendType::Vulkan);
			AppendBackend(candidates, wgpu::BackendType::OpenGLES);
#elif defined(FNK_PLATFORM_WEB)
			AppendBackend(candidates, wgpu::BackendType::WebGPU);
#else
			AppendBackend(candidates, wgpu::BackendType::Vulkan);
			AppendBackend(candidates, wgpu::BackendType::OpenGL);
#endif

			return candidates;
		}

		void PrepareBackendRuntime(wgpu::BackendType type) {
#if defined(FNK_PLATFORM_WINDOWS)
			static bool d3dAttempted = false;
			static bool vulkanAttempted = false;
			static HMODULE d3dCompiler = nullptr;
			static HMODULE vulkan = nullptr;

			if ((type == wgpu::BackendType::D3D11 || type == wgpu::BackendType::D3D12)
				&& !d3dAttempted) {
				d3dAttempted = true;
				d3dCompiler = ::LoadLibraryW(L"d3dcompiler_47.dll");
				FNK_CORE_INFO_TAG("WebGPUApi", "D3D compiler preload: {}",
					d3dCompiler ? "ok" : "missing");
			}

			if (type == wgpu::BackendType::Vulkan && !vulkanAttempted) {
				vulkanAttempted = true;
				vulkan = ::LoadLibraryW(L"vulkan-1.dll");
				FNK_CORE_INFO_TAG("WebGPUApi", "Vulkan loader preload: {}",
					vulkan ? "ok" : "missing");
			}
#else
			(void)type;
#endif
		}

		bool TryRequestAdapter(wgpu::BackendType backendType,
			wgpu::Adapter& outAdapter, std::string& outError)
		{
			wgpu::RequestAdapterOptions opts{};
			opts.compatibleSurface = g_Surface;
			opts.powerPreference   = wgpu::PowerPreference::HighPerformance;
			opts.backendType       = backendType;

#if defined(FNK_PLATFORM_ANDROID)
			// Mali drivers (seen on G715) cache a subpass for every distinct VkRenderingInfo and never free it;
			// with fresh swapchain views each frame that leaked ~100 MB/s until the process was killed.
			// Classic render passes are cached by Dawn itself. Dawn already does this for Mali-G68.
			const char* const disabledToggles[] = { "vulkan_use_dynamic_rendering" };
			wgpu::DawnTogglesDescriptor toggles{};
			if (backendType == wgpu::BackendType::Vulkan) {
				toggles.disabledToggleCount = 1;
				toggles.disabledToggles = disabledToggles;
				opts.nextInChain = &toggles;
			}
#endif

			struct AdapterCtx { wgpu::Adapter Adapter; std::string Error; };
			AdapterCtx ctx;

			wgpu::Future future = g_Instance.RequestAdapter(
				&opts,
				wgpu::CallbackMode::WaitAnyOnly,
				[&ctx](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView msg) {
					if (status == wgpu::RequestAdapterStatus::Success) {
						ctx.Adapter = std::move(adapter);
					} else {
						ctx.Error = FromStringView(msg);
					}
				});

			wgpu::FutureWaitInfo wait{ future };
			g_Instance.WaitAny(1, &wait, /*timeoutNS*/ UINT64_MAX);

			if (!ctx.Adapter) {
				outError = std::move(ctx.Error);
				return false;
			}
			outAdapter = std::move(ctx.Adapter);
			return true;
		}

		bool RequestAdapterSync() {
			wgpu::Adapter adapter;
			std::string error;

			for (wgpu::BackendType backend : BackendCandidates()) {
				PrepareBackendRuntime(backend);
				if (TryRequestAdapter(backend, adapter, error)) {
					g_Adapter = std::move(adapter);
					return true;
				}

				FNK_CORE_WARN_TAG("WebGPUApi",
					"{} backend unavailable ({}); trying the next backend.",
					BackendTypeName(backend),
					error.empty() ? "no compatible GPU" : error);
			}

			FNK_CORE_ERROR_TAG("WebGPUApi", "RequestAdapter failed: {}",
				error.empty() ? "no compatible GPU" : error);
			return false;
		}

		bool RequestDeviceSync() {
			wgpu::DeviceDescriptor desc{};
			static wgpu::FeatureName s_RequestedFeatures[1] = {};
			uint32_t requestedFeatureCount = 0;
			if (g_Adapter && g_Adapter.HasFeature(wgpu::FeatureName::TimestampQuery)) {
				s_RequestedFeatures[requestedFeatureCount++] = wgpu::FeatureName::TimestampQuery;
			}
			desc.requiredFeatureCount = requestedFeatureCount;
			desc.requiredFeatures     = requestedFeatureCount > 0 ? s_RequestedFeatures : nullptr;

			desc.SetUncapturedErrorCallback(
				[](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView msg) {
					const char* kind = "Unknown";
					switch (type) {
						case wgpu::ErrorType::Validation:  kind = "Validation";  break;
						case wgpu::ErrorType::OutOfMemory: kind = "OutOfMemory"; break;
						case wgpu::ErrorType::Internal:    kind = "Internal";    break;
						default: break;
					}
					FNK_CORE_ERROR_TAG("WebGPUApi", "WebGPU [{}]: {}",
						kind, FromStringView(msg));
				});
			desc.SetDeviceLostCallback(
				wgpu::CallbackMode::AllowSpontaneous,
				[](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView msg) {
					if (reason == wgpu::DeviceLostReason::Destroyed) {
						return;
					}
					FNK_CORE_ERROR_TAG("WebGPUApi", "Device lost ({}): {}",
						static_cast<int>(reason), FromStringView(msg));
				});

			struct DeviceCtx { wgpu::Device Device; std::string Error; };
			DeviceCtx ctx;

			wgpu::Future future = g_Adapter.RequestDevice(
				&desc,
				wgpu::CallbackMode::WaitAnyOnly,
				[&ctx](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView msg) {
					if (status == wgpu::RequestDeviceStatus::Success) {
						ctx.Device = std::move(device);
					} else {
						ctx.Error = FromStringView(msg);
					}
				});

			wgpu::FutureWaitInfo wait{ future };
			g_Instance.WaitAny(1, &wait, UINT64_MAX);

			if (!ctx.Device) {
				FNK_CORE_ERROR_TAG("WebGPUApi", "RequestDevice failed: {}",
					ctx.Error.empty() ? "unknown" : ctx.Error);
				return false;
			}
			g_Device = std::move(ctx.Device);
			g_Queue  = g_Device.GetQueue();
			g_HasTimestampQuery = (requestedFeatureCount > 0);
			return true;
		}

		void ConfigureSurface(uint32_t width, uint32_t height) {
			bool replaced = false;
			if (g_SurfaceWindow && !RefreshSurfaceIfReplaced(replaced)) {
				// Remember the size; the first frame after resume configures the new surface with it.
				g_BackbufferWidth  = width;
				g_BackbufferHeight = height;
				return;
			}

			wgpu::SurfaceCapabilities caps{};
			g_Surface.GetCapabilities(g_Adapter, &caps);

			g_SurfaceFormat = (caps.formatCount > 0)
				? caps.formats[0]
				: wgpu::TextureFormat::BGRA8Unorm;

			wgpu::SurfaceConfiguration config{};
			config.device      = g_Device;
			config.format      = g_SurfaceFormat;
			config.usage       = wgpu::TextureUsage::RenderAttachment;
			// CopySrc lets ScreenCapture read the finished frame (screenshot command, --screenshot-at).
			g_SurfaceCopySrc = (caps.usages & wgpu::TextureUsage::CopySrc) != wgpu::TextureUsage::None;
			if (g_SurfaceCopySrc) config.usage |= wgpu::TextureUsage::CopySrc;
			config.width       = width;
			config.height      = height;
			config.presentMode = ChoosePresentMode(caps);
			LogPresentModeChoice("Main", caps, config.presentMode);
			// Opaque where offered; Android (Mali, Adreno) often only offers Inherit, and an unsupported mode
			// makes Configure fail, leaving the screen black.
			config.alphaMode   = caps.alphaModeCount > 0 ? caps.alphaModes[0] : wgpu::CompositeAlphaMode::Auto;
			for (size_t i = 0; i < caps.alphaModeCount; ++i) {
				if (caps.alphaModes[i] == wgpu::CompositeAlphaMode::Opaque) config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
			}
			config.viewFormatCount = 0;

			g_Surface.Configure(&config);

			g_BackbufferWidth  = width;
			g_BackbufferHeight = height;

			// Initial render target = swap chain.
			g_CurrentTarget = TargetState{};
			g_CurrentTarget.IsSwapChain = true;
			g_CurrentTarget.Width  = width;
			g_CurrentTarget.Height = height;

			// Viewport defaults to full surface so callers don't have to
			// set one before the first Clear.
			g_ViewportX = 0; g_ViewportY = 0;
			g_ViewportW = width; g_ViewportH = height;
		}

		// ── Frame lifecycle ─────────────────────────────────────────────────

		void EnsureFrameEncoder() {
			if (g_Frame.HasEncoder) return;
			wgpu::CommandEncoderDescriptor desc{};
			g_Frame.Encoder = g_Device.CreateCommandEncoder(&desc);
			g_Frame.HasEncoder = true;
		}

		bool EnsureSurfaceTexture() {
			if (g_Frame.HasSurfaceTexture) return true;

			bool replaced = false;
			if (g_SurfaceWindow && !RefreshSurfaceIfReplaced(replaced)) return false;
			if (replaced) ConfigureSurface(g_BackbufferWidth, g_BackbufferHeight);

			wgpu::SurfaceTexture surfaceTex{};
			g_Surface.GetCurrentTexture(&surfaceTex);

			// Android replaces its native window when the app returns from the background (or the system
			// bars toggle); the old surface then reports Lost. Rebuild it from the window SDL has now.
			if (surfaceTex.status == wgpu::SurfaceGetCurrentTextureStatus::Lost && g_SurfaceWindow) {
				if (wgpu::Surface replacement = WebGPUSurfaceSource::Create(g_Instance, g_SurfaceWindow)) {
					g_Surface = std::move(replacement);
					g_SurfaceNativeHandle = WebGPUSurfaceSource::CurrentNativeHandle(g_SurfaceWindow);
					ConfigureSurface(g_BackbufferWidth, g_BackbufferHeight);
					surfaceTex = wgpu::SurfaceTexture{};
					g_Surface.GetCurrentTexture(&surfaceTex);
				}
			}
			else if (surfaceTex.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated) {
				ConfigureSurface(g_BackbufferWidth, g_BackbufferHeight);
				surfaceTex = wgpu::SurfaceTexture{};
				g_Surface.GetCurrentTexture(&surfaceTex);
			}

			// SuboptimalConfiguration is non-fatal — the surface still has a
			// usable texture, we just want to re-configure on next resize.
			const bool ok =
				surfaceTex.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal ||
				surfaceTex.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal;
			static wgpu::SurfaceGetCurrentTextureStatus s_LastFailure{};
			if (!ok) {
				// Once per failure kind: a backgrounded app fails every frame.
				if (surfaceTex.status != s_LastFailure) {
					FNK_CORE_WARN_TAG("WebGPUApi", "GetCurrentTexture: status={} — skipping frames",
						static_cast<int>(surfaceTex.status));
				}
				s_LastFailure = surfaceTex.status;
				return false;
			}
			s_LastFailure = {};

			g_Frame.SurfaceTexture = wgpu::Texture(surfaceTex.texture);

			wgpu::TextureViewDescriptor viewDesc{};
			viewDesc.format          = g_SurfaceFormat;
			viewDesc.dimension       = wgpu::TextureViewDimension::e2D;
			viewDesc.mipLevelCount   = 1;
			viewDesc.arrayLayerCount = 1;
			viewDesc.aspect          = wgpu::TextureAspect::All;
			g_Frame.SurfaceView = g_Frame.SurfaceTexture.CreateView(&viewDesc);

			g_Frame.HasSurfaceTexture = true;
			return true;
		}

		void EndActivePassIfAny() {
			if (!g_Frame.HasActivePass) return;
			g_Frame.ActivePass.End();
			g_Frame.ActivePass = nullptr;
			g_Frame.HasActivePass = false;
		}

		// MUST submit between target switches: Dawn flushes ALL WriteBuffer calls before ANY pass executes,
		// so a second WriteBuffer(uniform) on the same buffer overwrites the first before the first pass runs.
		// Separate submissions give each FBO its own atomic WriteBuffer+pass unit.
		void FlushFrameCommands() {
			EndActivePassIfAny();
			if (!g_Frame.HasEncoder) return;

			wgpu::CommandBufferDescriptor cbDesc{};
			wgpu::CommandBuffer cmd = g_Frame.Encoder.Finish(&cbDesc);
			g_Queue.Submit(1, &cmd);
			++g_FrameSubmitSerial;

			g_Frame.Encoder    = nullptr;
			g_Frame.HasEncoder = false;
			// Surface texture/view and PresentedSwapChain stay alive — ImGui pass still needs them.
		}
	}  // anonymous namespace

	namespace WebGPUBackend {
		bool ReadTextureRGBA8(const wgpu::Texture& texture, wgpu::TextureFormat format,
			uint32_t width, uint32_t height, std::vector<uint8_t>& outRgba)
		{
			const bool bgra = format == wgpu::TextureFormat::BGRA8Unorm || format == wgpu::TextureFormat::BGRA8UnormSrgb;
			const bool rgba = format == wgpu::TextureFormat::RGBA8Unorm || format == wgpu::TextureFormat::RGBA8UnormSrgb;
			if (!g_Initialized || !texture || width == 0 || height == 0 || (!bgra && !rgba)) {
				return false;
			}

			FlushFrameCommands();

			// Buffer rows must be 256-byte aligned for CopyTextureToBuffer.
			const uint32_t tightRow = width * 4;
			const uint32_t paddedRow = (tightRow + 255u) & ~255u;

			wgpu::BufferDescriptor bufDesc{};
			bufDesc.size  = static_cast<uint64_t>(paddedRow) * height;
			bufDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
			wgpu::Buffer buffer = g_Device.CreateBuffer(&bufDesc);
			if (!buffer) return false;

			wgpu::TexelCopyTextureInfo src{};
			src.texture = texture;
			wgpu::TexelCopyBufferInfo dst{};
			dst.buffer              = buffer;
			dst.layout.bytesPerRow  = paddedRow;
			dst.layout.rowsPerImage = height;
			wgpu::Extent3D extent{ width, height, 1 };

			wgpu::CommandEncoder encoder = g_Device.CreateCommandEncoder();
			encoder.CopyTextureToBuffer(&src, &dst, &extent);
			wgpu::CommandBuffer cmd = encoder.Finish();
			g_Queue.Submit(1, &cmd);

			bool mapped = false;
			wgpu::Future future = buffer.MapAsync(wgpu::MapMode::Read, 0, bufDesc.size,
				wgpu::CallbackMode::WaitAnyOnly,
				[&mapped](wgpu::MapAsyncStatus status, wgpu::StringView) {
					mapped = (status == wgpu::MapAsyncStatus::Success);
				});
			wgpu::FutureWaitInfo wait{ future };
#if defined(FNK_PLATFORM_WEB)
			// The browser resolves the map only after control returns to its event loop.
			(void)wait;
			buffer.Destroy();
			return false;
#else
			g_Instance.WaitAny(1, &wait, UINT64_MAX);
#endif
			if (!mapped) return false;

			const auto* data = static_cast<const uint8_t*>(buffer.GetConstMappedRange(0, bufDesc.size));
			if (!data) {
				buffer.Unmap();
				return false;
			}
			outRgba.resize(static_cast<size_t>(tightRow) * height);
			for (uint32_t y = 0; y < height; ++y) {
				const uint8_t* row = data + static_cast<size_t>(y) * paddedRow;
				uint8_t* out = outRgba.data() + static_cast<size_t>(y) * tightRow;
				std::memcpy(out, row, tightRow);
				if (bgra) {
					for (uint32_t x = 0; x < width; ++x) std::swap(out[x * 4 + 0], out[x * 4 + 2]);
				}
			}
			buffer.Unmap();
			return true;
		}

		bool ReadSwapChainRGBA8(uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRgba) {
			if (!g_Initialized || !g_SurfaceCopySrc || !g_Frame.HasSurfaceTexture || !g_Frame.SurfaceTexture) return false;
			outWidth = g_Frame.SurfaceTexture.GetWidth();
			outHeight = g_Frame.SurfaceTexture.GetHeight();
			return ReadTextureRGBA8(g_Frame.SurfaceTexture, g_SurfaceFormat, outWidth, outHeight, outRgba);
		}

		void Present() {
			if (!g_Initialized) return;

			if (!g_Frame.PresentedSwapChain) {
				EnsureFrameEncoder();
				if (EnsureSurfaceTexture()) {
					wgpu::RenderPassColorAttachment colorAtt{};
					colorAtt.view       = g_Frame.SurfaceView;
					colorAtt.loadOp     = wgpu::LoadOp::Clear;
					colorAtt.storeOp    = wgpu::StoreOp::Store;
					colorAtt.clearValue = wgpu::Color{
						g_ClearColor.r, g_ClearColor.g, g_ClearColor.b, g_ClearColor.a };
					colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

					wgpu::RenderPassDescriptor passDesc{};
					passDesc.colorAttachmentCount = 1;
					passDesc.colorAttachments     = &colorAtt;

					wgpu::RenderPassEncoder pass = g_Frame.Encoder.BeginRenderPass(&passDesc);
					pass.End();
					g_Frame.PresentedSwapChain = true;
				}
			} else {
				EndActivePassIfAny();
			}

			if (g_Frame.HasEncoder) {
				wgpu::CommandBufferDescriptor cbDesc{};
				wgpu::CommandBuffer cmd = g_Frame.Encoder.Finish(&cbDesc);
				g_Queue.Submit(1, &cmd);
				++g_FrameSubmitSerial;
			}

#if !defined(FNK_PLATFORM_WEB)
			// The browser presents the canvas itself when the requestAnimationFrame callback returns.
			if (g_Frame.HasSurfaceTexture) {
				g_Surface.Present();
			}
#endif

			Framebuffer::ProcessFrameEndDeferredDestroy();
			g_Frame = FrameState{};
			PumpEvents();
		}

		wgpu::Device GetDevice() { return g_Device; }
		wgpu::Queue  GetQueue()  { return g_Queue; }
		wgpu::TextureFormat GetSurfaceFormat() { return g_SurfaceFormat; }
		bool HasTimestampQuery() { return g_HasTimestampQuery; }
		bool HasBindlessTextures() { return g_HasBindlessTextures; }
		bool IsInitialized() { return g_Initialized; }

		uint32_t GetMaxTextureDimension2D() {
			wgpu::Limits limits{};
			if (g_Device && g_Device.GetLimits(&limits) == wgpu::Status::Success && limits.maxTextureDimension2D != 0) {
				return limits.maxTextureDimension2D;
			}
			return 8192u;
		}

		uint64_t GetMaxBufferSize() {
			wgpu::Limits limits{};
			if (g_Device && g_Device.GetLimits(&limits) == wgpu::Status::Success && limits.maxBufferSize != 0) {
				return limits.maxBufferSize;
			}
			return 256ull * 1024ull * 1024ull;
		}

		bool ShaderModuleCompiled(const wgpu::ShaderModule& module, std::string* outErrors) {
			if (!module) return false;
#if defined(FNK_PLATFORM_WEB)
			// No synchronous waits in the browser; errors still reach the uncaptured-error callback.
			(void)outErrors;
			return true;
#endif
			if (!g_Instance) return true;  // no way to wait; assume the caller's module is usable
			bool ok = true;
			std::string errors;
			wgpu::Future future = module.GetCompilationInfo(wgpu::CallbackMode::WaitAnyOnly,
				[&ok, &errors](wgpu::CompilationInfoRequestStatus status, const wgpu::CompilationInfo* info) {
					if (status != wgpu::CompilationInfoRequestStatus::Success || !info) return;
					for (size_t i = 0; i < info->messageCount; ++i) {
						const wgpu::CompilationMessage& message = info->messages[i];
						if (message.type != wgpu::CompilationMessageType::Error) continue;
						ok = false;
						errors += "line " + std::to_string(message.lineNum) + ": ";
						if (message.message.data) {
							const size_t length = message.message.length == WGPU_STRLEN
								? std::strlen(message.message.data) : message.message.length;
							errors.append(message.message.data, length);
						}
						errors += '\n';
					}
				});
			wgpu::FutureWaitInfo wait{ future };
			g_Instance.WaitAny(1, &wait, UINT64_MAX);
			if (outErrors) *outErrors = std::move(errors);
			return ok;
		}

		wgpu::CommandEncoder GetFrameEncoder() {
			if (!g_Initialized) return nullptr;
			EnsureFrameEncoder();
			return g_Frame.Encoder;
		}

		void ApplyCachedViewportToPass(wgpu::RenderPassEncoder& pass) {
			if (!pass) return;
			if (g_ViewportW == 0 || g_ViewportH == 0) return; // Dawn validation errors on zero w/h
			pass.SetViewport(
				static_cast<float>(g_ViewportX),
				static_cast<float>(g_ViewportY),
				static_cast<float>(g_ViewportW),
				static_cast<float>(g_ViewportH),
				0.0f, 1.0f);
		}

		bool GetCachedViewport(uint32_t& x, uint32_t& y, uint32_t& width, uint32_t& height) {
			if (g_ViewportW == 0 || g_ViewportH == 0) return false;
			x = g_ViewportX;
			y = g_ViewportY;
			width = g_ViewportW;
			height = g_ViewportH;
			return true;
		}

		CurrentTargetInfo BeginRenderToCurrentTarget() {
			CurrentTargetInfo out;
			if (!g_Initialized) return out;

			EnsureFrameEncoder();

			if (g_CurrentTarget.IsSwapChain) {
				if (!EnsureSurfaceTexture()) return out;
				out.ColorView   = g_Frame.SurfaceView;
				out.DepthView   = nullptr;
				out.ColorFormat = g_SurfaceFormat;
				out.HasDepth    = false;
				out.IsSwapChain = true;
				out.Width       = g_BackbufferWidth;
				out.Height      = g_BackbufferHeight;
			} else {
				if (!g_CurrentTarget.ColorView) return out;
				out.ColorView   = g_CurrentTarget.ColorView;
				out.DepthView   = g_CurrentTarget.DepthView;
				out.ColorFormat = g_CurrentTarget.ColorFormat;
				out.HasDepth    = static_cast<bool>(g_CurrentTarget.DepthView);
				out.IsSwapChain = false;
				out.Width       = g_CurrentTarget.Width;
				out.Height      = g_CurrentTarget.Height;
				out.SampleCount = g_CurrentTarget.SampleCount;
			}

			EndActivePassIfAny();

			out.Valid = true;
			return out;
		}

		void MarkSwapChainRendered() {
			g_Frame.PresentedSwapChain = true;
		}

		void FlushCommands() {
			FlushFrameCommands();
		}

		uint64_t GetFrameSubmitSerial() {
			return g_FrameSubmitSerial;
		}

		BoundTargetSnapshot SaveBoundTarget() {
			BoundTargetSnapshot snap;
			snap.ColorView   = g_CurrentTarget.ColorView;
			snap.DepthView   = g_CurrentTarget.DepthView;
			snap.ColorFormat = g_CurrentTarget.ColorFormat;
			snap.Width       = g_CurrentTarget.Width;
			snap.Height      = g_CurrentTarget.Height;
			snap.SampleCount = g_CurrentTarget.SampleCount;
			snap.IsSwapChain = g_CurrentTarget.IsSwapChain;
			return snap;
		}

		void RestoreBoundTarget(const BoundTargetSnapshot& snap) {
			FlushFrameCommands();

			g_CurrentTarget = TargetState{};
			g_CurrentTarget.ColorView   = snap.ColorView;
			g_CurrentTarget.DepthView   = snap.DepthView;
			g_CurrentTarget.ColorFormat = snap.ColorFormat;
			g_CurrentTarget.Width       = snap.Width;
			g_CurrentTarget.Height      = snap.Height;
			g_CurrentTarget.SampleCount = snap.SampleCount;
			g_CurrentTarget.IsSwapChain = snap.IsSwapChain;
		}

		// ── Multi-viewport surface support ─────────────────────────────────
		// See WebGPUBackend.hpp for the rationale. Each ViewportSurface is
		// self-contained and presents independently of the main surface.

		struct ViewportSurface {
			wgpu::Surface            Surface;
			wgpu::Texture            CurrentTexture;
			wgpu::TextureView        CurrentView;
			uint32_t                 Width  = 0;
			uint32_t                 Height = 0;
			wgpu::PresentMode        PresentMode = wgpu::PresentMode::Fifo;
			wgpu::CompositeAlphaMode AlphaMode   = wgpu::CompositeAlphaMode::Opaque;
			bool                     HasCurrent  = false;
			SDL_Window*              Window      = nullptr;
		};

		std::vector<ViewportSurface*> g_LiveViewportSurfaces;

		namespace {
			bool ConfigureViewportSurface(ViewportSurface* vs,
				uint32_t width, uint32_t height)
			{
				if (!vs || !vs->Surface || width == 0 || height == 0) return false;

				wgpu::SurfaceCapabilities caps{};
				vs->Surface.GetCapabilities(g_Adapter, &caps);

				vs->PresentMode = ChoosePresentMode(caps);
				LogPresentModeChoice("Viewport", caps, vs->PresentMode);
				vs->AlphaMode   = wgpu::CompositeAlphaMode::Opaque;
				for (size_t i = 0; i < caps.alphaModeCount; ++i) {
					if (caps.alphaModes[i] == wgpu::CompositeAlphaMode::Premultiplied) {
						vs->AlphaMode = wgpu::CompositeAlphaMode::Premultiplied;
						break;
					}
				}

				wgpu::SurfaceConfiguration config{};
				config.device          = g_Device;
				config.format          = g_SurfaceFormat;
				config.usage           = wgpu::TextureUsage::RenderAttachment;
				config.width           = width;
				config.height          = height;
				config.presentMode     = vs->PresentMode;
				config.alphaMode       = vs->AlphaMode;
				config.viewFormatCount = 0;

				vs->Surface.Configure(&config);
				vs->Width  = width;
				vs->Height = height;
				return true;
			}
		}

		// Re-Configure requires Unconfigure first on D3D12; called by SetVsync to propagate mode change to all viewport surfaces.
		void ReconfigureAllViewportSurfaces() {
			for (ViewportSurface* vs : g_LiveViewportSurfaces) {
				if (!vs || !vs->Surface || vs->Width == 0 || vs->Height == 0) continue;
				vs->CurrentView    = nullptr;
				vs->CurrentTexture = nullptr;
				vs->HasCurrent     = false;
				vs->Surface.Unconfigure();
				ConfigureViewportSurface(vs, vs->Width, vs->Height);
			}
		}

		void ProvideDevice(wgpu::Instance instance, wgpu::Adapter adapter, wgpu::Device device) {
			g_ProvidedInstance = std::move(instance);
			g_ProvidedAdapter  = std::move(adapter);
			g_ProvidedDevice   = std::move(device);
		}

		ViewportSurface* CreateViewportSurface(SDL_Window* window,
			uint32_t width, uint32_t height)
		{
			if (!g_Initialized || !g_Instance || !window) return nullptr;

			wgpu::Surface surface = WebGPUSurfaceSource::Create(g_Instance, window);
			if (!surface) {
				FNK_CORE_ERROR_TAG("WebGPUApi",
					"CreateViewportSurface: instance.CreateSurface returned null");
				WebGPUSurfaceSource::Release(window);
				return nullptr;
			}

			auto* vs = new ViewportSurface{};
			vs->Surface = std::move(surface);
			vs->Window  = window;
			if (!ConfigureViewportSurface(vs, width, height)) {
				FNK_CORE_ERROR_TAG("WebGPUApi",
					"CreateViewportSurface: ConfigureViewportSurface failed at {}x{}",
					width, height);
				vs->Surface = nullptr;
				WebGPUSurfaceSource::Release(window);
				delete vs;
				return nullptr;
			}
			g_LiveViewportSurfaces.push_back(vs);
			return vs;
		}

		void DestroyViewportSurface(ViewportSurface* vs) {
			if (!vs) return;
			g_LiveViewportSurfaces.erase(
				std::remove(g_LiveViewportSurfaces.begin(), g_LiveViewportSurfaces.end(), vs),
				g_LiveViewportSurfaces.end());
			vs->CurrentView    = nullptr;
			vs->CurrentTexture = nullptr;
			if (vs->Surface) vs->Surface.Unconfigure();
			vs->Surface = nullptr;
			WebGPUSurfaceSource::Release(vs->Window);
			delete vs;
		}

		void ResizeViewportSurface(ViewportSurface* vs,
			uint32_t width, uint32_t height)
		{
			if (!vs || !vs->Surface || width == 0 || height == 0) return;
			if (vs->Width == width && vs->Height == height) return;
			vs->CurrentView    = nullptr;
			vs->CurrentTexture = nullptr;
			vs->HasCurrent     = false;
			ConfigureViewportSurface(vs, width, height);
		}

		wgpu::TextureView AcquireViewportSurfaceView(ViewportSurface* vs) {
			if (!vs || !vs->Surface) return nullptr;
			if (vs->HasCurrent) return vs->CurrentView;

			wgpu::SurfaceTexture st{};
			vs->Surface.GetCurrentTexture(&st);
			const bool ok =
				st.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal ||
				st.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal;
			if (!ok) {
				return nullptr;
			}
			vs->CurrentTexture = wgpu::Texture(st.texture);

			wgpu::TextureViewDescriptor viewDesc{};
			viewDesc.format          = g_SurfaceFormat;
			viewDesc.dimension       = wgpu::TextureViewDimension::e2D;
			viewDesc.mipLevelCount   = 1;
			viewDesc.arrayLayerCount = 1;
			viewDesc.aspect          = wgpu::TextureAspect::All;
			vs->CurrentView = vs->CurrentTexture.CreateView(&viewDesc);
			vs->HasCurrent  = true;
			return vs->CurrentView;
		}

		void PresentViewportSurface(ViewportSurface* vs) {
			if (!vs || !vs->Surface || !vs->HasCurrent) return;
			vs->Surface.Present();
			vs->CurrentView    = nullptr;
			vs->CurrentTexture = nullptr;
			vs->HasCurrent     = false;
		}

		wgpu::TextureFormat GetViewportSurfaceFormat() { return g_SurfaceFormat; }

		wgpu::Instance GetInstance() { return g_Instance; }
	}

	// ── RenderApi: Lifecycle ────────────────────────────────────────────────

	bool RenderApi::Init(const GLInitSpecifications& spec) {
		if (g_Initialized) return false;

#if defined(FNK_PLATFORM_WINDOWS)
		// Dawn's bare-name LoadLibraryEx calls need default search directories on Windows 11 24H2.
		const BOOL didSetDirs = ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		FNK_CORE_INFO_TAG("WebGPUApi", "Default DLL search directories: {} (last-error={})",
			didSetDirs ? "ok" : "FAIL",
			didSetDirs ? 0u : static_cast<uint32_t>(::GetLastError()));
#endif

		g_ClearColor = spec.ClearColor;
		g_VsyncEnabled = Window::IsVsync();

		Window* win = Application::GetWindow();
		if (win) {
			g_BackbufferWidth  = static_cast<uint32_t>(win->GetWidth());
			g_BackbufferHeight = static_cast<uint32_t>(win->GetHeight());
		}
		if (g_BackbufferWidth == 0)  g_BackbufferWidth  = 1280;
		if (g_BackbufferHeight == 0) g_BackbufferHeight = 720;

		const bool provided = g_ProvidedInstance && g_ProvidedAdapter && g_ProvidedDevice;
		if (provided) {
			g_Instance = g_ProvidedInstance;
		}
		else {
			// TimedWaitAny is REQUIRED: without it WaitAny rejects any non-zero timeout and adapter/device requests silently fail.
			const wgpu::InstanceFeatureName requiredFeatures[] = {
				wgpu::InstanceFeatureName::TimedWaitAny,
			};
			wgpu::InstanceDescriptor instanceDesc{};
			instanceDesc.requiredFeatureCount = 1;
			instanceDesc.requiredFeatures     = requiredFeatures;
			g_Instance = wgpu::CreateInstance(&instanceDesc);
		}
		if (!g_Instance) {
			FNK_CORE_ERROR_TAG("WebGPUApi", "wgpu::CreateInstance failed");
			return false;
		}

		if (!CreateSurface())       { Shutdown(); return false; }
		if (provided) {
			g_Adapter = g_ProvidedAdapter;
			g_Device  = g_ProvidedDevice;
			g_Queue   = g_Device.GetQueue();
			g_HasTimestampQuery = g_Device.HasFeature(wgpu::FeatureName::TimestampQuery);
		}
		else {
			if (!RequestAdapterSync())  { Shutdown(); return false; }
			if (!RequestDeviceSync())   { Shutdown(); return false; }
		}
		ConfigureSurface(g_BackbufferWidth, g_BackbufferHeight);

		wgpu::AdapterInfo info{};
		g_Adapter.GetInfo(&info);
		g_VendorString   = FromStringView(info.vendor);
		g_RendererString = FromStringView(info.device);
		g_GraphicsApi    = MapGraphicsApi(info.backendType);
		g_GpuVendor      = MapGpuVendor(info.vendorID, g_VendorString, g_RendererString);
		g_VersionString  = std::string("WebGPU (Dawn) / ") + BackendTypeName(info.backendType)
			+ " / " + AdapterTypeName(info.adapterType);

		FNK_CORE_INFO_TAG("WebGPUApi",
			"WebGPU initialized — adapter='{}' vendor='{}' backend={} type={} backbuffer={}x{} format={}",
			g_RendererString, g_VendorString,
			BackendTypeName(info.backendType), AdapterTypeName(info.adapterType),
			g_BackbufferWidth, g_BackbufferHeight,
			static_cast<int>(g_SurfaceFormat));

		g_Initialized = true;
		return true;
	}

	void RenderApi::Present() {
		WebGPUBackend::Present();
	}

	void RenderApi::Shutdown() {
		if (!g_Initialized && !g_Instance) return;

		g_Frame = FrameState{};
		Framebuffer::ReleaseAllBackendResources();
		Texture2D::ReleaseMipGenResources();
		if (g_Surface) g_Surface.Unconfigure();
		g_CurrentTarget = TargetState{};
		// Surface before device: Dawn's Vulkan swapchain detaches through the device when the surface dies.
		g_Surface = nullptr;
		g_Queue   = nullptr;
		g_Device  = nullptr;
		g_Adapter = nullptr;
		WebGPUSurfaceSource::Release(g_SurfaceWindow);
		g_SurfaceWindow = nullptr;
		g_SurfaceNativeHandle = nullptr;
		g_Instance = nullptr;

		g_VersionString.clear();
		g_VendorString.clear();
		g_RendererString.clear();
		g_GraphicsApi = GraphicsApi::Unknown;
		g_GpuVendor = GpuVendor::Unknown;
		g_SurfaceFormat = wgpu::TextureFormat::Undefined;

		g_Initialized = false;
	}

	bool RenderApi::IsInitialized() {
		return g_Initialized;
	}

	std::string_view RenderApi::BackendName() {
		return "webgpu";
	}

	GraphicsApi RenderApi::GetGraphicsApi() noexcept { return g_GraphicsApi; }
	GpuVendor RenderApi::GetGpuVendor() noexcept { return g_GpuVendor; }
	const std::string& RenderApi::GetVersionString() noexcept  { return g_VersionString; }
	const std::string& RenderApi::GetVendorString() noexcept   { return g_VendorString; }
	const std::string& RenderApi::GetRendererString() noexcept { return g_RendererString; }

	// ── Per-frame state ─────────────────────────────────────────────────────

	void RenderApi::Clear(ClearFlags /*flags*/) {
		EnsureFrameEncoder();

		wgpu::TextureView targetColorView;
		if (g_CurrentTarget.IsSwapChain) {
			if (!EnsureSurfaceTexture()) return;
			targetColorView = g_Frame.SurfaceView;
		} else {
			targetColorView = g_CurrentTarget.ColorView;
			if (!targetColorView) {
				FNK_CORE_WARN_TAG("WebGPUApi", "Clear on FBO target with no colour view bound");
				return;
			}
		}

		EndActivePassIfAny();

		wgpu::RenderPassColorAttachment colorAtt{};
		colorAtt.view       = targetColorView;
		colorAtt.loadOp     = wgpu::LoadOp::Clear;
		colorAtt.storeOp    = wgpu::StoreOp::Store;
		colorAtt.clearValue = wgpu::Color{
			g_ClearColor.r, g_ClearColor.g, g_ClearColor.b, g_ClearColor.a };
		colorAtt.depthSlice = wgpu::kDepthSliceUndefined;

		wgpu::RenderPassDescriptor passDesc{};
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments     = &colorAtt;

		wgpu::RenderPassDepthStencilAttachment depthAtt{};
		if (!g_CurrentTarget.IsSwapChain && g_CurrentTarget.DepthView) {
			depthAtt.view              = g_CurrentTarget.DepthView;
			depthAtt.depthLoadOp       = wgpu::LoadOp::Clear;
			depthAtt.depthStoreOp      = wgpu::StoreOp::Store;
			depthAtt.depthClearValue   = 1.0f;
			depthAtt.stencilLoadOp     = wgpu::LoadOp::Clear;
			depthAtt.stencilStoreOp    = wgpu::StoreOp::Store;
			depthAtt.stencilClearValue = 0;
			passDesc.depthStencilAttachment = &depthAtt;
		}

		wgpu::RenderPassEncoder pass = g_Frame.Encoder.BeginRenderPass(&passDesc);
		pass.End();

		if (g_CurrentTarget.IsSwapChain) {
			g_Frame.PresentedSwapChain = true;
		}
	}

	void RenderApi::SetClearColor(const Color& color) {
		g_ClearColor = color;
		// No GPU work — value picked up on next Clear / next render pass.
	}

	Color RenderApi::GetClearColor() {
		return g_ClearColor;
	}

	void RenderApi::SetViewport(int x, int y, int width, int height) {
		g_ViewportX = static_cast<uint32_t>(x < 0 ? 0 : x);
		g_ViewportY = static_cast<uint32_t>(y < 0 ? 0 : y);
		g_ViewportW = static_cast<uint32_t>(width  > 0 ? width  : 1);
		g_ViewportH = static_cast<uint32_t>(height > 0 ? height : 1);
	}

	void RenderApi::OnWindowResize(int width, int height) {
		if (!g_Initialized) return;
		if (width <= 0 || height <= 0) return;

		const uint32_t uw = static_cast<uint32_t>(width);
		const uint32_t uh = static_cast<uint32_t>(height);
		if (uw == g_BackbufferWidth && uh == g_BackbufferHeight) return;

#ifdef FNK_DEBUG
		FNK_CORE_INFO_TAG("WebGPUApi",
			"OnWindowResize: {}x{} -> {}x{} (surface.Configure)",
			g_BackbufferWidth, g_BackbufferHeight, uw, uh);
#endif

		g_Frame = FrameState{};
		// Unconfigure before Configure: D3D12 re-Configure reuses the swap chain and can preserve stale flags; clean teardown forces a fresh one.
		g_Surface.Unconfigure();
		ConfigureSurface(uw, uh);
	}

	void RenderApi::SetVsync(bool enabled) {
		// Rebuilding the swap chain for the same mode costs a frame (games apply their settings on every scene load).
		if (g_Initialized && g_Surface && enabled == g_VsyncEnabled) return;
		g_VsyncEnabled = enabled;
		if (!g_Initialized || !g_Surface) return;
		if (g_BackbufferWidth == 0 || g_BackbufferHeight == 0) return;

		FNK_CORE_INFO_TAG("WebGPUApi",
			"SetVsync: enabled={} -> unconfigure + reconfigure", enabled);

		// g_Frame MUST reset before Unconfigure (Dawn rejects views that outlive the surface).
		g_Frame = FrameState{};
		g_Surface.Unconfigure();
		ConfigureSurface(g_BackbufferWidth, g_BackbufferHeight);
		WebGPUBackend::ReconfigureAllViewportSurfaces();
	}

	void RenderApi::SetScissor(int x, int y, int width, int height) {
		g_ScissorX = static_cast<uint32_t>(x < 0 ? 0 : x);
		g_ScissorY = static_cast<uint32_t>(y < 0 ? 0 : y);
		g_ScissorW = static_cast<uint32_t>(width  > 0 ? width  : 0);
		g_ScissorH = static_cast<uint32_t>(height > 0 ? height : 0);
		g_ScissorActive = (g_ScissorW > 0 && g_ScissorH > 0);
	}

	void RenderApi::EnableScissorTest()  { g_ScissorActive = (g_ScissorW > 0 && g_ScissorH > 0); }
	void RenderApi::DisableScissorTest() { g_ScissorActive = false; }

	void RenderApi::EnableDepthTest()                            { /* per-pipeline via DepthStencilState; Stage 2 */ }
	void RenderApi::DisableDepthTest()                           { /* per-pipeline via DepthStencilState; Stage 2 */ }
	void RenderApi::SetCullMode(CullMode /*mode*/)               { /* per-pipeline via PrimitiveState::cullMode; Stage 2 */ }
	void RenderApi::SetBlendMode(BlendMode /*mode*/)             { /* per-pipeline via BlendState; Stage 2 */ }
	void RenderApi::SetBlendingEnabled(bool /*enabled*/)         { /* per-pipeline via ColorTargetState::blend; Stage 2 */ }
	void RenderApi::SetPolygonMode(PolygonMode mode)             { g_PolygonMode = mode; }
	PolygonMode RenderApi::GetPolygonMode()                     { return g_PolygonMode; }
	void RenderApi::SetLineWidth(float /*width*/)                { /* WebGPU has no wide-lines feature — gpu-side wide lines or fat-line geometry shader in Stage 2 */ }
	void RenderApi::SetColorMask(bool /*r*/, bool /*g*/, bool /*b*/, bool /*a*/) {
		// Per-pipeline via ColorTargetState::writeMask; Stage 2.
	}

	void RenderApi::BindFramebuffer(const Framebuffer& fbo) {
		const uint32_t backendId = fbo.GetBackendId();
		if (backendId == 0) {
			BindDefaultFramebuffer();
			return;
		}

		const auto lookup = WebGPUBackend::LookupFramebufferByFboId(backendId);
		if (!lookup.Valid) {
			FNK_CORE_WARN_TAG("WebGPUApi",
				"BindFramebuffer: no GPU resources for fboId={} — falling back to swap chain",
				backendId);
			BindDefaultFramebuffer();
			return;
		}

		const bool sameTarget = !g_CurrentTarget.IsSwapChain
			&& g_CurrentTarget.ColorView.Get() == lookup.ColorView.Get();
		if (!sameTarget) {
			FlushFrameCommands();
		} else {
			EndActivePassIfAny();
		}

		g_CurrentTarget = TargetState{};
		g_CurrentTarget.ColorView   = lookup.ColorView;
		g_CurrentTarget.DepthView   = lookup.DepthView;
		g_CurrentTarget.ColorFormat = lookup.ColorFormat;
		g_CurrentTarget.IsSwapChain = false;
		g_CurrentTarget.Width       = lookup.Width;
		g_CurrentTarget.Height      = lookup.Height;
		g_CurrentTarget.SampleCount = lookup.SampleCount;
	}

	void RenderApi::BindDefaultFramebuffer() {
		if (!g_CurrentTarget.IsSwapChain) {
			FlushFrameCommands();
		} else {
			EndActivePassIfAny();
		}

		g_CurrentTarget = TargetState{};
		g_CurrentTarget.IsSwapChain = true;
		g_CurrentTarget.ColorFormat = g_SurfaceFormat;
		g_CurrentTarget.Width       = g_BackbufferWidth;
		g_CurrentTarget.Height      = g_BackbufferHeight;
	}

}  // namespace Funke
