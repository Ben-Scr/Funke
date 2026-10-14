# iOS app bundle. Configured by Builder/build.py with the Xcode generator.
cmake_minimum_required(VERSION 3.24)
project(IndexIOSPlayer C CXX OBJC OBJCXX)

foreach(required INDEX_ROOT INDEX_MONO_PACKAGE INDEX_MONO_PACK_NATIVE INDEX_SDL3_DIR INDEX_DAWN_LIBRARY
                 INDEX_DAWN_INCLUDE_DIRS INDEX_CONTENT_DIR INDEX_MANAGED_DIR INDEX_BUNDLE_ID INDEX_APP_NAME)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be passed by the iOS builder")
    endif()
endforeach()

include("${INDEX_ROOT}/cmake/IndexEngine.cmake")
include("${INDEX_MONO_PACKAGE}/cmake/IndexMono.cmake")

add_library(SDL3::SDL3 STATIC IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${INDEX_SDL3_DIR}/lib/libSDL3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${INDEX_SDL3_DIR}/include"
    INTERFACE_LINK_LIBRARIES "-framework AudioToolbox;-framework AVFoundation;-framework CoreAudio;-framework CoreBluetooth;-framework CoreGraphics;-framework CoreHaptics;-framework CoreMotion;-framework Foundation;-framework GameController;-framework Metal;-framework QuartzCore;-framework UIKit;-framework UniformTypeIdentifiers")

add_library(IndexWebGPU STATIC IMPORTED)
set_target_properties(IndexWebGPU PROPERTIES
    IMPORTED_LOCATION "${INDEX_DAWN_LIBRARY}"
    INTERFACE_LINK_LIBRARIES "-framework Metal;-framework QuartzCore;-framework IOSurface;-framework Foundation")

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

add_executable(IndexGame MACOSX_BUNDLE "${CMAKE_CURRENT_LIST_DIR}/iOSMain.mm")
index_add_runtime(IndexGame)
index_link_mono(IndexGame PACK_NATIVE_DIR "${INDEX_MONO_PACK_NATIVE}" MODE STATIC)
target_link_libraries(IndexGame PRIVATE "-lz" "-liconv" "-lc++")

set_target_properties(IndexGame PROPERTIES
    OUTPUT_NAME "${INDEX_EXECUTABLE_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/../Template/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${INDEX_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "${INDEX_APP_NAME}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${INDEX_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${INDEX_BUILD_NUMBER}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${INDEX_BUNDLE_ID}"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${INDEX_DEVELOPMENT_TEAM}")

# Game content and managed assemblies live in the bundle root, next to the executable.
add_custom_command(TARGET IndexGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${INDEX_CONTENT_DIR}" "$<TARGET_BUNDLE_CONTENT_DIR:IndexGame>/game"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${INDEX_MANAGED_DIR}" "$<TARGET_BUNDLE_CONTENT_DIR:IndexGame>/managed"
    VERBATIM)
