# iOS app bundle. Configured by Builder/build.py with the Xcode generator.
cmake_minimum_required(VERSION 3.24)
project(FunkeIOSPlayer C CXX OBJC OBJCXX)

foreach(required FUNKE_ROOT FUNKE_MONO_PACKAGE FUNKE_MONO_PACK_NATIVE FUNKE_SDL3_DIR FUNKE_DAWN_LIBRARY
                 FUNKE_DAWN_INCLUDE_DIRS FUNKE_CONTENT_DIR FUNKE_MANAGED_DIR FUNKE_BUNDLE_ID FUNKE_APP_NAME)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be passed by the iOS builder")
    endif()
endforeach()

include("${FUNKE_ROOT}/cmake/FunkeEngine.cmake")
include("${FUNKE_MONO_PACKAGE}/cmake/FunkeMono.cmake")

add_library(SDL3::SDL3 STATIC IMPORTED)
set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${FUNKE_SDL3_DIR}/lib/libSDL3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${FUNKE_SDL3_DIR}/include"
    INTERFACE_LINK_LIBRARIES "-framework AudioToolbox;-framework AVFoundation;-framework CoreAudio;-framework CoreBluetooth;-framework CoreGraphics;-framework CoreHaptics;-framework CoreMotion;-framework Foundation;-framework GameController;-framework Metal;-framework QuartzCore;-framework UIKit;-framework UniformTypeIdentifiers")

add_library(FunkeWebGPU STATIC IMPORTED)
set_target_properties(FunkeWebGPU PROPERTIES
    IMPORTED_LOCATION "${FUNKE_DAWN_LIBRARY}"
    INTERFACE_LINK_LIBRARIES "-framework Metal;-framework QuartzCore;-framework IOSurface;-framework Foundation")

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

add_executable(FunkeGame MACOSX_BUNDLE "${CMAKE_CURRENT_LIST_DIR}/iOSMain.mm")
funke_add_runtime(FunkeGame)
# The game packages' native layers (FUNKE_NATIVE_PACKAGES: native-packages.json from the builder).
funke_add_native_packages(FunkeGame JSON "${FUNKE_NATIVE_PACKAGES}")
funke_link_mono(FunkeGame PACK_NATIVE_DIR "${FUNKE_MONO_PACK_NATIVE}" MODE STATIC)
target_link_libraries(FunkeGame PRIVATE "-lz" "-liconv" "-lc++")

set_target_properties(FunkeGame PROPERTIES
    OUTPUT_NAME "${FUNKE_EXECUTABLE_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/../Template/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${FUNKE_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "${FUNKE_APP_NAME}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${FUNKE_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${FUNKE_BUILD_NUMBER}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${FUNKE_BUNDLE_ID}"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${FUNKE_DEVELOPMENT_TEAM}")

# Game content and managed assemblies live in the bundle root, next to the executable.
add_custom_command(TARGET FunkeGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${FUNKE_CONTENT_DIR}" "$<TARGET_BUNDLE_CONTENT_DIR:FunkeGame>/game"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${FUNKE_MANAGED_DIR}" "$<TARGET_BUNDLE_CONTENT_DIR:FunkeGame>/managed"
    VERBATIM)
