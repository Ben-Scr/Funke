# Links the Mono runtime and the Index Mono backend into a game target.
#
#   include(<Index.ScriptRuntime.Mono>/cmake/IndexMono.cmake)
#   index_link_mono(<target> PACK_NATIVE_DIR <runtime pack>/runtimes/<rid>/native MODE SHARED|STATIC)
#
# SHARED links libmonosgen-2.0.so (Android ships the runtime and its native BCL helpers as .so files).
# STATIC links the .a archives (iOS, Web) together with the interpreter and the component stubs.

set(_INDEX_MONO_PACKAGE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

function(index_link_mono target)
    cmake_parse_arguments(ARG "" "PACK_NATIVE_DIR;MODE" "" ${ARGN})
    if(NOT ARG_PACK_NATIVE_DIR OR NOT EXISTS "${ARG_PACK_NATIVE_DIR}/include/mono-2.0")
        message(FATAL_ERROR "index_link_mono: PACK_NATIVE_DIR must point at <runtime pack>/runtimes/<rid>/native")
    endif()

    target_sources(${target} PRIVATE "${_INDEX_MONO_PACKAGE_DIR}/Native/MonoScriptRuntime.cpp")
    target_include_directories(${target} PRIVATE
        "${ARG_PACK_NATIVE_DIR}/include/mono-2.0"
        "${_INDEX_MONO_PACKAGE_DIR}/Native")

    set(_dir "${ARG_PACK_NATIVE_DIR}")
    if(ARG_MODE STREQUAL "SHARED")
        target_link_libraries(${target} PRIVATE "${_dir}/libmonosgen-2.0.so")
    else()
        # Component archives register themselves through constructor tables; keep every object.
        set(_whole
            "${_dir}/libmono-component-marshal-ilgen-static.a"
            "${_dir}/libmono-component-hot_reload-stub-static.a"
            "${_dir}/libmono-component-debugger-stub-static.a"
            "${_dir}/libmono-component-diagnostics_tracing-stub-static.a")
        set(_libs
            "${_dir}/libmonosgen-2.0.a"
            "${_dir}/libmono-ee-interp.a"
            "${_dir}/libSystem.Native.a"
            "${_dir}/libSystem.IO.Compression.Native.a"
            "${_dir}/libSystem.Globalization.Native.a")
        foreach(_optional libmono-icall-table.a libmono-wasm-eh-wasm.a libmono-wasm-simd.a libz.a libzstd.a
                          libbrotlienc.a libbrotlidec.a libbrotlicommon.a)
            if(EXISTS "${_dir}/${_optional}")
                list(APPEND _libs "${_dir}/${_optional}")
            endif()
        endforeach()
        if(APPLE)
            # The PInvoke resolver looks SystemNative_* up with dlsym; dead-stripping must not drop them.
            list(APPEND _whole "${_dir}/libSystem.Native.a" "${_dir}/libSystem.IO.Compression.Native.a" "${_dir}/libSystem.Globalization.Native.a")
            list(REMOVE_ITEM _libs "${_dir}/libSystem.Native.a" "${_dir}/libSystem.IO.Compression.Native.a" "${_dir}/libSystem.Globalization.Native.a")
            foreach(_lib ${_whole})
                target_link_options(${target} PRIVATE "LINKER:-force_load,${_lib}")
            endforeach()
        else()
            target_link_libraries(${target} PRIVATE "-Wl,--whole-archive" ${_whole} "-Wl,--no-whole-archive")
        endif()
        target_link_libraries(${target} PRIVATE ${_libs})
    endif()
endfunction()
