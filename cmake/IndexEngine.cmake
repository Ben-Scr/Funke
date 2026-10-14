# Index engine as a CMake static library, for targets premake cannot drive (Android, iOS, Web).
#
# This file is engine-owned and platform-neutral. Platform packages (platforms/Index.Platform.*)
# include it from their own CMakeLists, provide the toolchain and the per-target binaries of SDL3
# and WebGPU, and add their own entry point.
#
#   set(INDEX_ROOT <engine root>)
#   include(${INDEX_ROOT}/cmake/IndexEngine.cmake)
#   index_add_engine(
#       CONFIGURATION Development|Release   # Release == premake's Dist (editor code stripped)
#       ENTITY_BITS 20
#       SDL3_TARGET SDL3::SDL3               # imported target from the platform's SDL3 build
#       WEBGPU_TARGET webgpu                 # Dawn library, or an interface target for emdawnwebgpu
#       WEBGPU_INCLUDE_DIRS <dirs with webgpu/webgpu_cpp.h>
#       MODULES Index.2D Index.3D)           # engine feature modules (Index-Engine/EngineModules.json); default: all
#   index_add_runtime(<target>)              # Index-Runtime sources (INDEX_CUSTOM_MAIN) onto an app target
#
# Targets created: IndexEngine (STATIC), plus IndexImGui, and per module IndexBox2D + IndexPhysics (Index.2D)
# and IndexJolt (Index.3D).

cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED INDEX_ROOT)
    get_filename_component(INDEX_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(INDEX_EXTERNAL "${INDEX_ROOT}/External")

function(_index_apply_common_options target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        # The engine is written against MSVC first; these warnings are noise on Clang.
        target_compile_options(${target} PRIVATE
            -Wno-unused-parameter -Wno-missing-field-initializers -Wno-deprecated-declarations
            -Wno-nullability-completeness -Wno-unknown-pragmas -Wno-invalid-offsetof)
    endif()
endfunction()

# Converts an EngineModules.json glob (relative to Index-Engine/, `*` within a segment, `**` across) to a regex.
function(_index_module_glob_to_regex glob out_var)
    string(REPLACE "." "\\." _regex "${glob}")
    string(REPLACE "+" "\\+" _regex "${_regex}")
    string(REPLACE "**" "@@ANY@@" _regex "${_regex}")
    string(REPLACE "*" "[^/]*" _regex "${_regex}")
    string(REPLACE "@@ANY@@" ".*" _regex "${_regex}")
    set(${out_var} "/Index-Engine/${_regex}$" PARENT_SCOPE)
endfunction()

function(index_add_engine)
    cmake_parse_arguments(ARG "" "CONFIGURATION;ENTITY_BITS;SDL3_TARGET;WEBGPU_TARGET" "WEBGPU_INCLUDE_DIRS;MODULES" ${ARGN})
    if(NOT ARG_CONFIGURATION)
        set(ARG_CONFIGURATION Release)
    endif()
    if(NOT ARG_ENTITY_BITS)
        set(ARG_ENTITY_BITS 20)
    endif()
    foreach(required SDL3_TARGET WEBGPU_TARGET)
        if(NOT ARG_${required})
            message(FATAL_ERROR "index_add_engine: ${required} is required")
        endif()
    endforeach()

    # ── Engine feature modules ──────────────────────────────────────────
    file(READ "${INDEX_ROOT}/Index-Engine/EngineModules.json" _modules_json)
    string(JSON _module_count LENGTH "${_modules_json}" modules)
    math(EXPR _module_last "${_module_count} - 1")
    set(_module_defines)
    set(_module_excludes)
    set(_index_with_2d ON)
    set(_index_with_3d ON)
    foreach(_module_index RANGE ${_module_last})
        string(JSON _module_id GET "${_modules_json}" modules ${_module_index} id)
        string(JSON _module_define GET "${_modules_json}" modules ${_module_index} define)
        set(_module_enabled ON)
        if(DEFINED ARG_MODULES AND NOT "${_module_id}" IN_LIST ARG_MODULES)
            set(_module_enabled OFF)
        endif()
        if(_module_enabled)
            list(APPEND _module_defines ${_module_define}=1)
        else()
            list(APPEND _module_defines ${_module_define}=0)
            string(JSON _source_count LENGTH "${_modules_json}" modules ${_module_index} sources)
            if(_source_count GREATER 0)
                math(EXPR _source_last "${_source_count} - 1")
                foreach(_source_index RANGE ${_source_last})
                    string(JSON _glob GET "${_modules_json}" modules ${_module_index} sources ${_source_index})
                    _index_module_glob_to_regex("${_glob}" _regex)
                    list(APPEND _module_excludes "${_regex}")
                endforeach()
            endif()
        endif()
        if(_module_id STREQUAL "Index.2D")
            set(_index_with_2d ${_module_enabled})
        elseif(_module_id STREQUAL "Index.3D")
            set(_index_with_3d ${_module_enabled})
        endif()
    endforeach()
    message(STATUS "Index engine modules: ${_module_defines}")

    # ── Third-party static libraries ────────────────────────────────────
    set(_physics_targets)
    if(_index_with_2d)
        file(GLOB_RECURSE _box2d_sources CONFIGURE_DEPENDS "${INDEX_EXTERNAL}/box2d/src/*.c" "${INDEX_EXTERNAL}/box2d/src/*.cpp")
        add_library(IndexBox2D STATIC ${_box2d_sources})
        target_include_directories(IndexBox2D PUBLIC "${INDEX_EXTERNAL}/box2d/include" PRIVATE "${INDEX_EXTERNAL}/box2d/src")
        target_compile_features(IndexBox2D PRIVATE c_std_17)

        file(GLOB_RECURSE _physics_sources CONFIGURE_DEPENDS "${INDEX_EXTERNAL}/Index-Physics/src/*.c" "${INDEX_EXTERNAL}/Index-Physics/src/*.cpp")
        add_library(IndexPhysics STATIC ${_physics_sources})
        target_include_directories(IndexPhysics PUBLIC "${INDEX_EXTERNAL}/Index-Physics/include" "${INDEX_EXTERNAL}/glm")
        target_compile_features(IndexPhysics PUBLIC cxx_std_23)
        list(APPEND _physics_targets IndexBox2D IndexPhysics)
    endif()

    if(_index_with_3d)
        # Same source set and (empty) JPH_* define set as premake/dependencies/jolt.lua.
        file(GLOB_RECURSE _jolt_sources CONFIGURE_DEPENDS "${INDEX_EXTERNAL}/JoltPhysics/Jolt/*.cpp")
        list(FILTER _jolt_sources EXCLUDE REGEX "/Jolt/(Compute/(CPU|DX12|MTL|VK)|Shaders)/")
        list(FILTER _jolt_sources EXCLUDE REGEX "/Jolt/ObjectStream/(ObjectStream[A-Za-z]*|TypeDeclarations)\.cpp$")
        add_library(IndexJolt STATIC ${_jolt_sources})
        target_include_directories(IndexJolt PUBLIC "${INDEX_EXTERNAL}/JoltPhysics")
        target_compile_features(IndexJolt PUBLIC cxx_std_17)
        list(APPEND _physics_targets IndexJolt)
    endif()

    add_library(IndexImGui STATIC
        "${INDEX_EXTERNAL}/imgui/imgui.cpp"
        "${INDEX_EXTERNAL}/imgui/imgui_draw.cpp"
        "${INDEX_EXTERNAL}/imgui/imgui_tables.cpp"
        "${INDEX_EXTERNAL}/imgui/imgui_widgets.cpp"
        "${INDEX_EXTERNAL}/imgui/backends/imgui_impl_sdl3.cpp"
        "${INDEX_EXTERNAL}/imgui/backends/imgui_impl_wgpu.cpp")
    target_include_directories(IndexImGui PUBLIC "${INDEX_EXTERNAL}/imgui" "${INDEX_EXTERNAL}/imgui/backends" ${ARG_WEBGPU_INCLUDE_DIRS})
    target_compile_definitions(IndexImGui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
    target_link_libraries(IndexImGui PUBLIC ${ARG_SDL3_TARGET} ${ARG_WEBGPU_TARGET})

    # ── Engine ──────────────────────────────────────────────────────────
    file(GLOB_RECURSE _engine_sources CONFIGURE_DEPENDS
        "${INDEX_ROOT}/Index-Engine/src/*.c" "${INDEX_ROOT}/Index-Engine/src/*.cpp")
    # Diagnostics overlays belong to the runtime app (premake adds them there too); the profiler is a desktop dev tool.
    list(FILTER _engine_sources EXCLUDE REGEX "/src/Diagnostics/")
    list(FILTER _engine_sources EXCLUDE REGEX "/src/Profiling/")
    list(FILTER _engine_sources EXCLUDE REGEX "/src/pch\\.cpp$")
    foreach(_regex IN LISTS _module_excludes)
        list(FILTER _engine_sources EXCLUDE REGEX "${_regex}")
    endforeach()

    add_library(IndexEngine STATIC ${_engine_sources})
    _index_apply_common_options(IndexEngine)
    target_include_directories(IndexEngine PUBLIC
        "${INDEX_ROOT}/Index-Engine/src"
        "${INDEX_ROOT}/Index-Engine/src/Generated"
        "${INDEX_EXTERNAL}/spdlog/include"
        "${INDEX_EXTERNAL}/glm"
        "${INDEX_EXTERNAL}/entt/src"
        "${INDEX_EXTERNAL}/stb"
        "${INDEX_EXTERNAL}/magic_enum/include"
        "${INDEX_EXTERNAL}/rapidxml"
        "${INDEX_EXTERNAL}/concurrentqueue"
        "${INDEX_EXTERNAL}/miniaudio"
        "${INDEX_EXTERNAL}/dotnet"
        ${ARG_WEBGPU_INCLUDE_DIRS})
    target_precompile_headers(IndexEngine PRIVATE "${INDEX_ROOT}/Index-Engine/src/pch.hpp")

    if(ARG_CONFIGURATION STREQUAL "Release")
        set(_config_defines IDX_DIST NDEBUG INDEX_WITH_EDITOR=0)
    else()
        set(_config_defines IDX_RELEASE NDEBUG INDEX_WITH_EDITOR=0)
    endif()
    target_compile_definitions(IndexEngine PUBLIC
        IDX_RHI_WEBGPU
        WEBGPU_CPP_HAS_ENUM_CLASS_BITMASKS=1
        INDEX_ALL_MODULES=1
        ${_module_defines}
        INDEX_ENTITY_BITS=${ARG_ENTITY_BITS}
        MAGIC_ENUM_RANGE_MIN=-1
        MAGIC_ENUM_RANGE_MAX=32
        ${_config_defines})

    target_link_libraries(IndexEngine PUBLIC IndexImGui ${_physics_targets} ${ARG_SDL3_TARGET} ${ARG_WEBGPU_TARGET})
    if(NOT EMSCRIPTEN AND NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(IndexEngine PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    endif()
endfunction()

# Adds the shipped runtime (Index-Runtime) to an executable or shared-library target. The platform
# package defines main() and calls IndexRuntimeMain / IndexRuntimeBegin (see EntryPoint.hpp).
function(index_add_runtime target)
    file(GLOB _runtime_sources CONFIGURE_DEPENDS "${INDEX_ROOT}/Index-Runtime/src/*.cpp")
    file(GLOB _diagnostics_sources CONFIGURE_DEPENDS "${INDEX_ROOT}/Index-Engine/src/Diagnostics/*.cpp")
    target_sources(${target} PRIVATE ${_runtime_sources} ${_diagnostics_sources})
    target_include_directories(${target} PRIVATE "${INDEX_ROOT}/Index-Runtime/src")
    target_compile_definitions(${target} PRIVATE INDEX_CUSTOM_MAIN)
    _index_apply_common_options(${target})
    target_link_libraries(${target} PRIVATE IndexEngine)
endfunction()
