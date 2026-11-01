#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <doctest/doctest.h>
#include "Gui/AsyncTexturePreview.hpp"
#include "Core/Application.hpp"
#include "Core/UUID.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Utils/Process.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

using namespace Funke;
using namespace std::chrono_literals;

namespace {
	bool WaitResult(PreviewDecodeQueue& queue, PreviewDecodeQueue::Result& result) {
		const auto deadline = std::chrono::steady_clock::now() + 3s;
		while (std::chrono::steady_clock::now() < deadline) {
			if (queue.Take(result)) return true;
			std::this_thread::sleep_for(1ms);
		}
		return false;
	}
	std::shared_ptr<TexturePreview> Request(const char* path) {
		return std::make_shared<TexturePreview>(path, Filter::Point, false);
	}
}

TEST_CASE("Texture preview decode runs off the caller thread and preserves RGBA rows") {
	const auto caller = std::this_thread::get_id();
	std::thread::id decoderThread;
	PreviewDecodeQueue queue([&](const std::string&) {
		decoderThread = std::this_thread::get_id();
		return std::make_unique<ImageData>(1, 2,
			std::vector<unsigned char>{255, 0, 0, 255, 0, 0, 255, 17});
	});
	auto request = Request("sheet");
	queue.Enqueue(request, false);
	PreviewDecodeQueue::Result result;
	REQUIRE(WaitResult(queue, result));
	CHECK(decoderThread != caller);
	REQUIRE(result.Image);
	CHECK(result.Image->Width == 1);
	CHECK(result.Image->Height == 2);
	CHECK(result.Image->Pixels[0] == 255);
	CHECK(result.Image->Pixels[6] == 255);
	CHECK(result.Image->Pixels[7] == 17);
	CHECK(result.Request.lock() == request);
	CHECK(request->GetTexture() == nullptr);
}

TEST_CASE("Texture preview cancellation skips queued work and discards an in-flight selection") {
	std::promise<void> started;
	std::promise<void> release;
	auto released = release.get_future().share();
	std::atomic<int> calls{0};
	PreviewDecodeQueue queue([&](const std::string& path) -> std::unique_ptr<ImageData> {
		++calls;
		if (path == "old") { started.set_value(); released.wait(); }
		return nullptr;
	});
	auto old = Request("old");
	queue.Enqueue(old, false);
	const bool running = started.get_future().wait_for(3s) == std::future_status::ready;
	auto cancelled = Request("cancelled");
	queue.Enqueue(cancelled, false);
	cancelled.reset();
	auto current = Request("current");
	queue.Enqueue(current, true);
	old.reset();
	release.set_value();
	REQUIRE(running);
	PreviewDecodeQueue::Result result;
	REQUIRE(WaitResult(queue, result));
	CHECK(result.Request.lock() == current);
	CHECK(calls.load() == 2);
	CHECK_FALSE(result.Image);
}

TEST_CASE("Texture preview decoder exceptions do not kill the worker and shutdown permits restart") {
	PreviewDecodeQueue queue([](const std::string&) -> std::unique_ptr<ImageData> {
		throw std::runtime_error("bad image");
	});
	auto request = Request("broken");
	queue.Enqueue(request, false);
	PreviewDecodeQueue::Result result;
	REQUIRE(WaitResult(queue, result));
	CHECK_FALSE(result.Image);
	queue.Shutdown();
	queue.Enqueue(request, true);
	REQUIRE(WaitResult(queue, result));
	CHECK(result.Request.lock() == request);
}

TEST_CASE("Texture preview completed images apply backpressure and priority precedes thumbnails") {
	std::atomic<int> calls{0};
	PreviewDecodeQueue queue([&](const std::string&) -> std::unique_ptr<ImageData> {
		++calls;
		return std::make_unique<ImageData>(1, 1, std::vector<unsigned char>(4, 255));
	});
	auto first = Request("first");
	auto thumbnail = Request("thumbnail");
	auto inspector = Request("inspector");
	queue.Enqueue(first, false);
	const auto deadline = std::chrono::steady_clock::now() + 3s;
	while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
	queue.Enqueue(thumbnail, false);
	queue.Enqueue(inspector, true);
	std::this_thread::sleep_for(30ms);
	CHECK(calls.load() == 1);
	PreviewDecodeQueue::Result result;
	REQUIRE(WaitResult(queue, result));
	CHECK(result.Request.lock() == first);
	REQUIRE(WaitResult(queue, result));
	CHECK(result.Request.lock() == inspector);
	REQUIRE(WaitResult(queue, result));
	CHECK(result.Request.lock() == thumbnail);
}

TEST_CASE("Texture preview missing files reach a stable failed state without a GPU") {
	auto& previews = AsyncTexturePreviews::Get();
	auto request = previews.Request("missing-preview-file-does-not-exist.png");
	const auto deadline = std::chrono::steady_clock::now() + 3s;
	while (request->State == TexturePreview::Status::Loading && std::chrono::steady_clock::now() < deadline) {
		previews.Pump();
		std::this_thread::sleep_for(1ms);
	}
	CHECK(request->State == TexturePreview::Status::Failed);
	CHECK(request->GetTexture() == nullptr);
	previews.Pump();
	CHECK(request->State == TexturePreview::Status::Failed);
	previews.Shutdown();
}

namespace {
	class PreviewUploadApplication final : public Application {
	public:
		std::string Path;
		bool Completed = false;
		bool NativePixels = false;
		bool Deferred = false;
		int UploadFrames = 0;
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Minimal();
			config.WindowSpecification = WindowSpecification(160, 120, "Texture preview test", false, true, false);
			config.SetWindowIcon = false;
			config.EnableTextureManager = false;
			config.RunInBackground = true;
			config.Vsync = false;
			config.TargetFrameRate = 240.0f;
			return config;
		}
		void Start() override {
			m_Request = AsyncTexturePreviews::Get().Request(Path, Filter::Point, true);
			Deferred = m_Request->GetTexture() == nullptr;
			m_Start = std::chrono::steady_clock::now();
		}
		void Update() override {
			AsyncTexturePreviews::Get().Pump();
			if (m_Request->Texture) ++UploadFrames;
			if (m_Request->State == TexturePreview::Status::Ready) {
				Completed = m_Request->GetTexture()->IsValid();
				const auto& pixels = m_Request->Pixels;
				NativePixels = pixels && pixels->Width == 2048 && pixels->Height == 2048
					&& pixels->Pixels[0] == 255 && pixels->Pixels[1] == 0
					&& pixels->Pixels[pixels->OwnedPixels.size() - 2] == 255
					&& pixels->Pixels[pixels->OwnedPixels.size() - 1] == 127
					&& !m_Request->Texture->IsFlippedY()
					&& m_Request->Texture->GetFilter() == Filter::Point;
			}
			if (Completed || m_Request->State == TexturePreview::Status::Failed
				|| std::chrono::steady_clock::now() - m_Start > 10s) Application::Quit();
		}
		void OnQuit() override {
			m_Request.reset();
			AsyncTexturePreviews::Get().Shutdown();
		}
	private:
		std::shared_ptr<TexturePreview> m_Request;
		std::chrono::steady_clock::time_point m_Start;
	};
}

TEST_CASE("Texture preview GPU upload spans frames and retains native sprite pixels") {
	const char* enabled = std::getenv("FUNKE_TEST_PREVIEW_GPU");
	if (!enabled || std::string(enabled) != "1") {
		MESSAGE("Set FUNKE_TEST_PREVIEW_GPU=1 for the background WebGPU integration test.");
		return;
	}
	const auto path = std::filesystem::temp_directory_path()
		/ ("funke_preview_" + std::to_string(static_cast<uint64_t>(UUID())) + ".tga");
	struct Cleanup {
		std::filesystem::path Path;
		~Cleanup() {
			Application::SetCommandLineArgs(0, nullptr);
			RunAutomation::Reset();
			Process::ResetCancellation();
			std::error_code error;
			std::filesystem::remove(Path, error);
		}
	} cleanup{path};
	std::vector<unsigned char> tga(18 + 2048 * 2048 * 4, 0);
	tga[2] = 2;
	tga[13] = tga[15] = 8;
	tga[16] = 32;
	tga[17] = 0x28;
	for (size_t i = 18; i < tga.size(); i += 4) {
		tga[i + 2] = 255;
		tga[i + 3] = 255;
	}
	tga[tga.size() - 4] = 255;
	tga.back() = 127;
	{
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char*>(tga.data()), static_cast<std::streamsize>(tga.size()));
		REQUIRE(file.good());
	}
	char executable[] = "Funke-Preview-Test";
	char background[] = "--background";
	char* arguments[] = {executable, background};
	RunAutomation::Options options;
	options.Background = true;
	RunAutomation::Configure(options);
	Application::SetCommandLineArgs(2, arguments);
	PreviewUploadApplication app;
	app.Path = path.string();
	CHECK(app.Run() == 0);
	CHECK(app.Deferred);
	CHECK(app.Completed);
	CHECK(app.NativePixels);
	CHECK(app.UploadFrames == 4);
}
