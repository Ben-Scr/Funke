#include "pch.hpp"
#include "Packages/StaticPackages.hpp"

#include <cstring>

namespace Funke::StaticPackages {
	namespace {
		std::vector<StaticPackage>& Packages() {
			static std::vector<StaticPackage> s_Packages;
			return s_Packages;
		}

		bool Equals(const char* a, const char* b) {
			return a && b && std::strcmp(a, b) == 0;
		}
	}

	void Register(const StaticPackage& package) {
		for (StaticPackage& existing : Packages()) {
			if (Equals(existing.Name, package.Name)) {
				existing = package;
				return;
			}
		}
		Packages().push_back(package);
	}

	const std::vector<StaticPackage>& GetAll() {
		return Packages();
	}

	const StaticPackage* Find(const char* packageName) {
		for (const StaticPackage& package : Packages()) {
			if (Equals(package.Name, packageName)) return &package;
		}
		return nullptr;
	}

	const void* FindSymbol(const char* libraryName, const char* entryPoint) {
		if (!libraryName || !entryPoint) return nullptr;
		for (const StaticPackage& package : Packages()) {
			if (!Equals(package.NativeLibrary, libraryName)) continue;
			for (size_t i = 0; i < package.SymbolCount; ++i) {
				if (Equals(package.Symbols[i].Name, entryPoint)) return package.Symbols[i].Address;
			}
			return nullptr;
		}
		return nullptr;
	}
}
