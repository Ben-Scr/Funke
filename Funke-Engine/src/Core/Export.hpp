#pragma once

#include "Core/PlatformDetection.hpp"

#if defined(FNK_PLATFORM_WINDOWS)
#if defined(FNK_BUILD_DLL)
#define FUNKE_API __declspec(dllexport)
#elif defined(FNK_IMPORT_DLL)
#define FUNKE_API __declspec(dllimport)
#else
#define FUNKE_API
#endif
#elif defined(FNK_BUILD_DLL) || defined(FNK_IMPORT_DLL)
#define FUNKE_API __attribute__((visibility("default")))
#else
#define FUNKE_API
#endif

#if defined(FUNKE_ALL_MODULES)
#undef FUNKE_WITH_RENDER
#undef FUNKE_WITH_AUDIO
#undef FUNKE_WITH_PHYSICS
#undef FUNKE_WITH_SCRIPTING
#undef FUNKE_WITH_APPLICATION
#define FUNKE_WITH_RENDER 1
#define FUNKE_WITH_AUDIO 1
#define FUNKE_WITH_PHYSICS 1
#define FUNKE_WITH_SCRIPTING 1
#define FUNKE_WITH_APPLICATION 1
// FUNKE_WITH_EDITOR is deliberately NOT forced on by FUNKE_ALL_MODULES: it is
// controlled per build configuration via premake's ApplyFunkeEditorModuleDefine()
// so a Dist (shipped runtime) build can strip editor-only engine systems while
// keeping the full module surface. It falls through to the explicit -D below.
#endif

#ifndef FUNKE_WITH_RENDER
#define FUNKE_WITH_RENDER 0
#endif

#ifndef FUNKE_WITH_AUDIO
#define FUNKE_WITH_AUDIO 0
#endif

#ifndef FUNKE_WITH_PHYSICS
#define FUNKE_WITH_PHYSICS 0
#endif

#ifndef FUNKE_WITH_SCRIPTING
#define FUNKE_WITH_SCRIPTING 0
#endif

#ifndef FUNKE_WITH_EDITOR
#define FUNKE_WITH_EDITOR 0
#endif

#ifndef FUNKE_WITH_APPLICATION
#define FUNKE_WITH_APPLICATION 0
#endif

// Engine feature modules (Funke-Engine/EngineModules.json). Builds that don't name them (packages,
// NativeScripts) get both whenever the application surface exists; runtime variants pass =0.
#ifndef FUNKE_WITH_2D
#define FUNKE_WITH_2D FUNKE_WITH_APPLICATION
#endif

#ifndef FUNKE_WITH_3D
#define FUNKE_WITH_3D FUNKE_WITH_APPLICATION
#endif

#if !FUNKE_WITH_RENDER && !FUNKE_WITH_AUDIO && !FUNKE_WITH_PHYSICS && !FUNKE_WITH_SCRIPTING && !FUNKE_WITH_EDITOR
#define FUNKE_CORE_ONLY 1
#else
#define FUNKE_CORE_ONLY 0
#endif

#define FUNKE_CORE_API FUNKE_API
#define FUNKE_RENDER_API FUNKE_API
#define FUNKE_AUDIO_API FUNKE_API
#define INDEX_PHYSICS_API FUNKE_API
#define FUNKE_SCRIPTING_API FUNKE_API
#define FUNKE_EDITOR_API FUNKE_API
