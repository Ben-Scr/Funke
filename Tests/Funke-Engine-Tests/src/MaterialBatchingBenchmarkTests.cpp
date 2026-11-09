#include <doctest/doctest.h>

#include "Assets/AssetRegistry.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Graphics/CustomShaderGpu.hpp"
#include "Graphics/Framebuffer.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/RenderApi.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/TextureManager.hpp"
#include "Jobs/JobSystem.hpp"
#include "Profiling/Profiler.hpp"
#include "Profiling/GpuTimer.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <glm/ext/matrix_clip_space.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

	using namespace Funke;
	using BenchmarkClock = std::chrono::steady_clock;
	constexpr int k_Sprites = 10000;
	constexpr int k_Width = 1280;
	constexpr int k_Height = 720;
	constexpr uint64_t k_Shader = 0x7138000050000001ull;
	constexpr uint64_t k_FirstMaterial = k_Shader + 1;
	constexpr const char* k_ShaderSource = R"WGSL(// @name Material batching probe
// @target Sprite2D
// @blend Alpha
// @property vec4 tint = (0.25, 0.75, 0.5, 1)
fn surface(in: SurfaceInput) -> SurfaceOutput {
 var out: SurfaceOutput;
 out.color = material.tint * in.texColor * in.color;
 return out;
}
)WGSL";

	void Require(bool condition, const char* message) {
		if (!condition) throw std::runtime_error(message);
	}

	std::string Environment(const char* name, const char* fallback = "") {
		const char* value = std::getenv(name);
		return value ? value : fallback;
	}

	int Setting(const char* name, int fallback) {
		const int value = std::stoi(Environment(name, std::to_string(fallback).c_str()));
		Require(value > 0 && value <= 10000, "Invalid material benchmark sample setting");
		return value;
	}

	double Milliseconds(BenchmarkClock::time_point begin) {
		return std::chrono::duration<double, std::milli>(BenchmarkClock::now() - begin).count();
	}

	void SetTint(uint64_t id, glm::vec4 tint) {
		Material material;
		material.Shader = k_Shader;
		MaterialPropertyValue value;
		value.Type = ShaderPropertyType::Vec4;
		value.Value = tint;
		material.Properties.emplace("tint", value);
		MaterialLibrary::Set(id, material);
	}

	void ClearSamples(const char* name) {
		Profiler::SetModuleEnabled(name, false);
		Profiler::SetModuleEnabled(name, true);
	}

	class MaterialBenchmarkApplication final : public Application {
	public:
		explicit MaterialBenchmarkApplication(std::filesystem::path output) : m_Output(std::move(output)) {}

		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Minimal();
			config.WindowSpecification = WindowSpecification(160, 120, "Funke material benchmark", false, true, false);
			config.EnableShaderManager = true;
			config.SetWindowIcon = false;
			config.RunInBackground = true;
			config.Vsync = false;
			if (Environment("FUNKE_SPRITE_MATRIX") == "1") {
				config.JobSystemWorkerCount = std::stoi(Environment("FUNKE_SPRITE_WORKERS", "4"));
				config.EnableJobSystem = config.JobSystemWorkerCount > 0;
			}
			return config;
		}

		void Update() override {
			Application::Quit();
			try { Measure(); }
			catch (const std::exception& error) { Error = error.what(); }
		}

		bool Completed = false;
		std::string Error;

	private:
		struct Sample { double Cpu; double Gpu; size_t Draws; size_t Visible; };

		Sample Draw(Renderer2D& renderer, Scene& scene, Framebuffer& target) {
			ClearSamples("Renderer2D.Submit");
			ClearSamples("GPU");
			renderer.BeginFrame();
			RenderApi::BindFramebuffer(target);
			RenderApi::SetViewport(0, 0, k_Width, k_Height);
			RenderApi::SetClearColor(Color::Black());
			RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
			renderer.RenderSceneWithVP(scene, glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f),
				AABB({ -1, -1 }, { 1, 1 }));
			const auto* cpu = Profiler::Find("Renderer2D.Submit");
			Require(cpu && cpu->Count == 1, "Expected exactly one CPU submit sample; enable the profiler");
			const double cpuMs = cpu->CurrentValue;
			RenderApi::BindDefaultFramebuffer();
			// Mapping is legal only after the sprite pass and its query resolve have been submitted.
			renderer.OnAfterPresent();
			double gpuMs = -1;
			if (GpuTimer::IsSupported()) {
				const auto deadline = BenchmarkClock::now() + std::chrono::seconds(10);
				while (true) {
					renderer.EndFrame();
					const auto* gpu = Profiler::Find("GPU");
					if (gpu && gpu->Count == 1) { gpuMs = gpu->CurrentValue; break; }
					Require(BenchmarkClock::now() < deadline, "GPU timestamp readback timed out");
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			return { cpuMs, gpuMs, renderer.GetDrawCallsCount(), renderer.GetRenderedInstancesCount() };
		}

		std::vector<uint8_t> Pixels(Framebuffer& target) {
			std::vector<uint8_t> pixels;
			Require(target.ReadPixelsRGBA8(pixels) && pixels.size() == k_Width * k_Height * 4,
				"Framebuffer readback failed");
			return pixels;
		}

		void SaveImage(const std::vector<uint8_t>& pixels, const std::string& name) {
			std::ofstream image(m_Output / name, std::ios::binary);
			image << "P6\n" << k_Width << " " << k_Height << "\n255\n";
			for (size_t i = 0; i < pixels.size(); i += 4) image.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
			Require(image.good(), "Cannot save benchmark image");
		}

		void CheckTransparency(Renderer2D& renderer, Framebuffer& target, TextureHandle texture) {
			auto scene = Scene::CreateDetachedScene("Material transparency regression");
			SetTint(k_FirstMaterial, { 1, 0, 0, 0.5f });
			SetTint(k_FirstMaterial + 1, { 0, 1, 0, 0.5f });
			for (int i = 0; i < 3; ++i) {
				Entity entity = scene->CreateEmptyEntity();
				entity.AddComponent<Transform2DComponent>();
				auto& sprite = entity.AddComponent<SpriteRendererComponent>();
				sprite.TextureHandle = texture;
				sprite.SortingLayer = i == 2 ? 1 : 0;
				sprite.SortingOrder = static_cast<short>(i == 2 ? -10 : i);
				sprite.Material = k_FirstMaterial + (i == 1 ? 1 : 0);
			}
			auto sample = Draw(renderer, *scene, target);
			Require(sample.Draws == 3 && sample.Visible == 3, "Transparent A/B/A material runs were merged or culled");
			auto pixels = Pixels(target);
			const size_t center = (k_Height / 2 * k_Width + k_Width / 2) * 4;
			Require(std::abs(int(pixels[center]) - 159) <= 2 && std::abs(int(pixels[center + 1]) - 64) <= 2,
				"Transparent sorting layer/order or distinct material properties were lost");
			SaveImage(pixels, "transparency.ppm");
			SetTint(k_FirstMaterial, { 0, 0, 1, 0.5f });
			Draw(renderer, *scene, target);
			pixels = Pixels(target);
			Require(pixels[center] <= 2 && std::abs(int(pixels[center + 2]) - 159) <= 2,
				"Material mutation did not reach both shared references");
			Renderer2D::ClearSceneCache(scene.get());
		}

		void MeasureSpriteMatrix(Renderer2D& renderer, Framebuffer& target, TextureHandle texture) {
			Require(JobSystem::GetWorkerCount() == std::stoi(Environment("FUNKE_SPRITE_WORKERS", "4")),
				"Sprite matrix worker configuration was not applied");
			Framebuffer secondTarget(k_Width, k_Height);
			Require(secondTarget.IsValid(), "No second view framebuffer");
			constexpr std::array scopes{ "Renderer2D.Collect", "Renderer2D.Sort", "Renderer2D.Upload", "Renderer2D.Submit" };
			for (const auto* scope : scopes) Profiler::Register(scope);
			Profiler::SetTrackingSpan(128);
			const int warmups = Setting("FUNKE_BATCH_WARMUPS", 20);
			const int samples = Setting("FUNKE_BATCH_SAMPLES", 60);
			const int repeats = Setting("FUNKE_BATCH_REPEATS", 3);
			struct Workload { int Entities; int VisiblePercent; int Order; bool Static; int Scenes; int Views; };
			std::vector<Workload> matrix;
			for (int count : { 100, 2048, 4095, 4096, 4097, 8192 })
				for (int visible : { 0, 10, 100 }) for (int order : { 0, 1, 2 }) for (bool isStatic : { false, true })
					matrix.push_back({ count, visible, order, isStatic, 1, 1 });
			for (int scenes : { 1, 4, 16 }) for (int views : { 1, 2 })
				matrix.push_back({ 8192, 100, 2, false, scenes, views });
			std::ofstream raw(m_Output / "sprite-matrix.csv");
			raw.imbue(std::locale::classic());
			raw << "case,repeat,sample,entities,visible,order,is_static,scenes,views,cpu_render_ms,collect_ms,sort_ms,upload_ms,submit_ms,collect_calls,sort_calls,upload_calls,submit_calls,draw_calls\n" << std::setprecision(10);
			SetTint(k_FirstMaterial, { 0.25f, 0.75f, 0.5f, 1 });
			for (int repeat = 0; repeat < repeats; ++repeat) for (size_t index = 0; index < matrix.size(); ++index) {
				const size_t caseIndex = (index + repeat * 43) % matrix.size();
				const auto& work = matrix[caseIndex];
				std::vector<std::unique_ptr<Scene>> scenes;
				for (int i = 0; i < work.Scenes; ++i) scenes.push_back(Scene::CreateDetachedScene("Sprite matrix " + std::to_string(i)));
				const int visible = work.Entities * work.VisiblePercent / 100;
				for (int i = 0; i < work.Entities; ++i) {
					Entity entity = scenes[i % work.Scenes]->CreateEmptyEntity();
					entity.AddComponent<Transform2DComponent>(Vec2(i < visible ? -0.9921875f + (i % 128) / 64.0f : 3.0f,
						-0.984375f + (i / 128) / 32.0f), Vec2(0.010f, 0.020f));
					auto& sprite = entity.AddComponent<SpriteRendererComponent>();
					sprite.TextureHandle = texture;
					sprite.Material = k_FirstMaterial;
					sprite.SortingOrder = static_cast<short>((work.Order == 0 ? i : work.Order == 1 ? work.Entities - i - 1
						: (i * 4051) % work.Entities) - work.Entities / 2);
					if (work.Static) entity.AddComponent<StaticTag>();
				}
				const auto draw = [&](int sample) {
					for (const auto* scope : scopes) ClearSamples(scope);
					renderer.BeginFrame();
					double cpu = 0;
					for (int view = 0; view < work.Views; ++view) {
						RenderApi::BindFramebuffer(view == 0 ? target : secondTarget);
						RenderApi::SetViewport(0, 0, k_Width, k_Height);
						RenderApi::SetClearColor(Color::Black());
						RenderApi::Clear(ClearFlags::Color | ClearFlags::Depth);
						const auto begin = BenchmarkClock::now();
						renderer.RenderScenesWithVP([&](const auto& render) { for (const auto& scene : scenes) render(*scene); },
							glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f), AABB({ -1, -1 }, { 1, 1 }));
						cpu += Milliseconds(begin);
						Require(renderer.GetRenderedInstancesCount() == static_cast<size_t>(visible), "Sprite matrix visible count changed");
					}
					RenderApi::BindDefaultFramebuffer();
					renderer.OnAfterPresent();
					const auto pixels = Pixels(target);
					if (work.Views == 2) Require(Pixels(secondTarget) == pixels, "Equal views disagree after target switching");
					renderer.EndFrame();
					const auto* collect = Profiler::Find(scopes[0]);
					Require(collect && collect->Count == static_cast<size_t>(work.Scenes * work.Views), "Missing scene/view collect samples");
					Require(renderer.GetDrawCallsCount() == static_cast<size_t>(visible == 0 ? 0 : work.Scenes * work.Views), "Missing sprite matrix draw calls");
					if (sample >= 0) {
						raw << caseIndex << ',' << repeat << ',' << sample << ',' << work.Entities << ',' << visible << ','
							<< work.Order << ',' << work.Static << ',' << work.Scenes << ',' << work.Views << ',' << cpu;
						for (const auto* scope : scopes) raw << ',' << Profiler::Find(scope)->RunningSum;
						for (const auto* scope : scopes) raw << ',' << Profiler::Find(scope)->Count;
						raw << ',' << renderer.GetDrawCallsCount() << '\n';
					}
					if (sample == -1) {
						SaveImage(pixels, "sprite-" + std::to_string(caseIndex) + "-r" + std::to_string(repeat) + ".ppm");
						for (int i = 0; i < work.Entities; ++i) {
							const size_t offset = (static_cast<size_t>((63 - i / 128 + 0.5) * k_Height / 64) * k_Width
								+ static_cast<size_t>((i % 128 + 0.5) * k_Width / 128)) * 4;
							Require(i < visible ? pixels[offset + 1] >= 189 && pixels[offset + 1] <= 193 : pixels[offset + 1] == 0,
								"Sprite matrix image disagrees with visibility reference");
						}
					}
				};
				for (int i = 0; i < warmups; ++i) draw(-i - 2);
				draw(-1);
				for (int i = 0; i < samples; ++i) draw(i);
				for (const auto& scene : scenes) Renderer2D::ClearSceneCache(scene.get());
			}
			Require(raw.good(), "Cannot write sprite matrix samples");
			std::ofstream marker(m_Output / "sprite-matrix.complete.txt");
			marker << Engine::GetVersionLong() << '\n' << RenderApi::GetVersionString() << '\n' << RenderApi::GetRendererString() << '\n';
			marker << JobSystem::GetWorkerCount() << '\n';
#ifdef _MSC_FULL_VER
			marker << "MSVC " << _MSC_FULL_VER << '\n';
#elif defined(__VERSION__)
			marker << __VERSION__ << '\n';
#else
			marker << "unknown\n";
#endif
			Require(marker.good(), "Cannot write sprite matrix completion marker");
		}

		void Measure() {
			Require(RenderApi::IsInitialized(), "No render backend");
			Require(TextureManager::IsValid(TextureManager::GetDefaultTexture(DefaultTexture::Square)),
				"Missing default mask texture; stage Funke-Runtime/FunkeAssets next to the test executable");
			const auto* program = CustomShaderGpu::GetProgram(k_Shader, ShaderTarget::Sprite2D);
			Require(program && !program->IsFallback, "Benchmark custom shader failed to compile");
			Profiler::SetBackgroundTracking(true);
			Profiler::SetSamplingHz(0);
			Profiler::SetTrackingSpan(4);
			Profiler::Register("Renderer2D.Submit");
			Renderer2D renderer;
			renderer.Initialize();
			struct Cleanup {
				Renderer2D& Renderer;
				~Cleanup() { RenderApi::BindDefaultFramebuffer(); Renderer.Shutdown(); }
			} cleanup{ renderer };
			renderer.SetSkipBeginFrameRender(true);
			renderer.SetPostProcessingEnabled(false);
			Framebuffer target(k_Width, k_Height);
			Require(target.IsValid(), "No benchmark framebuffer");
			const uint8_t white[] = { 255, 255, 255, 255 };
			const auto textureId = TextureManager::CreateRuntimeTexture(1, 1, white, Filter::Point);
			const auto texture = TextureManager::LoadTextureByUUID(textureId);
			Require(TextureManager::IsValid(texture), "No explicit benchmark texture");
			CheckTransparency(renderer, target, texture);
			if (Environment("FUNKE_SPRITE_MATRIX") == "1") {
				MeasureSpriteMatrix(renderer, target, texture);
				TextureManager::DestroyRuntimeTexture(textureId);
				Completed = true;
				return;
			}
			const int warmups = Setting("FUNKE_BATCH_WARMUPS", 20);
			const int samples = Setting("FUNKE_BATCH_SAMPLES", 60);
			const int repeats = Setting("FUNKE_BATCH_REPEATS", 3);
			Json::Value result = Json::Value::MakeObject();
			result.AddMember("engineVersion", std::string(Engine::GetVersionLong()));
			result.AddMember("graphicsBackend", RenderApi::GetVersionString());
			result.AddMember("gpu", RenderApi::GetRendererString());
			result.AddMember("cpu", std::string(Engine::GetCpuName()));
			result.AddMember("timestampQueries", GpuTimer::IsSupported());
#ifdef FNK_DEBUG
			result.AddMember("nativeConfiguration", "Debug");
#else
			result.AddMember("nativeConfiguration", "Release");
#endif
#ifdef _MSC_FULL_VER
			result.AddMember("compiler", "MSVC " + std::to_string(_MSC_FULL_VER));
#elif defined(__VERSION__)
			result.AddMember("compiler", __VERSION__);
#else
			result.AddMember("compiler", nullptr);
#endif
			result.AddMember("transparencyAndMutationPassed", true);
			Json::Value runs = Json::Value::MakeArray();
			std::vector<uint8_t> reference;
			const std::array<const char*, 4> modes{ "duplicates-grouped", "shared", "distinct-grouped", "duplicates-interleaved" };
			for (int repeat = 0; repeat < repeats; ++repeat) {
				for (int caseIndex = 0; caseIndex < 16; ++caseIndex) {
					const int rotated = (caseIndex + repeat * 5) % 16;
					const int materials = std::array{ 1, 10, 100, 1000 }[rotated / 4];
					const std::string mode = modes[rotated % 4];
					const bool shared = mode == "shared", distinct = mode == "distinct-grouped";
					const bool interleaved = mode == "duplicates-interleaved";
					const int activeMaterials = shared ? 1 : materials;
					const auto begin = BenchmarkClock::now();
					const auto epoch = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
					for (int i = 0; i < activeMaterials; ++i) {
						SetTint(k_FirstMaterial + i, { distinct ? 0.25f + 0.5f * i / materials : 0.25f, 0.75f, 0.5f, 1 });
					}
					auto scene = Scene::CreateDetachedScene("Material batching grid 100x100");
					for (int i = 0; i < k_Sprites; ++i) {
						Entity entity = scene->CreateEmptyEntity();
						entity.AddComponent<Transform2DComponent>(Vec2(-0.99f + (i % 100) * 0.02f, -0.99f + (i / 100) * 0.02f), Vec2(0.016f));
						auto& sprite = entity.AddComponent<SpriteRendererComponent>();
						sprite.TextureHandle = texture;
						sprite.SortingOrder = static_cast<short>(i);
						sprite.Material = k_FirstMaterial + (shared ? 0 : interleaved ? i % materials : i * materials / k_Sprites);
					}
					const double setupMs = Milliseconds(begin);
					const auto warmupBegin = BenchmarkClock::now();
					for (int i = 0; i < warmups; ++i) Draw(renderer, *scene, target);
					const double warmupSeconds = Milliseconds(warmupBegin) / 1000;
					auto pixels = Pixels(target);
					for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) {
						const size_t offset = (static_cast<size_t>((y + 0.5) * k_Height / 100) * k_Width
							+ static_cast<size_t>((x + 0.5) * k_Width / 100)) * 4;
						Require(pixels[offset + 1] >= 189 && pixels[offset + 1] <= 193, "A grid sprite is missing or uses a fallback shader");
					}
					if (!distinct) {
						if (reference.empty()) reference = pixels;
						Require(pixels == reference, "Equivalent-material reuse changed visible output");
					}
					const std::string id = mode + "-" + std::to_string(materials) + "-r" + std::to_string(repeat);
					SaveImage(pixels, id + ".ppm");
					std::ofstream raw(m_Output / (id + ".samples.csv"));
					raw.imbue(std::locale::classic());
					raw << "sample,cpu_submit_ms,gpu_ms,draw_calls,visible_sprites\n" << std::setprecision(10);
					for (int i = 0; i < samples; ++i) {
						const auto sample = Draw(renderer, *scene, target);
						const size_t expected = shared ? 1 : interleaved && materials > 1 ? k_Sprites : materials;
						Require(sample.Draws == expected && sample.Visible == k_Sprites, "Unexpected actual draw count or visible sprite count");
						raw << i << ',' << sample.Cpu << ',';
						if (sample.Gpu >= 0) raw << sample.Gpu;
						raw << ',' << sample.Draws << ',' << sample.Visible << '\n';
					}
					Require(raw.good(), "Cannot write benchmark samples");
					Json::Value run = Json::Value::MakeObject();
					run.AddMember("runId", id);
					run.AddMember("mode", mode);
					run.AddMember("repeat", repeat);
					run.AddMember("requestedMaterials", materials);
					run.AddMember("materials", activeMaterials);
					run.AddMember("samples", samples);
					run.AddMember("warmupIterations", warmups);
					run.AddMember("warmupSeconds", warmupSeconds);
					run.AddMember("setupMs", setupMs);
					run.AddMember("startedAtEpoch", epoch);
					run.AddMember("wallDurationSeconds", Milliseconds(begin) / 1000);
					runs.Append(std::move(run));
					Renderer2D::ClearSceneCache(scene.get());
				}
			}
			result.AddMember("runs", std::move(runs));
			Require(File::WriteAllText(Path::ToUtf8(m_Output / "capture.json"), Json::Stringify(result, true)), "Cannot write capture metadata");
			TextureManager::DestroyRuntimeTexture(textureId);
			Completed = true;
		}

		std::filesystem::path m_Output;
	};
}

TEST_CASE("GPU material batching benchmark preserves visible work and transparent order") {
	if (Environment("FUNKE_BATCH_BENCH") != "1") {
		MESSAGE("Set FUNKE_BATCH_BENCH=1 and FUNKE_BATCH_OUTPUT for the opt-in material benchmark.");
		return;
	}
	const auto output = Path::FromUtf8(Environment("FUNKE_BATCH_OUTPUT"));
	REQUIRE(!output.empty());
	std::filesystem::create_directories(output);
	const auto shader = Path::ToUtf8(output / "probe.wgsl");
	REQUIRE(File::WriteAllText(shader, k_ShaderSource));
	AssetRegistry::RegisterBuiltInAsset(shader, k_Shader, AssetKind::Shader);
	struct Cleanup {
		~Cleanup() {
			Application::SetCommandLineArgs(0, nullptr);
			RunAutomation::Reset();
			for (int i = 0; i < 1000; ++i) MaterialLibrary::Invalidate(k_FirstMaterial + i);
		}
	} cleanup;
	RunAutomation::Options options;
	options.Background = true;
	RunAutomation::Configure(options);
	char executable[] = "Funke-Material-Benchmark", background[] = "--background";
	char* arguments[] = { executable, background };
	Application::SetCommandLineArgs(2, arguments);
	MaterialBenchmarkApplication application(output);
	CHECK(application.Run() == 0);
	INFO(application.Error);
	CHECK(application.Completed);
}
