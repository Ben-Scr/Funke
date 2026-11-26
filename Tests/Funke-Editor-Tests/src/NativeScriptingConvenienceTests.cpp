#include <Funke/Scripting.hpp>

#include <doctest/doctest.h>
#include "Core/Application.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/Path.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace Game {
    struct Input {};
    static_assert(!std::is_same_v<Input, Funke::Input>);
}

static_assert(sizeof(Funke::ButtonComponent) > 0);
static_assert(sizeof(Funke::AudioSourceComponent) > 0);
static_assert(sizeof(Funke::Rigidbody2DComponent) > 0);
static_assert(sizeof(Funke::Rigidbody3DComponent) > 0);

TEST_CASE("Native scripting convenience template preserves lifecycle and registration") {
    const auto source = Funke::EditorScriptDiscovery::BuildNativeScriptSource("Player");
    CHECK(source.starts_with("#include <Funke/Scripting.hpp>\n\nusing namespace Funke;"));
    CHECK(source.find("public NativeScript") != std::string::npos);
    for (const auto* method : {"Start", "Update", "FixedUpdate", "OnDestroy"})
        CHECK(source.find(std::string("void ") + method + "() override") != std::string::npos);
    CHECK(source.find("REGISTER_SCRIPT(Player)") != std::string::npos);
}

// The integration driver supplies an isolated directory and the built SDK library.
TEST_CASE("Native scripting convenience emits real project fixtures" * doctest::skip(std::getenv("FUNKE_NATIVE_TEST_PROJECT") == nullptr)) {
    using namespace Funke;
    const auto root = Path::FromUtf8(std::getenv("FUNKE_NATIVE_TEST_PROJECT"));
    FunkeProject project;
    project.Name = "Convenience";
    project.RootDirectory = Path::ToUtf8(root);
    project.AssetsDirectory = Path::ToUtf8(root / "Assets");
    project.NativeScriptsDir = Path::ToUtf8(root / "NativeScripts");
    project.NativeSourceDir = Path::ToUtf8(root / "NativeScripts/Source");
    std::filesystem::create_directories(root / "Assets");
    project.EnsureNativeScriptProjectFiles();
    std::ofstream(root / "Assets/Generated.cpp") << EditorScriptDiscovery::BuildNativeScriptSource("Generated");
    std::ofstream(root / "Assets/Gameplay.cpp") << R"cpp(#include <Funke/Scripting.hpp>
#include <memory>
#include <stdexcept>

using namespace Funke;

namespace Convenience {
    struct Value { std::unique_ptr<int> Number; };
    struct Tag {};
    class Health : public NativeScript {
    public:
        int Starts = 0;
        void Start() override { ++Starts; }
    };
}
REGISTER_SCRIPT(Convenience::Health)

class Gameplay : public NativeScript {
public:
    int Updates = 0;
    int Checks = 0;
    void Check(bool condition) {
        if (!condition) throw std::runtime_error("Native convenience check " + std::to_string(Checks));
        ++Checks;
    }
    void Start() override {
        Print("Convenience script started");
        Check(GetEntity().GetScene() == GetScene());
        Check(GetEntity().GetHandle() == GetEntityHandle());
        Check(TryGetComponent<Convenience::Value>() == nullptr);
        auto& value = GetOrAddComponent<Convenience::Value>(std::make_unique<int>(17));
        auto unused = std::make_unique<int>(99);
        Check(&GetOrAddComponent<Convenience::Value>(std::move(unused)) == &value);
        Check(unused != nullptr && *value.Number == 17);
        Check(TryGetComponent<Convenience::Value>() == &value);
        Convenience::Value* out = nullptr;
        Check(TryGetComponent(out) && out == &value);
        const Gameplay& readOnly = *this;
        Check(readOnly.TryGetComponent<Convenience::Value>() == &value);
        const Convenience::Value* constOut = nullptr;
        Check(readOnly.TryGetComponent(constOut) && constOut == &value);
        GetOrAddComponent<Convenience::Tag>();
        GetOrAddComponent<Convenience::Tag>();
        Check(HasComponent<Convenience::Tag>());
        RemoveComponent<Convenience::Tag>();
        RemoveComponent<Convenience::Value>();
        Check(!TryGetComponent(out) && out == nullptr);
        Check(!readOnly.TryGetComponent(constOut) && constOut == nullptr);

        auto* health = AddComponent<Convenience::Health>();
        Check(health != nullptr && health->Starts == 1);
        Check(HasComponent<Convenience::Health>());
        Check(GetComponent<Convenience::Health>() == health);
        Check(GetOrAddComponent<Convenience::Health>() == health && health->Starts == 1);
        Check(TryGetComponent<Convenience::Health>() == health);
        Check(readOnly.GetComponent<Convenience::Health>() == health);
        Check(readOnly.TryGetComponent<Convenience::Health>() == health);
        Convenience::Health* scriptOut = nullptr;
        const Convenience::Health* constScriptOut = nullptr;
        Check(TryGetComponent(scriptOut) && scriptOut == health);
        Check(readOnly.TryGetComponent(constScriptOut) && constScriptOut == health);
        Check(GetEntity().GetComponent<Convenience::Health>() == health);
        Check(GetEntity().TryGetComponent<Convenience::Health>() == health);
        Check(std::as_const(GetScene()->GetRegistry()).storage<Convenience::Health>() == nullptr);
        RemoveComponent<Convenience::Health>();
        Check(!HasComponent<Convenience::Health>());
        Check(!TryGetComponent(scriptOut) && scriptOut == nullptr);
        Check(!readOnly.TryGetComponent(constScriptOut) && constScriptOut == nullptr);
        health = GetOrAddComponent<Convenience::Health>();
        Check(health != nullptr && health->Starts == 1);
        Check(GetEntity().GetOrAddComponent<Convenience::Health>() == health);
        GetEntity().RemoveComponent<Convenience::Health>();
        health = GetEntity().GetOrAddComponent<Convenience::Health>();
        Check(health != nullptr && health->Starts == 1);
        Check(std::as_const(readOnly).GetEntity().GetHandle() == GetEntityHandle());
        RemoveComponent<Convenience::Health>();
    }
    void Update() override {
        ++Updates;
        if (Input::GetKeyDown(KeyCode::Space)) Print("Jump");
        auto* activeScene = SceneManager::GetActiveScene();
        (void)activeScene;
        if (GetScene() && HasComponent<Transform2DComponent>())
            GetComponent<Transform2DComponent>().SetPosition({float(Updates), 2.0f});
    }
    FNK_NATIVE_FIELDS(NativeFields::Make<&Gameplay::Updates>("Updates"), NativeFields::Make<&Gameplay::Checks>("Checks"))
};
REGISTER_SCRIPT(Gameplay)
)cpp";
    std::ofstream(root / "Assets/Legacy.cpp") << R"cpp(#include <Scripting/NativeScript.hpp>
#include <Core/Input.hpp>
namespace Game {
    struct Input {};
    class Legacy : public Funke::NativeScript {
        void Update() override { (void)Funke::Input::GetKeyDown(Funke::KeyCode::Space); }
    };
}
REGISTER_SCRIPT(Game::Legacy)
)cpp";
    const auto module = root / "NativeScripts/FunkeNativeScripts.cmake";
    const auto moduleTime = std::filesystem::last_write_time(module);
    const auto sourceTime = std::filesystem::last_write_time(root / "Assets/Generated.cpp");
    std::ofstream(root / "NativeScripts/CMakeLists.txt", std::ios::app | std::ios::binary)
        << "\ntarget_compile_definitions(${PROJECT_NAME} PRIVATE USER_SETTING=1)\n";
    const auto customTime = std::filesystem::last_write_time(root / "NativeScripts/CMakeLists.txt");
    project.EnsureNativeScriptProjectFiles();
    CHECK(std::filesystem::last_write_time(module) == moduleTime);
    CHECK(std::filesystem::last_write_time(root / "NativeScripts/CMakeLists.txt") == customTime);
    CHECK(std::filesystem::last_write_time(root / "Assets/Generated.cpp") == sourceTime);
}

TEST_CASE("Native scripting convenience loads multiple scripts and reloads lifecycle" * doctest::skip(std::getenv("FUNKE_NATIVE_TEST_DLL") == nullptr)) {
    using namespace Funke;
    Application application;
    auto& host = ScriptSystem::GetNativeHost();
    REQUIRE(host.LoadDLL(std::getenv("FUNKE_NATIVE_TEST_DLL")));
    struct Unload { NativeScriptHost& Host; ~Unload() { Host.UnloadDLL(); } } unload{host};
    for (const auto* name : {"Generated", "Gameplay", "Game::Legacy"}) CHECK(host.HasClass(name));
    CHECK_FALSE(host.HasClass("Unregistered"));
    for (int reload = 0; reload < 2; ++reload) {
        auto scene = Scene::CreateDetachedScene("Convenience");
        auto entity = scene->CreateEntity("Player");
        if (!entity.HasComponent<Transform2DComponent>()) entity.AddComponent<Transform2DComponent>();
        auto* script = host.CreateInstance("Gameplay", entity.GetHandle(), scene.get());
        REQUIRE(script);
        script->Start();
        std::string checks;
        REQUIRE(host.GetFieldValue(script, "Checks", checks));
        CHECK(checks == "31");
        CHECK(host.SetFieldValue(script, "Updates", "40"));
        script->Update();
        std::string value;
        CHECK(host.GetFieldValue(script, "Updates", value));
        CHECK(value == "41");
        CHECK(entity.GetComponent<Transform2DComponent>().LocalPosition.x == 41.0f);
        host.DestroyInstance(script);
        // Custom component storage owns vtables from the script DLL, even after removing its last component.
        scene.reset();
        REQUIRE(host.Reload());
    }
}

TEST_CASE("Native Print and logging macros report the user call site") {
    static std::string file, message;
    static int line;
    Funke::NativeEngineAPI api{};
    api.LogInfo = [](const char* text, const char* path, int number) { message = text; file = path; line = number; };
    auto* previous = Funke::g_EngineAPI;
    Funke::g_EngineAPI = &api;
    Funke::NativeScript script;
    const int first = __LINE__ + 1;
    script.Print("value ", 42);
    CHECK(line == first);
    CHECK(file == __FILE__);
    CHECK(message == "value 42");
    const int second = __LINE__ + 1;
    FNK_NATIVE_LOG_INFO("same message");
    CHECK(line == second);
    script.Print();
    CHECK(message.empty());
    Funke::g_EngineAPI = previous;
}
