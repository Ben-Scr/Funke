#pragma once
#include "Core/Export.hpp"

#include <cstddef>
#include <vector>

namespace Funke {

	// An exported function of a statically linked package, for [DllImport] resolution.
	struct StaticPackageSymbol {
		const char* Name = nullptr;
		const void* Address = nullptr;
	};

	// A package's native layer linked into the player (Android, iOS, Web), where PackageHost has no DLL
	// to load. The platform builder generates one entry per package (cmake funke_add_native_packages).
	struct StaticPackage {
		const char* Name = nullptr;           // "Funke.Physics"
		const char* Version = nullptr;        // From funke-package.lua
		const char* NativeLibrary = nullptr;  // [DllImport] library name, "Pkg.Funke.Physics.Native"
		int (*OnLoad)() = nullptr;
		void (*OnUnload)() = nullptr;
		const StaticPackageSymbol* Symbols = nullptr;
		size_t SymbolCount = 0;
	};

	namespace StaticPackages {
		// Called by the generated FunkeRegisterStaticPackages() before the engine starts, dependencies first.
		FUNKE_API void Register(const StaticPackage& package);
		FUNKE_API const std::vector<StaticPackage>& GetAll();
		FUNKE_API const StaticPackage* Find(const char* packageName);
		// A [DllImport] target of a linked package, or null (another library, unknown entry point).
		// Platform players hand it to Mono as the P/Invoke override.
		FUNKE_API const void* FindSymbol(const char* libraryName, const char* entryPoint);
	}

}
