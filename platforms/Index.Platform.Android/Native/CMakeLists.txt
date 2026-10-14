# libmain.so: engine + runtime + Mono backend + Android entry point. Configured by Builder/build.py.
cmake_minimum_required(VERSION 3.22)
project(IndexAndroidPlayer C CXX)

foreach(required INDEX_ROOT INDEX_MONO_PACKAGE INDEX_MONO_PACK_NATIVE INDEX_SDL3_DIR INDEX_DAWN_LIBRARY INDEX_DAWN_INCLUDE_DIRS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be passed by the Android builder")
    endif()
endforeach()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include("${INDEX_ROOT}/cmake/IndexEngine.cmake")
include("${INDEX_MONO_PACKAGE}/cmake/IndexMono.cmake")

add_library(SDL3::SDL3 SHARED IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${INDEX_SDL3_DIR}/lib/libSDL3.so"
    INTERFACE_INCLUDE_DIRECTORIES "${INDEX_SDL3_DIR}/include")

add_library(IndexWebGPU STATIC IMPORTED)
set_target_properties(IndexWebGPU PROPERTIES
    IMPORTED_LOCATION "${INDEX_DAWN_LIBRARY}"
    INTERFACE_LINK_LIBRARIES "android;log")

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
    WEBGPU_INCLUDE_DIRS ${INDEX_DAWN_INCLUDE_DIRS}
    ${_index_modules})

add_library(main SHARED "${CMAKE_CURRENT_LIST_DIR}/AndroidMain.cpp")
index_add_runtime(main)
index_link_mono(main PACK_NATIVE_DIR "${INDEX_MONO_PACK_NATIVE}" MODE SHARED)
target_link_libraries(main PRIVATE android log)
# SDLActivity resolves SDL_main by name.
target_link_options(main PRIVATE "-Wl,--export-dynamic-symbol=SDL_main" "-Wl,--gc-sections")
