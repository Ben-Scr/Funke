#pragma once

#if defined(_WIN32)
    #define FUNKE_PACKAGE_API __declspec(dllexport)
#else
    #define FUNKE_PACKAGE_API __attribute__((visibility("default")))
#endif

extern "C" {

    // Called once after the package's DLL is loaded. Return 0 on success;
    // non-zero is logged but the module stays loaded. Optional — packages
    // without it just appear in the loaded list with no init step.
    //
    // Android, iOS and Web link native layers statically into the player; the
    // platform builder then compiles each package with
    // -DFunkePackage_OnLoad=FunkePackage_OnLoad_<Package_Name> (same for OnUnload)
    // so the entry points of several packages don't collide. Define them only via
    // these names (not through a function pointer or a second alias).
    FUNKE_PACKAGE_API int FunkePackage_OnLoad();

    // Called once before the package's DLL is unloaded (engine shutdown,
    // currently). Optional.
    FUNKE_PACKAGE_API void FunkePackage_OnUnload();

}
