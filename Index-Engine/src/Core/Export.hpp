#pragma once

#include "Core/PlatformDetection.hpp"

#if defined(IDX_PLATFORM_WINDOWS)
#if defined(IDX_BUILD_DLL)
#define INDEX_API __declspec(dllexport)
#elif defined(IDX_IMPORT_DLL)
#define INDEX_API __declspec(dllimport)
#else
#define INDEX_API
#endif
#elif defined(IDX_BUILD_DLL) || defined(IDX_IMPORT_DLL)
#define INDEX_API __attribute__((visibility("default")))
#else
#define INDEX_API
#endif

#if defined(INDEX_ALL_MODULES)
#undef INDEX_WITH_RENDER
#undef INDEX_WITH_AUDIO
#undef INDEX_WITH_PHYSICS
#undef INDEX_WITH_SCRIPTING
#undef INDEX_WITH_APPLICATION
#define INDEX_WITH_RENDER 1
#define INDEX_WITH_AUDIO 1
#define INDEX_WITH_PHYSICS 1
#define INDEX_WITH_SCRIPTING 1
#define INDEX_WITH_APPLICATION 1
// INDEX_WITH_EDITOR is deliberately NOT forced on by INDEX_ALL_MODULES: it is
// controlled per build configuration via premake's ApplyIndexEditorModuleDefine()
// so a Dist (shipped runtime) build can strip editor-only engine systems while
// keeping the full module surface. It falls through to the explicit -D below.
#endif

#ifndef INDEX_WITH_RENDER
#define INDEX_WITH_RENDER 0
#endif

#ifndef INDEX_WITH_AUDIO
#define INDEX_WITH_AUDIO 0
#endif

#ifndef INDEX_WITH_PHYSICS
#define INDEX_WITH_PHYSICS 0
#endif

#ifndef INDEX_WITH_SCRIPTING
#define INDEX_WITH_SCRIPTING 0
#endif

#ifndef INDEX_WITH_EDITOR
#define INDEX_WITH_EDITOR 0
#endif

#ifndef INDEX_WITH_APPLICATION
#define INDEX_WITH_APPLICATION 0
#endif

// Engine feature modules (Index-Engine/EngineModules.json). Builds that don't name them (packages,
// NativeScripts) get both whenever the application surface exists; runtime variants pass =0.
#ifndef INDEX_WITH_2D
#define INDEX_WITH_2D INDEX_WITH_APPLICATION
#endif

#ifndef INDEX_WITH_3D
#define INDEX_WITH_3D INDEX_WITH_APPLICATION
#endif

#if !INDEX_WITH_RENDER && !INDEX_WITH_AUDIO && !INDEX_WITH_PHYSICS && !INDEX_WITH_SCRIPTING && !INDEX_WITH_EDITOR
#define INDEX_CORE_ONLY 1
#else
#define INDEX_CORE_ONLY 0
#endif

#define INDEX_CORE_API INDEX_API
#define INDEX_RENDER_API INDEX_API
#define INDEX_AUDIO_API INDEX_API
#define INDEX_PHYSICS_API INDEX_API
#define INDEX_SCRIPTING_API INDEX_API
#define INDEX_EDITOR_API INDEX_API
