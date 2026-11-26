cmake_minimum_required(VERSION 3.25)
cmake_policy(SET CMP0141 NEW)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
project(FunkeLoadingResourceProbe LANGUAGES CXX)
if(NOT MSVC)
    message(FATAL_ERROR "This focused source/import probe currently requires MSVC on Windows.")
endif()
get_filename_component(FUNKE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FUNKE_BINARY_ROOT "${FUNKE_ROOT}/bin/Release-windows-x86_64" CACHE PATH "Existing matching Release dependency libraries")
# This focused runner is not a replacement for Build.ps1 or its integrity/ABI gates.
# Implementation files below are built from source; remaining dependencies use the supplied engine DLL.

set(CMAKE_CXX_STANDARD 20)
file(GLOB registration_sources "${FUNKE_ROOT}/Funke-Engine/src/Scene/BuiltInComponentRegistration*.cpp")
add_executable(loading-resource-probe Main.cpp ${registration_sources}
"${FUNKE_ROOT}/Funke-Engine/src/Audio/AudioManager.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Graphics/MeshAsset.cpp"
"${FUNKE_ROOT}/Tests/Funke-Engine-Tests/src/SceneMaterializationBudgetTests.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Assets/AssetLoader.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Graphics/Texture2D.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Graphics/TextureManager.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Graphics/Text/Font.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Graphics/Text/FontManager.cpp"
"${FUNKE_ROOT}/Tests/Funke-Engine-Tests/support/LoadingResourceBackend.cpp"
"${FUNKE_ROOT}/Tests/Funke-Engine-Tests/src/LoadingResourceTests.cpp"
"${FUNKE_ROOT}/Tests/Funke-Engine-Tests/src/AssetLoaderAsyncTests.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Diagnostics/LoadingDiagnostics.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Serialization/Json.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Localization/GameLocalization.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Localization/LanguageFile.cpp"
"${FUNKE_ROOT}/Funke-Editor/src/Gui/StringTableDocument.cpp"
"${FUNKE_ROOT}/Tests/Funke-Editor-Tests/src/StringTableDocumentTests.cpp"
"${FUNKE_ROOT}/Tests/Funke-Engine-Tests/src/LoadingDiagnosticsTests.cpp"
"${FUNKE_ROOT}/Funke-Editor/src/Undo/TransformEditCommand.cpp"
"${FUNKE_ROOT}/Funke-Editor/src/Undo/InspectorEditCommand.cpp"
"${FUNKE_ROOT}/Funke-Editor/src/Undo/InspectorUndoRecorder.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Serialization/EntityReferenceSerialization.cpp"

"${FUNKE_ROOT}/Funke-Engine/src/Systems/UILayoutSystem.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Systems/TransformHierarchySystem.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Systems/ParticleUpdateSystem.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Systems/Particle3DUpdateSystem.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scene/SceneManager.cpp"
"${FUNKE_ROOT}/Funke-Editor/src/Gui/PrefabAuthoring.cpp"
"${FUNKE_ROOT}/Funke-Editor/src/Undo/AssetFileHistory.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scripting/ScriptBindingsEcb.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Serialization/PrefabTemplateCache.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Serialization/SceneSerializer.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Serialization/SceneSerializerDeserialize.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scene/SceneExtensions.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scene/Scene.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scene/Entity.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scene/ComponentAccess.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scene/ComponentRegistry.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scripting/StructFieldBytes.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Scripting/NativeEntityCommandBuffer.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Systems/Transform3DHierarchySystem.cpp"
"${FUNKE_ROOT}/Funke-Engine/src/Systems/CameraFollowRigSystem.cpp"
"${FUNKE_ROOT}/Funke-Editor/src/Undo/UndoStack.cpp"
"${FUNKE_ROOT}/Funke-Editor/src/Undo/EditorHistorySession.cpp"
)

target_include_directories(loading-resource-probe PRIVATE "${FUNKE_ROOT}/Funke-Engine/src" "${FUNKE_ROOT}/Funke-Engine/src/Generated" "${FUNKE_ROOT}/Funke-Editor/src" "${FUNKE_ROOT}/Funke-EditorRuntime/src" "${FUNKE_ROOT}/External/doctest" "${FUNKE_ROOT}/External/spdlog/include" "${FUNKE_ROOT}/External/glm" "${FUNKE_ROOT}/External/entt/src" "${FUNKE_ROOT}/External/stb" "${FUNKE_ROOT}/External/magic_enum/include" "${FUNKE_ROOT}/External/rapidxml" "${FUNKE_ROOT}/External/concurrentqueue" "${FUNKE_ROOT}/External/SDL/include" "${FUNKE_ROOT}/External/imgui" "${FUNKE_ROOT}/External/dawn/include" "${FUNKE_ROOT}/External/dawn/build/gen/include" "${FUNKE_ROOT}/External/miniaudio" "${FUNKE_ROOT}/External/box2d/include" "${FUNKE_ROOT}/External/JoltPhysics" "${FUNKE_ROOT}/External/dotnet" "${FUNKE_ROOT}/External/tracy/public")
target_compile_definitions(loading-resource-probe PRIVATE UNICODE _UNICODE FNK_PLATFORM_WINDOWS FNK_RELEASE NDEBUG SPDLOG_WCHAR_FILENAMES FNK_RHI_WEBGPU FUNKE_ALL_MODULES=1 FUNKE_WITH_EDITOR=1 FUNKE_WITH_2D=1 FUNKE_WITH_3D=1 FUNKE_WITH_PHYSICS2D=0 FUNKE_WITH_PHYSICS3D=1 FUNKE_ENTITY_BITS=20 FUNKE_PROFILER_ENABLED TRACY_ENABLE TRACY_IMPORTS TRACY_ON_DEMAND)
target_compile_options(loading-resource-probe PRIVATE /utf-8 /O2 /Gy /wd4251 /wd4275)
target_link_options(loading-resource-probe PRIVATE /OPT:REF)
target_link_libraries(loading-resource-probe PRIVATE "${FUNKE_BINARY_ROOT}/ImGui/ImGui.lib" "${FUNKE_BINARY_ROOT}/Funke-Engine/Funke-Engine.lib" "${FUNKE_BINARY_ROOT}/Box2D/Box2D.lib" "${FUNKE_BINARY_ROOT}/Tracy/Tracy.lib" "${FUNKE_BINARY_ROOT}/Jolt/Jolt.lib")

target_compile_options(loading-resource-probe PRIVATE /FI"${CMAKE_CURRENT_SOURCE_DIR}/Imports.hpp")

target_compile_definitions(loading-resource-probe PRIVATE FUNKE_LOADING_RESOURCE_HOST WEBGPU_CPP_HAS_ENUM_CLASS_BITMASKS=1)
target_link_libraries(loading-resource-probe PRIVATE "${FUNKE_ROOT}/External/dawn/build/src/dawn/native/Release/webgpu_dawn.lib")

target_link_libraries(loading-resource-probe PRIVATE dxguid OneCore)

target_sources(loading-resource-probe PRIVATE ${FUNKE_ROOT}/Funke-Engine/src/Audio/Backends/MiniaudioImplementation.cpp)
