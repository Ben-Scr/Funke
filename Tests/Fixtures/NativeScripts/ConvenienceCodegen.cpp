#include <Scripting/NativeScript.hpp>

using namespace Funke;

struct ProbeValue { int Value = 0; };
struct ProbeTag {};
class ProbeScript : public NativeScript {};

extern "C" __declspec(noinline) ProbeValue* NativeTry(NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	auto* registry = script.GetRegistry();
	const auto handle = script.GetEntityHandle();
	if (!registry || handle == entt::null) return nullptr;
	return ComponentUtils::TryGetComponent<ProbeValue>(*registry, handle);
#else
	return script.TryGetComponent<ProbeValue>();
#endif
}

extern "C" __declspec(noinline) const ProbeValue* NativeConstTry(const NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	const auto* registry = script.GetRegistry();
	const auto handle = script.GetEntityHandle();
	if (!registry || handle == entt::null) return nullptr;
	return ComponentUtils::TryGetComponent<ProbeValue>(*registry, handle);
#else
	return script.TryGetComponent<ProbeValue>();
#endif
}

extern "C" __declspec(noinline) ProbeValue* EntityTry(Entity& entity) {
#if FUNKE_CONVENIENCE_DIRECT
	ProbeValue* value = nullptr;
	entity.TryGetComponent(value);
	return value;
#else
	return entity.TryGetComponent<ProbeValue>();
#endif
}

extern "C" __declspec(noinline) const ProbeValue* EntityConstTry(const Entity& entity) {
#if FUNKE_CONVENIENCE_DIRECT
	const ProbeValue* value = nullptr;
	entity.TryGetComponent(value);
	return value;
#else
	return entity.TryGetComponent<ProbeValue>();
#endif
}

extern "C" __declspec(noinline) ProbeValue& NativeEnsure(NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	if (auto* value = script.TryGetComponent<ProbeValue>()) return *value;
	return script.AddComponent<ProbeValue>();
#else
	return script.GetOrAddComponent<ProbeValue>();
#endif
}

extern "C" __declspec(noinline) ProbeValue& EntityEnsure(Entity& entity) {
#if FUNKE_CONVENIENCE_DIRECT
	if (auto* value = entity.TryGetComponent<ProbeValue>()) return *value;
	return entity.AddComponent<ProbeValue>();
#else
	return entity.GetOrAddComponent<ProbeValue>();
#endif
}

extern "C" __declspec(noinline) void NativeEnsureTag(NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	if (!script.HasComponent<ProbeTag>()) script.AddComponent<ProbeTag>();
#else
	script.GetOrAddComponent<ProbeTag>();
#endif
}

extern "C" __declspec(noinline) ProbeScript* NativeGetScript(NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	return EntityScriptOps::GetScriptOnEntity<ProbeScript>(script.GetScene(), script.GetEntityHandle());
#else
	return script.GetComponent<ProbeScript>();
#endif
}

extern "C" __declspec(noinline) ProbeScript* NativeAddScript(NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	return EntityScriptOps::AddScriptToEntity<ProbeScript>(script.GetScene(), script.GetEntityHandle());
#else
	return script.AddComponent<ProbeScript>();
#endif
}

extern "C" __declspec(noinline) bool NativeHasScript(const NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	return EntityScriptOps::HasScriptOnEntity<ProbeScript>(script.GetScene(), script.GetEntityHandle());
#else
	return script.HasComponent<ProbeScript>();
#endif
}

extern "C" __declspec(noinline) void NativeRemoveScript(NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	(void)EntityScriptOps::RemoveScriptFromEntity<ProbeScript>(script.GetScene(), script.GetEntityHandle());
#else
	script.RemoveComponent<ProbeScript>();
#endif
}

extern "C" __declspec(noinline) ProbeScript* NativeEnsureScript(NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	if (auto* value = script.GetComponent<ProbeScript>()) return value;
	return script.AddComponent<ProbeScript>();
#else
	return script.GetOrAddComponent<ProbeScript>();
#endif
}

extern "C" __declspec(noinline) Entity NativeEntity(const NativeScript& script) {
#if FUNKE_CONVENIENCE_DIRECT
	if (!script.GetScene()) return script.GetEntity();
	return script.GetScene()->GetEntity(script.GetEntityHandle());
#else
	return script.GetEntity();
#endif
}

struct PreviousNativeLayout {
	virtual ~PreviousNativeLayout() = default;
	EntityHandle Handle = entt::null;
	Scene* Owner = nullptr;
};
static_assert(sizeof(NativeScript) == sizeof(PreviousNativeLayout));
static_assert(alignof(NativeScript) == alignof(PreviousNativeLayout));
