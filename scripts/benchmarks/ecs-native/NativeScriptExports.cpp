#include <Scripting/NativeScript.hpp>

#if defined(_WIN32)
#define FUNKE_NATIVE_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__)
#define FUNKE_NATIVE_SCRIPT_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define FUNKE_NATIVE_SCRIPT_EXPORT extern "C"
#endif

FUNKE_NATIVE_SCRIPT_EXPORT void FunkeInitialize(void* engineAPI) {
	Funke::g_EngineAPI = static_cast<Funke::NativeEngineAPI*>(engineAPI);
}

FUNKE_NATIVE_SCRIPT_EXPORT Funke::NativeScript* FunkeCreateScript(const char* className) {
	return Funke::NativeScriptRegistry::Create(className);
}

FUNKE_NATIVE_SCRIPT_EXPORT int FunkeHasScript(const char* className) {
	return Funke::NativeScriptRegistry::Has(className) ? 1 : 0;
}

FUNKE_NATIVE_SCRIPT_EXPORT void FunkeDestroyScript(Funke::NativeScript* script) {
	delete script;
}

FUNKE_NATIVE_SCRIPT_EXPORT uint32_t FunkeNativeScriptAbiVersion() {
	return Funke::k_NativeScriptAbiVersion;
}
