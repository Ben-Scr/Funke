// iOS entry point: the game content and managed assemblies ship inside the app bundle, so no
// extraction is needed. iOS forbids JIT, so Mono runs interpreter-only, and every native library
// is linked statically: [DllImport]s resolve against the app binary.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "MonoScriptRuntime.hpp"
#include "Serialization/Path.hpp"

#include <dlfcn.h>
#include <string>
#include <vector>

int FunkeRuntimeMain(int argc, char** argv);

namespace {
	const void* ResolveStaticPInvoke(const char* /*libraryName*/, const char* entryPoint) {
		return dlsym(RTLD_MAIN_ONLY, entryPoint);
	}
}

int main(int argc, char** argv) {
	const char* base = SDL_GetBasePath();  // <App>.app/ on iOS
	const std::string bundle = base ? base : "./";

	Funke::Path::SetExecutableDirOverride(bundle + "game");

	FunkeMonoConfig mono;
	mono.AssembliesDir = bundle + "managed";
	mono.RuntimeIdentifier = "ios-arm64";
	mono.InterpreterOnly = true;
	mono.PInvokeResolver = &ResolveStaticPInvoke;
	FunkeScriptRuntimeMono_Register(mono);

	std::vector<char*> args(argv, argv + argc);
	return FunkeRuntimeMain(static_cast<int>(args.size()), args.data());
}
