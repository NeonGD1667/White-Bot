cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform architectures
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(WhiteBot 
    VERSION 3.2.0
    LANGUAGES CXX
)

# This mod is built with clang everywhere (GitHub Actions already does). cl.exe is not supported:
# the old MSVC-only block below forced /Od (no optimization) and disabled LTO.
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(WARNING
        "WhiteBot is built with clang. cl.exe is not supported; configure with clang/clang-cl instead."
    )
endif()

# Read mod ID from mod.json
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mod.json" MOD_JSON)
string(JSON MOD_ID GET "${MOD_JSON}" id)

# Mod source files
add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/global.cpp
    src/pathfinder.cpp
    src/macro.cpp
    src/keybinds.cpp
    src/utils/utils.cpp
    src/replay/gdr.cpp
    src/replay/legacy/legacy_gdr.cpp
    src/renderer/renderer.cpp
    src/download/version_manager.cpp

    src/hacks/other.cpp
    src/hacks/tps_bypass.cpp
    src/hacks/frame_stepper.cpp
    src/hacks/layout_mode.cpp
    src/hacks/show_trajectory.cpp
    src/hacks/coin_finder.cpp
    src/hacks/clickbot.cpp
    src/hacks/autoclicker.cpp
    src/hacks/fake_taps.cpp
    src/hacks/noclip_tint.cpp

    src/ui/macro_editor.cpp
    src/ui/macro_timeline_layer.cpp
    src/ui/button_edit_layer.cpp
    src/ui/game_ui.cpp
    src/ui/record_layer.cpp
    src/ui/render_settings_layer.cpp
    src/ui/load_macro_layer.cpp
    src/ui/macro_cell.cpp
    src/ui/clickbot_layer.cpp
    src/ui/button_setting.cpp

    src/practice_fixes/input.cpp
    src/practice_fixes/player.cpp
    src/practice_fixes/play_layer.cpp
)

# Locate Geode SDK
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR
        "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode"
    )
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory(
    "$ENV{GEODE_SDK}"
    "${CMAKE_CURRENT_BINARY_DIR}/geode"
)

setup_geode_mod(${PROJECT_NAME})

# Android, iOS, macOS and Windows: ffmpeg is embedded into the mod (replaces the eclipse.ffmpeg-api mod, which White Bot no longer uses on any platform).
# Needs a prebuilt package with this layout (FFmpeg 8.x, headers must match the libs):
#   include/libavcodec/... include/libavutil/... (all ffmpeg headers)
#   lib/<target>/lib{avformat,avcodec,avfilter,swscale,swresample,avutil}.a   (required)
#   lib/<target>/lib{aom,vpx,x264,x265}.a                                      (optional extras)
# <target> is arm64-v8a / armeabi-v7a (Android), ios-arm64 (iOS), macos-universal (macOS,
# arm64 + x86_64 merged with lipo; this project builds both architectures) or windows-x64.
# Windows: static libs built for the MSVC ABI with the dynamic CRT (/MD), named lib<name>.lib
# or <name>.lib. Windows keeps the old ffmpeg.exe render as "Legacy Render" (mod setting).
# Either extract it into ./ffmpeg, set -DWB_FFMPEG_DIR=..., or set -DWB_FFMPEG_URL=<.tar.gz>.
# NOTE: libx264/libx265 are GPL; a build linked against them makes the mod GPL. An LGPL build
# can encode with MediaCodec (Android) / VideoToolbox (iOS) instead.
set(WB_IS_IOS FALSE)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(WB_IS_IOS TRUE)
endif()

set(WB_IS_MACOS FALSE)
if (APPLE AND NOT WB_IS_IOS)
    set(WB_IS_MACOS TRUE)
endif()

if (ANDROID OR WB_IS_IOS OR WB_IS_MACOS OR WIN32)
    # Tells the sources to use the embedded ffmpeg (src/renderer/ffmpeg/backend.hpp).
    target_compile_definitions(${PROJECT_NAME} PRIVATE WB_FFMPEG_EMBEDDED)

    if (ANDROID)
        if (NOT ANDROID_ABI STREQUAL "arm64-v8a" AND NOT ANDROID_ABI STREQUAL "armeabi-v7a")
            message(FATAL_ERROR "Unsupported Android ABI '${ANDROID_ABI}', expected arm64-v8a or armeabi-v7a.")
        endif()
        set(WB_FFMPEG_TARGET "${ANDROID_ABI}")
    elseif (WB_IS_IOS)
        set(WB_FFMPEG_TARGET "ios-arm64")
    elseif (WIN32)
        set(WB_FFMPEG_TARGET "windows-x64")
    else()
        set(WB_FFMPEG_TARGET "macos-universal")
    endif()

    set(WB_FFMPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg" CACHE PATH "Prebuilt ffmpeg package (include/ + lib/<target>/)")
    set(WB_FFMPEG_URL "" CACHE STRING "URL of the prebuilt ffmpeg package (.tar.gz)")

    if (NOT EXISTS "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}" AND NOT "${WB_FFMPEG_URL}" STREQUAL "")
        CPMAddPackage(
            NAME wb_ffmpeg
            URL "${WB_FFMPEG_URL}"
            DOWNLOAD_ONLY YES
        )
        set(WB_FFMPEG_DIR "${wb_ffmpeg_SOURCE_DIR}")
    endif()

    if (NOT EXISTS "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}")
        message(FATAL_ERROR
            "Missing prebuilt ffmpeg for ${WB_FFMPEG_TARGET}: expected ${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}. "
            "Set -DWB_FFMPEG_DIR=<dir> or -DWB_FFMPEG_URL=<package .tar.gz>."
        )
    endif()

    # lib<name>.a (Android/Apple/mingw-style) or lib<name>.lib / <name>.lib (Windows)
    function(wb_find_ffmpeg_lib out name)
        set(found "")
        foreach (candidate "lib${name}.a" "lib${name}.lib" "${name}.lib")
            if (EXISTS "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}/${candidate}")
                set(found "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}/${candidate}")
                break()
            endif()
        endforeach()
        set(${out} "${found}" PARENT_SCOPE)
    endfunction()

    set(WB_FFMPEG_STATIC)
    foreach (lib IN ITEMS avformat avcodec avfilter swscale swresample avutil)
        wb_find_ffmpeg_lib(lib_path ${lib})
        if ("${lib_path}" STREQUAL "")
            message(FATAL_ERROR "Missing ffmpeg library '${lib}' in ${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}")
        endif()
        list(APPEND WB_FFMPEG_STATIC "${lib_path}")
    endforeach()
    foreach (lib IN ITEMS aom vpx x264 x265)
        wb_find_ffmpeg_lib(lib_path ${lib})
        if (NOT "${lib_path}" STREQUAL "")
            list(APPEND WB_FFMPEG_STATIC "${lib_path}")
        endif()
    endforeach()

    target_sources(${PROJECT_NAME} PRIVATE
        src/renderer/ffmpeg/recorder.cpp
        src/renderer/ffmpeg/audio_mixer.cpp
        src/renderer/ffmpeg/ffmpeg_utils.cpp
    )

    # Only the prebuilt package's headers (they match the libs), not src/renderer/ffmpeg/libav*.
    target_include_directories(${PROJECT_NAME} PRIVATE "${WB_FFMPEG_DIR}/include")

    if (ANDROID)
        target_link_libraries(${PROJECT_NAME}
            -Wl,--start-group ${WB_FFMPEG_STATIC} -Wl,--end-group
            z android mediandk log
        )

        # Keep ffmpeg symbols private so they never clash with another mod that bundles ffmpeg.
        target_link_options(${PROJECT_NAME} PRIVATE -Wl,--exclude-libs,ALL)

        if (ANDROID_ABI STREQUAL "armeabi-v7a")
            # 64-bit file offsets on 32-bit ARM; also needs -DANDROID_PLATFORM=24.
            target_compile_definitions(${PROJECT_NAME} PRIVATE __USE_FILE_OFFSET64)
        else()
            target_link_options(${PROJECT_NAME} PRIVATE -Wl,-z,max-page-size=16384)
        endif()
    elseif (WIN32)
        # Windows system libraries pulled in by a static ffmpeg (Media Foundation, crypto, sockets).
        target_link_libraries(${PROJECT_NAME}
            ${WB_FFMPEG_STATIC}
            ws2_32 secur32 bcrypt ole32 user32 uuid strmiids mfuuid mfplat ncrypt shlwapi crypt32
        )
    else()
        # iOS / macOS: VideoToolbox is the hardware encoder; the rest is what libavcodec/avformat pull in.
        target_link_libraries(${PROJECT_NAME}
            ${WB_FFMPEG_STATIC}
            z bz2 iconv c++
            "-framework VideoToolbox"
            "-framework CoreMedia"
            "-framework CoreVideo"
            "-framework CoreFoundation"
            "-framework AudioToolbox"
            "-framework Security"
        )
    endif()
endif()

# iOS only: GeneratedSource.cpp (in Geode's GeodeBindings target) uses FMOD_* C
# types without including an FMOD header. Force-include fmod.hpp for that target
# ONLY. A global CMAKE_CXX_FLAGS would also hit fmt/asp2/arc, which have no
# Geode include path, and fail with "file not found".
# Remove this block once geode-sdk/bindings fixes it upstream (PR #1390).
if (("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS) AND TARGET GeodeBindings)
    target_compile_options(GeodeBindings PRIVATE -include Geode/fmod/fmod.hpp)
endif()

# nlohmann/json
CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    GIT_TAG v3.11.3
)

target_link_libraries(${PROJECT_NAME}
    nlohmann_json::nlohmann_json
)

# SLC include root
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/replay/slc"
)

# Deploy packaged mod to local Geometry Dash mods folder on Windows
if (WIN32)
    set(
        DESKTOP_GEODE_MODS_DIR
        "C:/Users/drean/OneDrive/Desktop/GeometryDash/geode/mods"
    )

    add_custom_target(${PROJECT_NAME}_DEPLOY_DESKTOP ALL
        DEPENDS ${PROJECT_NAME}_PACKAGE

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${DESKTOP_GEODE_MODS_DIR}"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/${MOD_ID}.geode"
            "${DESKTOP_GEODE_MODS_DIR}/${MOD_ID}.geode"

        VERBATIM
    )
endif()