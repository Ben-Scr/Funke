#include <Funke/Scripting.hpp>

#include <doctest/doctest.h>
#include "Scripting/ScriptDiscovery.hpp"
#include "Scripting/NativeScriptHost.hpp"
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

using namespace Funke;

class Gameplay : public NativeScript {
public:
    int Updates = 0;
    void Start() override { Print("Convenience script started"); }
    void Update() override {
        ++Updates;
        if (Input::GetKeyDown(KeyCode::Space)) Print("Jump");
        auto* activeScene = SceneManager::GetActiveScene();
        (void)activeScene;
        if (GetScene() && HasComponent<Transform2DComponent>())
            GetComponent<Transform2DComponent>().SetPosition({float(Updates), 2.0f});
    }
    FNK_NATIVE_FIELDS(NativeFields::Make<&Gameplay::Updates>("Updates"))
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
    auto scene = Scene::CreateDetachedScene("Convenience");
    auto entity = scene->CreateEntity("Player");
    if (!entity.HasComponent<Transform2DComponent>()) entity.AddComponent<Transform2DComponent>();
    NativeScriptHost host;
    REQUIRE(host.LoadDLL(std::getenv("FUNKE_NATIVE_TEST_DLL")));
    struct Unload { NativeScriptHost& Host; ~Unload() { Host.UnloadDLL(); } } unload{host};
    for (const auto* name : {"Generated", "Gameplay", "Game::Legacy"}) CHECK(host.HasClass(name));
    CHECK_FALSE(host.HasClass("Unregistered"));
    for (int reload = 0; reload < 2; ++reload) {
        auto* script = host.CreateInstance("Gameplay", entity.GetHandle(), scene.get());
        REQUIRE(script);
        script->Start();
        CHECK(host.SetFieldValue(script, "Updates", "40"));
        script->Update();
        std::string value;
        CHECK(host.GetFieldValue(script, "Updates", value));
        CHECK(value == "41");
        CHECK(entity.GetComponent<Transform2DComponent>().LocalPosition.x == 41.0f);
        host.DestroyInstance(script);
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
