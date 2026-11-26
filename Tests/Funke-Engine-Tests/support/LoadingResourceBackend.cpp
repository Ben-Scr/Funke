// Opt-in source-built resource probe. No window, swapchain or engine loop.
// Defaults to D3D12. The explicit null mode tests resource lifecycle/validation only, never GPU execution.
#include "Graphics/Backend/WebGPUBackend.hpp"
#include <iostream>
#include <cstdlib>
#include <string>
#include <atomic>

namespace {
	wgpu::Instance g_Instance;
	wgpu::Device g_Device;
	wgpu::Queue g_Queue;
	std::atomic<unsigned> g_Errors = 0;
    bool g_Available = true;
	uint32_t g_Limit = 8192;
}
bool InitializeLoadingResourceBackend() {
	const wgpu::InstanceFeatureName feature = wgpu::InstanceFeatureName::TimedWaitAny;
	wgpu::InstanceDescriptor instanceDesc{};
	instanceDesc.requiredFeatureCount = 1; instanceDesc.requiredFeatures = &feature;
	g_Instance = wgpu::CreateInstance(&instanceDesc);
	if (!g_Instance) return false;
	wgpu::RequestAdapterOptions options{}; options.backendType = wgpu::BackendType::D3D12;
    if (const char* mode = std::getenv("FUNKE_LOADING_BACKEND")) {
        if (std::string(mode) == "vulkan") options.backendType = wgpu::BackendType::Vulkan;
        else if (std::string(mode) == "null") options.backendType = wgpu::BackendType::Null;
        std::cout << "Requested backend: " << mode << '\n';
    }
	options.powerPreference = wgpu::PowerPreference::HighPerformance;
	wgpu::Adapter adapter;
	auto future = g_Instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
		[&](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
			if (status == wgpu::RequestAdapterStatus::Success) adapter = std::move(result);
			else std::cerr << "Adapter unavailable: " << std::string(message.data, message.length) << '\n';
		});
	wgpu::FutureWaitInfo wait{ future }; g_Instance.WaitAny(1, &wait, 30000000000ULL);
	if (!adapter) return false;
	wgpu::AdapterInfo info{}; adapter.GetInfo(&info);
	std::cout << "Resource probe adapter: " << std::string(info.description.data, info.description.length) << '\n';
	wgpu::DeviceDescriptor desc{};
	desc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
		++g_Errors; std::cerr << "WebGPU validation: " << std::string(message.data, message.length) << '\n';
	});
	future = adapter.RequestDevice(&desc, wgpu::CallbackMode::WaitAnyOnly,
		[](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
			if (status == wgpu::RequestDeviceStatus::Success) g_Device = std::move(device);
			else std::cerr << "Device unavailable: " << std::string(message.data, message.length) << '\n';
		});
	wait = { future }; g_Instance.WaitAny(1, &wait, 30000000000ULL);
	if (!g_Device) return false;
	g_Queue = g_Device.GetQueue();
	wgpu::Limits limits{}; g_Device.GetLimits(&limits); g_Limit = limits.maxTextureDimension2D;
	return true;
}
void SetLoadingResourceBackendAvailable(bool available) { g_Available = available; }
unsigned LoadingResourceBackendErrors() { return g_Errors.load(); }
void ShutdownLoadingResourceBackend() { g_Queue = nullptr; g_Device = nullptr; g_Instance = nullptr; }
namespace Funke::WebGPUBackend {
	wgpu::Device GetDevice() { return g_Available ? g_Device : wgpu::Device{}; }
	wgpu::Queue GetQueue() { return g_Available ? g_Queue : wgpu::Queue{}; }
	bool IsInitialized() { return g_Available && bool(g_Device); }
	uint32_t GetMaxTextureDimension2D() { return g_Limit; }
}
