# Funke engine as a CMake static library, for targets premake cannot drive (Android, iOS, Web).
#
# This file is engine-owned and platform-neutral. Platform packages (platforms/Funke.Platform.*)
# include it from their own CMakeLists, provide the toolchain and the per-target binaries of SDL3
# and WebGPU, and add their own entry point.
#
#   set(FUNKE_ROOT <engine root>)
#   include(${FUNKE_ROOT}/cmake/FunkeEngine.cmake)
#   funke_add_engine(
#       CONFIGURATION Development|Release   # Release == premake's Dist (editor code stripped)
#       ENTITY_BITS 20
#       SDL3_TARGET SDL3::SDL3               # imported target from the platform's SDL3 build
#       WEBGPU_TARGET webgpu                 # Dawn library, or an interface target for emdawnwebgpu
#       WEBGPU_INCLUDE_DIRS <dirs with webgpu/webgpu_cpp.h>
#       MODULES Funke.2D Funke.3D)           # engine feature modules (Funke-Engine/EngineModules.json); default: all
#   funke_add_runtime(<target>)              # Funke-Runtime sources (FUNKE_CUSTOM_MAIN) onto an app target
#   funke_add_native_packages(<target> JSON <native-packages.json>)   # game packages' native layers
#
# Targets created: FunkeEngine (STATIC), plus FunkeImGui, and per module FunkeBox2D (Funke.2D)
# and FunkeJolt (Funke.3D); per native package a STATIC Pkg.<Name>.Native.

cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED FUNKE_ROOT)
    get_filename_component(FUNKE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(FUNKE_EXTERNAL "${FUNKE_ROOT}/External")

function(_funke_apply_common_options target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        # The engine is written against MSVC first; these warnings are noise on Clang.
        target_compile_options(${target} PRIVATE
            -Wno-unused-parameter -Wno-missing-field-initializers -Wno-deprecated-declarations
            -Wno-nullability-completeness -Wno-unknown-pragmas -Wno-invalid-offsetof)
    endif()
endfunction()

# Converts an EngineModules.json glob (relative to Funke-Engine/, `*` within a segment, `**` across) to a regex.
function(_funke_module_glob_to_regex glob out_var)
    string(REPLACE "." "\\." _regex "${glob}")
    string(REPLACE "+" "\\+" _regex "${_regex}")
    string(REPLACE "**" "@@ANY@@" _regex "${_regex}")
    string(REPLACE "*" "[^/]*" _regex "${_regex}")
    string(REPLACE "@@ANY@@" ".*" _regex "${_regex}")
    set(${out_var} "/Funke-Engine/${_regex}$" PARENT_SCOPE)
endfunction()

function(funke_add_engine)
    cmake_parse_arguments(ARG "" "CONFIGURATION;ENTITY_BITS;SDL3_TARGET;WEBGPU_TARGET" "WEBGPU_INCLUDE_DIRS;MODULES" ${ARGN})
    if(NOT ARG_CONFIGURATION)
        set(ARG_CONFIGURATION Release)
    endif()
    if(NOT ARG_ENTITY_BITS)
        set(ARG_ENTITY_BITS 20)
    endif()
    foreach(required SDL3_TARGET WEBGPU_TARGET)
        if(NOT ARG_${required})
            message(FATAL_ERROR "funke_add_engine: ${required} is required")
        endif()
    endforeach()

    # ── Engine feature modules ──────────────────────────────────────────
    file(READ "${FUNKE_ROOT}/Funke-Engine/EngineModules.json" _modules_json)
    string(JSON _module_count LENGTH "${_modules_json}" modules)
    math(EXPR _module_last "${_module_count} - 1")
    set(_module_defines)
    set(_module_excludes)
    set(_funke_with_2d ON)
    set(_funke_with_3d ON)
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
                    _funke_module_glob_to_regex("${_glob}" _regex)
                    list(APPEND _module_excludes "${_regex}")
                endforeach()
            endif()
        endif()
        if(_module_id STREQUAL "Funke.2D")
            set(_funke_with_2d ${_module_enabled})
        elseif(_module_id STREQUAL "Funke.3D")
            set(_funke_with_3d ${_module_enabled})
        endif()
    endforeach()
    message(STATUS "Funke engine modules: ${_module_defines}")

    # ── Third-party static libraries ────────────────────────────────────
    set(_physics_targets)
    if(_funke_with_2d)
        file(GLOB_RECURSE _box2d_sources CONFIGURE_DEPENDS "${FUNKE_EXTERNAL}/box2d/src/*.c" "${FUNKE_EXTERNAL}/box2d/src/*.cpp")
        add_library(FunkeBox2D STATIC ${_box2d_sources})
        target_include_directories(FunkeBox2D PUBLIC "${FUNKE_EXTERNAL}/box2d/include" PRIVATE "${FUNKE_EXTERNAL}/box2d/src")
        target_compile_features(FunkeBox2D PRIVATE c_std_17)
        list(APPEND _physics_targets FunkeBox2D)
    endif()

    if(_funke_with_3d)
        # Same source set and (empty) JPH_* define set as premake/dependencies/jolt.lua.
        file(GLOB_RECURSE _jolt_sources CONFIGURE_DEPENDS "${FUNKE_EXTERNAL}/JoltPhysics/Jolt/*.cpp")
        list(FILTER _jolt_sources EXCLUDE REGEX "/Jolt/(Compute/(CPU|DX12|MTL|VK)|Shaders)/")
        list(FILTER _jolt_sources EXCLUDE REGEX "/Jolt/ObjectStream/(ObjectStream[A-Za-z]*|TypeDeclarations)\.cpp$")
        add_library(FunkeJolt STATIC ${_jolt_sources})
        target_include_directories(FunkeJolt PUBLIC "${FUNKE_EXTERNAL}/JoltPhysics")
        target_compile_features(FunkeJolt PUBLIC cxx_std_17)
        list(APPEND _physics_targets FunkeJolt)
    endif()

    add_library(FunkeImGui STATIC
        "${FUNKE_EXTERNAL}/imgui/imgui.cpp"
        "${FUNKE_EXTERNAL}/imgui/imgui_draw.cpp"
        "${FUNKE_EXTERNAL}/imgui/imgui_tables.cpp"
        "${FUNKE_EXTERNAL}/imgui/imgui_widgets.cpp"
        "${FUNKE_EXTERNAL}/imgui/backends/imgui_impl_sdl3.cpp"
        "${FUNKE_EXTERNAL}/imgui/backends/imgui_impl_wgpu.cpp")
    target_include_directories(FunkeImGui PUBLIC "${FUNKE_EXTERNAL}/imgui" "${FUNKE_EXTERNAL}/imgui/backends" ${ARG_WEBGPU_INCLUDE_DIRS})
    target_compile_definitions(FunkeImGui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
    target_link_libraries(FunkeImGui PUBLIC ${ARG_SDL3_TARGET} ${ARG_WEBGPU_TARGET})

    # ── Engine ──────────────────────────────────────────────────────────
    file(GLOB_RECURSE _engine_sources CONFIGURE_DEPENDS
        "${FUNKE_ROOT}/Funke-Engine/src/*.c" "${FUNKE_ROOT}/Funke-Engine/src/*.cpp")
    # Diagnostics overlays belong to the runtime app (premake adds them there too); the profiler is a desktop dev tool.
    list(FILTER _engine_sources EXCLUDE REGEX "/src/Diagnostics/")
    list(FILTER _engine_sources EXCLUDE REGEX "/src/Profiling/")
    list(FILTER _engine_sources EXCLUDE REGEX "/src/pch\\.cpp$")
    foreach(_regex IN LISTS _module_excludes)
        list(FILTER _engine_sources EXCLUDE REGEX "${_regex}")
    endforeach()

    add_library(FunkeEngine STATIC ${_engine_sources})
    _funke_apply_common_options(FunkeEngine)
    target_include_directories(FunkeEngine PUBLIC
        "${FUNKE_ROOT}/Funke-Engine/src"
        "${FUNKE_ROOT}/Funke-Engine/src/Generated"
        "${FUNKE_EXTERNAL}/spdlog/include"
        "${FUNKE_EXTERNAL}/glm"
        "${FUNKE_EXTERNAL}/entt/src"
        "${FUNKE_EXTERNAL}/stb"
        "${FUNKE_EXTERNAL}/magic_enum/include"
        "${FUNKE_EXTERNAL}/rapidxml"
        "${FUNKE_EXTERNAL}/concurrentqueue"
        "${FUNKE_EXTERNAL}/miniaudio"
        "${FUNKE_EXTERNAL}/dotnet"
        ${ARG_WEBGPU_INCLUDE_DIRS})
    target_precompile_headers(FunkeEngine PRIVATE "${FUNKE_ROOT}/Funke-Engine/src/pch.hpp")

    if(ARG_CONFIGURATION STREQUAL "Release")
        set(_config_defines FNK_DIST NDEBUG FUNKE_WITH_EDITOR=0)
    else()
        set(_config_defines FNK_RELEASE NDEBUG FUNKE_WITH_EDITOR=0)
    endif()
    target_compile_definitions(FunkeEngine PUBLIC
        FNK_RHI_WEBGPU
        WEBGPU_CPP_HAS_ENUM_CLASS_BITMASKS=1
        FUNKE_ALL_MODULES=1
        ${_module_defines}
        FUNKE_ENTITY_BITS=${ARG_ENTITY_BITS}
        MAGIC_ENUM_RANGE_MIN=-1
        MAGIC_ENUM_RANGE_MAX=32
        ${_config_defines})

    target_link_libraries(FunkeEngine PUBLIC FunkeImGui ${_physics_targets} ${ARG_SDL3_TARGET} ${ARG_WEBGPU_TARGET})
    if(NOT EMSCRIPTEN AND NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(FunkeEngine PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    endif()
endfunction()

# Adds the shipped runtime (Funke-Runtime) to an executable or shared-library target. The platform
# package defines main() and calls FunkeRuntimeMain / FunkeRuntimeBegin (see EntryPoint.hpp).
function(funke_add_runtime target)
    file(GLOB _runtime_sources CONFIGURE_DEPENDS "${FUNKE_ROOT}/Funke-Runtime/src/*.cpp")
    file(GLOB _diagnostics_sources CONFIGURE_DEPENDS "${FUNKE_ROOT}/Funke-Engine/src/Diagnostics/*.cpp")
    target_sources(${target} PRIVATE ${_runtime_sources} ${_diagnostics_sources})
    target_include_directories(${target} PRIVATE "${FUNKE_ROOT}/Funke-Runtime/src")
    target_compile_definitions(${target} PRIVATE FUNKE_CUSTOM_MAIN)
    _funke_apply_common_options(${target})
    target_link_libraries(${target} PRIVATE FunkeEngine)
endfunction()

# Items of a JSON array (path given as the remaining arguments) as a CMake list.
function(_funke_json_list json out_var)
    string(JSON _count LENGTH "${json}" ${ARGN})
    set(_items)
    if(_count GREATER 0)
        math(EXPR _last "${_count} - 1")
        foreach(_index RANGE ${_last})
            string(JSON _item GET "${json}" ${ARGN} ${_index})
            list(APPEND _items "${_item}")
        endforeach()
    endif()
    set(${out_var} "${_items}" PARENT_SCOPE)
endfunction()

# The game packages' native layers, linked statically into <target>: these players cannot load package
# DLLs. The JSON comes from funke_platform_sdk.native_packages. Each package becomes a STATIC library
# named like its [DllImport] library (Pkg.<Name>.Native, in <build>/packages/) so the Web relink can scan
# it, compiled with its own FunkePackage_OnLoad/OnUnload names. The registry source it lists defines
# FunkeRegisterStaticPackages(), which the player calls before the engine starts; without JSON the
# function is added empty.
function(funke_add_native_packages target)
    cmake_parse_arguments(ARG "" "JSON" "" ${ARGN})
    if(NOT ARG_JSON)
        set(_registry "${CMAKE_CURRENT_BINARY_DIR}/FunkeStaticPackages.cpp")
        file(CONFIGURE OUTPUT "${_registry}" CONTENT "// No native packages.\nextern \"C\" void FunkeRegisterStaticPackages() {}\n")
        target_sources(${target} PRIVATE "${_registry}")
        return()
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ARG_JSON}")
    file(READ "${ARG_JSON}" _json)
    string(JSON _registry GET "${_json}" registry)
    target_sources(${target} PRIVATE "${_registry}")
    target_include_directories(${target} PRIVATE "${FUNKE_ROOT}/Funke-Engine/src")

    string(JSON _count LENGTH "${_json}" packages)
    if(_count EQUAL 0)
        return()
    endif()
    math(EXPR _last "${_count} - 1")
    foreach(_index RANGE ${_last})
        string(JSON _package GET "${_json}" packages ${_index} target)
        string(JSON _id GET "${_json}" packages ${_index} id)
        string(JSON _links_engine GET "${_json}" packages ${_index} engine)
        _funke_json_list("${_json}" _sources packages ${_index} sources)
        _funke_json_list("${_json}" _includes packages ${_index} includes)

        add_library(${_package} STATIC ${_sources})
        set_target_properties(${_package} PROPERTIES
            PREFIX "" OUTPUT_NAME "${_package}"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
        _funke_apply_common_options(${_package})
        target_include_directories(${_package} PRIVATE ${_includes})
        target_compile_definitions(${_package} PRIVATE
            FunkePackage_OnLoad=FunkePackage_OnLoad_${_id}
            FunkePackage_OnUnload=FunkePackage_OnUnload_${_id})
        if(_links_engine)
            target_link_libraries(${_package} PUBLIC FunkeEngine)
        endif()
        target_link_libraries(${target} PRIVATE ${_package})
        message(STATUS "Funke native package: ${_package} (${_count} total)")
    endforeach()
endfunction()
