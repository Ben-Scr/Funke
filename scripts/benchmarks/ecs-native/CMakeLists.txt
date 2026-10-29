cmake_minimum_required(VERSION 3.20)
project(EcsNativeComparison-NativeScripts LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(${PROJECT_NAME} SHARED NativeEcsBenchmark.cpp NativeScriptExports.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${FUNKE_DIR}/Funke-Engine/src"
    "${FUNKE_DIR}/Funke-Engine/src/Generated"
    "${FUNKE_DIR}/External/dawn/include"
    "${FUNKE_DIR}/External/dawn/build/gen/include"
    "${FUNKE_DIR}/External/spdlog/include"
    "${FUNKE_DIR}/External/glm"
    "${FUNKE_DIR}/External/entt/src"
    "${FUNKE_DIR}/External/stb"
    "${FUNKE_DIR}/External/magic_enum/include"
    "${FUNKE_DIR}/External/rapidxml"
    "${FUNKE_DIR}/External/SDL/include"
    "${FUNKE_DIR}/External/miniaudio"
    "${FUNKE_DIR}/External/box2d/include"
    "${FUNKE_DIR}/External/dotnet")
target_compile_definitions(${PROJECT_NAME} PRIVATE
    FNK_IMPORT_DLL FNK_PLATFORM_WINDOWS FUNKE_ALL_MODULES=1
    FUNKE_WITH_2D=1 FUNKE_WITH_3D=1 FUNKE_WITH_EDITOR=1
    FNK_RELEASE NDEBUG FUNKE_BUILD_DEVELOPMENT NOMINMAX)
target_link_directories(${PROJECT_NAME} PRIVATE "${FUNKE_ENGINE_LIBRARY_DIR}")
target_link_libraries(${PROJECT_NAME} PRIVATE Funke-Engine)
target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /O2)
set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${FUNKE_NATIVE_OUTPUT_DIR}")
