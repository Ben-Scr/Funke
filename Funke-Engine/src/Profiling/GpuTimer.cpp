#include "pch.hpp"
#include "Profiling/GpuTimer.hpp"

#ifdef FUNKE_PROFILER_ENABLED

#include "Core/Log.hpp"
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Profiling/Profiler.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Funke {
	namespace {
		size_t g_ActiveTimers = 0;
		GpuTimer::Status g_UnavailableStatus = GpuTimer::Status::Uninitialized;
		std::unordered_map<std::string, std::vector<std::unique_ptr<GpuTimer>>> g_PassTimers;

		bool IsDeviceLost(const wgpu::Instance& instance, wgpu::Future future) {
			if (!instance || future.id == 0) return true;
			wgpu::FutureWaitInfo waitInfo{};
			waitInfo.future = future;
			return instance.WaitAny(1, &waitInfo, 0) == wgpu::WaitStatus::Success && waitInfo.completed;
		}
	}

	GpuTimer::GpuTimer(std::string moduleName) : m_ModuleName(std::move(moduleName)) {}
	GpuTimer::~GpuTimer() { Shutdown(); }

	bool GpuTimer::IsSupported() {
		if (!WebGPUBackend::IsInitialized() || !WebGPUBackend::HasTimestampQuery()) return false;
		const auto device = WebGPUBackend::GetDevice();
		return device && !IsDeviceLost(WebGPUBackend::GetInstance(), device.GetLostFuture());
	}

	const char* GpuTimer::GetUnavailableReason() {
		if (g_ActiveTimers > 0) return nullptr;
		switch (g_UnavailableStatus) {
			case Status::Unsupported: return "Timestamp queries are unsupported";
			case Status::ReadbackError: return "GPU readback failed; see log";
			case Status::DeviceLost: return "GPU device was lost";
			default: return "GPU timer is not initialized";
		}
	}

	void GpuTimer::Initialize() {
		if (!WebGPUBackend::IsInitialized()) return;
		Initialize(WebGPUBackend::GetInstance(), WebGPUBackend::GetDevice());
	}

	void GpuTimer::Initialize(wgpu::Instance instance, wgpu::Device device) {
		if (m_Status == Status::Ready) return;
		Shutdown();
		if (!instance || !device) return;
		if (IsDeviceLost(instance, device.GetLostFuture())) {
			Disable(Status::DeviceLost, "GPU device was lost before timer initialization");
			return;
		}
		if (!device.HasFeature(wgpu::FeatureName::TimestampQuery)) {
			Disable(Status::Unsupported, "Timestamp queries are unsupported on this device");
			return;
		}
		m_Instance = std::move(instance);
		m_DeviceLostFuture = device.GetLostFuture();

		wgpu::QuerySetDescriptor qsDesc{};
		qsDesc.type  = wgpu::QueryType::Timestamp;
		qsDesc.count = static_cast<uint32_t>(k_QueriesPerFrame * k_RingDepth);
		qsDesc.label = "gpu-timer-queryset";
		m_QuerySet = device.CreateQuerySet(&qsDesc);
		if (!m_QuerySet) {
			Disable(Status::ReadbackError, "Failed to create timestamp query set");
			return;
		}

		wgpu::BufferDescriptor resolveDesc{};
		// k_ResolveSlotStride=256: WebGPU requires ResolveQuerySet's destinationOffset to be a multiple of 256.
		resolveDesc.size  = static_cast<uint64_t>(k_ResolveSlotStride * k_RingDepth);
		resolveDesc.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
		resolveDesc.label = "gpu-timer-resolve";
		m_ResolveBuffer = device.CreateBuffer(&resolveDesc);
		if (!m_ResolveBuffer) {
			Disable(Status::ReadbackError, "Failed to create resolve buffer");
			return;
		}

		wgpu::BufferDescriptor rbDesc{};
		rbDesc.size  = static_cast<uint64_t>(k_BytesPerFrame);
		rbDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
		std::string label;
		for (size_t i = 0; i < k_RingDepth; ++i) {
			label = "gpu-timer-readback-" + std::to_string(i);
			rbDesc.label = label.c_str();
			m_Slots[i].Readback = device.CreateBuffer(&rbDesc);
			m_Slots[i].State    = SlotState::Empty;
			if (!m_Slots[i].Readback) {
				Disable(Status::ReadbackError, "Failed to create readback buffer");
				return;
			}
		}

		m_CurrentSlot       = 0;
		m_FrameWritesIssued = false;
		m_Status            = Status::Ready;
		++g_ActiveTimers;
	}

	void GpuTimer::Shutdown() {
		if (m_Status == Status::Ready && --g_ActiveTimers == 0) {
			g_UnavailableStatus = Status::Uninitialized;
		}
		m_Status = Status::Uninitialized;
		m_QuerySet      = nullptr;
		m_ResolveBuffer = nullptr;
		for (auto& s : m_Slots) {
			// Late callbacks own only their detached result, never the slot or timer.
			s.Result.reset();
			if (s.Readback) {
				s.Readback.Unmap();
			}
			s.Readback = nullptr;
			s.State    = SlotState::Empty;
		}
		m_CurrentSlot       = 0;
		m_FrameWritesIssued = false;
		m_DeviceLostFuture  = {};
		m_Instance          = nullptr;
	}

	void GpuTimer::Disable(Status status, const char* reason) {
		Shutdown();
		m_Status = status;
		g_UnavailableStatus = status;
		FNK_CORE_WARN_TAG("GpuTimer", "GPU timing unavailable: {}", reason);
	}

	bool GpuTimer::CheckDevice() {
		if (m_Status != Status::Ready) return false;
		if (!IsDeviceLost(m_Instance, m_DeviceLostFuture)) return true;
		Disable(Status::DeviceLost, "GPU device was lost; pending timestamp readbacks discarded");
		return false;
	}

	GpuTimer::FrameSlots GpuTimer::BeginFrameWrites() {
		FrameSlots out;
		if (!CheckDevice()) return out;
		if (m_Slots[m_CurrentSlot].State != SlotState::Empty) {
			return out;
		}
		out.QuerySet                  = m_QuerySet;
		out.BeginningOfPassWriteIndex = static_cast<uint32_t>(m_CurrentSlot * k_QueriesPerFrame);
		out.EndOfPassWriteIndex       = static_cast<uint32_t>(m_CurrentSlot * k_QueriesPerFrame + 1);
		out.Valid                     = true;
		m_FrameWritesIssued           = true;
		return out;
	}

	void GpuTimer::ResolveCurrentFrame(wgpu::CommandEncoder encoder) {
		if (!CheckDevice() || !m_FrameWritesIssued || !encoder) return;

		const uint32_t firstQuery    = static_cast<uint32_t>(m_CurrentSlot * k_QueriesPerFrame);
		// MUST use k_ResolveSlotStride (256), not payload size (16) — Dawn validates destinationOffset%256==0.
		const uint64_t resolveOffset = static_cast<uint64_t>(m_CurrentSlot * k_ResolveSlotStride);
		encoder.ResolveQuerySet(m_QuerySet,
			firstQuery, static_cast<uint32_t>(k_QueriesPerFrame),
			m_ResolveBuffer, resolveOffset);
		encoder.CopyBufferToBuffer(m_ResolveBuffer, resolveOffset,
			m_Slots[m_CurrentSlot].Readback, 0,
			static_cast<uint64_t>(k_BytesPerFrame));

		// MapAsync deferred to OnFrameStart(): calling it here transitions readback to Pending before
		// Queue::Submit, causing Dawn "used in submit while mapped" validation failure on the whole frame.
		m_Slots[m_CurrentSlot].State = SlotState::AwaitingSubmit;

		m_FrameWritesIssued = false;
		m_CurrentSlot       = (m_CurrentSlot + 1) % k_RingDepth;
	}

	void GpuTimer::OnFrameStart() {
		if (!CheckDevice()) return;
		for (auto& s : m_Slots) {
			if (s.State != SlotState::AwaitingSubmit) continue;
			s.Result = std::make_shared<ReadbackResult>();
			s.State = SlotState::AwaitingMap;
			s.Readback.MapAsync(
				wgpu::MapMode::Read, 0,
				static_cast<size_t>(k_BytesPerFrame),
				wgpu::CallbackMode::AllowProcessEvents,
				[result = s.Result](wgpu::MapAsyncStatus status, wgpu::StringView message) {
					CompleteReadback(result, status, message);
				});
		}
	}

	void GpuTimer::CompleteReadback(const std::shared_ptr<ReadbackResult>& result,
		wgpu::MapAsyncStatus status, wgpu::StringView message) {
		result->Status = status;
		if (message.data && message.length > 0) {
			result->Message.assign(message.data,
				message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length);
		}
		result->Completed.store(true, std::memory_order_release);
	}

	void GpuTimer::PollAndPublish() {
		if (!CheckDevice()) return;
		m_Instance.ProcessEvents();
		if (!CheckDevice()) return;

		for (auto& s : m_Slots) {
			if (s.State != SlotState::AwaitingMap) continue;
			if (!s.Result->Completed.load(std::memory_order_acquire)) continue;
			if (s.Result->Status != wgpu::MapAsyncStatus::Success) {
				FNK_CORE_WARN_TAG("GpuTimer", "MapAsync failed (status {}): {}",
					static_cast<uint32_t>(s.Result->Status), s.Result->Message);
				Disable(Status::ReadbackError, "Timestamp readback failed; timer disabled until reinitialized");
				return;
			}
			if (!s.Readback || s.Readback.GetMapState() != wgpu::BufferMapState::Mapped) {
				Disable(Status::ReadbackError, "Successful timestamp readback is no longer mapped");
				return;
			}

			const void* mapped = s.Readback.GetConstMappedRange(0, k_BytesPerFrame);
			if (!mapped) {
				Disable(Status::ReadbackError, "Timestamp readback has no mapped range");
				return;
			}
			uint64_t ts[2] = { 0, 0 };
			std::memcpy(ts, mapped, sizeof(ts));
			const uint64_t deltaNs = ts[1] > ts[0] ? ts[1] - ts[0] : 0;
			const float deltaMs = static_cast<float>(deltaNs) * 1.0e-6f;
			Profiler::PushSample(m_ModuleName, deltaMs);
			if (m_ModuleName == "GPU") TracyPlot("GPU.ms", double(deltaMs));
			s.Readback.Unmap();
			s.Result.reset();
			s.State = SlotState::Empty;
		}
	}

	long long GpuTimer::QueryGpuMemoryMb() {
		return -1;
	}

	void GpuPassTrace::Begin(wgpu::CommandEncoder encoder, wgpu::StringView label) {
		if (!Profiler::IsFrameCaptureEnabled() || !encoder) return;
		const std::string name = "GPU." + std::string(label.data ? label.data : "unnamed",
			label.data ? (label.length == WGPU_STRLEN ? std::strlen(label.data) : label.length) : 7);
		Profiler::Register("GPU.PassRequests");
		Profiler::Register("GPU.PassUnavailable");
		Profiler::PushValue("GPU.PassRequests", 1);
		if (GpuTimer::IsSupported()) {
			auto& timers = g_PassTimers[name];
			Profiler::Register(name);
			for (size_t index = 0; index <= timers.size() && index < 16; ++index) {
				if (index == timers.size()) {
					auto timer = std::make_unique<GpuTimer>(name);
					timer->Initialize();
					if (timer->GetStatus() != GpuTimer::Status::Ready) break;
					timers.push_back(std::move(timer));
				}
				const auto slots = timers[index]->BeginFrameWrites();
				if (!slots.Valid) continue;
				m_Timer = timers[index].get();
				m_Encoder = encoder;
				m_Writes.querySet = slots.QuerySet;
				m_Writes.beginningOfPassWriteIndex = slots.BeginningOfPassWriteIndex;
				m_Writes.endOfPassWriteIndex = slots.EndOfPassWriteIndex;
				return;
			}
		}
		Profiler::PushValue("GPU.PassUnavailable", 1);
	}

	GpuPassTrace::GpuPassTrace(wgpu::CommandEncoder encoder, wgpu::RenderPassDescriptor& desc) {
		Begin(encoder, desc.label);
		if (m_Timer) desc.timestampWrites = &m_Writes;
	}

	GpuPassTrace::GpuPassTrace(wgpu::CommandEncoder encoder, wgpu::ComputePassDescriptor& desc) {
		Begin(encoder, desc.label);
		if (m_Timer) desc.timestampWrites = &m_Writes;
	}

	void GpuPassTrace::Resolve() {
		if (m_Timer) m_Timer->ResolveCurrentFrame(m_Encoder);
		m_Timer = nullptr;
		m_Encoder = nullptr;
	}

	void GpuPassTrace::AfterSubmit() {
		for (auto& [name, timers] : g_PassTimers) for (auto& timer : timers) {
			timer->OnFrameStart();
			timer->PollAndPublish();
		}
	}

	void GpuPassTrace::Shutdown() { g_PassTimers.clear(); }

}  // namespace Funke

#else  // !FUNKE_PROFILER_ENABLED
namespace Funke {

	GpuTimer::GpuTimer(std::string moduleName) : m_ModuleName(std::move(moduleName)) {}
	GpuTimer::~GpuTimer() = default;
	void GpuTimer::Initialize() { m_Status = Status::Unsupported; }
	void GpuTimer::Shutdown() {}
	GpuTimer::FrameSlots GpuTimer::BeginFrameWrites() { return {}; }
	void GpuTimer::ResolveCurrentFrame(wgpu::CommandEncoder) {}
	void GpuTimer::OnFrameStart() {}
	void GpuTimer::PollAndPublish() {}
	long long GpuTimer::QueryGpuMemoryMb() { return -1; }

}
#endif  // FUNKE_PROFILER_ENABLED
