#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Core/GraphicsSettings.hpp"
#include "Core/Layer.hpp"
#include "Graphics/RenderSettings.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/TextureSampling.hpp"
#include "Project/FunkeProject.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <string>

using namespace Funke;

namespace {
	struct TempProjectDirectory {
		std::filesystem::path Root;
		explicit TempProjectDirectory(const char* prefix) {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(suffix));
			std::filesystem::create_directories(Root / "Assets");
		}
		~TempProjectDirectory() {
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}
	};

	constexpr std::array k_TextureQualities = { TextureQualityLevel::Full, TextureQualityLevel::Half, TextureQualityLevel::Quarter };
	constexpr std::array k_AnisotropyLevels = {
		AnisotropicFilteringLevel::Off, AnisotropicFilteringLevel::X2, AnisotropicFilteringLevel::X4,
		AnisotropicFilteringLevel::X8, AnisotropicFilteringLevel::X16 };

	// Restores the global scene sampler state other tests (and later frames) expect.
	struct SceneSamplingScope {
		TextureQualityLevel Quality = Texture2D::GetSceneTextureQuality();
		AnisotropicFilteringLevel Anisotropy = Texture2D::GetSceneAnisotropy();
		~SceneSamplingScope() { Texture2D::SetSceneSampling(Quality, Anisotropy); }
	};

	ApplicationConfig MakeHeadlessConfiguration() {
		ApplicationConfig config = ApplicationConfig::Headless();
		config.EnablePhysics2D = false;
		config.EnableAudio = false;
		config.EnableScripting = false;
		config.EnablePackageHost = false;
		config.JobSystemWorkerCount = 1;
		return config;
	}

	// Frame 1 requests new settings, frame 2 sees them committed.
	struct QualityApplyResults {
		GraphicsApplyStatus First = GraphicsApplyStatus::NotInitialized;
		GraphicsApplyStatus Repeated = GraphicsApplyStatus::NotInitialized;
		GraphicsApplyStatus Clamped = GraphicsApplyStatus::NotInitialized;
		GraphicsSettings RequestedAfterApply;
		GraphicsSettings EffectiveBeforeCommit;
		GraphicsSettings EffectiveAfterCommit;
		TextureQualityLevel SamplerQuality = TextureQualityLevel::Full;
		AnisotropicFilteringLevel SamplerAnisotropy = AnisotropicFilteringLevel::X16;
		GraphicsApplyStatus Reset = GraphicsApplyStatus::NotInitialized;
		GraphicsApplyStatus BindingMsaa = GraphicsApplyStatus::NotInitialized;
		AntiAliasingMode BindingMsaaRequested = AntiAliasingMode::Off;
		AntiAliasingMode BindingOutOfRangeRequested = AntiAliasingMode::Off;
		bool Committed = false;
	};

	class QualityApplyLayer final : public Layer {
	public:
		explicit QualityApplyLayer(QualityApplyResults& results)
			: Layer("Graphics Quality Apply"), m_Results(results) {}

		void OnUpdate(Application&, float) override {
			++m_Frame;
			if (m_Frame == 1) {
				m_Results.First = RenderSettings::SetTextureQuality(TextureQualityLevel::Half);
				RenderSettings::SetAnisotropicFiltering(AnisotropicFilteringLevel::X2);
				RenderSettings::SetAntiAliasing(AntiAliasingMode::FXAA);
				RenderSettings::SetAntiAliasingQuality(AntiAliasingQualityLevel::Low);
				m_Results.Repeated = RenderSettings::SetTextureQuality(TextureQualityLevel::Half);
				m_Results.RequestedAfterApply = RenderSettings::Get();
				m_Results.EffectiveBeforeCommit = RenderSettings::GetEffective();
				return;
			}
			if (m_Frame == 2) {
				m_Results.EffectiveAfterCommit = RenderSettings::GetEffective();
				m_Results.SamplerQuality = Texture2D::GetSceneTextureQuality();
				m_Results.SamplerAnisotropy = Texture2D::GetSceneAnisotropy();
				m_Results.Committed = true;
				m_Results.Clamped = RenderSettings::SetAnisotropicFiltering(static_cast<AnisotropicFilteringLevel>(200));
				// C# passes the enum as an int: MSAA4x must survive the binding, larger values clamp to it.
				NativeBindings bindings{};
				ScriptBindings::PopulateNativeBindings(bindings);
				m_Results.BindingMsaa = static_cast<GraphicsApplyStatus>(bindings.RenderSettings_ApplyQuality(0, 4, 2, 2));
				m_Results.BindingMsaaRequested = RenderSettings::GetAntiAliasing();
				bindings.RenderSettings_ApplyQuality(0, 4, 9, 2);
				m_Results.BindingOutOfRangeRequested = RenderSettings::GetAntiAliasing();
				m_Results.Reset = RenderSettings::ResetToDefaults();
				Application::Quit();
			}
		}

	private:
		QualityApplyResults& m_Results;
		int m_Frame = 0;
	};

	class QualityApplyApplication final : public Application {
	public:
		QualityApplyResults Results;

		QualityApplyApplication() {
			PushLayer<QualityApplyLayer>(Results);
		}

		ApplicationConfig GetConfiguration() const override {
			return MakeHeadlessConfiguration();
		}

		void ConfigureScenes() override {
			SceneManager::RegisterScene("Graphics Quality Scene")
				.WithoutDefaultCamera2D()
				.SetAsStartupScene();
		}
	};
}

TEST_CASE("Texture and anti-aliasing settings have stable defaults and clamp out-of-range values") {
	const GraphicsSettings defaults;
	CHECK(defaults.TextureQuality == TextureQualityLevel::Full);
	CHECK(defaults.AnisotropicFiltering == AnisotropicFilteringLevel::X16);
	CHECK(defaults.AntiAliasing == AntiAliasingMode::Off);
	CHECK(defaults.AntiAliasingQuality == AntiAliasingQualityLevel::High);
	CHECK(ClampGraphicsSettings(defaults) == defaults);

	GraphicsSettings settings;
	settings.TextureQuality = static_cast<TextureQualityLevel>(9);
	settings.AnisotropicFiltering = static_cast<AnisotropicFilteringLevel>(77);
	settings.AntiAliasing = static_cast<AntiAliasingMode>(5);
	settings.AntiAliasingQuality = static_cast<AntiAliasingQualityLevel>(255);
	const GraphicsSettings clamped = ClampGraphicsSettings(settings);
	CHECK(clamped.TextureQuality == TextureQualityLevel::Quarter);
	CHECK(clamped.AnisotropicFiltering == AnisotropicFilteringLevel::X16);
	CHECK(clamped.AntiAliasing == AntiAliasingMode::MSAA4x);
	CHECK(clamped.AntiAliasingQuality == AntiAliasingQualityLevel::High);

	settings = GraphicsSettings{};
	settings.AntiAliasing = AntiAliasingMode::MSAA4x;
	CHECK(ClampGraphicsSettings(settings).AntiAliasing == AntiAliasingMode::MSAA4x);

	// Serialized by name, so the values are append-only.
	CHECK(static_cast<int>(TextureQualityLevel::Quarter) == 2);
	CHECK(static_cast<int>(AnisotropicFilteringLevel::X16) == 4);
	CHECK(static_cast<int>(AntiAliasingMode::FXAA) == 1);
	CHECK(static_cast<int>(AntiAliasingMode::MSAA4x) == 2);
	CHECK(static_cast<int>(AntiAliasingQualityLevel::High) == 2);
}

TEST_CASE("Only MSAA4x multisamples the scene target") {
	CHECK(GetMultisampleCount(AntiAliasingMode::Off) == 1u);
	CHECK(GetMultisampleCount(AntiAliasingMode::FXAA) == 1u);
	CHECK(GetMultisampleCount(AntiAliasingMode::MSAA4x) == 4u);
}

TEST_CASE("Texture quality skips mip levels and anisotropy maps to the sampler limit") {
	CHECK(GetTextureMipSkip(TextureQualityLevel::Full) == 0u);
	CHECK(GetTextureMipSkip(TextureQualityLevel::Half) == 1u);
	CHECK(GetTextureMipSkip(TextureQualityLevel::Quarter) == 2u);
	CHECK(GetTextureMipSkip(static_cast<TextureQualityLevel>(40)) == 2u);

	CHECK(GetMaxAnisotropy(AnisotropicFilteringLevel::Off) == 1);
	CHECK(GetMaxAnisotropy(AnisotropicFilteringLevel::X2) == 2);
	CHECK(GetMaxAnisotropy(AnisotropicFilteringLevel::X4) == 4);
	CHECK(GetMaxAnisotropy(AnisotropicFilteringLevel::X8) == 8);
	CHECK(GetMaxAnisotropy(AnisotropicFilteringLevel::X16) == 16);
	CHECK(GetMaxAnisotropy(static_cast<AnisotropicFilteringLevel>(12)) == 16);
}

TEST_CASE("Point-filtered textures keep their sampler at every texture setting") {
	for (const TextureQualityLevel quality : k_TextureQualities) {
		for (const AnisotropicFilteringLevel anisotropy : k_AnisotropyLevels) {
			CAPTURE(static_cast<int>(quality));
			CAPTURE(static_cast<int>(anisotropy));
			CHECK(ResolveTextureSampling(Filter::Point, quality, anisotropy) == TextureSamplingQuality{});
			CHECK(ResolveTextureSampling(Filter::Default, quality, anisotropy) == TextureSamplingQuality{});
		}
	}
}

TEST_CASE("Texture sampling: Bilinear gets the mip limit, Trilinear and Anisotropic also the anisotropy") {
	const TextureSamplingQuality bilinear = ResolveTextureSampling(Filter::Bilinear, TextureQualityLevel::Half, AnisotropicFilteringLevel::X16);
	CHECK(bilinear.LodMinClamp == doctest::Approx(1.0f));
	CHECK(bilinear.MaxAnisotropy == 1);

	for (const Filter filter : { Filter::Trilinear, Filter::Anisotropic }) {
		CAPTURE(static_cast<int>(filter));
		const TextureSamplingQuality full = ResolveTextureSampling(filter, TextureQualityLevel::Full, AnisotropicFilteringLevel::Off);
		CHECK(full.LodMinClamp == doctest::Approx(0.0f));
		CHECK(full.MaxAnisotropy == 1);
		const TextureSamplingQuality quarter = ResolveTextureSampling(filter, TextureQualityLevel::Quarter, AnisotropicFilteringLevel::X8);
		CHECK(quarter.LodMinClamp == doctest::Approx(2.0f));
		CHECK(quarter.MaxAnisotropy == 8);
	}
}

TEST_CASE("Scene samplers follow SetSceneSampling and report real changes only") {
	SceneSamplingScope restore;
	Texture2D::SetSceneSampling(TextureQualityLevel::Full, AnisotropicFilteringLevel::X16);

	CHECK(Texture2D::SetSceneSampling(TextureQualityLevel::Half, AnisotropicFilteringLevel::X4));
	CHECK(Texture2D::GetSceneTextureQuality() == TextureQualityLevel::Half);
	CHECK(Texture2D::GetSceneAnisotropy() == AnisotropicFilteringLevel::X4);
	CHECK_FALSE(Texture2D::SetSceneSampling(TextureQualityLevel::Half, AnisotropicFilteringLevel::X4));
	CHECK(Texture2D::SetSceneSampling(TextureQualityLevel::Half, AnisotropicFilteringLevel::Off));
}

TEST_CASE("FunkeProject round-trips texture quality and anti-aliasing by name") {
	TempProjectDirectory dir("FunkeGraphicsQualityRT_");
	FunkeProject project;
	project.Name = "Graphics Quality RT";
	project.RootDirectory = dir.Root.string();
	project.ProjectFilePath = Path::Combine(dir.Root.string(), "project.json");
	project.Graphics.TextureQuality = TextureQualityLevel::Quarter;
	project.Graphics.AnisotropicFiltering = AnisotropicFilteringLevel::X4;
	project.Graphics.AntiAliasing = AntiAliasingMode::FXAA;
	project.Graphics.AntiAliasingQuality = AntiAliasingQualityLevel::Medium;
	REQUIRE(project.Save());

	Json::Value saved;
	std::string error;
	REQUIRE(Json::TryParse(File::ReadAllText(project.ProjectFilePath), saved, &error));
	const Json::Value* graphics = saved.FindMember("graphics");
	REQUIRE(graphics);
	REQUIRE(graphics->FindMember("textureQuality"));
	CHECK(graphics->FindMember("textureQuality")->AsStringOr() == "Quarter");
	REQUIRE(graphics->FindMember("anisotropicFiltering"));
	CHECK(graphics->FindMember("anisotropicFiltering")->AsStringOr() == "X4");
	REQUIRE(graphics->FindMember("antiAliasing"));
	CHECK(graphics->FindMember("antiAliasing")->AsStringOr() == "FXAA");
	REQUIRE(graphics->FindMember("antiAliasingQuality"));
	CHECK(graphics->FindMember("antiAliasingQuality")->AsStringOr() == "Medium");

	const FunkeProject loaded = FunkeProject::Load(dir.Root.string());
	CHECK(loaded.Graphics.TextureQuality == TextureQualityLevel::Quarter);
	CHECK(loaded.Graphics.AnisotropicFiltering == AnisotropicFilteringLevel::X4);
	CHECK(loaded.Graphics.AntiAliasing == AntiAliasingMode::FXAA);
	CHECK(loaded.Graphics.AntiAliasingQuality == AntiAliasingQualityLevel::Medium);

	SUBCASE("MSAA4x is stored and loaded by name") {
		project.Graphics.AntiAliasing = AntiAliasingMode::MSAA4x;
		REQUIRE(project.Save());
		Json::Value msaaSaved;
		REQUIRE(Json::TryParse(File::ReadAllText(project.ProjectFilePath), msaaSaved, &error));
		const Json::Value* msaaGraphics = msaaSaved.FindMember("graphics");
		REQUIRE(msaaGraphics);
		REQUIRE(msaaGraphics->FindMember("antiAliasing"));
		CHECK(msaaGraphics->FindMember("antiAliasing")->AsStringOr() == "MSAA4x");
		CHECK(FunkeProject::Load(dir.Root.string()).Graphics.AntiAliasing == AntiAliasingMode::MSAA4x);
	}

	SUBCASE("older project.json files without the keys load the defaults") {
		REQUIRE(File::WriteAllText(project.ProjectFilePath,
			R"({"name":"Graphics Quality RT","graphics":{"renderScale":0.5}})"));
		const FunkeProject legacy = FunkeProject::Load(dir.Root.string());
		CHECK(legacy.Graphics.RenderScale == doctest::Approx(0.5f));
		CHECK(legacy.Graphics.TextureQuality == GraphicsSettings{}.TextureQuality);
		CHECK(legacy.Graphics.AnisotropicFiltering == GraphicsSettings{}.AnisotropicFiltering);
		CHECK(legacy.Graphics.AntiAliasing == GraphicsSettings{}.AntiAliasing);
		CHECK(legacy.Graphics.AntiAliasingQuality == GraphicsSettings{}.AntiAliasingQuality);
	}

	SUBCASE("unknown names fall back to the defaults") {
		REQUIRE(File::WriteAllText(project.ProjectFilePath,
			R"({"name":"Graphics Quality RT","graphics":{"textureQuality":"Ultra","anisotropicFiltering":"32x",)"
			R"("antiAliasing":"TAA","antiAliasingQuality":"Insane"}})"));
		const FunkeProject unknown = FunkeProject::Load(dir.Root.string());
		CHECK(unknown.Graphics.TextureQuality == TextureQualityLevel::Full);
		CHECK(unknown.Graphics.AnisotropicFiltering == AnisotropicFilteringLevel::X16);
		CHECK(unknown.Graphics.AntiAliasing == AntiAliasingMode::Off);
		CHECK(unknown.Graphics.AntiAliasingQuality == AntiAliasingQualityLevel::High);
	}

	SUBCASE("a number instead of a name is rejected") {
		REQUIRE(File::WriteAllText(project.ProjectFilePath,
			R"({"name":"Graphics Quality RT","graphics":{"antiAliasing":1}})"));
		FunkeProject rejected;
		std::string loadError;
		CHECK_FALSE(FunkeProject::TryLoad(dir.Root.string(), rejected, &loadError));
		CHECK(loadError.find("antiAliasing") != std::string::npos);
	}
}

TEST_CASE("Render settings quality bindings read defaults and report NotInitialized without an application") {
	REQUIRE(Application::GetInstance() == nullptr);
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	REQUIRE(bindings.RenderSettings_GetQuality != nullptr);
	REQUIRE(bindings.RenderSettings_ApplyQuality != nullptr);

	int textureQuality = -1, anisotropy = -1, antiAliasing = -1, antiAliasingQuality = -1;
	bindings.RenderSettings_GetQuality(&textureQuality, &anisotropy, &antiAliasing, &antiAliasingQuality);
	CHECK(textureQuality == static_cast<int>(TextureQualityLevel::Full));
	CHECK(anisotropy == static_cast<int>(AnisotropicFilteringLevel::X16));
	CHECK(antiAliasing == static_cast<int>(AntiAliasingMode::Off));
	CHECK(antiAliasingQuality == static_cast<int>(AntiAliasingQualityLevel::High));
	bindings.RenderSettings_GetQuality(nullptr, nullptr, nullptr, nullptr);

	CHECK(bindings.RenderSettings_ApplyQuality(1, 2, 1, 0) == static_cast<int>(GraphicsApplyStatus::NotInitialized));
}

TEST_CASE("Texture and anti-aliasing changes are committed at the next frame") {
	SceneSamplingScope restore;
	QualityApplyApplication application;
	application.Run();

	const QualityApplyResults& r = application.Results;
	REQUIRE(r.Committed);
	CHECK(r.First == GraphicsApplyStatus::Applied);
	CHECK(r.Repeated == GraphicsApplyStatus::Unchanged);
	CHECK(r.RequestedAfterApply.TextureQuality == TextureQualityLevel::Half);
	CHECK(r.RequestedAfterApply.AnisotropicFiltering == AnisotropicFilteringLevel::X2);
	CHECK(r.RequestedAfterApply.AntiAliasing == AntiAliasingMode::FXAA);
	CHECK(r.RequestedAfterApply.AntiAliasingQuality == AntiAliasingQualityLevel::Low);
	CHECK(r.EffectiveBeforeCommit.TextureQuality == TextureQualityLevel::Full);
	CHECK(r.EffectiveBeforeCommit.AntiAliasing == AntiAliasingMode::Off);
	CHECK(r.EffectiveAfterCommit == r.RequestedAfterApply);
	CHECK(r.SamplerQuality == TextureQualityLevel::Half);
	CHECK(r.SamplerAnisotropy == AnisotropicFilteringLevel::X2);
	// Clamped to X16, which differs from the requested X2.
	CHECK(r.Clamped == GraphicsApplyStatus::Applied);
	CHECK(r.BindingMsaa == GraphicsApplyStatus::Applied);
	CHECK(r.BindingMsaaRequested == AntiAliasingMode::MSAA4x);
	CHECK(r.BindingOutOfRangeRequested == AntiAliasingMode::MSAA4x);
	CHECK(r.Reset == GraphicsApplyStatus::Applied);
}
