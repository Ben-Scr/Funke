#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#define FIXTURE_EXPORT extern "C" __declspec(dllexport)
#else
#define FIXTURE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace { bool initialized = false; }

#if !defined(MISSING_ABI)
FIXTURE_EXPORT uint32_t FunkeNativeScriptAbiVersion() { return FIXTURE_ABI; }
#endif
#if !defined(MISSING_CREATE)
FIXTURE_EXPORT void* FunkeCreateScript(const char*) { return nullptr; }
#endif
#if !defined(MISSING_DESTROY)
FIXTURE_EXPORT void FunkeDestroyScript(void*) {}
#endif
FIXTURE_EXPORT void FunkeInitialize(void* api) { initialized = api != nullptr; }
FIXTURE_EXPORT int FunkeHasScript(const char* name) {
    return initialized && std::strcmp(name, FIXTURE_CLASS) == 0;
}
