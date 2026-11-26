#include <doctest/doctest.h>
#include "Assets/AssetRegistry.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Scene/Entity.hpp"
#if FUNKE_WITH_2D
#include "Physics/Collision2D.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#endif
#include "Scene/Scene.hpp"
#include "Scripting/BoltScriptHost.hpp"
#include "DevTools/DevConsole.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Scripting/ManagedScriptCompiler.hpp"
#include "Scripting/DataAssetManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Jobs/JobSystem.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptSystem.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {
	using namespace Funke;
	using Clock = std::chrono::steady_clock;

	std::string EditedMoveScript(std::string source, size_t literal, int step) {
		const auto value = std::to_string(step) + ".0f";
		const char* requested = std::getenv("FUNKE_BENCH_EDIT_KIND");
		const std::string_view kind = requested ? requested : "literal";
		if (kind == "declaration") {
			const auto name = "Step" + std::to_string(step);
			source.replace(literal, 4, name + "()");
			source.insert(source.rfind('}'), "	private float " + name + "() { return " + value + "; }\n");
		} else if (kind == "expression") {
			std::string expression = "1.0f";
			for (int i = 1; i < step; ++i) expression += " + 1.0f";
			source.replace(literal, 4, "(" + expression + ")");
		} else {
			REQUIRE(kind == "literal");
			source.replace(literal, 4, value);
		}
		return source;
	}

	struct FinishHeadlessRun {
		Application& App;
		~FinishHeadlessRun() { App.FinishRunCycle(); }
	};
	class Move final : public NativeScript {
	public:
		uint32_t EntityId = 0;
		void Update() override {
			const auto& api = NativeScriptHost::GetEngineAPI();
			float x, y;
			api.GetPosition(EntityId, &x, &y);
			api.SetPosition(EntityId, x + 1.0f, y);
		}
	};

	double Measure(ScriptSystem& scripts, Scene& scene, int frames) {
		const auto start = Clock::now();
		for (int i = 0; i < frames; ++i) scripts.Update(scene);
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count() / frames;
	}
}

TEST_CASE("Bolt and C++ entity update benchmark" * doctest::skip(std::getenv("FUNKE_BOLT_BENCHMARK") == nullptr)) {
	REQUIRE_FALSE(BoltScriptHost::FindCompilerLibrary().empty());
	const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/Move.bolt";
	REQUIRE(std::filesystem::is_regular_file(fixture));
	constexpr uint64_t guid = 0xAB04722026010101ull;
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(fixture), guid, AssetKind::Script);
	constexpr int count = 1000, frames = 8, samples = 11;
	std::array<Move, count> nativeScripts;
	auto bolt = Scene::CreateDetachedScene("Bolt");
	auto cpp = Scene::CreateDetachedScene("C++");
	ScriptSystem scripts;
	double first = 0;
	for (int i = 0; i < count; ++i) {
		auto entity = bolt->CreateEntity();
		entity.GetOrAddComponent<Transform2DComponent>();
		entity.AddComponent<ScriptComponent>().AddScript(std::to_string(guid), ScriptType::Bolt);
		if (i == 0) {
			first = Measure(scripts, *bolt, 1);
			REQUIRE(entity.GetComponent<Transform2DComponent>().Position.x == 1.0f);
			entity.GetComponent<Transform2DComponent>().Position.x = 0;
			entity.GetComponent<Transform2DComponent>().LocalPosition.x = 0;
		}
		auto nativeEntity = cpp->CreateEntity();
		nativeEntity.GetOrAddComponent<Transform2DComponent>();
		auto& sc = nativeEntity.AddComponent<ScriptComponent>();
		sc.AddScript("Move", ScriptType::Native);
		nativeScripts[i].EntityId = static_cast<uint32_t>(nativeEntity.GetHandle());
		sc.Scripts.front().SetNativePtr(&nativeScripts[i]);
	}
	Measure(scripts, *bolt, frames);
	Measure(scripts, *cpp, frames);
	std::array<double, samples> boltMs, cppMs;
	for (int i = 0; i < samples; ++i) {
		if (i % 2 == 0) cppMs[i] = Measure(scripts, *cpp, frames);
		boltMs[i] = Measure(scripts, *bolt, frames);
		if (i % 2 != 0) cppMs[i] = Measure(scripts, *cpp, frames);
	}
	for (Scene* scene : {bolt.get(), cpp.get()}) {
		double sum = 0;
		for (auto [entity, transform] : scene->GetRegistry().view<Transform2DComponent>().each()) {
			REQUIRE(transform.Position.x == transform.LocalPosition.x);
			sum += transform.Position.x;
		}
		REQUIRE(sum == count * frames * (samples + 1));
	}
	std::sort(boltMs.begin(), boltMs.end());
	std::sort(cppMs.begin(), cppMs.end());
	std::printf("Bolt first compile + first entity update %.3f ms\n", first);
	std::printf("%d entities, median [min, max] ms/frame: Bolt %.3f [%.3f, %.3f], C++ %.3f [%.3f, %.3f]\n",
		count, boltMs[samples / 2], boltMs.front(), boltMs.back(), cppMs[samples / 2], cppMs.front(), cppMs.back());
	for (auto [entity, sc] : cpp->GetRegistry().view<ScriptComponent>().each()) sc.Scripts.front().Unbind();
	scripts.OnDestroy(*bolt);
}

TEST_CASE("Bolt versus CSharp entity update benchmark" * doctest::skip(std::getenv("FUNKE_BOLT_CS_MODE") == nullptr)) {
	const char* selectedMode = std::getenv("FUNKE_BOLT_CS_MODE");
	REQUIRE(selectedMode != nullptr);
	const std::string mode = selectedMode;
	REQUIRE((mode == "bolt" || mode == "bolt-entity" || mode == "cs"));
	Application application;
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	const bool managed = mode == "cs";
	if (!managed) std::printf("BOLTHOST %s\n", Path::ToUtf8(BoltScriptHost::FindCompilerLibrary()).c_str());
	const bool compute = std::getenv("FUNKE_BENCH_COMPUTE") != nullptr;
	const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts"
		/ (mode == "bolt-entity" ? (compute ? "EntityCompute.bolt" : "EntityMove.bolt") : (compute ? "Compute.bolt" : "Move.bolt"));
	const auto millis = [](auto start) {
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	};
	double runtimeMs = 0.0, assemblyMs = 0.0;
	constexpr uint64_t guid = 0xAB04722026010102ull;
	if (managed) {
		REQUIRE(std::getenv("FUNKE_BENCH_CORE") != nullptr);
		REQUIRE(std::getenv("FUNKE_BENCH_ASSEMBLY") != nullptr);
		auto start = Clock::now();
		ScriptEngine::Init();
		ScriptEngine::LoadCoreAssembly(std::getenv("FUNKE_BENCH_CORE"));
		runtimeMs = millis(start);
		REQUIRE(ScriptEngine::IsInitialized());
		start = Clock::now();
		REQUIRE(ScriptEngine::LoadUserAssembly(std::getenv("FUNKE_BENCH_ASSEMBLY")));
		assemblyMs = millis(start);
	} else {
		REQUIRE(std::filesystem::is_regular_file(fixture));
		AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(fixture), guid, AssetKind::Script);
	}

	constexpr int entities = 1000, warmup = 256, samples = 31, frames = 16;
	auto scene = Scene::CreateDetachedScene("Language benchmark");
	ScriptSystem scripts;
	const std::string scriptName = managed ? (compute ? "BenchmarkCompute" : "BenchmarkMover") : std::to_string(guid);
	const auto scriptType = managed ? ScriptType::Managed : ScriptType::Bolt;
	auto add = [&]() {
		auto entity = scene->CreateEntity();
		entity.GetOrAddComponent<Transform2DComponent>();
		entity.AddComponent<ScriptComponent>().AddScript(scriptName, scriptType);
		return entity;
	};
	float expectedPosition = 0.0f, penultimate = 0.0f, firstPosition = 0.0f;
	uint64_t state = 0;
	for (int tick = 0; tick <= warmup + samples * frames; ++tick) {
		if (compute) {
			for (int step = 0; step < 256; ++step) state = (state * 25173 + 13849) % 65521;
		}
		penultimate = expectedPosition;
		expectedPosition += float(1 + state % 8);
		if (tick == 0) firstPosition = expectedPosition;
	}
	auto first = add();
	const double firstMs = Measure(scripts, *scene, 1);
	REQUIRE(first.GetComponent<Transform2DComponent>().Position.x == firstPosition);
	for (int i = 1; i < entities; ++i) add();
	Measure(scripts, *scene, warmup);
	std::array<double, samples> timings{};
	for (auto& sample : timings) sample = Measure(scripts, *scene, frames);
	for (auto [e, t] : scene->GetRegistry().view<Transform2DComponent>().each()) {
		const float expected = e == first.GetHandle() ? expectedPosition : penultimate;
		REQUIRE(t.Position.x == expected);
		REQUIRE(t.LocalPosition.x == expected);
	}
	std::sort(timings.begin(), timings.end());
	std::printf("LANGBENCH %s core_ms=%.4f assembly_ms=%.4f first_ms=%.4f update_ms=%.6f\n",
		mode.c_str(), runtimeMs, assemblyMs, firstMs, timings[samples / 2]);
	scripts.OnDestroy(*scene);
}

TEST_CASE("Bolt EntityScript keeps independent object fields between callbacks" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
	Application application;
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/EntityLifecycle.bolt";
	std::string error;
	const bool compiled = BoltScriptHost::ValidateFiles({Path::ToUtf8(path), Path::ToUtf8(path.parent_path() / "Procedural.bolt")}, error);
	INFO(error);
	REQUIRE(compiled);
	constexpr uint64_t guid = 0xAB04722026010205ull;
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(path), guid, AssetKind::Script);
	auto scene = Scene::CreateDetachedScene("Bolt lifecycle");
	ScriptSystem scripts;
	const auto add = [&]() {
		auto entity = scene->CreateEntity();
		entity.GetOrAddComponent<Transform2DComponent>();
		entity.AddComponent<ScriptComponent>().AddScript(std::to_string(guid), ScriptType::Bolt);
		return entity;
	};
	auto first = add();
	scripts.Update(*scene);
	CHECK(first.GetComponent<Transform2DComponent>().Position.x == 13.0f);
	auto second = add();
	const std::string identity = std::to_string(guid);
	auto fields = BoltScriptHost::GetFields(first, identity);
	REQUIRE(fields.size() == 2);
	CHECK(fields[0].Name == "Direction");
	CHECK(fields[1].DisplayName == "Ticks");
	CHECK(fields[1].Value == "1");
	second.GetComponent<ScriptComponent>().PendingFieldValues[identity + ".updates"] = "4";
	scripts.Update(*scene);
	CHECK(first.GetComponent<Transform2DComponent>().Position.x == 15.0f);
	CHECK(second.GetComponent<Transform2DComponent>().Position.x == 17.0f);
	second.GetComponent<ScriptComponent>().PendingFieldValues[identity + ".updates"] = "1";
	BoltScriptHost::ApplyField(second, identity, "updates");
	fields = BoltScriptHost::GetFields(second, identity);
	REQUIRE(fields.size() == 2);
	CHECK(fields[1].Value == "1");
	second.GetComponent<ScriptComponent>().PendingFieldValues[identity + ".Direction"] = "-4,8";
	BoltScriptHost::ApplyField(second, identity, "Direction");
	fields = BoltScriptHost::GetFields(second, identity);
	REQUIRE(fields.size() == 2);
	CHECK(std::stof(fields[0].Value) == -4.0f);
	CHECK(std::stof(fields[0].Value.substr(fields[0].Value.find(',') + 1)) == 8.0f);
	scripts.FixedUpdate(*scene);
	CHECK(first.GetComponent<Transform2DComponent>().Position.y == 1.0f);
	ScriptSystem::SetScriptsEnabled(first, false);
	CHECK(first.GetComponent<Transform2DComponent>().Position.y == 2.0f);
	ScriptSystem::SetScriptsEnabled(first, true);
	scripts.Update(*scene);
	CHECK(first.GetComponent<Transform2DComponent>().Position.x == 18.0f);
#if FUNKE_WITH_2D
	Collision2D collision{ first.GetHandle(), second.GetHandle(), scene.get(), scene.get(), {3.0f, 0.0f} };
	ScriptSystem::DispatchCollisionEnter2D(collision);
	CHECK(first.GetComponent<Transform2DComponent>().Position.x == 21.0f);
	CHECK(second.GetComponent<Transform2DComponent>().Position.x == 22.0f);
#endif
	REQUIRE(ScriptSystem::RemoveScript(first, 0));
	CHECK(first.GetComponent<Transform2DComponent>().Position.y == 4.0f);
	const float removedX = first.GetComponent<Transform2DComponent>().Position.x;
	auto replacement = add();
	scripts.Update(*scene);
	CHECK(replacement.GetComponent<Transform2DComponent>().Position.x == 13.0f);
	scripts.Update(*scene);
	CHECK(replacement.GetComponent<Transform2DComponent>().Position.x == 15.0f);
	CHECK(first.GetComponent<Transform2DComponent>().Position.x == removedX);
	scripts.OnDestroy(*scene);
}

#if FUNKE_WITH_2D && FUNKE_WITH_3D
TEST_CASE("Bolt public scripting API resolves cameras and updates component settings" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
	Application application;
	const auto previousRegion = Window::GetUIRegion();
	struct RestoreRegion {
		Window::UIRegion Previous;
		~RestoreRegion() {
			Window::ClearUIRegion();
			if (Previous.Source == Window::UIRegionSource::Editor)
				Window::SetUIRegion(Previous.OffsetX, Previous.OffsetY, Previous.Width, Previous.Height, Previous.WindowToRegionScale);
			else if (Previous.Source == Window::UIRegionSource::AspectLock)
				Window::PublishAspectLockUIRegion(1.0f, Previous);
		}
	} restore{previousRegion};
	Window::SetUIRegion(100, 50, 800, 600);
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/ScriptApi.bolt";
	constexpr uint64_t guid = 0xab04722026112601ull;
	const auto identity = std::to_string(guid);
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(path), guid, AssetKind::Script);
	auto scene = Scene::CreateDetachedScene("Bolt API");
	auto controls = scene->CreateEntity("Controls");
	controls.AddComponent<SliderComponent>();
	auto entity = scene->CreateEntity();
	entity.GetOrAddComponent<Transform2DComponent>();
	entity.GetOrAddComponent<Camera2DComponent>();
	REQUIRE(scene->SetMainCamera(entity.GetHandle()));
	entity.AddComponent<ScriptComponent>().AddScript(identity, ScriptType::Bolt);
	ScriptSystem scripts;
	scripts.Update(*scene);
	const auto fields = BoltScriptHost::GetFields(entity, identity);
	REQUIRE(fields.size() == 1);
	REQUIRE(fields.front().Value == "4");
	auto second = scene->CreateEntity();
	second.AddComponent<ScriptComponent>().AddScript(identity, ScriptType::Bolt);
	scripts.Update(*scene);
	ScriptEngine::OnPlayModeExited();
	scripts.Update(*scene);
	CHECK(BoltScriptHost::GetFields(entity, identity).front().Value == "5");
	controls.GetComponent<SliderComponent>().ValueChangedThisFrame = true;
	ScriptEngine::RaiseUiEventDispatch(*scene);
	ScriptEngine::RaiseWindowResize();
	scripts.Update(*scene);
	CHECK(BoltScriptHost::GetFields(entity, identity).front().Value == "115");
	scripts.OnDestroy(*scene);
}
#endif

#if FUNKE_WITH_2D && FUNKE_WITH_3D
TEST_CASE("Bolt borrows native rows and transforms points in a loaded scene" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
    struct TestApplication : Application {
        ApplicationConfig GetConfiguration() const override {
            auto config = ApplicationConfig::Headless();
            config.EnablePhysics2D = false;
            config.EnableAudio = false;
            config.EnableScripting = false;
            config.EnablePackageHost = false;
            return config;
        }
        void ConfigureScenes() override {
            SceneManager::RegisterScene("Bolt borrowed queries").WithoutDefaultCamera2D().SetAsStartupScene();
            SceneManager::RegisterScene("Bolt staged target").WithoutDefaultCamera2D();
        }
    } app;
    REQUIRE(app.BeginRun());
    const FinishHeadlessRun finish{app};
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/BorrowedSceneQuery.bolt";
    constexpr uint64_t guid = 0xab04722026112603ull;
    const auto identity = std::to_string(guid);
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(path), guid, AssetKind::Script);
    auto scene = SceneManager::GetActiveScene();
    REQUIRE(scene);
    auto entity = scene->CreateEntity();
    entity.GetOrAddComponent<Transform2DComponent>();
    entity.GetOrAddComponent<SpriteRendererComponent>();
    entity.AddComponent<ScriptComponent>().AddScript(identity, ScriptType::Bolt);
    ScriptSystem scripts;
    scripts.Update(*scene);
    const auto fields = BoltScriptHost::GetFields(entity, identity);
    REQUIRE(fields.size() == 1);
    REQUIRE(fields.front().Value == "4");
    CHECK(entity.GetComponent<Transform2DComponent>().Position.x == 8.0f);
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline && BoltScriptHost::GetFields(entity, identity).front().Value != "6") {
        REQUIRE(app.RunFrame());
        scripts.Update(*scene);
    }
    CHECK(BoltScriptHost::GetFields(entity, identity).front().Value == "6");
    CHECK(SceneManager::IsSceneLoaded("Bolt staged target"));
    scripts.OnDestroy(*scene);
}
#endif

#if FUNKE_WITH_RENDER
TEST_CASE("Bolt engine services preserve PNG pixels and 64-bit physics layers" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
	Application application;
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/ServiceApi.bolt";
	constexpr uint64_t guid = 0xab04722026112604ull;
	const auto identity = std::to_string(guid);
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(path), guid, AssetKind::Script);
	REQUIRE(DevConsole::Execute("service_api_sum 7").Output == "12");
	auto scene = Scene::CreateDetachedScene("Bolt services");
	auto entity = scene->CreateEntity();
	entity.AddComponent<ScriptComponent>().AddScript(identity, ScriptType::Bolt);
	ScriptSystem scripts;
	scripts.Update(*scene);
	const auto fields = BoltScriptHost::GetFields(entity, identity);
	REQUIRE(fields.size() == 1);
	CHECK(fields.front().Value == "6");
	const auto commands = DevConsole::ListCommands();
	CHECK(std::any_of(commands.begin(), commands.end(), [](const auto& item) {
		return item.Name == "service_api_echo" && item.Description == "Echo one argument.";
	}));
	CHECK(DevConsole::Execute("SERVICE_API_ECHO \"two words\"").Output == "two words");
	CHECK(DevConsole::Execute("service_api_sum nope").Result == DevCommandResult::Status::Error);
	if (!ScriptEngine::IsInitialized() && !RunAutomation::IsActive()) {
		RunAutomation::Options options;
		options.PrefsProfile = true;
		options.PrefsProfileOverride = "BoltServiceApiTests";
		RunAutomation::Configure(options);
		struct ResetAutomation { ~ResetAutomation() { RunAutomation::Reset(); } } restoreAutomation;
		REQUIRE(DevConsole::Execute("service_api_pref on").Result == DevCommandResult::Status::Ok);
		CHECK(ScriptEngine::GetSavedLanguage() == "service-api-language");
		CHECK(DevConsole::Execute("service_api_pref off").Result == DevCommandResult::Status::Ok);
	}
	scripts.OnDestroy(*scene);
	CHECK(DevConsole::Execute("service_api_echo").Result == DevCommandResult::Status::NotFound);
}
#endif

TEST_CASE("Bolt math and native collections preserve values and awaiter callbacks" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
    Application application;
    RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/MathCollectionsParity.bolt";
    const auto identity = std::to_string(0xab04722026112646ull);
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(path), 0xab04722026112646ull, AssetKind::Script);
    auto scene = Scene::CreateDetachedScene("Bolt math and native collections");
    auto entity = scene->CreateEntity();
    auto& transform = entity.GetOrAddComponent<Transform2DComponent>();
    entity.AddComponent<ScriptComponent>().AddScript(identity, ScriptType::Bolt);
    ScriptSystem scripts;
    scripts.Update(*scene);
    const auto fields = BoltScriptHost::GetFields(entity, identity);
    REQUIRE(fields.size() == 1);
    REQUIRE(fields.front().Value == "0");
    ScriptEngine::PumpCoroutinesUpdate(0.1f);
    scripts.Update(*scene);
    CHECK(transform.Position.x == 4.0f);
    ScriptEngine::PumpCoroutinesUpdate(0.1f);
    scripts.Update(*scene);
    CHECK(transform.Position.x == 5.0f);
    ScriptEngine::PumpCoroutinesFixedUpdate();
    scripts.Update(*scene);
    CHECK(transform.Position.x == 7.0f);
    scripts.OnDestroy(*scene);
}

TEST_CASE("Bolt registered static state resets on consecutive play sessions" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
    Application application;
    RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/ResetPlayProbe.bolt";
    constexpr uint64_t guid = 0xab04722026112645ull;
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(path), guid, AssetKind::Script);
    auto scene = Scene::CreateDetachedScene("Bolt play reset");
    auto entity = scene->CreateEntity();
    auto& transform = entity.GetOrAddComponent<Transform2DComponent>();
    entity.AddComponent<ScriptComponent>().AddScript(std::to_string(guid), ScriptType::Bolt);
    ScriptSystem scripts;
    REQUIRE(ScriptEngine::ResetRegisteredPlayState());
    scripts.Update(*scene);
    REQUIRE(transform.Position.x == 1.0f);
    REQUIRE(transform.Position.y == 1.0f);
    scripts.Update(*scene);
    REQUIRE(transform.Position.x == 2.0f);
    REQUIRE(transform.Position.y == 2.0f);
    REQUIRE(ScriptEngine::ResetRegisteredPlayState());
    scripts.Update(*scene);
    CHECK(transform.Position.x == 1.0f);
    CHECK(transform.Position.y == 1.0f);
    scripts.OnDestroy(*scene);
}

TEST_CASE("Bolt reload transfers scalar state after startup" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
    Application application;
    RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
    const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/ReloadStateProbe.bolt";
    constexpr uint64_t guid = 0xab04722026112644ull;
    const auto identity = std::to_string(guid);
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(fixture), guid, AssetKind::Script);
    auto scene = Scene::CreateDetachedScene("Bolt reload state");
    auto attach = [&] {
        auto entity = scene->CreateEntity();
        entity.GetOrAddComponent<Transform2DComponent>();
        entity.AddComponent<ScriptComponent>().AddScript(identity, ScriptType::Bolt);
        return entity;
    };
    ScriptSystem scripts;
    auto first = attach();
    scripts.Update(*scene);
    auto second = attach();
    scripts.Update(*scene);
    REQUIRE(first.GetComponent<Transform2DComponent>().Position.x == 12.0f);
    REQUIRE(second.GetComponent<Transform2DComponent>().Position.x == 11.0f);
    REQUIRE(BoltScriptHost::PollChanges(true));
    scripts.Update(*scene);
    CHECK(first.GetComponent<Transform2DComponent>().Position.x == 113.0f);
    CHECK(second.GetComponent<Transform2DComponent>().Position.x == 112.0f);
    CHECK(first.GetComponent<Transform2DComponent>().Position.y == 4.0f);
    CHECK(second.GetComponent<Transform2DComponent>().Position.y == 4.0f);
    scripts.OnDestroy(*scene);
}

TEST_CASE("Bolt EntityScript compile and one-line edit benchmark" * doctest::skip(std::getenv("FUNKE_BOLT_COMPILE_BENCHMARK") == nullptr)) {
	Application application;
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/EntityMove.bolt";
	std::printf("BOLTHOST %s\n", Path::ToUtf8(BoltScriptHost::FindCompilerLibrary()).c_str());
	const auto source = File::ReadAllText(Path::ToUtf8(fixture));
	const auto literal = source.find("1.0f");
	REQUIRE(literal != std::string::npos);
	struct TempAsset {
		std::filesystem::path Path;
		~TempAsset() { std::error_code ec; std::filesystem::remove(Path, ec); }
	};
	const TempAsset temp{std::filesystem::temp_directory_path() / ("FunkeBoltBench" + std::to_string(Clock::now().time_since_epoch().count()) + ".bolt")};
	const std::string path = Path::ToUtf8(temp.Path);
	REQUIRE(File::WriteAllText(path, source));
	std::string error;
	const auto start = Clock::now();
	const bool valid = BoltScriptHost::ValidateFiles({path}, error);
	const double cold = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	INFO(error);
	REQUIRE(valid);
	constexpr uint64_t guid = 0xAB04722026010207ull;
	AssetRegistry::RegisterBuiltInAsset(path, guid, AssetKind::Script);
	auto scene = Scene::CreateDetachedScene("Bolt compiler benchmark");
	auto entity = scene->CreateEntity();
	entity.GetOrAddComponent<Transform2DComponent>();
	entity.AddComponent<ScriptComponent>().AddScript(std::to_string(guid), ScriptType::Bolt);
	ScriptSystem scripts;
	scripts.Update(*scene);
	float expected = 1;
	std::array<double, 8> edits{}, reloads{};
	for (int i = 0; i < static_cast<int>(edits.size()); ++i) {
		const auto edited = EditedMoveScript(source, literal, i + 2);
		REQUIRE(File::WriteAllText(path, edited));
		const auto before = Clock::now();
		REQUIRE(BoltScriptHost::PollChanges(true));
		edits[i] = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
		scripts.Update(*scene);
		reloads[i] = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
		expected += static_cast<float>(i + 2);
		REQUIRE(entity.GetComponent<Transform2DComponent>().Position.x == expected);
	}
	std::sort(edits.begin(), edits.end());
	std::sort(reloads.begin(), reloads.end());
	std::printf("ENTITYBUILD cold_ms=%.4f edit_ms=%.4f reload_ms=%.4f\n", cold, (edits[3] + edits[4]) / 2, (reloads[3] + reloads[4]) / 2);
	scripts.OnDestroy(*scene);
}

TEST_CASE("CSharp EntityScript compile and live reload benchmark" * doctest::skip(std::getenv("FUNKE_CS_RELOAD_BENCHMARK") == nullptr)) {
	struct BenchmarkApplication : Application {
		ApplicationConfig GetConfiguration() const override {
			auto config = ApplicationConfig::Headless();
			config.EnablePhysics2D = config.EnableAudio = config.EnableScripting = config.EnablePackageHost = false;
			return config;
		}
		void ConfigureScenes() override {
			SceneManager::RegisterScene("CSharp reload benchmark").WithoutDefaultCamera2D().SetAsStartupScene();
		}
	} app;
	REQUIRE(app.BeginRun());
	const FinishHeadlessRun finish{app};
	const char* core = std::getenv("FUNKE_BENCH_CORE");
	REQUIRE(core != nullptr);
	ScriptEngine::Init();
	ScriptEngine::LoadCoreAssembly(core);
	REQUIRE(ManagedScriptCompiler::EnsureLoaded());

	const auto fixtures = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts";
	const auto directory = std::filesystem::temp_directory_path() / ("FunkeCsReload" + std::to_string(Clock::now().time_since_epoch().count()));
	REQUIRE(std::filesystem::create_directory(directory));
	struct Cleanup {
		std::filesystem::path Directory;
		~Cleanup() { std::error_code error; std::filesystem::remove_all(Directory, error); }
	} cleanup{directory};
	const auto project = directory / "BenchmarkScripts.csproj";
	const auto source = Path::ToUtf8(directory / "Move.cs");
	std::filesystem::copy_file(fixtures / "BenchmarkScripts.csproj", project);
	const auto original = File::ReadAllText(Path::ToUtf8(fixtures / "Move.cs"));
	const auto literal = original.find("1.0f");
	REQUIRE(literal != std::string::npos);
	REQUIRE(File::WriteAllText(source, original));
	const auto restored = Process::Run({"dotnet", "restore", Path::ToUtf8(project)}, directory);
	INFO(restored.Output);
	REQUIRE(restored.Succeeded());
	ManagedScriptCompiler::Request request{Path::ToUtf8(project), "Release", {std::string("-p:FunkeScriptCore=") + core},
		Path::ToUtf8(directory / "bin/Release/net10.0/BenchmarkScripts.dll"), {source}, false};
	auto compile = [&] {
		auto response = ManagedScriptCompiler::Compile(request);
		INFO(response.Message);
		INFO(response.Diagnostics);
		REQUIRE(response.Status == ManagedScriptCompiler::Status::Compiled);
		REQUIRE_FALSE(response.FullBuild);
	};
	const auto start = Clock::now();
	compile();
	const double cold = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	REQUIRE(ScriptEngine::LoadUserAssembly(request.OutputPath));
	auto scene = SceneManager::GetActiveScene();
	REQUIRE(scene);
	auto entity = scene->CreateEntity();
	entity.GetOrAddComponent<Transform2DComponent>();
	entity.AddComponent<ScriptComponent>().AddScript("BenchmarkMover", ScriptType::Managed);
	ScriptSystem scripts;
	scripts.Update(*scene);
	float expected = 1;
	REQUIRE(entity.GetComponent<Transform2DComponent>().Position.x == expected);
	std::array<double, 8> edits{}, reloads{};
	for (int i = 0; i < static_cast<int>(edits.size()); ++i) {
		const auto changed = EditedMoveScript(original, literal, i + 2);
		REQUIRE(File::WriteAllText(source, changed));
		const auto before = Clock::now();
		compile();
		edits[i] = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
		REQUIRE(ScriptSystem::ReloadManagedAssemblies());
		scripts.Update(*scene);
		reloads[i] = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
		expected += static_cast<float>(i + 2);
		REQUIRE(entity.GetComponent<Transform2DComponent>().Position.x == expected);
	}
	std::sort(edits.begin(), edits.end());
	std::sort(reloads.begin(), reloads.end());
	std::printf("ENTITYBUILD cold_ms=%.4f edit_ms=%.4f reload_ms=%.4f\n", cold, (edits[3] + edits[4]) / 2, (reloads[3] + reloads[4]) / 2);
	scripts.OnDestroy(*scene);
}

TEST_CASE("Bolt scene and global systems use native lifecycle instances" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
	Application application;
	auto dir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts";
	constexpr uint64_t sceneId = 0xAB04722026011123ull, globalId = sceneId + 1;
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir / "SceneCounter.bolt"), sceneId, AssetKind::Script);
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir / "GlobalCounter.bolt"), globalId, AssetKind::Script);
	constexpr uint64_t healthId = 0xAB04722026011137ull;
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir / "Health.bolt"), healthId, AssetKind::Script);
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	auto scene = Scene::CreateDetachedScene("Bolt systems");
	auto foreign = Scene::CreateDetachedScene("Unrelated scene");
	auto outside = foreign->CreateEntity();
	outside.GetOrAddComponent<Transform2DComponent>();
	auto target = scene->CreateEntity();
	target.GetOrAddComponent<Transform2DComponent>();
	target.AddComponent<ScriptComponent>().AddScript(std::to_string(healthId), ScriptType::Bolt);
	scene->CreateEntity().GetOrAddComponent<Transform2DComponent>();
	const auto type = std::to_string(sceneId);
	REQUIRE(ScriptEngine::SceneSystemClassExists(type));
	CHECK_FALSE(ScriptEngine::GlobalSystemClassExists(type));
	auto first = ScriptEngine::CreateSceneSystemInstance(type, scene->GetName(), scene.get());
	auto second = ScriptEngine::CreateSceneSystemInstance(type, scene->GetName(), scene.get());
	REQUIRE(first != 0);
	REQUIRE(second != 0);
	ScriptEngine::SetSceneSystemField(first, "Count", "10");
	ScriptEngine::InvokeSceneSystemAwake(first);
	ScriptEngine::InvokeSceneSystemEnable(first);
	ScriptEngine::InvokeSceneSystemStart(first);
	CHECK(target.GetComponent<Transform2DComponent>().Position.x == 12.0f);
	CHECK(target.GetComponent<Transform2DComponent>().Position.y == 1.0f);
	ScriptEngine::InvokeSceneSystemUpdate(first);
	ScriptEngine::InvokeSceneSystemFixedUpdate(first);
	CHECK(std::string(ScriptEngine::GetSceneSystemFields(first)).find("\"40\"") != std::string::npos);
	CHECK(std::string(ScriptEngine::GetSceneSystemFields(second)).find("\"1\"") != std::string::npos);
	ScriptEngine::InvokeSceneSystemAwake(second);
	ScriptEngine::PumpCoroutinesFixedUpdate();
	auto fieldValue = [](uint32_t handle, const char* name) {
		const auto fields = Json::Parse(ScriptEngine::GetSceneSystemFields(handle));
		for (const auto& field : fields.GetArray())
			if (field.FindMember("name")->AsStringOr() == name) return field.FindMember("value")->AsStringOr();
		return std::string{};
	};
	CHECK(fieldValue(second, "Registration") == "2");
	CHECK(fieldValue(first, "Pulses") == "1");
	CHECK(fieldValue(second, "Pulses") == "0");
	const auto global = std::to_string(globalId);
	REQUIRE(ScriptEngine::GlobalSystemClassExists(global));
	ScriptEngine::InitializeGlobalSystems({global, global});
	CHECK(ScriptEngine::GetEnabledGlobalSystemCount() == 1);
	ScriptEngine::UpdateGlobalSystems();
	ScriptEngine::InvokeSceneSystemFixedUpdate(first);
	CHECK(ScriptEngine::GetEnabledGlobalSystemCount() == 0);
	ScriptEngine::ShutdownGlobalSystems();
	for (auto handle : {first,second}) {
		ScriptEngine::InvokeSceneSystemDisable(handle);
		ScriptEngine::InvokeSceneSystemDestroy(handle);
		ScriptEngine::DestroySceneSystemInstance(handle);
	}
}

TEST_CASE("Bolt component reference reads and writes the selected entity across files" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
	Application application;
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	const auto dir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts";
	constexpr uint64_t health = 0xAB04722026011137ull, reader = health + 1;
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir / "Health.bolt"), health, AssetKind::Script);
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir / "ComponentReader.bolt"), reader, AssetKind::Script);
	std::string error;
	bool valid = BoltScriptHost::ValidateFiles({Path::ToUtf8(dir / "ComponentReader.bolt")}, error);
	INFO(error);
	REQUIRE(valid);
	auto scene = Scene::CreateDetachedScene("Bolt component references");
	auto source = scene->CreateEntity();
	auto target = scene->CreateEntity();
	source.GetOrAddComponent<Transform2DComponent>();
	auto& scripts = source.AddComponent<ScriptComponent>();
	scripts.AddScript(std::to_string(reader), ScriptType::Bolt);
	target.AddComponent<ScriptComponent>().AddScript(std::to_string(health), ScriptType::Bolt);
	const auto serialized = std::to_string(scene->GetEntityPersistentID(target.GetHandle())) + ":Health";
	scripts.PendingFieldValues[std::to_string(reader) + ".Target"] = serialized;
	ScriptSystem system;
	system.Update(*scene);
	CHECK(source.GetComponent<Transform2DComponent>().Position.x == 17.0f);
	system.Update(*scene);
	CHECK(target.GetComponent<ScriptComponent>().PendingFieldValues[std::to_string(health) + ".Points"] == "22");
	const auto fields = BoltScriptHost::GetFields(source, std::to_string(reader));
	REQUIRE(fields.size() == 1);
	CHECK(fields[0].Type == "component:Health");
	CHECK(fields[0].Value == serialized);
	system.OnDestroy(*scene);
}

TEST_CASE("Bolt native coroutine resumes on frame time and fixed pumps" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
	Application app;
	const auto dir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts";
	constexpr uint64_t guid = 0xAB04722026011140ull;
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir / "Coroutine.bolt"), guid, AssetKind::Script);
	std::string error;
	bool valid = BoltScriptHost::ValidateFiles({Path::ToUtf8(dir / "Coroutine.bolt")}, error);
	INFO(error);
	REQUIRE(valid);
	auto scene = Scene::CreateDetachedScene("Coroutines");
	auto entity = scene->CreateEntity();
	entity.AddComponent<ScriptComponent>().AddScript(std::to_string(guid), ScriptType::Bolt);
	ScriptSystem system;
	system.Update(*scene);
	auto count = [&] { return BoltScriptHost::GetFields(entity, std::to_string(guid)).at(0).Value; };
	CHECK(count() == "4");
	ScriptEngine::PumpCoroutinesUpdate(0.1f);
	CHECK(count() == "4");
	ScriptEngine::PumpCoroutinesUpdate(0.1f);
	CHECK(count() == "8");
	ScriptEngine::PumpCoroutinesUpdate(0.1f);
	ScriptEngine::PumpCoroutinesUpdate(0.1f);
	CHECK(count() == "12");
	ScriptEngine::PumpCoroutinesUpdate(1.0f);
	CHECK(count() == "12");
	ScriptEngine::PumpCoroutinesFixedUpdate();
	CHECK(count() == "17");
	ScriptEngine::PumpCoroutinesUpdate(0.25f);
	CHECK(count() == "17");
	ScriptEngine::PumpCoroutinesUpdate(0.25f);
	CHECK(count() == "25");
	ScriptEngine::PumpCoroutinesUpdate(0.1f);
	CHECK(count() == "32");
	ScriptEngine::PumpCoroutinesUpdate(0.1f);
	CHECK(count() == "35");
	auto second = scene->CreateEntity();
	second.AddComponent<ScriptComponent>().AddScript(std::to_string(guid), ScriptType::Bolt);
	system.Update(*scene);
	REQUIRE(ScriptSystem::RemoveScript(second, 0));
	ScriptEngine::PumpCoroutinesFixedUpdate();
	ScriptEngine::PumpCoroutinesUpdate(1.0f);
	CHECK(count() == "35");
	system.OnDestroy(*scene);
}

TEST_CASE("Bolt overlapping coroutine invocations keep owner and frame order" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
    Application app;
    const auto dir=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts";
    constexpr uint64_t guid=0xAB04722026011141ull;
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir/"OverlappingCoroutines.bolt"),guid,AssetKind::Script);
    const auto id=std::to_string(guid);
    auto scene=Scene::CreateDetachedScene("Overlapping");
    auto first=scene->CreateEntity(),second=scene->CreateEntity();
    for (auto entity:{first,second}) entity.AddComponent<ScriptComponent>().AddScript(id,ScriptType::Bolt);
    ScriptSystem system;
    system.Update(*scene);
    ScriptEngine::PumpCoroutinesUpdate(0.1f);
    CHECK(BoltScriptHost::GetFields(first,id).at(0).Value=="12");
    CHECK(BoltScriptHost::GetFields(second,id).at(0).Value=="12");
    REQUIRE(ScriptSystem::RemoveScript(first,0));
    ScriptEngine::PumpCoroutinesFixedUpdate();
    CHECK(BoltScriptHost::GetFields(second,id).at(0).Value=="12");
    ScriptEngine::PumpCoroutinesUpdate(0.1f);
    CHECK(BoltScriptHost::GetFields(second,id).at(0).Value=="12123");
    ScriptEngine::PumpCoroutinesUpdate(0.1f);
    CHECK(BoltScriptHost::GetFields(second,id).at(0).Value=="121233");
    system.OnDestroy(*scene);
}

TEST_CASE("Bolt advanced jobs share native ECS data and unwind suspended tasks" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
    struct TestApplication : Application {
        ApplicationConfig GetConfiguration() const override {
            auto config=ApplicationConfig::Headless();
            config.EnablePhysics2D=false;
            config.EnableAudio=false;
            config.EnableScripting=false;
            config.EnablePackageHost=false;
            return config;
        }
        void ConfigureScenes() override {
            SceneManager::RegisterScene("AdvancedBolt").WithoutDefaultCamera2D().SetAsStartupScene();
        }
    } app;
    REQUIRE(app.BeginRun());
    const FinishHeadlessRun finish{app};
    const auto dir=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts";
    constexpr uint64_t guid=0xab04722026011306ull;
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir/"MotionData.bolt"),guid+1,AssetKind::Script);
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir/"DistanceJob.bolt"),guid+2,AssetKind::Script);
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir/"AdvancedGameplay.bolt"),guid,AssetKind::Script);
    const std::string identity=std::to_string(guid);
    std::string error;
    const bool valid=BoltScriptHost::ValidateFiles({Path::ToUtf8(dir/"AdvancedGameplay.bolt")},error);
    INFO(error);
    REQUIRE(valid);
    auto scene=SceneManager::GetActiveScene();
    REQUIRE(scene);
    auto entity=scene->CreateEntity();
    entity.AddComponent<ScriptComponent>().AddScript(identity,ScriptType::Bolt);
    ScriptSystem system;
    system.Update(*scene);
    auto stage=[&]{return BoltScriptHost::GetFields(entity,identity).at(0).Value;};
    REQUIRE(stage()=="1");
    ScriptEngine::PumpCoroutinesUpdate(0.01f);
    CHECK(stage()=="44");
    system.Update(*scene);
    ScriptEngine::PumpCoroutinesUpdate(0.01f);
    CHECK(stage()=="144");
    ScriptEngine::PumpCoroutinesUpdate(0.01f);
    CHECK(stage()=="345");
    auto* component=SceneManager::GetComponentRegistry().FindBySerializedName("bolt:Gameplay.MotionData");
    REQUIRE(component);
    REQUIRE(component->rawSize==16);
    const auto count=[&](Entity target) {
        int value=0;
        const auto* data=static_cast<const char*>(component->getRaw(target));
        if(data) std::memcpy(&value,data+4,sizeof(value));
        return value;
    };
    CHECK(count(entity)==345);
    auto* pathSample=SceneManager::GetComponentRegistry().FindBySerializedName("bolt:Gameplay.PathSample");
    REQUIRE(pathSample);
    REQUIRE(pathSample->rawSize==20);
    auto bytes=static_cast<const char*>(pathSample->getRaw(entity));
    REQUIRE(bytes!=nullptr);
    float z; uint32_t sequence;
    std::memcpy(&z,bytes+12,sizeof(z));
    std::memcpy(&sequence,bytes+16,sizeof(sequence));
    CHECK(z==3.0f);
    CHECK(sequence==49u);
    CHECK(static_cast<unsigned char>(bytes[0])==11);
    double distance=0;
    std::memcpy(&distance,static_cast<const char*>(component->getRaw(entity))+8,sizeof(distance));
    CHECK(distance==12.75);
    auto second=scene->CreateEntity();
    second.AddComponent<ScriptComponent>().AddScript(identity,ScriptType::Bolt);
    system.Update(*scene);
    REQUIRE(ScriptSystem::RemoveScript(second,0));
    CHECK(count(second)==201);
    ScriptEngine::PumpCoroutinesUpdate(1.0f);
    CHECK(count(second)==201);
    system.OnDestroy(*scene);
}

TEST_CASE("Bolt data assets expose editable fields and validate shared values" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
    Application application;
    const auto script = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/DataAssetProbe.bolt";
    constexpr uint64_t scriptId = 0xAB04722026011174ull, guid = 0xAB04722026011175ull;
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(script), scriptId, AssetKind::Script);
    auto type = "bolt:" + std::to_string(scriptId);
    REQUIRE(ScriptEngine::DataAssetClassExists(type));
    REQUIRE(ScriptEngine::CreateDataAssetInstance(type, guid) == 1);
    ScriptEngine::SetDataAssetField(guid, "Count", "12");
    const std::string fields = ScriptEngine::GetDataAssetFields(guid);
    CHECK(fields.find("\"8\"") != fields.npos);
    CHECK(fields.find("Count") != fields.npos);
    ScriptEngine::DestroyDataAssetInstance(guid);
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(script.parent_path() / "Probe.dataasset"), guid, AssetKind::DataAsset);
    constexpr uint64_t reader = 0xAB04722026011176ull;
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(script.parent_path() / "DataAssetReader.bolt"), reader, AssetKind::Script);
    auto scene = Scene::CreateDetachedScene("Data asset reader");
    auto entity = scene->CreateEntity();
    auto& attachment = entity.AddComponent<ScriptComponent>();
    const auto identity = std::to_string(reader);
    attachment.AddScript(identity, ScriptType::Bolt);
    attachment.PendingFieldValues[identity + ".Config"] = std::to_string(guid);
    ScriptSystem scripts;
    scripts.Update(*scene);
    CHECK(std::string(DataAssetManager::GetFields(guid)).find("read in gameplay") != std::string::npos);
    scripts.OnDestroy(*scene);
    DataAssetManager::Unload(guid);
}

TEST_CASE("Bolt editor windows discover metadata and retain their instance between draws" * doctest::skip(std::getenv("FUNKE_TEST_BOLT_SDK") == nullptr)) {
    Application application;
    const auto script = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/EditorWindowProbe.bolt";
    constexpr uint64_t guid = 0xAB04722026011173ull;
    AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(script), guid, AssetKind::Script);
    const auto windows = ScriptEngine::GetEditorWindowTypes();
    CHECK(windows.find("Tests/Bolt probe") != windows.npos);
    CHECK(windows.find("360") != windows.npos);
    const auto name = "bolt:" + std::to_string(guid);
    CHECK(ScriptEngine::DrawEditorWindow(name.c_str()) == 1);
    CHECK(ScriptEngine::DrawEditorWindow(name.c_str()) == 1);
    ScriptEngine::CloseEditorWindow(name.c_str());
}

TEST_CASE("Bolt references live CSharp components on another entity" * doctest::skip(std::getenv("FUNKE_BOLT_MIXED_ASSEMBLY") == nullptr)) {
	Application application;
	RegisterBuiltInComponents(SceneManager::GetComponentRegistry());
	auto core = std::getenv("FUNKE_BENCH_CORE"), assembly = std::getenv("FUNKE_BOLT_MIXED_ASSEMBLY");
	REQUIRE(core != nullptr);
	const auto script = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts/ManagedReference.bolt";
	std::string error;
	const bool exported = BoltScriptHost::ValidateMixedFiles({Path::ToUtf8(script)}, core, assembly, error);
	INFO(error);
	REQUIRE(exported);
	ScriptEngine::Init();
	ScriptEngine::LoadCoreAssembly(core);
	REQUIRE(ScriptEngine::LoadUserAssembly(assembly));
	constexpr uint64_t guid = 0xAB04722026011172ull;
	AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(script), guid, AssetKind::Script);
	auto scene = Scene::CreateDetachedScene("Mixed component references");
	auto source = scene->CreateEntity(), target = scene->CreateEntity();
	source.GetOrAddComponent<Transform2DComponent>();
	target.GetOrAddComponent<Transform2DComponent>();
	auto& scripts = source.AddComponent<ScriptComponent>();
	scripts.AddScript(std::to_string(guid), ScriptType::Bolt);
	scripts.PendingFieldValues[std::to_string(guid) + ".Target"] = std::to_string(scene->GetEntityPersistentID(target.GetHandle())) + ":type:MixedGameplay.Vitals";
	auto& managed = target.AddComponent<ScriptComponent>();
	managed.AddManagedComponent("type:MixedGameplay.Vitals");
	managed.AddScript("type:MixedGameplay.VitalsReader", ScriptType::Managed);
	ScriptSystem system;
	system.Update(*scene);
	system.Update(*scene);
	CHECK(source.GetComponent<Transform2DComponent>().Position.x == 66.0f);
	CHECK(target.GetComponent<Transform2DComponent>().Position.x == 66.0f);
	ScriptEngine::PumpCoroutinesUpdate(0.01f);
	CHECK(source.GetComponent<Transform2DComponent>().Position.y == 19.0f);
    auto* meter=SceneManager::GetComponentRegistry().FindBySerializedName("type:MixedGameplay.ManagedMeter");
    REQUIRE(meter);
    REQUIRE(meter->getRaw(source));
    int canceledValue=0;
    std::memcpy(&canceledValue,static_cast<const char*>(meter->getRaw(source))+4,sizeof(canceledValue));
    CHECK(canceledValue==81);
	auto fields = BoltScriptHost::GetFields(source, std::to_string(guid));
	REQUIRE(fields.size() == 1);
	CHECK(fields[0].Type == "component:type:MixedGameplay.Vitals");
	system.OnDestroy(*scene);
	ScriptEngine::Shutdown();
}

TEST_CASE("Bolt LLVM versus CSharp complex scene frame benchmark" * doctest::skip(std::getenv("FUNKE_COMPLEX_SCENE") == nullptr)) {
    const std::string workload = std::getenv("FUNKE_COMPLEX_SCENE");
    const char* language = std::getenv("FUNKE_BOLT_CS_MODE");
    REQUIRE(language);
    const bool managed = std::string_view(language) == "cs";
    REQUIRE((managed || std::string_view(language) == "llvm"));
    const bool mixed = workload == "mixed";
    const bool computeJob = workload == "jobs_compute";
    const bool jobs = mixed || workload == "jobs" || computeJob;
    const bool components = mixed || workload == "components";
    const bool coroutines = mixed || workload == "coroutines";
    REQUIRE((jobs || components || coroutines));

    struct BenchmarkApp : Application {
        ApplicationConfig GetConfiguration() const override {
            auto config = ApplicationConfig::Headless();
            config.EnablePhysics2D = config.EnableAudio = config.EnableScripting = config.EnablePackageHost = false;
            return config;
        }
        void ConfigureScenes() override {
            SceneManager::RegisterScene("ComplexSceneBenchmark").WithoutDefaultCamera2D().SetAsStartupScene();
        }
    } app;
    REQUIRE(app.BeginRun());
    const FinishHeadlessRun finish{app};
    REQUIRE(JobSystem::Reconfigure(4) == 4);
    const auto scene = SceneManager::GetActiveScene();
    REQUIRE(scene);
    constexpr uint64_t asset = 0xAB04722026012000ull;
    if (managed) {
        REQUIRE(std::getenv("FUNKE_BENCH_CORE"));
        REQUIRE(std::getenv("FUNKE_BENCH_ASSEMBLY"));
        ScriptEngine::Init();
        ScriptEngine::LoadCoreAssembly(std::getenv("FUNKE_BENCH_CORE"));
        REQUIRE(ScriptEngine::LoadUserAssembly(std::getenv("FUNKE_BENCH_ASSEMBLY")));
    } else {
        REQUIRE(std::getenv("FUNKE_BOLT_PROFILE"));
        REQUIRE(std::string_view(std::getenv("FUNKE_BOLT_PROFILE")) == "release");
        std::printf("BOLTHOST %s\n", Path::ToUtf8(BoltScriptHost::FindCompilerLibrary()).c_str());
        const auto dir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Fixtures/BoltScripts";
        auto add = [&](const char* file, uint64_t guid) {
            REQUIRE(std::filesystem::is_regular_file(dir / file));
            AssetRegistry::RegisterBuiltInAsset(Path::ToUtf8(dir / file), guid, AssetKind::Script);
        };
        if (components || jobs) add("MotionData.bolt", asset + 3);
        if (jobs) {
            add(computeJob ? "ComplexScenes/ComputeDistanceJob.bolt" : "DistanceJob.bolt", asset + 4);
            add(computeJob ? "ComplexScenes/ComputeQueryDriver.bolt" : "ComplexScenes/QueryDriver.bolt", asset);
        }
        if (components) add("ComplexScenes/ComponentTicks.bolt", asset + 1);
        if (coroutines) add("ComplexScenes/FrameCoroutine.bolt", asset + 2);
    }

    auto attach = [&](Entity entity, const char* name, uint64_t guid) {
        entity.GetOrAddComponent<ScriptComponent>().AddScript(
            managed ? std::string("type:Gameplay.") + name : std::to_string(guid),
            managed ? ScriptType::Managed : ScriptType::Bolt);
    };
    constexpr int warmup = 64, samples = 121;
    int count = 1000;
    if (const char* requested = std::getenv("FUNKE_BENCH_ENTITIES")) {
        const char* end = requested + std::strlen(requested);
        const auto parsed = std::from_chars(requested, end, count);
        REQUIRE((parsed.ec == std::errc{} && parsed.ptr == end && count > 0 && count <= 65521));
    }
    if (jobs) attach(scene->CreateEntity(), computeJob ? "ComputeQueryDriver" : "QueryDriver", asset);
    std::vector<Entity> rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto entity = scene->CreateEntity();
        rows.push_back(entity);
        if (components) attach(entity, "ComponentTicks", asset + 1);
        if (coroutines) {
            entity.GetOrAddComponent<Transform2DComponent>();
            attach(entity, "FrameCoroutine", asset + 2);
        }
    }
    ScriptSystem scripts;
    auto frame = [&]() {
        scripts.Update(*scene);
        ScriptEngine::PumpCoroutinesUpdate(1.0f / 60.0f);
    };
    frame();
    frame();
    scripts.FixedUpdate(*scene);
    const ComponentInfo* motion = nullptr;
    if (components || jobs) {
        motion = SceneManager::GetComponentRegistry().FindBySerializedName(
            managed ? "type:Gameplay.MotionData" : "bolt:Gameplay.MotionData");
        REQUIRE(motion);
        REQUIRE(motion->rawSize == 16);
    }
    struct Payload { uint8_t Mode; int32_t Count; double Distance; };
    static_assert(sizeof(Payload) == 16 && offsetof(Payload, Count) == 4 && offsetof(Payload, Distance) == 8);
    std::vector<float> initialResumes;
    for (int i = 0; i < count; ++i) {
        auto entity = rows[i];
        if (motion) {
            if (!components) motion->add(entity);
            void* raw = motion->getRaw(entity);
            REQUIRE(raw);
            const Payload seed{static_cast<uint8_t>(1 + i % 7), i, static_cast<double>(i)};
            std::memcpy(raw, &seed, sizeof(seed));
        }
        if (coroutines) {
            float resumes = entity.GetComponent<Transform2DComponent>().Position.x;
            REQUIRE(resumes > 0);
            initialResumes.push_back(resumes);
        }
    }
    for (int i = 0; i < warmup; ++i) frame();
    std::array<double, samples> frames{}, updates{}, pumps{};
    for (int i = 0; i < samples; ++i) {
        const auto start = Clock::now();
        scripts.Update(*scene);
        const auto afterUpdate = Clock::now();
        ScriptEngine::PumpCoroutinesUpdate(1.0f / 60.0f);
        const auto end = Clock::now();
        frames[i] = std::chrono::duration<double, std::milli>(end - start).count();
        updates[i] = std::chrono::duration<double, std::milli>(afterUpdate - start).count();
        pumps[i] = std::chrono::duration<double, std::milli>(end - afterUpdate).count();
    }
    scripts.FixedUpdate(*scene);
    constexpr int ticks = warmup + samples;
    for (int i = 0; i < count; ++i) {
        auto entity = rows[i];
        if (motion) {
            const void* raw = motion->getRaw(entity);
            REQUIRE(raw);
            Payload data{};
            std::memcpy(&data, raw, sizeof(data));
            REQUIRE(data.Mode == 1 + i % 7);
            uint64_t expectedCount = i;
            if (computeJob) {
                for (int step = 0; step < ticks * 256; ++step)
                    expectedCount = (expectedCount * 25173 + 13849) % 65521;
            } else if (components) {
                expectedCount += ticks * (1 + i % 7);
            }
            REQUIRE(data.Count == static_cast<int32_t>(expectedCount));
            REQUIRE(data.Distance == i + (jobs ? ticks * 0.5 : 0.0));
        }
        if (coroutines) REQUIRE(entity.GetComponent<Transform2DComponent>().Position.x == initialResumes[i] + ticks);
    }
    scripts.OnDestroy(*scene);
    std::printf("FRAME_SAMPLES");
    for (double ms : frames) std::printf(" %.6f", ms);
    std::printf("\n");
    std::sort(frames.begin(), frames.end());
    std::sort(updates.begin(), updates.end());
    std::sort(pumps.begin(), pumps.end());
    std::printf("SCENEBENCH %s %s entities=%d workers=%d warmup=%d samples=%d frame_ms=%.6f p95_ms=%.6f update_ms=%.6f pump_ms=%.6f\n",
        language, workload.c_str(), count, JobSystem::GetWorkerCount(), warmup, samples, frames[samples / 2],
        frames[(samples * 95 + 99) / 100 - 1], updates[samples / 2], pumps[samples / 2]);
}
