# libmain.so: engine + runtime + Mono backend + Android entry point. Configured by Builder/build.py.
cmake_minimum_required(VERSION 3.22)
project(FunkeAndroidPlayer C CXX)

foreach(required FUNKE_ROOT FUNKE_MONO_PACKAGE FUNKE_MONO_PACK_NATIVE FUNKE_SDL3_DIR FUNKE_DAWN_LIBRARY FUNKE_DAWN_INCLUDE_DIRS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be passed by the Android builder")
    endif()
endforeach()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include("${FUNKE_ROOT}/cmake/FunkeEngine.cmake")
include("${FUNKE_MONO_PACKAGE}/cmake/FunkeMono.cmake")

add_library(SDL3::SDL3 SHARED IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${FUNKE_SDL3_DIR}/lib/libSDL3.so"
    INTERFACE_INCLUDE_DIRECTORIES "${FUNKE_SDL3_DIR}/include")

add_library(FunkeWebGPU STATIC IMPORTED)
set_target_properties(FunkeWebGPU PROPERTIES
    IMPORTED_LOCATION "${FUNKE_DAWN_LIBRARY}"
    INTERFACE_LINK_LIBRARIES "android;log")

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
    WEBGPU_INCLUDE_DIRS ${FUNKE_DAWN_INCLUDE_DIRS}
    ${_funke_modules})

add_library(main SHARED "${CMAKE_CURRENT_LIST_DIR}/AndroidMain.cpp")
funke_add_runtime(main)
funke_link_mono(main PACK_NATIVE_DIR "${FUNKE_MONO_PACK_NATIVE}" MODE SHARED)
target_link_libraries(main PRIVATE android log)
# SDLActivity resolves SDL_main by name.
target_link_options(main PRIVATE "-Wl,--export-dynamic-symbol=SDL_main" "-Wl,--gc-sections")
