#pragma once

#include <string>

// Mono backend for Funke::ScriptRuntimeHost. Compiled into the game binary by a platform package;
// the platform's entry point registers it before the engine starts:
//
//     FunkeMonoConfig config{};
//     config.AssembliesDir = "<dir with the BCL, Funke-ScriptCore.dll and the game assembly>";
//     FunkeScriptRuntimeMono_Register(config);

extern "C" {
	// Resolves a [DllImport] to a statically linked function (iOS / Web); null to fall back to dlopen.
	typedef const void* (*FunkeMonoPInvokeResolver)(const char* libraryName, const char* entryPoint);
}

struct FunkeMonoConfig {
	// Directory holding System.Private.CoreLib.dll, the BCL and the game's managed assemblies.
	std::string AssembliesDir;
	// Where shared native libraries (libSystem.Native.so, ...) live; empty when everything is static.
	std::string NativeLibraryDir;
	// .NET runtime identifier the runtime pack was built for ("android-arm64", "ios-arm64", ...).
	std::string RuntimeIdentifier;
	// iOS forbids JIT; the Web runtime only has the interpreter.
	bool InterpreterOnly = false;
	// Precompiled code for the assemblies listed in <AssembliesDir>/funke-aot.txt is loaded as
	// "<prefix><Assembly>.dll.so" from the library search path; everything else is JIT compiled.
	std::string AotLibraryPrefix = "libaot-";
	// The Web host starts Mono from dotnet.js before the engine runs; the backend then only attaches.
	bool RuntimeAlreadyStarted = false;
	FunkeMonoPInvokeResolver PInvokeResolver = nullptr;
};

void FunkeScriptRuntimeMono_Register(const FunkeMonoConfig& config);
