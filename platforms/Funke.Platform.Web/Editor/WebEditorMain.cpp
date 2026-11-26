// Browser host for the original editor panels and engine. No HTML reimplementation of the editor.
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu_cpp.h>
#include <Funke.hpp>
#include "Graphics/Backend/WebGPUBackend.hpp"
#include "Gui/ImGuiContextLayer.hpp"
#include "Systems/ImGuiEditorLayer.hpp"
#include "Systems/GizmosDebugSystem.hpp"
#include "Editor/EditorComponentRegistration.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/Path.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/FunkeProject.hpp"
#include "Core/Window.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/File.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/ScriptComponent.hpp"
#if FUNKE_WEB_EDITOR_MANAGED
#include "MonoScriptRuntime.hpp"
#endif
#include <filesystem>
#include <cstdio>

namespace {
    Funke::ImGuiEditorLayer* g_Editor = nullptr;
    bool g_Player = false;
    class BrowserEditorApplication final : public Funke::Application {
    public:
        Funke::ApplicationConfig GetConfiguration() const override {
            Funke::ApplicationConfig config;
            config.WindowSpecification = Funke::WindowSpecification(1440, 900, "Funke Web Editor · Experimental", true, true, false);
            config.AutoCreateDefaultCamera2D = false;
            config.EnableAudio = true;
            config.EnableScripting = true;
            config.EnablePackageHost = false;
            config.EnableGizmoRenderer = !g_Player;
            config.EnableGuiRenderer = true;
            config.EnablePhysics2D = true;
            config.EnableFullscreenToggle = false;
            config.RunInBackground = false;
            return config;
        }
        void ConfigureScenes() override {
            Funke::Application::SetIsPlaying(g_Player);
            if (g_Player) {
                const auto* project = Funke::ProjectManager::GetCurrentProject();
                for (const auto& entry : std::filesystem::recursive_directory_iterator(project->ScenesDirectory)) {
                    if (entry.path().extension() != ".scene") continue;
                    const auto path = entry.path().string();
                    auto& definition = Funke::SceneManager::RegisterScene(entry.path().stem().string()).OnLoadChecked([path](Funke::Scene& scene) {
                        return Funke::SceneSerializer::LoadFromFile(scene, path);
                    });
                    if (entry.path().stem().string() == project->StartupScene) definition.SetAsStartupScene();
                }
                return;
            }
            Funke::SceneManager::RegisterScene("$EditorWorkingScene").OnLoadChecked([](Funke::Scene& scene) {
                const auto* project = Funke::ProjectManager::GetCurrentProject();
                const auto path = project->GetSceneFilePath(project->LastOpenedScene);
                if (std::filesystem::exists(path) && Funke::SceneSerializer::LoadFromFile(scene, path)) return true;
                scene.SetName("BrowserScene");
                auto camera = Funke::EntityHelper::CreateCamera2DEntity(scene);
                camera.GetComponent<Funke::Camera2DComponent>().SetOrthographicSize(6.0f);
                camera.GetComponent<Funke::Camera2DComponent>().SetClearColor({0.11f, 0.14f, 0.19f, 1.0f});
                auto block = [&](const char* name, Funke::Vec2 position, Funke::Vec2 scale, Funke::Color color, Funke::BodyType type) {
                    auto entity = Funke::EntityHelper::CreateSpriteEntity(scene);
                    entity.AddComponent<Funke::NameComponent>(name);
                    auto& transform = entity.GetComponent<Funke::Transform2DComponent>();
                    transform.SetPosition(position);
                    transform.SetScale(scale);
                    entity.GetComponent<Funke::SpriteRendererComponent>().Color = color;
                    entity.AddComponent<Funke::Rigidbody2DComponent>().SetBodyType(type);
                    entity.AddComponent<Funke::BoxCollider2DComponent>();
                };
                block("Ground", {0.0f, -3.0f}, {12.0f, 0.6f}, {0.36f, 0.44f, 0.56f, 1.0f}, Funke::BodyType::Static);
                block("Falling Box", {-1.0f, 2.0f}, {1.0f, 1.0f}, {0.31f, 0.53f, 0.93f, 1.0f}, Funke::BodyType::Dynamic);
                block("Platform", {3.0f, -1.0f}, {3.0f, 0.4f}, {0.39f, 0.72f, 0.63f, 1.0f}, Funke::BodyType::Static);
                return true;
            }).SetAsStartupScene();
        }
        void ConfigureLayers() override {
            if (g_Player) return;
            PushLayer<Funke::ImGuiContextLayer>();
            g_Editor = &PushLayer<Funke::ImGuiEditorLayer>();
            PushOverlay<Funke::GizmosDebugSystem>();
        }
        void Start() override {
            if (!g_Player) Funke::RegisterEditorComponentInspectors(Funke::SceneManager::GetComponentRegistry());
            Funke::EditorPreferences::SetAutoRecompileScripts(false);
            Funke::EditorPreferences::SetRecompileScriptsOnPlay(false);
            Funke::ScriptSystem::SetAutoRecompileEnabled(false);
        }
    };
}
Funke::Application* Funke::CreateApplication() {
    Application::SetEditorHost(!g_Player);
    auto project = std::make_unique<FunkeProject>();
    project->Name = "Web Editor Test";
    project->RootDirectory = g_Player ? "/game/project" : "/libsdl/FunkeWebEditor";
    project->AssetsDirectory = project->RootDirectory + "/Assets";
    project->ScenesDirectory = project->AssetsDirectory + "/Scenes";
    project->ScriptsDirectory = project->AssetsDirectory + "/Scripts";
    project->PackagesDirectory = project->RootDirectory + "/Packages";
    project->FunkeAssetsDirectory = "/editor/FunkeAssets";
    project->ProjectFilePath = project->RootDirectory + "/project.json";
    project->LastOpenedScene = project->StartupScene = "BrowserScene";
    project->EditorView3D = false;
    project->Modules = {"Funke.2D", "Funke.Physics2D"};
    FunkeProject stored;
    if (std::filesystem::exists(project->ProjectFilePath)
        && FunkeProject::TryLoad(project->RootDirectory, stored, nullptr, false)) {
        *project = std::move(stored);
        project->FunkeAssetsDirectory = "/editor/FunkeAssets";
        project->Modules = {"Funke.2D", "Funke.Physics2D"};
    }
    project->RootDirectory = g_Player ? "/game/project" : "/libsdl/FunkeWebEditor";
    project->AssetsDirectory = project->RootDirectory + "/Assets";
    project->ScenesDirectory = project->AssetsDirectory + "/Scenes";
    project->ScriptsDirectory = project->AssetsDirectory + "/Scripts";
    project->PackagesDirectory = project->RootDirectory + "/Packages";
    project->ProjectFilePath = project->RootDirectory + "/project.json";
    project->CsprojPath.clear();
    std::filesystem::create_directories(project->ScenesDirectory);
    std::filesystem::create_directories(project->ScriptsDirectory);
    ProjectManager::SetCurrentProject(std::move(project));
    return new BrowserEditorApplication();
}

#include <EntryPoint.hpp>

// Read-only diagnostics for browser smoke tests; values come from the real scene and engine.
extern "C" EMSCRIPTEN_KEEPALIVE const char* FunkeWebEditorState() {
    using Funke::Json::Value;
    static std::string result;
    auto state = Value::MakeObject();
    state.AddMember("playing", Value(Funke::Application::GetIsPlaying()));
    state.AddMember("player", Value(g_Player));
    state.AddMember("scripting", Value(Funke::ScriptEngine::IsInitialized()));
    state.AddMember("busy", Value(g_Player ? "" : g_Editor ? g_Editor->GetBridgeBusyReason() : "starting"));
    if (auto* project = Funke::ProjectManager::GetCurrentProject()) {
        state.AddMember("assemblyPath", Value(project->GetUserAssemblyOutputPath()));
        state.AddMember("projectRoot", Value(project->RootDirectory));
    }
    auto entities = Value::MakeArray();
    if (auto* scene = Funke::SceneManager::GetActiveScene()) {
        state.AddMember("scene", Value(scene->GetName()));
        state.AddMember("scenePath", Value(Funke::ProjectManager::GetCurrentProject()->GetSceneFilePath(scene->GetName())));
        state.AddMember("dirty", Value(scene->IsDirty()));
        for (auto [handle, name, transform] : scene->GetRegistry().view<Funke::NameComponent, Funke::Transform2DComponent>().each()) {
            auto entry = Value::MakeObject();
            entry.AddMember("id", Value(std::to_string(static_cast<uint32_t>(handle))));
            entry.AddMember("name", Value(name.Name));
            entry.AddMember("x", Value(double(transform.Position.x)));
            entry.AddMember("y", Value(double(transform.Position.y)));
            entities.Append(std::move(entry));
        }
    }
    state.AddMember("entities", std::move(entities));
    result = Funke::Json::Stringify(state);
    return result.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* FunkeWebEditorReloadScripts() {
    static std::string error;
    error.clear();
    if (Funke::Application::GetIsPlaying()) error = "Bitte zuerst den Play-Modus beenden.";
    else if (!Funke::ScriptEngine::IsInitialized()) error = "Die C#-Laufzeit ist nicht bereit.";
    else {
        const auto path = Funke::ProjectManager::GetCurrentProject()->GetUserAssemblyOutputPath();
        if (!Funke::ScriptEngine::HasUserAssembly() && !Funke::ScriptEngine::LoadUserAssembly(path)) error = "Die Skripte konnten nicht geladen werden.";
        else if (!Funke::ScriptSystem::ReloadManagedAssemblies()) error = "Die Skripte konnten nicht geladen werden. Details stehen im Log.";
    }
    return error.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* FunkeWebEditorPlay() {
    static std::string error;
    error.clear();
    if (g_Editor) g_Editor->BridgeRequestPlay(error);
    return error.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE void FunkeWebEditorStop() {
    if (g_Editor) g_Editor->BridgeRequestStop();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* FunkeWebEditorAttachScript(uint32_t id, const char* name) {
    static std::string error;
    error.clear();
    auto* scene = Funke::SceneManager::GetActiveScene();
    const auto handle = static_cast<Funke::EntityHandle>(id);
    if (!scene || !scene->GetRegistry().valid(handle) || Funke::Application::GetIsPlaying()) error = "Bitte ein Objekt im Bearbeitungsmodus wählen.";
    else if (!name || !Funke::ScriptEngine::ClassExists(name)) error = "Diese EntityScript-Klasse wurde nicht gefunden. Bitte zuerst kompilieren.";
    else {
        auto& script = scene->GetRegistry().get_or_emplace<Funke::ScriptComponent>(handle);
        if (!script.AddScript(name)) error = "Dieses Skript ist bereits zugewiesen.";
        else scene->MarkDirty();
    }
    return error.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* FunkeWebEditorSave() {
    static std::string error;
    error.clear();
    std::vector<std::string> saved;
    if (!g_Editor) error = "Der Editor ist noch nicht bereit.";
    else g_Editor->BridgeSaveScenes(saved, error);
    return error.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* FunkeWebEditorOpen(const char* path) {
    static std::string error;
    error.clear();
    if (!g_Editor) error = "Der Editor ist noch nicht bereit.";
    else if (Funke::Application::GetIsPlaying()) error = "Bitte zuerst den Play-Modus beenden.";
    else {
        // Scene names are serialized inside the file. Give an imported copy its own
        // name as well as a new filename, so later native saves cannot overwrite the source.
        Funke::Json::Value root;
        if (!Funke::SceneSerializer::PrepareFile(path, root, &error)) return error.c_str();
        const auto* entities = root.FindMember("entities");
        if (!root.IsObject() || !entities || !entities->IsArray()) {
            error = "Die Datei enthält keine gültige Funke-Szene.";
            return error.c_str();
        }
        root.AddMember("name", Funke::Json::Value(std::filesystem::path(path).stem().string()));
        if (!Funke::File::WriteAllText(path, Funke::Json::Stringify(root))) error = "Die importierte Szene konnte nicht gespeichert werden.";
        else g_Editor->BridgeOpenScene(path, true, false, error);
    }
    return error.c_str();
}

namespace {
    wgpu::Instance g_Instance;
    wgpu::Adapter g_Adapter;
    void Error(const char* text) {
        std::fprintf(stderr, "Funke Web Editor: %s\n", text);
        EM_ASM({ Module.funkeReportError(UTF8ToString($0)); }, text);
    }
    void Frame() {
        if (!FunkeRuntimeFrame()) { emscripten_cancel_main_loop(); FunkeRuntimeEnd(); Error("The editor stopped; see the browser console."); }
    }
    void Start(Funke::Application*) {
        emscripten_set_main_loop(Frame, 0, false);
        EM_ASM({ Module.funkeReady = true; Module.setStatus(''); });
    }
}
extern "C" EMSCRIPTEN_KEEPALIVE void FunkeWebEditorStart() {
    Funke::Path::SetExecutableDirOverride("/editor");
    g_Instance = wgpu::CreateInstance(nullptr);
    wgpu::RequestAdapterOptions options{};
    g_Instance.RequestAdapter(&options, wgpu::CallbackMode::AllowSpontaneous,
        [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView) {
            if (status != wgpu::RequestAdapterStatus::Success) { Error("No WebGPU adapter available."); return; }
            g_Adapter = std::move(adapter);
            wgpu::DeviceDescriptor descriptor{};
            descriptor.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) { std::fprintf(stderr, "WebGPU: %.*s\n", (int)message.length, message.data); });
            g_Adapter.RequestDevice(&descriptor, wgpu::CallbackMode::AllowSpontaneous,
                [](wgpu::RequestDeviceStatus result, wgpu::Device device, wgpu::StringView) {
                    if (result != wgpu::RequestDeviceStatus::Success) { Error("WebGPU device creation failed."); return; }
                    Funke::WebGPUBackend::ProvideDevice(g_Instance, g_Adapter, std::move(device));
                    static char name[] = "Funke-WebEditor"; static char* argv[] = {name, nullptr};
                    if (auto* app = FunkeRuntimeBegin(1, argv)) Start(app);
                    else Error("Editor startup failed; see the browser console.");
                });
        });
}
extern "C" EMSCRIPTEN_KEEPALIVE int FunkeWeb_Start() {
    g_Player = EM_ASM_INT({ return Module.funkePlayer ? 1 : 0; });
#if FUNKE_WEB_EDITOR_MANAGED
    FunkeMonoConfig mono{};
    mono.AssembliesDir = "/editor";
    mono.RuntimeIdentifier = "browser-wasm";
    mono.InterpreterOnly = true;
    mono.RuntimeAlreadyStarted = true;
    FunkeScriptRuntimeMono_Register(mono);
#endif
    EM_ASM({
        if (!navigator.gpu) return;
        try { FS.mkdir('/libsdl'); } catch (_) {}
        try { FS.mount(IDBFS, {autoPersist:true}, '/libsdl'); }
        catch (e) { console.warn(e); _FunkeWebEditorStart(); return; }
        FS.syncfs(true, error => { if (error) console.warn(error); _FunkeWebEditorStart(); });
        addEventListener('pagehide', () => FS.syncfs(false, () => {}));
    });
    return 0;
}
#if !FUNKE_WEB_EDITOR_MANAGED
int main() { return FunkeWeb_Start(); }
#endif
