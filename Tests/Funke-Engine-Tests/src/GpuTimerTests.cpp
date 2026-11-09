#include <doctest/doctest.h>

#if defined(FUNKE_PROFILER_ENABLED) && FUNKE_WITH_RENDER

#include "GpuTestEvidence.hpp"
#include "Profiling/GpuTimer.hpp"
#include "Profiling/Profiler.hpp"
#include "Graphics/SkinningCompute.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace Funke {
	struct GpuTimerTestAccess {
		using Result = GpuTimer::ReadbackResult;

		static void Initialize(GpuTimer& timer, wgpu::Instance instance, wgpu::Device device) {
			timer.Initialize(std::move(instance), std::move(device));
		}

		static std::vector<std::shared_ptr<Result>> Results(const GpuTimer& timer) {
			std::vector<std::shared_ptr<Result>> results;
			for (const auto& slot : timer.m_Slots) if (slot.Result) results.push_back(slot.Result);
			return results;
		}

		static bool Empty(const GpuTimer& timer) {
			for (const auto& slot : timer.m_Slots) {
				if (slot.State != GpuTimer::SlotState::Empty || slot.Result) return false;
			}
			return true;
		}

		static void DestroyReadback(GpuTimer& timer) { timer.m_Slots[0].Readback.Destroy(); }

		static std::function<void(wgpu::MapAsyncStatus, wgpu::StringView)> PendingCallback(GpuTimer& timer) {
			auto result = std::make_shared<Result>();
			timer.m_Slots[0].Result = result;
			return [result](wgpu::MapAsyncStatus status, wgpu::StringView message) {
				GpuTimer::CompleteReadback(result, status, message);
			};
		}
	};
}

using namespace Funke;

namespace {
	std::string CopyMessage(wgpu::StringView message) {
		if (!message.data) return {};
		return std::string(message.data, message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length);
	}

	template<class TPredicate>
	bool Until(TPredicate predicate) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!predicate()) {
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}

	bool Wait(const wgpu::Instance& instance, wgpu::Future future) {
		return Until([&] { return instance.WaitAny(future, 0) == wgpu::WaitStatus::Success; });
	}

	struct DeviceEvents {
		std::mutex Mutex;
		bool Lost = false;
		wgpu::DeviceLostReason Reason = wgpu::DeviceLostReason::Unknown;
		std::string Message;
		std::vector<std::string> Errors;
	};

	struct TimerDevice {
		wgpu::Instance Instance;
		wgpu::Adapter Adapter;
		wgpu::Device Device;
		std::shared_ptr<DeviceEvents> Events = std::make_shared<DeviceEvents>();

		void Initialize(bool timestamps) {
#ifdef _WIN32
			REQUIRE(SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
			static const HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
			REQUIRE(compiler);
#endif
			Instance = wgpu::CreateInstance();
			REQUIRE(Instance);
			wgpu::RequestAdapterOptions options{};
			const auto backend = GpuTestEvidence::Environment("FUNKE_TEST_GPU_BACKEND");
			if (backend == "Direct3D12") options.backendType = wgpu::BackendType::D3D12;
			else if (backend == "Vulkan") options.backendType = wgpu::BackendType::Vulkan;
			else if (backend == "Metal") options.backendType = wgpu::BackendType::Metal;
			else REQUIRE(backend.empty());
			const auto adapterClass = GpuTestEvidence::Environment("FUNKE_TEST_GPU_ADAPTER");
			REQUIRE((adapterClass.empty() || adapterClass == "hardware"));
			auto adapter = std::make_shared<wgpu::Adapter>();
			auto future = Instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
				[adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter value, wgpu::StringView) {
					if (status == wgpu::RequestAdapterStatus::Success) *adapter = std::move(value);
				});
			REQUIRE(Wait(Instance, future));
			Adapter = std::move(*adapter);
			REQUIRE(Adapter);
			if (timestamps) REQUIRE(Adapter.HasFeature(wgpu::FeatureName::TimestampQuery));
			wgpu::AdapterInfo info{};
			REQUIRE(Adapter.GetInfo(&info) == wgpu::Status::Success);
			if (adapterClass == "hardware") {
				REQUIRE((info.adapterType == wgpu::AdapterType::DiscreteGPU || info.adapterType == wgpu::AdapterType::IntegratedGPU));
			}

			const wgpu::FeatureName feature = wgpu::FeatureName::TimestampQuery;
			wgpu::DeviceDescriptor descriptor{};
			descriptor.requiredFeatureCount = timestamps ? 1 : 0;
			descriptor.requiredFeatures = timestamps ? &feature : nullptr;
			descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents,
				[events = Events](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
					std::scoped_lock lock(events->Mutex);
					events->Lost = true;
					events->Reason = reason;
					events->Message = CopyMessage(message);
				});
			descriptor.SetUncapturedErrorCallback(
				[](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message, DeviceEvents* events) {
					std::scoped_lock lock(events->Mutex);
					events->Errors.push_back(CopyMessage(message));
				}, Events.get());
			auto device = std::make_shared<wgpu::Device>();
			future = Adapter.RequestDevice(&descriptor, wgpu::CallbackMode::WaitAnyOnly,
				[device](wgpu::RequestDeviceStatus status, wgpu::Device value, wgpu::StringView) {
					if (status == wgpu::RequestDeviceStatus::Success) *device = std::move(value);
				});
			REQUIRE(Wait(Instance, future));
			Device = std::move(*device);
			REQUIRE(Device);
		}

		void CheckErrors() {
			std::scoped_lock lock(Events->Mutex);
			for (const auto& error : Events->Errors) CHECK_MESSAGE(false, error);
			CHECK(Events->Errors.empty());
		}

		~TimerDevice() {
			if (Device) Device.Destroy();
			if (Instance) Instance.ProcessEvents();
		}
	};

	void ResetSamples() {
		Profiler::Shutdown();
		Profiler::Initialize();
		Profiler::SetPanelVisible(true);
		Profiler::SetSamplingHz(0);
		Profiler::SetTrackingSpan(64);
	}

	void SubmitRing(TimerDevice& gpu, GpuTimer& timer) {
		wgpu::TextureDescriptor textureDescriptor{};
		textureDescriptor.size = { 1, 1, 1 };
		textureDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
		textureDescriptor.usage = wgpu::TextureUsage::RenderAttachment;
		auto target = gpu.Device.CreateTexture(&textureDescriptor);
		auto encoder = gpu.Device.CreateCommandEncoder();
		for (size_t index = 0; index < 3; ++index) {
			const auto slots = timer.BeginFrameWrites();
			REQUIRE(slots.Valid);
			wgpu::PassTimestampWrites timestamps{};
			timestamps.querySet = slots.QuerySet;
			timestamps.beginningOfPassWriteIndex = slots.BeginningOfPassWriteIndex;
			timestamps.endOfPassWriteIndex = slots.EndOfPassWriteIndex;
			wgpu::RenderPassColorAttachment color{};
			color.view = target.CreateView();
			color.loadOp = wgpu::LoadOp::Clear;
			color.storeOp = wgpu::StoreOp::Store;
			wgpu::RenderPassDescriptor descriptor{};
			descriptor.colorAttachmentCount = 1;
			descriptor.colorAttachments = &color;
			descriptor.timestampWrites = &timestamps;
			auto pass = encoder.BeginRenderPass(&descriptor);
			pass.End();
			timer.ResolveCurrentFrame(encoder);
		}
		const auto commands = encoder.Finish();
		gpu.Device.GetQueue().Submit(1, &commands);
		CHECK_FALSE(timer.BeginFrameWrites().Valid);
	}

	bool AllCompleted(const std::vector<std::shared_ptr<GpuTimerTestAccess::Result>>& results) {
		for (const auto& result : results) if (!result->Completed.load(std::memory_order_acquire)) return false;
		return true;
	}

	void SaveEvidence(const char* name, TimerDevice& gpu, size_t pending, bool loss = false) {
		wgpu::AdapterInfo adapter{};
		REQUIRE(gpu.Adapter.GetInfo(&adapter) == wgpu::Status::Success);
		auto evidence = Json::Value::MakeObject();
		evidence.AddMember("adapter", CopyMessage(adapter.device));
		evidence.AddMember("backend", static_cast<int>(adapter.backendType));
		evidence.AddMember("adapterType", static_cast<int>(adapter.adapterType));
		evidence.AddMember("pendingCallbacksBeforeAction", static_cast<uint64_t>(pending));
		evidence.AddMember("gpuSamples", static_cast<uint64_t>(Profiler::Find("GPU")->Count));
		evidence.AddMember("realDawnDevice", true);
		evidence.AddMember("forceLossInjected", loss);
		{
			std::scoped_lock lock(gpu.Events->Mutex);
			evidence.AddMember("deviceLostCallback", gpu.Events->Lost);
			evidence.AddMember("deviceLostReason", static_cast<int>(gpu.Events->Reason));
			evidence.AddMember("deviceLostMessage", gpu.Events->Message);
			evidence.AddMember("uncapturedErrors", static_cast<uint64_t>(gpu.Events->Errors.size()));
		}
		REQUIRE(GpuTestEvidence::WriteJson(name, evidence));
	}
}

TEST_CASE("GpuTimer callback result survives timer destruction without a device") {
	for (const auto status : { wgpu::MapAsyncStatus::Success, wgpu::MapAsyncStatus::Error,
		wgpu::MapAsyncStatus::Aborted, wgpu::MapAsyncStatus::CallbackCancelled }) {
		auto timer = std::make_unique<GpuTimer>();
		auto callback = GpuTimerTestAccess::PendingCallback(*timer);
		auto result = GpuTimerTestAccess::Results(*timer).front();
		timer.reset();
		std::thread completion([&] { callback(status, "injected callback result"); });
		completion.join();
		REQUIRE(result->Completed.load(std::memory_order_acquire));
		CHECK(result->Status == status);
		CHECK(result->Message == "injected callback result");
		std::weak_ptr<GpuTimerTestAccess::Result> lifetime = result;
		result.reset();
		callback = {};
		CHECK(lifetime.expired());
	}
	CHECK(GpuTimer::QueryGpuMemoryMb() == -1);
}

TEST_CASE("GPU timer unavailable without timestamp feature"
	* doctest::skip(!GpuTestEvidence::Requested("FUNKE_TEST_GPU_TIMER"))) {
	REQUIRE(GpuTestEvidence::Environment("FUNKE_TEST_GPU_TIMER") == "1");
	ResetSamples();
	TimerDevice gpu;
	gpu.Initialize(false);
	GpuTimer timer;
	GpuTimerTestAccess::Initialize(timer, gpu.Instance, gpu.Device);
	CHECK(timer.GetStatus() == GpuTimer::Status::Unsupported);
	CHECK(GpuTimer::GetUnavailableReason() != nullptr);
	CHECK_FALSE(timer.BeginFrameWrites().Valid);
	timer.OnFrameStart();
	timer.PollAndPublish();
	CHECK(GpuTimerTestAccess::Empty(timer));
	CHECK(Profiler::Find("GPU")->Count == 0);
	gpu.CheckErrors();
	SaveEvidence("timer-unsupported", gpu, 0);
}

TEST_CASE("GPU timer readback failure disables ring and reinitializes"
	* doctest::skip(!GpuTestEvidence::Requested("FUNKE_TEST_GPU_TIMER"))) {
	REQUIRE(GpuTestEvidence::Environment("FUNKE_TEST_GPU_TIMER") == "1");
	ResetSamples();
	TimerDevice gpu;
	gpu.Initialize(true);
	GpuTimer timer;
	GpuTimerTestAccess::Initialize(timer, gpu.Instance, gpu.Device);
	REQUIRE(timer.GetStatus() == GpuTimer::Status::Ready);
	SubmitRing(gpu, timer);
	GpuTimerTestAccess::DestroyReadback(timer);
	gpu.Device.PushErrorScope(wgpu::ErrorFilter::Validation);
	timer.OnFrameStart();
	const auto results = GpuTimerTestAccess::Results(timer);
	REQUIRE(results.size() == 3);
	auto scopeError = std::make_shared<wgpu::ErrorType>(wgpu::ErrorType::NoError);
	const auto scope = gpu.Device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
		[scopeError](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView) { *scopeError = type; });
	REQUIRE(Wait(gpu.Instance, scope));
	CHECK(*scopeError == wgpu::ErrorType::Validation);
	REQUIRE(Until([&] { timer.PollAndPublish(); return timer.GetStatus() != GpuTimer::Status::Ready; }));
	CHECK(timer.GetStatus() == GpuTimer::Status::ReadbackError);
	CHECK(GpuTimerTestAccess::Empty(timer));
	CHECK_FALSE(timer.BeginFrameWrites().Valid);
	CHECK(GpuTimer::GetUnavailableReason() != nullptr);
	REQUIRE(Until([&] { gpu.Instance.ProcessEvents(); return AllCompleted(results); }));
	CHECK(results[0]->Status == wgpu::MapAsyncStatus::Error);
	CHECK_FALSE(results[0]->Message.empty());
	CHECK(Profiler::Find("GPU")->Count == 0);

	GpuTimerTestAccess::Initialize(timer, gpu.Instance, gpu.Device);
	REQUIRE(timer.GetStatus() == GpuTimer::Status::Ready);
	CHECK(GpuTimer::GetUnavailableReason() == nullptr);
	SubmitRing(gpu, timer);
	timer.OnFrameStart();
	REQUIRE(Until([&] { timer.PollAndPublish(); return Profiler::Find("GPU")->Count == 3; }));
	CHECK(GpuTimerTestAccess::Empty(timer));
	gpu.CheckErrors();
	SaveEvidence("timer-map-error", gpu, results.size());
}

TEST_CASE("GPU timer shutdown detaches pending callbacks from replacement timer"
	* doctest::skip(!GpuTestEvidence::Requested("FUNKE_TEST_GPU_TIMER"))) {
	REQUIRE(GpuTestEvidence::Environment("FUNKE_TEST_GPU_TIMER") == "1");
	ResetSamples();
	TimerDevice gpu;
	gpu.Initialize(true);
	auto timer = std::make_unique<GpuTimer>();
	GpuTimerTestAccess::Initialize(*timer, gpu.Instance, gpu.Device);
	SubmitRing(gpu, *timer);
	timer->OnFrameStart();
	const auto oldResults = GpuTimerTestAccess::Results(*timer);
	REQUIRE(oldResults.size() == 3);
	for (const auto& result : oldResults) CHECK_FALSE(result->Completed.load());
	timer.reset();
	timer = std::make_unique<GpuTimer>();
	GpuTimerTestAccess::Initialize(*timer, gpu.Instance, gpu.Device);
	SubmitRing(gpu, *timer);
	timer->OnFrameStart();
	REQUIRE(Until([&] {
		timer->PollAndPublish();
		return AllCompleted(oldResults) && Profiler::Find("GPU")->Count == 3;
	}));
	CHECK(timer->GetStatus() == GpuTimer::Status::Ready);
	CHECK(GpuTimerTestAccess::Empty(*timer));
	gpu.CheckErrors();
	SaveEvidence("timer-shutdown", gpu, oldResults.size());
}

TEST_CASE("GPU timer device loss on isolated device drains pending callbacks"
	* doctest::skip(!GpuTestEvidence::Requested("FUNKE_TEST_GPU_TIMER"))) {
	REQUIRE(GpuTestEvidence::Environment("FUNKE_TEST_GPU_TIMER") == "1");
	ResetSamples();
	TimerDevice gpu;
	gpu.Initialize(true);
	auto timer = std::make_unique<GpuTimer>();
	GpuTimerTestAccess::Initialize(*timer, gpu.Instance, gpu.Device);
	SubmitRing(gpu, *timer);
	timer->OnFrameStart();
	const auto pending = GpuTimerTestAccess::Results(*timer);
	REQUIRE(pending.size() == 3);
	for (const auto& result : pending) CHECK_FALSE(result->Completed.load());
	gpu.Device.ForceLoss(wgpu::DeviceLostReason::Unknown, "GFX-045 isolated device loss injection");
	REQUIRE(Until([&] { timer->PollAndPublish(); return timer->GetStatus() == GpuTimer::Status::DeviceLost; }));
	CHECK(GpuTimerTestAccess::Empty(*timer));
	CHECK_FALSE(timer->BeginFrameWrites().Valid);
	CHECK(GpuTimer::GetUnavailableReason() != nullptr);
	timer.reset();
	REQUIRE(Until([&] { gpu.Instance.ProcessEvents(); return AllCompleted(pending); }));
	{
		std::scoped_lock lock(gpu.Events->Mutex);
		CHECK(gpu.Events->Lost);
		CHECK(gpu.Events->Reason == wgpu::DeviceLostReason::Unknown);
		CHECK(gpu.Events->Message.find("GFX-045") != std::string::npos);
	}
	CHECK(Profiler::Find("GPU")->Count == 0);
	GpuTimer lostTimer;
	GpuTimerTestAccess::Initialize(lostTimer, gpu.Instance, gpu.Device);
	CHECK(lostTimer.GetStatus() == GpuTimer::Status::DeviceLost);
	CHECK_FALSE(lostTimer.BeginFrameWrites().Valid);
	gpu.CheckErrors();
	SaveEvidence("timer-device-loss", gpu, pending.size(), true);
}

TEST_CASE("Backlog GPU skinning agrees with CPU for reflected and nonuniform transforms"
	* doctest::skip(!GpuTestEvidence::Requested("FUNKE_TEST_SKINNING_GPU"))) {
	TimerDevice gpu;
	gpu.Initialize(false);
	wgpu::ShaderSourceWGSL source{};
	source.code = SkinningCompute::k_Shader;
	wgpu::ShaderModuleDescriptor shaderDescription{};
	shaderDescription.nextInChain = &source;
	const auto shader = gpu.Device.CreateShaderModule(&shaderDescription);
	wgpu::ComputePipelineDescriptor pipelineDescription{};
	pipelineDescription.compute.module = shader;
	pipelineDescription.compute.entryPoint = "skin";
	const auto pipeline = gpu.Device.CreateComputePipeline(&pipelineDescription);
	REQUIRE(pipeline);
	SkinnedMeshData data;
	MeshVertex vertex{};
	vertex.Position[0] = 1;
	vertex.Position[1] = 2;
	vertex.Position[2] = -3;
	vertex.Normal[0] = vertex.Normal[1] = 0.70710678f;
	vertex.UV[0] = 0.25f;
	vertex.UV[1] = 0.75f;
	data.BindVertices.push_back(vertex);
	data.VertexJoints.push_back({0, 0, 0, 0});
	data.VertexWeights.push_back(glm::vec4(1, 0, 0, 0));
	std::vector<SkinningCompute::Vertex> packed;
	REQUIRE(SkinningCompute::PackVertices(data, packed));
	const auto buffer = [&](uint64_t size, wgpu::BufferUsage usage) {
		wgpu::BufferDescriptor description{};
		description.size = size;
		description.usage = usage;
		return gpu.Device.CreateBuffer(&description);
	};
	auto input = buffer(sizeof(SkinningCompute::Vertex), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
	auto joints = buffer(sizeof(glm::mat4), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
	auto output = buffer(sizeof(MeshVertex), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
	auto readback = buffer(sizeof(MeshVertex), wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst);
	wgpu::BindGroupEntry entries[3]{};
	entries[0].buffer = input;
	entries[0].size = sizeof(SkinningCompute::Vertex);
	entries[1].buffer = joints;
	entries[1].size = sizeof(glm::mat4);
	entries[2].buffer = output;
	entries[2].size = sizeof(MeshVertex);
	for (uint32_t i = 0; i < 3; ++i) entries[i].binding = i;
	wgpu::BindGroupDescriptor groupDescription{};
	groupDescription.layout = pipeline.GetBindGroupLayout(0);
	groupDescription.entryCount = 3;
	groupDescription.entries = entries;
	auto group = gpu.Device.CreateBindGroup(&groupDescription);
	gpu.Device.GetQueue().WriteBuffer(input, 0, packed.data(), sizeof(SkinningCompute::Vertex));
	const glm::mat4 identity(1);
	const std::vector<glm::mat4> transforms = {identity, glm::translate(identity, {2, -4, 7}),
		glm::rotate(identity, 0.73f, glm::normalize(glm::vec3(1, 2, 3))),
		glm::scale(identity, {2, 2, 2}), glm::scale(identity, {2, 0.5f, 3}),
		glm::scale(identity, {-2, 0.5f, 3}), glm::scale(identity, {0, 0, 0})};
	for (const auto& transform : transforms) {
		gpu.Device.GetQueue().WriteBuffer(joints, 0, &transform, sizeof(transform));
		auto encoder = gpu.Device.CreateCommandEncoder();
		auto pass = encoder.BeginComputePass();
		pass.SetPipeline(pipeline);
		pass.SetBindGroup(0, group);
		pass.DispatchWorkgroups(1);
		pass.End();
		encoder.CopyBufferToBuffer(output, 0, readback, 0, sizeof(MeshVertex));
		auto command = encoder.Finish();
		gpu.Device.GetQueue().Submit(1, &command);
		wgpu::MapAsyncStatus status = wgpu::MapAsyncStatus::Error;
		auto mapped = readback.MapAsync(wgpu::MapMode::Read, 0, sizeof(MeshVertex), wgpu::CallbackMode::WaitAnyOnly,
			[&status](wgpu::MapAsyncStatus value, wgpu::StringView) { status = value; });
		REQUIRE(Wait(gpu.Instance, mapped));
		REQUIRE(status == wgpu::MapAsyncStatus::Success);
		MeshVertex actual;
		std::memcpy(&actual, readback.GetConstMappedRange(), sizeof(actual));
		readback.Unmap();
		std::vector<MeshVertex> expected;
		SkeletalAnimation::SkinVertices(data, {transform}, expected);
		for (int axis = 0; axis < 3; ++axis) {
			CHECK(actual.Position[axis] == doctest::Approx(expected[0].Position[axis]).epsilon(1e-5));
			CHECK(actual.Normal[axis] == doctest::Approx(expected[0].Normal[axis]).epsilon(1e-5));
		}
		CHECK(actual.UV[0] == vertex.UV[0]);
		CHECK(actual.UV[1] == vertex.UV[1]);
	}
	gpu.CheckErrors();
}

#endif
