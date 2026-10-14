# Browser player, configured by Builder/build.py through emcmake.
#
# Native-only games link straight to index.html / .js / .wasm / .data here. With INDEX_WEB_MANAGED the
# player becomes the static library IndexWeb.a instead: the .NET WebAssembly SDK links it together with
# the Mono runtime, because only that relink can generate the P/Invoke and callback tables wasm needs.
cmake_minimum_required(VERSION 3.22)
project(IndexWebPlayer C CXX)

option(INDEX_WEB_MANAGED "Build IndexWeb.a for the .NET WebAssembly relink (C# scripting)" OFF)

set(_required INDEX_ROOT INDEX_SDL3_DIR INDEX_EMDAWNWEBGPU_PORT INDEX_WEBGPU_INCLUDE_DIRS)
if(INDEX_WEB_MANAGED)
    list(APPEND _required INDEX_MONO_PACKAGE_DIR INDEX_MONO_INCLUDE_DIR)
else()
    list(APPEND _required INDEX_CONTENT_DIR INDEX_SHELL_FILE)
endif()
foreach(required ${_required})
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be passed by the Web builder")
    endif()
endforeach()

# The engine relies on C++ exceptions; native wasm exceptions are supported by every WebGPU browser
# and are what the .NET runtime is built with.
add_compile_options(-fwasm-exceptions)
add_link_options(-fwasm-exceptions)
# Box2D's wasm path uses SSE2 intrinsics, which Emscripten maps onto WebAssembly SIMD.
add_compile_options(-msimd128 -msse2)
set(_port "--use-port=${INDEX_EMDAWNWEBGPU_PORT}")
add_compile_options("${_port}")

include("${INDEX_ROOT}/cmake/IndexEngine.cmake")

add_library(SDL3::SDL3 STATIC IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${INDEX_SDL3_DIR}/lib/libSDL3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${INDEX_SDL3_DIR}/include")

add_library(IndexWebGPU INTERFACE)
target_link_options(IndexWebGPU INTERFACE "${_port}")

# Engine feature modules from the game's project.json (passed by the builder); unset = all.
set(_index_modules)
if(DEFINED INDEX_ENGINE_MODULES)
    set(_index_modules MODULES ${INDEX_ENGINE_MODULES})
endif()

index_add_engine(
    CONFIGURATION ${INDEX_CONFIGURATION}
    ENTITY_BITS ${INDEX_ENTITY_BITS}
    SDL3_TARGET SDL3::SDL3
    WEBGPU_TARGET IndexWebGPU
    WEBGPU_INCLUDE_DIRS ${INDEX_WEBGPU_INCLUDE_DIRS}
    ${_index_modules})

if(INDEX_WEB_MANAGED)
    add_library(IndexWeb STATIC
        "${CMAKE_CURRENT_LIST_DIR}/WebMain.cpp"
        "${INDEX_MONO_PACKAGE_DIR}/Native/MonoScriptRuntime.cpp")
    index_add_runtime(IndexWeb)
    target_compile_definitions(IndexWeb PRIVATE INDEX_WEB_MANAGED=1)
    target_include_directories(IndexWeb PRIVATE "${INDEX_MONO_INCLUDE_DIR}" "${INDEX_MONO_PACKAGE_DIR}/Native")
    # The DllImport name in the generated host program is "IndexWeb"; the .NET SDK matches it against
    # the archive's file name.
    set_target_properties(IndexWeb PROPERTIES PREFIX "" OUTPUT_NAME "IndexWeb")
    return()
endif()

add_executable(index "${CMAKE_CURRENT_LIST_DIR}/WebMain.cpp")
index_add_runtime(index)
set_target_properties(index PROPERTIES SUFFIX ".html")
target_link_options(index PRIVATE
    "--shell-file=${INDEX_SHELL_FILE}"
    "--preload-file=${INDEX_CONTENT_DIR}@/game"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=134217728"
    "-sSTACK_SIZE=5242880"
    "-sEXIT_RUNTIME=0"
    "-sFORCE_FILESYSTEM=1"
    "-sENVIRONMENT=web"
    "-lidbfs.js"
    "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString']")
