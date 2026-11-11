#pragma once

#include "Core/Export.hpp"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Funke {

#ifdef FUNKE_PROFILER_ENABLED
	// Methods and availability queries run on the render thread; callbacks own only detached results.
	class FUNKE_API GpuTimer {
	public:
		explicit GpuTimer(std::string moduleName = "GPU");
		~GpuTimer();
		GpuTimer(const GpuTimer&) = delete;
		GpuTimer& operator=(const GpuTimer&) = delete;

		enum class Status : uint8_t {
			Uninitialized,
			Unsupported,
			Ready,
			ReadbackError,
			DeviceLost,
		};
		Status GetStatus() const { return m_Status; }

		static bool IsSupported();
		static const char* GetUnavailableReason();

		// Allocates the QuerySet + resolve + readback buffers when the
		// WebGPU device was created with TimestampQuery enabled. No-op
		// otherwise. Must run after the WebGPU device exists.
		void Initialize();

		// Releases all GPU resources. Safe to call before any Initialize.
		void Shutdown();

		// Slot info handed to the renderer so it can attach a
		// wgpu::RenderPassTimestampWrites to its pass descriptor. The
		// QuerySet field stays non-null only when Valid is true.
		struct FrameSlots {
			wgpu::QuerySet QuerySet;
			uint32_t       BeginningOfPassWriteIndex = 0;
			uint32_t       EndOfPassWriteIndex       = 0;
			bool           Valid = false;
		};

		FrameSlots BeginFrameWrites();

		// MapAsync deferred to OnFrameStart(): issuing it here races with the pending Submit and Dawn rejects 'used in submit while mapped'.
		void ResolveCurrentFrame(wgpu::CommandEncoder encoder);

		void OnFrameStart();

		// Polls any pending readbacks and publishes completed deltas into
		// the "GPU" profiler module (milliseconds). Non-blocking. Call once
		// per frame from outside any render pass.
		void PollAndPublish();

		// Returns -1 when no driver extension is available. WebGPU has no
		// portable surface for GPU memory queries today.
		static long long QueryGpuMemoryMb();

	private:
		friend struct GpuTimerTestAccess;
		void Initialize(wgpu::Instance instance, wgpu::Device device);
		bool CheckDevice();
		void Disable(Status status, const char* reason);

		static constexpr size_t k_RingDepth        = 3;
		static constexpr size_t k_QueriesPerFrame  = 2;          // begin + end
		static constexpr size_t k_BytesPerQuery    = 8;          // u64 timestamp
		static constexpr size_t k_BytesPerFrame    = k_QueriesPerFrame * k_BytesPerQuery; // 16
		// 256-byte stride: WebGPU requires ResolveQuerySet dest offset to be 256-aligned; each slot wastes 240 bytes of padding.
		static constexpr size_t k_ResolveSlotStride = 256;

		enum class SlotState : uint8_t {
			Empty,           // Available for the next frame's resolve
			AwaitingSubmit,  // Copy encoded into this frame's cmd buffer; MapAsync deferred
			AwaitingMap,     // MapAsync issued; PollAndPublish drains on completion
		};

		struct ReadbackResult {
			std::atomic<bool> Completed{ false };
			wgpu::MapAsyncStatus Status = wgpu::MapAsyncStatus::Error;
			std::string Message;
		};

		static void CompleteReadback(const std::shared_ptr<ReadbackResult>& result,
			wgpu::MapAsyncStatus status, wgpu::StringView message);

		struct Slot {
			wgpu::Buffer Readback;
			SlotState    State = SlotState::Empty;
			std::shared_ptr<ReadbackResult> Result;
		};

		wgpu::Instance                m_Instance;
		wgpu::Future                  m_DeviceLostFuture{};
		wgpu::QuerySet                m_QuerySet;
		wgpu::Buffer                  m_ResolveBuffer;
		std::array<Slot, k_RingDepth> m_Slots{};
		size_t                        m_CurrentSlot = 0;
		Status                        m_Status = Status::Uninitialized;
		bool                          m_FrameWritesIssued = false;
		std::string m_ModuleName;
	};
#else
	class GpuTimer {
	public:
		static bool IsSupported() { return false; }
		static const char* GetUnavailableReason() { return "Profiling is disabled"; }
		static long long QueryGpuMemoryMb() { return -1; }
	private:
		GpuTimer() = delete;
	};
#endif

#ifdef FUNKE_PROFILER_ENABLED
	// Resolve after pass.End, before submitting the encoder. Capture is opt-in and never waits for GPU completion.
	class FUNKE_API GpuPassTrace {
	public:
		GpuPassTrace(wgpu::CommandEncoder encoder, wgpu::RenderPassDescriptor& desc);
		GpuPassTrace(wgpu::CommandEncoder encoder, wgpu::ComputePassDescriptor& desc);
		void Resolve();
		static void AfterSubmit();
		static void Shutdown();
	private:
		void Begin(wgpu::CommandEncoder encoder, wgpu::StringView label);
		wgpu::CommandEncoder m_Encoder;
		wgpu::PassTimestampWrites m_Writes{};
		GpuTimer* m_Timer = nullptr;
	};
#else
	class GpuPassTrace {
	public:
		GpuPassTrace(wgpu::CommandEncoder, wgpu::RenderPassDescriptor&) {}
		GpuPassTrace(wgpu::CommandEncoder, wgpu::ComputePassDescriptor&) {}
		void Resolve() {}
		static void AfterSubmit() {}
		static void Shutdown() {}
	};
#endif

} // namespace Funke
