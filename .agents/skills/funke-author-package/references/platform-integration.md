# Package platform integration

## Android, iOS, Web: native layers are linked statically

These players can't load package DLLs, so the platform builders compile `native` / `native_standalone` layers into the player (`libmain.so`, the iOS app, the wasm archive set):

- `scripts/platform/funke_platform_sdk/native_packages.py` (`prepare_native_packages`) reads the project's packages (dependencies first), expands the layer's sources/includes, scans `.cpp/.c` files for `FUNKE_PACKAGE_API` functions and writes `native-packages.json` + `FunkeStaticPackages.cpp` (defines `FunkeRegisterStaticPackages()`; on Android/iOS also a symbol table). Offline tests: `python -m unittest scripts.ci.test_native_packages`.
- `funke_add_native_packages(<target> JSON ...)` in `cmake/FunkeEngine.cmake` makes one STATIC `Pkg.<Name>.Native` per package (archive in `<build>/packages/`) with `-DFunkePackage_OnLoad=FunkePackage_OnLoad_<Name_Id>` (same for OnUnload) so entry points don't collide.
- The player mains call `FunkeRegisterStaticPackages()` → `Funke::StaticPackages` (`Packages/StaticPackages.hpp`); `PackageHost::LoadAll` runs their OnLoad before DLLs, `UnloadAll` their OnUnload. Mono's `PINVOKE_OVERRIDE` resolves `[DllImport("Pkg.<Name>.Native")]` via `StaticPackages::FindSymbol` (Android, iOS); the Web relink scans `Pkg.<Name>.Native.a` (`ScanForPInvokes`) and generates `pinvoke-table.h` itself.
- Rules for authors: every C#-called export carries `FUNKE_PACKAGE_API` in a `.cpp`; OnLoad must exist; don't rely on static initializers; keep helpers namespaced (one binary shared by all packages). Blittable P/Invoke signatures (pointers, ints) work everywhere.

## Android contributions (Java, Maven AARs, manifest)

Besides native layers (above), Android SDKs (ads, billing, Play Games) come in through a top-level `android` table, read only by the Android builder (`platforms/Funke.Platform.Android/Builder/android_libraries.py`; premake and the C++ manifest reader ignore it, but it must stay plain data):

```lua
android = {
    java         = { "Android/Java/**.java" },                     -- Java 8 syntax, no lambdas (anonymous classes)
    maven        = { "com.google.android.gms:play-services-ads:25.5.0" },
    manifest     = "Android/AndroidManifest.xml",                 -- merged by android_manifest.py
    placeholders = { admobAppId = "ca-app-pub-3940256099942544~3347511713" },  -- ${admobAppId}; profile option overrides
},
```

- `maven` closures are resolved from POMs (Google Maven + Central, newest version wins, androidx constraints honoured) and **must be pinned**: `python platforms/Funke.Platform.Android/Builder/android_maven.py lock <packageDir>` writes `Android/maven.lock.json` (URL + SHA-256 per file). Without a lock matching the `maven` list the build re-resolves and warns.
- The builder links library resources as aapt2 overlays, regenerates each library package's `R` class from the app's `R.txt`, compiles package Java against all jars, dexes with d8 (multidex), copies `jni/arm64-v8a/*.so`, AAR assets and jar Java resources (`META-INF/services` merged).
- Java ↔ C#: Java exposes `public static String method(String)`; C# `AndroidBridge.Call(class, method, json)` (engine `Platform::CallJava` → `dev.funke.player.FunkeBridge.call`, class loaded via the activity's class loader). Java answers with `FunkeBridge.send(channel, json)` → `Platform::PushJavaMessage` → dispatched to `AndroidBridge.Subscribe` handlers each frame before coroutines. Off Android `Call` returns null; `AndroidBridge.Simulate` feeds handlers for editor stand-ins.
- Static state survives editor Play sessions; reset it in `Application.PlayModeExited`.
- Reference implementation: `Funke.Ads` in the `Funke-PackageRegistry` repo (C# facade + simulated/Android/unsupported backends, `FunkeAds.java`, manifest meta-data from `admobAppId`). Offline tests: `python -m unittest scripts.ci.test_android_builder`.
