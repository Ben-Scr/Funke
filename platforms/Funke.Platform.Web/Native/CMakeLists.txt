# Browser player, configured by Builder/build.py through emcmake.
#
# Native-only games link straight to index.html / .js / .wasm / .data here. With FUNKE_WEB_MANAGED the
# player becomes the static library FunkeWeb.a instead: the .NET WebAssembly SDK links it together with
# the Mono runtime, because only that relink can generate the P/Invoke and callback tables wasm needs.
cmake_minimum_required(VERSION 3.22)
project(FunkeWebPlayer C CXX)

option(FUNKE_WEB_MANAGED "Build FunkeWeb.a for the .NET WebAssembly relink (C# scripting)" OFF)

set(_required FUNKE_ROOT FUNKE_SDL3_DIR FUNKE_EMDAWNWEBGPU_PORT FUNKE_WEBGPU_INCLUDE_DIRS)
if(FUNKE_WEB_MANAGED)
    list(APPEND _required FUNKE_MONO_PACKAGE_DIR FUNKE_MONO_INCLUDE_DIR)
else()
    list(APPEND _required FUNKE_CONTENT_DIR FUNKE_SHELL_FILE)
endif()
foreach(required ${_required})
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be passed by the Web builder")
    endif()
endforeach()

# The engine relies on C++ exceptions; native wasm exceptions are supported by every WebGPU browser.
add_compile_options(-fwasm-exceptions)
add_link_options(-fwasm-exceptions)
if(FUNKE_WEB_MANAGED)
    # .NET 11 builds its runtime with the new (exnref) encoding; browsers refuse a module that mixes it
    # with the legacy one ("module uses a mix of legacy and new exception handling instructions").
    add_compile_options(-sWASM_LEGACY_EXCEPTIONS=0)
    add_link_options(-sWASM_LEGACY_EXCEPTIONS=0)
endif()
# Box2D's wasm path uses SSE2 intrinsics, which Emscripten maps onto WebAssembly SIMD.
add_compile_options(-msimd128 -msse2)
set(_port "--use-port=${FUNKE_EMDAWNWEBGPU_PORT}")
add_compile_options("${_port}")

include("${FUNKE_ROOT}/cmake/FunkeEngine.cmake")

add_library(SDL3::SDL3 STATIC IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${FUNKE_SDL3_DIR}/lib/libSDL3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${FUNKE_SDL3_DIR}/include")

add_library(FunkeWebGPU INTERFACE)
target_link_options(FunkeWebGPU INTERFACE "${_port}")

# Engine feature modules from the game's project.json (passed by the builder); unset = all.
set(_funke_modules)
if(DEFINED FUNKE_ENGINE_MODULES)
    set(_funke_modules MODULES ${FUNKE_ENGINE_MODULES})
endif()

funke_add_engine(
    CONFIGURATION ${FUNKE_CONFIGURATION}
    ENTITY_BITS ${FUNKE_ENTITY_BITS}
    SDL3_TARGET SDL3::SDL3
    WEBGPU_TARGET FunkeWebGPU
    WEBGPU_INCLUDE_DIRS ${FUNKE_WEBGPU_INCLUDE_DIRS}
    ${_funke_modules})

if(FUNKE_WEB_MANAGED)
    add_library(FunkeWeb STATIC
        "${CMAKE_CURRENT_LIST_DIR}/WebMain.cpp"
        "${FUNKE_MONO_PACKAGE_DIR}/Native/MonoScriptRuntime.cpp")
    funke_add_runtime(FunkeWeb)
    target_compile_definitions(FunkeWeb PRIVATE FUNKE_WEB_MANAGED=1)
    target_include_directories(FunkeWeb PRIVATE "${FUNKE_MONO_INCLUDE_DIR}" "${FUNKE_MONO_PACKAGE_DIR}/Native")
    # The DllImport name in the generated host program is "FunkeWeb"; the .NET SDK matches it against
    # the archive's file name.
    set_target_properties(FunkeWeb PROPERTIES PREFIX "" OUTPUT_NAME "FunkeWeb")
    return()
endif()

add_executable(index "${CMAKE_CURRENT_LIST_DIR}/WebMain.cpp")
funke_add_runtime(index)
set_target_properties(index PROPERTIES SUFFIX ".html")
target_link_options(index PRIVATE
    "--shell-file=${FUNKE_SHELL_FILE}"
    "--preload-file=${FUNKE_CONTENT_DIR}@/game"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=134217728"
    "-sSTACK_SIZE=5242880"
    "-sEXIT_RUNTIME=0"
    "-sFORCE_FILESYSTEM=1"
    "-sENVIRONMENT=web"
    "-lidbfs.js"
    "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString']")
