#include <doctest/doctest.h>
#include "Core/Application.hpp"
#include "Core/UUID.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Graphics/RenderApi.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Gui/ThumbnailCache.hpp"
#include "Packages/PackageImGuiBridge.hpp"
#include "Utils/Process.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace Funke;

namespace {
	class ThumbnailApplication final : public Application {
	public:
		std::filesystem::path Directory;
		int Verified = 0;
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Minimal();
			config.WindowSpecification = WindowSpecification(160, 120, "Thumbnail lifetime test", false, true, false);
			config.SetWindowIcon = false;
			config.EnableTextureManager = false;
			config.RunInBackground = true;
			config.Vsync = false;
			return config;
		}
		void Start() override {
			ImGui::CreateContext();
			ImGui::GetIO().IniFilename = nullptr;
			ImGuiMemAllocFunc allocate;
			ImGuiMemFreeFunc release;
			void* user;
			ImGui::GetAllocatorFunctions(&allocate, &release, &user);
			PackageImGuiBridge::Publish(ImGui::GetCurrentContext(), reinterpret_cast<void*>(allocate), reinterpret_cast<void*>(release), user);
			REQUIRE(ImGuiImplWebGPU::Init());
			REQUIRE(m_Target.Recreate(64, 64, TextureFormat::BGRA8, TextureFilter::Nearest));
			m_Start = std::chrono::steady_clock::now();
		}
		void Update() override {
			AsyncTexturePreviews::Get().Pump();
			ImGuiImplWebGPU::NewFrame();
			auto& io = ImGui::GetIO();
			io.DisplaySize = {64, 64};
			io.DeltaTime = 1.0f / 60;
			ImGui::NewFrame();
			const auto source = (Directory / "source.tga").string();
			const auto handle = m_Cache.GetThumbnail(source);
			if (handle) {
				ImGui::GetBackgroundDrawList()->AddImage(static_cast<ImTextureID>(handle), {0, 0}, {64, 64});
				if (Verified % 3 == 0) m_Cache.Invalidate(source);
				else if (Verified % 3 == 1) m_Cache.Clear();
				else {
					std::filesystem::copy_file(Directory / "source.tga", Directory / "pasted.tga", std::filesystem::copy_options::overwrite_existing);
					m_Cache.Clear();
					m_Cache.GetThumbnail((Directory / "pasted.tga").string());
				}
			}
			ImGui::Render();
			RenderApi::BindFramebuffer(m_Target);
			RenderApi::SetClearColor({0, 0, 0, 1});
			RenderApi::Clear(ClearFlags::Color);
			ImGuiImplWebGPU::RenderDrawData(ImGui::GetDrawData(), 0);
			if (handle) {
				std::vector<uint8_t> pixels;
				REQUIRE(m_Target.ReadPixelsRGBA8(pixels));
				REQUIRE(pixels.size() == 64 * 64 * 4);
				const size_t center = (32 * 64 + 32) * 4;
				CHECK(pixels[center] == 255);
				CHECK(pixels[center + 1] == 0);
				CHECK(pixels[center + 2] == 0);
				CHECK(pixels[center + 3] == 255);
				++Verified;
			}
			RenderApi::BindDefaultFramebuffer();
			if (Verified == 9 || std::chrono::steady_clock::now() - m_Start > std::chrono::seconds(15)) Application::Quit();
		}
		void OnQuit() override {
			m_Cache.Shutdown();
			AsyncTexturePreviews::Get().Shutdown();
			m_Target.Destroy();
			ImGuiImplWebGPU::Shutdown();
			PackageImGuiBridge::Clear();
			ImGui::DestroyContext();
		}
	private:
		ThumbnailCache m_Cache;
		Framebuffer m_Target;
		std::chrono::steady_clock::time_point m_Start;
	};
}

TEST_CASE("Backlog thumbnail invalidation and copy paste retain submitted GPU images"
	* doctest::skip(std::getenv("FUNKE_TEST_PREVIEW_GPU") == nullptr)) {
	const auto directory = std::filesystem::temp_directory_path() / ("funke_thumbnails_" + std::to_string(static_cast<uint64_t>(UUID())));
	std::filesystem::create_directory(directory);
	struct Cleanup {
		std::filesystem::path Directory;
		~Cleanup() {
			Application::SetCommandLineArgs(0, nullptr);
			RunAutomation::Reset();
			Process::ResetCancellation();
			std::error_code error;
			std::filesystem::remove_all(Directory, error);
		}
	} cleanup{directory};
	std::vector<unsigned char> tga(18 + 4 * 4 * 4, 0);
	tga[2] = 2;
	tga[12] = tga[14] = 4;
	tga[16] = 32;
	tga[17] = 0x28;
	for (size_t i = 18; i < tga.size(); i += 4) tga[i + 2] = tga[i + 3] = 255;
	{
		std::ofstream file(directory / "source.tga", std::ios::binary);
		file.write(reinterpret_cast<const char*>(tga.data()), static_cast<std::streamsize>(tga.size()));
		REQUIRE(file.good());
	}
	char executable[] = "Funke-Thumbnail-Test";
	char background[] = "--background";
	char* arguments[] = {executable, background};
	RunAutomation::Options options;
	options.Background = true;
	RunAutomation::Configure(options);
	Application::SetCommandLineArgs(2, arguments);
	ThumbnailApplication app;
	app.Directory = directory;
	CHECK(app.Run() == 0);
	CHECK(app.Verified == 9);
}
