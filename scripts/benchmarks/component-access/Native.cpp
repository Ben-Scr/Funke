#include "Core/Application.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <memory>

namespace {
	std::unique_ptr<Funke::Application> g_App;
	std::shared_ptr<Funke::Scene> g_Scene;
	std::unique_ptr<Funke::SceneExecutionScope> g_Scope;
}

extern "C" __declspec(dllexport) int InitializeProbe(Funke::NativeBindings* bindings, size_t size) {
	if (!bindings || size != sizeof(*bindings)) return 0;
	g_App = std::make_unique<Funke::Application>();
	Funke::RegisterBuiltInComponents(Funke::SceneManager::GetComponentRegistry());
	g_Scene = Funke::Scene::CreateDetachedScene("Component access benchmark");
	g_Scope = std::make_unique<Funke::SceneExecutionScope>(*g_Scene);
	Funke::ScriptBindings::PopulateNativeBindings(*bindings);
	return 1;
}

extern "C" __declspec(dllexport) void CreateProbeEntities(uint64_t* ids, int count) {
	for (int index = 0; index < count; ++index) {
		const auto handle = g_Scene->CreateEntityHandle();
		g_Scene->AddComponent<Funke::Transform2DComponent>(handle);
		ids[index] = g_Scene->GetEntityPersistentID(handle);
	}
}

extern "C" __declspec(dllexport) void ShutdownProbe() {
	g_Scene->ClearEntities();
	g_Scope.reset();
	g_Scene.reset();
	g_App.reset();
}
