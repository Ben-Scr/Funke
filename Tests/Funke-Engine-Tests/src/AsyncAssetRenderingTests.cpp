#include <doctest/doctest.h>
#include "GpuTestEvidence.hpp"

#include "Assets/AssetLoader.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Audio/Audio.hpp"
#include "Audio/AudioManager.hpp"
#include "Audio/Backends/MiniaudioBackend.hpp"
#include "Core/Application.hpp"
#include "Core/UUID.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Graphics/RenderApi.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/TextureManager.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptEngine.hpp"

#include <glm/ext/matrix_clip_space.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Funke;

namespace {

	const std::array<uint8_t, 16> k_TexturePixels{
		255, 0, 0, 255, 0, 255, 0, 127,
		0, 0, 255, 63, 17, 29, 41, 0
	};
	constexpr std::array<AssetKind, 3> k_AssetKinds{ AssetKind::Texture, AssetKind::Font, AssetKind::Audio };

	std::filesystem::path FindTestFont() {
		for (auto directory : { std::filesystem::path(__FILE__).parent_path(),
			std::filesystem::path(Path::ExecutableDir()), std::filesystem::current_path() }) {
			while (!directory.empty()) {
				const auto candidate = directory / "Funke-Runtime/FunkeAssets/Fonts/IBMPlexSansCondensed-Regular.ttf";
				if (std::filesystem::is_regular_file(candidate)) return candidate;
				const auto parent = directory.parent_path();
				if (parent == directory) break;
				directory = parent;
			}
		}
		return {};
	}

	struct RenderingAssetFiles {
		std::filesystem::path Root = std::filesystem::temp_directory_path()
			/ ("funke_async_rendering_" + std::to_string(static_cast<uint64_t>(UUID())));
		std::string TexturePath;
		std::string AudioPath;
		std::array<uint64_t, 3> Ids{ UUID(), UUID(), UUID() };

		~RenderingAssetFiles() {
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		bool Write(const std::filesystem::path& fontPath) {
			std::filesystem::create_directories(Root);
			TexturePath = Path::ToUtf8(Root / "pixels.tga");
			std::vector<uint8_t> tga(18, 0);
			tga[2] = 2;
			tga[12] = 2;
			tga[14] = 2;
			tga[16] = 32;
			tga[17] = 0x28;
			for (size_t index = 0; index < k_TexturePixels.size(); index += 4) {
				tga.insert(tga.end(), { k_TexturePixels[index + 2], k_TexturePixels[index + 1],
					k_TexturePixels[index], k_TexturePixels[index + 3] });
			}
			if (!File::WriteAllBytes(TexturePath, tga)) return false;
			TextureMeta meta;
			meta.HasImportBlock = true;
			meta.Import.FilterMode = Filter::Point;
			meta.Import.WrapU = Wrap::Mirror;
			meta.Import.WrapV = Wrap::Repeat;
			if (!AssetRegistry::WriteTextureMeta(TexturePath, meta)) return false;
			AssetRegistry::RegisterBuiltInAsset(TexturePath, Ids[0], AssetKind::Texture);

			const auto copiedFont = Root / "test.ttf";
			std::filesystem::copy_file(fontPath, copiedFont);
			AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(copiedFont), Ids[1], AssetKind::Font);

			AudioPath = Path::ToUtf8(Root / "silence.wav");
			std::vector<uint8_t> wav(44 + 32, 0);
			auto write32 = [&](size_t offset, uint32_t value) {
				for (size_t byte = 0; byte < 4; ++byte) wav[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
			};
			std::memcpy(wav.data(), "RIFF", 4);
			write32(4, 68);
			std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
			write32(16, 16);
			wav[20] = 1;
			wav[22] = 1;
			write32(24, 48000);
			write32(28, 96000);
			wav[32] = 2;
			wav[34] = 16;
			std::memcpy(wav.data() + 36, "data", 4);
			write32(40, 32);
			if (!File::WriteAllBytes(AudioPath, wav)) return false;
			AssetRegistry::RegisterBuiltInAsset(AudioPath, Ids[2], AssetKind::Audio);
			return true;
		}
	};

	class AsyncRenderingApplication final : public Application {
	public:
		explicit AsyncRenderingApplication(const RenderingAssetFiles& files) : m_Files(files) {}

		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Minimal();
			config.WindowSpecification = WindowSpecification(160, 120, "Funke async asset test", false, true, false);
			config.SetWindowIcon = false;
			config.EnableAudio = true;
			config.EnableScenes = true;
			config.EnableShaderManager = true;
			config.RunInBackground = true;
			config.Vsync = false;
			config.TargetFrameRate = 240.0f;
			return config;
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene("Texture lifetime").WithoutDefaultCamera2D().SetAsStartupScene();
		}

		void Start() override {
			BackendReady = RenderApi::IsInitialized();
			AdapterValid = GpuTestEvidence::Adapter("async");
			if (!AdapterValid) { Application::Quit(); return; }
			FontManagerReady = FontManager::Initialize();
			Background = RunAutomation::IsBackground();
			ScriptBindings::PopulateAssetBindings(m_Bindings);
			const auto runtimeBaseline = TextureManager::GetRuntimeTextureCount();
			for (int cycle = 0; cycle < 50; ++cycle) {
				const auto id = TextureManager::CreateRuntimeTexture(2, 2, k_TexturePixels.data(), Filter::Point, Wrap::Clamp, Wrap::Clamp);
				REQUIRE(id != 0);
				CHECK(TextureManager::GetRuntimeTextureCount() == runtimeBaseline + 1);
				TextureManager::DestroyRuntimeTexture(id);
				TextureManager::DestroyRuntimeTexture(id);
				CHECK(TextureManager::GetRuntimeTextureCount() == runtimeBaseline);
			}
			const auto sourceBudget = FontManager::GetSourceMemoryBudgetBytes();
			FontManager::SetSourceMemoryBudgetBytes(0);
			for (int cycle = 0; cycle < 10; ++cycle) {
				const auto font = FontManager::LoadFontByUUID(m_Files.Ids[1], 24.0f);
				REQUIRE(font.IsValid());
				CHECK(FontManager::GetSourceCacheBytes() > 0);
				CHECK(FontManager::GetUnusedSourceCacheBytes() == 0);
				FontManager::UnloadFont(font);
				CHECK(FontManager::GetSourceCacheBytes() == 0);
			}
			FontManager::SetSourceMemoryBudgetBytes(sourceBudget);
			if (const char* core = std::getenv("FUNKE_TEST_SCRIPTCORE")) {
				const char* assembly = std::getenv("FUNKE_TEST_RELOAD_ASSEMBLY");
				REQUIRE(assembly != nullptr);
				ScriptEngine::LoadCoreAssembly(core);
				REQUIRE(ScriptEngine::LoadUserAssembly(assembly));
				const auto scenes = SceneManager::GetLoadedScenes();
				REQUIRE_FALSE(scenes.empty());
				auto scene = scenes.front().lock();
				REQUIRE(scene);
				for (int cycle = 0; cycle < 50; ++cycle) {
					Application::SetIsPlaying(true);
					REQUIRE(scene->AddSceneSystem("Backlog.TextureLifetimeProbe"));
					CHECK(TextureManager::GetRuntimeTextureCount() == runtimeBaseline + 1);
					REQUIRE(ScriptEngine::PrepareResourceRelease(true));
					scene->ClearSceneSystems();
					Application::SetIsPlaying(false);
					ScriptEngine::OnPlayModeExited();
					CHECK(TextureManager::GetRuntimeTextureCount() == runtimeBaseline);
				}
			}

			const uint64_t runtime = TextureManager::CreateRuntimeTexture(2, 2, k_TexturePixels.data(),
				Filter::Point, Wrap::Clamp, Wrap::Clamp);
			RuntimeTextureAccepted = runtime != 0 && !AssetRegistry::Exists(runtime)
				&& m_Bindings.Asset_LoadTyped(nullptr, runtime, static_cast<int>(AssetKind::Texture)) == runtime;
			RuntimeTextureKindChecked = runtime != 0
				&& m_Bindings.Asset_LoadTyped(nullptr, runtime, static_cast<int>(AssetKind::Audio)) == 0;
			TextureManager::DestroyRuntimeTexture(runtime);
			DestroyedRuntimeTextureRejected = runtime != 0
				&& m_Bindings.Asset_LoadTyped(nullptr, runtime, static_cast<int>(AssetKind::Texture)) == 0;
			m_StartedAt = std::chrono::steady_clock::now();
			for (size_t index = 0; index < m_Requests.size(); ++index) {
				m_Requests[index] = AssetLoader::LoadAsync(m_Files.Ids[index]);
				Deferred = Deferred && m_Requests[index] != 0
					&& AssetLoader::GetResult(m_Requests[index]).Status == AssetLoadStatus::Pending;
				m_TypedRequests[index] = m_Bindings.Asset_BeginLoadTypedAsync(nullptr, m_Files.Ids[index],
					static_cast<int>(k_AssetKinds[index]));
				TypedDeferred = TypedDeferred && m_TypedRequests[index] != 0
					&& AssetLoader::GetResult(m_TypedRequests[index]).Status == AssetLoadStatus::Pending;
			}
		}

		void Update() override {
			++UpdateCount;
			bool pending = false;
			TypedRequestsReleased = true;
			for (size_t index = 0; index < m_Requests.size(); ++index) {
				const auto result = AssetLoader::GetResult(m_Requests[index]);
				Statuses[index] = result.Status;
				Errors[index] = result.Message;
				pending = pending || result.Status == AssetLoadStatus::Pending;
				const auto typedStatus = AssetLoader::GetResult(m_TypedRequests[index]).Status;
				TypedRequestsReleased = TypedRequestsReleased && m_TypedRequests[index] != 0
					&& typedStatus == AssetLoadStatus::NotFound;
				pending = pending || (m_TypedRequests[index] != 0 && typedStatus != AssetLoadStatus::NotFound);
			}
			if (pending && std::chrono::steady_clock::now() - m_StartedAt < std::chrono::seconds(15)) return;
			TimedOut = pending;
			ValidatePublishedAssets();
			for (uint64_t request : m_Requests) AssetLoader::Release(request);
			m_Bindings.Asset_CancelLoads(m_TypedRequests.data(), static_cast<int>(m_TypedRequests.size()), 1);
			Application::Quit();
		}

		bool BackendReady = false;
		bool AdapterValid = false;
		bool FontManagerReady = false;
		bool Background = false;
		bool Deferred = true;
		bool TypedDeferred = true;
		bool TypedRequestsReleased = false;
		bool TypedSyncLoadsSucceeded = false;
		bool RuntimeTextureAccepted = false;
		bool RuntimeTextureKindChecked = false;
		bool DestroyedRuntimeTextureRejected = false;
		bool TimedOut = false;
		bool TextureCacheShared = false;
		bool TextureSamplingCorrect = false;
		bool TexturePixelsCorrect = false;
		bool TextureGpuPixelsCorrect = false;
		bool FontCacheShared = false;
		bool FontAtlasReady = false;
		bool GlyphUvsValid = false;
		bool AudioCacheShared = false;
		bool AudioDecoded = false;
		int UpdateCount = 0;
		std::array<AssetLoadStatus, 3> Statuses{};
		std::array<std::string, 3> Errors{};

	private:
		void ValidatePublishedAssets() {
			TypedSyncLoadsSucceeded = true;
			for (size_t index = 0; index < m_Files.Ids.size(); ++index) {
				TypedSyncLoadsSucceeded = TypedSyncLoadsSucceeded
					&& m_Bindings.Asset_LoadTyped(nullptr, m_Files.Ids[index], static_cast<int>(k_AssetKinds[index])) == m_Files.Ids[index];
			}
			const auto textureHandle = TextureManager::GetTextureHandle(m_Files.TexturePath, Filter::Point, Wrap::Mirror, Wrap::Repeat);
			if (Texture2D* texture = TextureManager::GetTexture(textureHandle)) {
				TextureCacheShared = textureHandle == TextureManager::LoadTextureByUUID(m_Files.Ids[0]);
				TextureSamplingCorrect = texture->IsValid() && texture->GetWidth() == 2 && texture->GetHeight() == 2
					&& !texture->IsFlippedY() && texture->GetFilter() == Filter::Point
					&& texture->GetWrapU() == Wrap::Mirror && texture->GetWrapV() == Wrap::Repeat;
				const auto pixels = texture->GetImageData();
				TexturePixelsCorrect = pixels && pixels->OwnedPixels.size() == k_TexturePixels.size()
					&& std::memcmp(pixels->Pixels, k_TexturePixels.data(), k_TexturePixels.size()) == 0;
				Renderer2D renderer;
				renderer.Initialize();
				struct Cleanup { Renderer2D& Value; ~Cleanup() { Value.Shutdown(); } } cleanup{ renderer };
				renderer.SetPostProcessingEnabled(false);
				Framebuffer target(64, 64);
				auto scene = Scene::CreateDetachedScene("Async texture readback");
				Entity entity = scene->CreateEmptyEntity();
				entity.AddComponent<Transform2DComponent>(Vec2(0, 0), Vec2(2, 2));
				entity.AddComponent<SpriteRendererComponent>().TextureHandle = textureHandle;
				RenderApi::BindFramebuffer(target);
				RenderApi::SetViewport(0, 0, 64, 64);
				RenderApi::SetClearColor(Color::Black());
				RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
				renderer.RenderSceneWithVP(*scene, glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f),
					AABB({ -1, -1 }, { 1, 1 }));
				std::vector<uint8_t> actual;
				target.ReadPixelsRGBA8(actual);
				auto reference = GpuTestEvidence::Solid(64, 64, { 0, 0, 0, 255 });
				GpuTestEvidence::Rectangle(reference, 64, 0, 0, 32, 32, { 255, 0, 0, 255 });
				GpuTestEvidence::Rectangle(reference, 64, 32, 0, 64, 32, { 0, 127, 0, 255 });
				GpuTestEvidence::Rectangle(reference, 64, 0, 32, 32, 64, { 0, 0, 63, 255 });
				TextureGpuPixelsCorrect = GpuTestEvidence::Compare("async-texture", 64, 64, actual, reference);
				RenderApi::BindDefaultFramebuffer();
			}
			const auto fontHandle = FontManager::FindLoadedFontByUUID(m_Files.Ids[1], 32.0f);
			if (Font* font = FontManager::GetFont(fontHandle)) {
				FontCacheShared = fontHandle == FontManager::LoadFontByUUID(m_Files.Ids[1], 32.0f);
				FontAtlasReady = font->IsLoaded() && font->GetAtlasTexture() != 0 && font->GetAtlasWidth() > 0
					&& font->GetAtlasHeight() > 0 && font->GetPixelSize() == 32.0f;
				const GlyphMetrics* glyph = font->GetGlyph('A');
				GlyphUvsValid = glyph && glyph->XAdvance > 0 && glyph->U0 >= 0 && glyph->V0 >= 0
					&& glyph->U1 > glyph->U0 && glyph->V1 > glyph->V0 && glyph->U1 <= 1 && glyph->V1 <= 1;
			}
			const auto audioHandle = AudioManager::FindLoadedAudio(m_Files.AudioPath);
			if (const Audio* audio = AudioManager::GetAudio(audioHandle)) {
				AudioCacheShared = audioHandle == AudioManager::LoadAudioByUUID(m_Files.Ids[2]);
				AudioDecoded = audio->IsLoaded() && audio->GetFrameCount() == 16
					&& audio->GetSampleRate() == 48000 && audio->GetChannels() == 1;
			}
		}

		const RenderingAssetFiles& m_Files;
		NativeBindings m_Bindings{};
		std::array<uint64_t, 3> m_Requests{};
		std::array<uint64_t, 3> m_TypedRequests{};
		std::chrono::steady_clock::time_point m_StartedAt;
	};

}

TEST_CASE("Async asset rendering publishes textures fonts and audio through the Application loop"
	* doctest::skip(!GpuTestEvidence::Requested("FUNKE_TEST_ASYNC_ASSET_GPU"))) {
	REQUIRE(GpuTestEvidence::Environment("FUNKE_TEST_ASYNC_ASSET_GPU") == "1");
	const auto fontPath = FindTestFont();
	REQUIRE_FALSE(fontPath.empty());
	RenderingAssetFiles files;
	REQUIRE(files.Write(fontPath));
	char executable[] = "Funke-AsyncAsset-Test";
	char background[] = "--background";
	char* arguments[] = { executable, background };
	struct RunCleanup {
		~RunCleanup() { Application::SetCommandLineArgs(0, nullptr); RunAutomation::Reset(); }
	} cleanup;
	RunAutomation::Options options;
	options.Background = true;
	RunAutomation::Configure(options);
	Application::SetCommandLineArgs(2, arguments);
	AudioManager::SetBackend(std::make_unique<MiniaudioBackend>(true));
	AsyncRenderingApplication application(files);
	CHECK(application.Run() == 0);
	CHECK(application.BackendReady);
	CHECK(application.AdapterValid);
	CHECK(application.FontManagerReady);
	CHECK(application.Background);
	CHECK(application.Deferred);
	CHECK(application.TypedDeferred);
	CHECK(application.TypedRequestsReleased);
	CHECK(application.TypedSyncLoadsSucceeded);
	CHECK(application.RuntimeTextureAccepted);
	CHECK(application.RuntimeTextureKindChecked);
	CHECK(application.DestroyedRuntimeTextureRejected);
	CHECK_FALSE(application.TimedOut);
	CHECK(application.UpdateCount > 0);
	for (size_t index = 0; index < application.Statuses.size(); ++index) {
		INFO(application.Errors[index]);
		CHECK(application.Statuses[index] == AssetLoadStatus::Success);
	}
	CHECK(application.TextureCacheShared);
	CHECK(application.TextureSamplingCorrect);
	CHECK(application.TexturePixelsCorrect);
	CHECK(application.TextureGpuPixelsCorrect);
	CHECK(application.FontCacheShared);
	CHECK(application.FontAtlasReady);
	CHECK(application.GlyphUvsValid);
	CHECK(application.AudioCacheShared);
	CHECK(application.AudioDecoded);
}
