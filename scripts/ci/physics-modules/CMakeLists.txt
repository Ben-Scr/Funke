cmake_minimum_required(VERSION 3.22)
project(FunkeModuleGraph LANGUAGES C CXX)

get_filename_component(FUNKE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${FUNKE_ROOT}/cmake/FunkeEngine.cmake")
add_library(ModuleProbeSDL INTERFACE)
add_library(ModuleProbeWebGPU INTERFACE)
funke_add_engine(CONFIGURATION Development
    SDL3_TARGET ModuleProbeSDL WEBGPU_TARGET ModuleProbeWebGPU
    MODULES ${PROBE_MODULES})

get_target_property(sources FunkeEngine SOURCES)
get_target_property(definitions FunkeEngine COMPILE_DEFINITIONS)
get_target_property(libraries FunkeEngine LINK_LIBRARIES)
file(WRITE "${CMAKE_BINARY_DIR}/module-graph.txt" "${definitions}\n${libraries}\n${sources}\n")
