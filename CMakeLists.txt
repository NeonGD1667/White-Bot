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

# This mod is built with clang everywhere (GitHub Actions already does).
# cl.exe is not supported.
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(WARNING
        "WhiteBot is built with clang. cl.exe is not supported; "
        "configure with clang/clang-cl instead."
    )
endif()

# Read mod ID from mod.json
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mod.json" MOD_JSON)
string(JSON MOD_ID GET "${MOD_JSON}" id)

# ---------------------------------------------------------------------------
# Mod source files
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Geode SDK
# ---------------------------------------------------------------------------
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR
        "Unable to find Geode SDK! "
        "Please define GEODE_SDK environment variable to point to Geode"
    )
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory(
    "$ENV{GEODE_SDK}"
    "${CMAKE_CURRENT_BINARY_DIR}/geode"
)

setup_geode_mod(${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
# SLC replay format include root.
# (nlohmann/json is NOT taken from CMake/CPM: the sources include the vendored
#  copy src/replay/json.hpp directly, see macro_index_popup.hpp / record_layer.cpp.)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/replay/slc"
)

# ---------------------------------------------------------------------------
# Embedded FFmpeg (Android, iOS, macOS and Windows)
# ---------------------------------------------------------------------------
# Replaces the eclipse.ffmpeg-api dependency on every platform.
# Windows additionally keeps ffmpeg.exe for "Legacy Render".
#
# Package layout (built by https://github.com/NeonGD1667/White-Bot-FFMPEG):
#   include/libavcodec/... include/libavformat/... include/libavutil/... (FFmpeg headers)
#   lib/arm64-v8a/        Android arm64
#   lib/armeabi-v7a/      Android armv7
#   lib/ios-arm64/        iOS
#   lib/macos-universal/  macOS (arm64 + x86_64)
#   lib/windows-x64/      Windows (MSVC ABI, dynamic CRT /MD)
#
# Required libraries: avformat avcodec avfilter swscale swresample avutil
# Optional libraries: aom vpx x264 x265 (linked only if present)
#
# GPL note: linking GPL libraries such as x264/x265 makes the resulting
# distribution subject to the GPL.
#
# Package selection, in this order:
#   1. -DWB_FFMPEG_DIR=<dir> (default: <source dir>/ffmpeg) if it contains lib/<target>
#   2. -DWB_FFMPEG_URL=<package .tar.gz>
#   3. the default release URL below

set(WB_IS_IOS FALSE)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(WB_IS_IOS TRUE)
endif()

set(WB_IS_MACOS FALSE)
if (APPLE AND NOT WB_IS_IOS)
    set(WB_IS_MACOS TRUE)
endif()

if (ANDROID OR WB_IS_IOS OR WB_IS_MACOS OR WIN32)

    # Tells the sources to use the embedded FFmpeg (src/renderer/ffmpeg/backend.hpp).
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WB_FFMPEG_EMBEDDED
    )

    # Platform folder inside the package.
    if (ANDROID)
        if (NOT ANDROID_ABI STREQUAL "arm64-v8a"
            AND NOT ANDROID_ABI STREQUAL "armeabi-v7a")
            message(FATAL_ERROR
                "Unsupported Android ABI '${ANDROID_ABI}'. "
                "Expected arm64-v8a or armeabi-v7a."
            )
        endif()

        set(WB_FFMPEG_TARGET "${ANDROID_ABI}")
    elseif (WB_IS_IOS)
        set(WB_FFMPEG_TARGET "ios-arm64")
    elseif (WIN32)
        set(WB_FFMPEG_TARGET "windows-x64")
    else()
        set(WB_FFMPEG_TARGET "macos-universal")
    endif()

    set(WB_FFMPEG_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg"
        CACHE PATH
        "Prebuilt FFmpeg package directory (include/ + lib/<target>/)"
    )

    set(WB_FFMPEG_URL
        ""
        CACHE STRING
        "URL of the prebuilt FFmpeg package (.tar.gz)"
    )

    # Default: the official White Bot FFmpeg v1.0.0 package (GPL build, with x264).
    # A URL passed through -DWB_FFMPEG_URL takes priority.
    if ("${WB_FFMPEG_URL}" STREQUAL "")
        set(WB_FFMPEG_URL
            "https://github.com/NeonGD1667/White-Bot-FFMPEG/releases/download/v1.0.0/white-bot-ffmpeg-8.1.2-r1-gpl.tar.gz"
        )
    endif()

    message(STATUS "White Bot FFmpeg target: ${WB_FFMPEG_TARGET}")
    message(STATUS "White Bot FFmpeg directory: ${WB_FFMPEG_DIR}")

    # Download and extract the package if it is not available locally.
    if (NOT EXISTS "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}")
        if ("${WB_FFMPEG_URL}" STREQUAL "")
            message(FATAL_ERROR
                "FFmpeg package not found. Set WB_FFMPEG_DIR or WB_FFMPEG_URL."
            )
        endif()

        message(STATUS "Downloading White Bot FFmpeg package: ${WB_FFMPEG_URL}")

        CPMAddPackage(
            NAME wb_ffmpeg
            URL "${WB_FFMPEG_URL}"
            DOWNLOAD_ONLY YES
        )

        if (NOT wb_ffmpeg_SOURCE_DIR)
            message(FATAL_ERROR
                "Failed to download or extract the White Bot FFmpeg package."
            )
        endif()

        set(WB_FFMPEG_DIR "${wb_ffmpeg_SOURCE_DIR}")
        message(STATUS "FFmpeg package extracted to: ${WB_FFMPEG_DIR}")
    endif()

    # Validate the package.
    if (NOT EXISTS "${WB_FFMPEG_DIR}/include")
        message(FATAL_ERROR "FFmpeg headers not found: ${WB_FFMPEG_DIR}/include")
    endif()

    if (NOT EXISTS "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}")
        message(FATAL_ERROR
            "Missing prebuilt FFmpeg for ${WB_FFMPEG_TARGET}: "
            "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}"
        )
    endif()

    # lib<name>.a (Android / Apple), lib<name>.lib or <name>.lib (Windows / MSVC).
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

    # Required libraries.
    foreach (lib IN ITEMS avformat avcodec avfilter swscale swresample avutil)
        wb_find_ffmpeg_lib(lib_path "${lib}")

        if ("${lib_path}" STREQUAL "")
            message(FATAL_ERROR
                "Missing required FFmpeg library '${lib}' in "
                "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}"
            )
        endif()

        list(APPEND WB_FFMPEG_STATIC "${lib_path}")
    endforeach()

    # Optional codec libraries.
    foreach (lib IN ITEMS aom vpx x264 x265)
        wb_find_ffmpeg_lib(lib_path "${lib}")

        if (NOT "${lib_path}" STREQUAL "")
            list(APPEND WB_FFMPEG_STATIC "${lib_path}")
        endif()
    endforeach()

    message(STATUS "FFmpeg static libraries:")
    foreach (lib_path IN LISTS WB_FFMPEG_STATIC)
        message(STATUS "  ${lib_path}")
    endforeach()

    # FFmpeg wrapper sources (recorder / audio mixer).
    target_sources(${PROJECT_NAME} PRIVATE
        src/renderer/ffmpeg/recorder.cpp
        src/renderer/ffmpeg/audio_mixer.cpp
        src/renderer/ffmpeg/ffmpeg_utils.cpp
    )

    # Headers must match the prebuilt libraries (not src/renderer/ffmpeg/libav*).
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${WB_FFMPEG_DIR}/include"
    )

    # Platform-specific linking.
    if (ANDROID)

        target_link_libraries(${PROJECT_NAME}
            -Wl,--start-group
            ${WB_FFMPEG_STATIC}
            -Wl,--end-group
            z
            android
            mediandk
            log
        )

        # Keep FFmpeg symbols private so they never clash with other mods.
        target_link_options(${PROJECT_NAME} PRIVATE
            -Wl,--exclude-libs,ALL
        )

        if (ANDROID_ABI STREQUAL "armeabi-v7a")
            # 64-bit file offsets on 32-bit ARM (needs ANDROID_PLATFORM >= 24).
            target_compile_definitions(${PROJECT_NAME} PRIVATE
                __USE_FILE_OFFSET64
            )
        else()
            # Android 16 KB page-size compatibility.
            target_link_options(${PROJECT_NAME} PRIVATE
                -Wl,-z,max-page-size=16384
            )
        endif()

    elseif (WIN32)

        target_link_libraries(${PROJECT_NAME}
            ${WB_FFMPEG_STATIC}
            ws2_32
            secur32
            bcrypt
            ole32
            user32
            uuid
            strmiids
            mfuuid
            mfplat
            ncrypt
            shlwapi
            crypt32
        )

    else()

        # iOS / macOS: VideoToolbox is the hardware encoder.
        target_link_libraries(${PROJECT_NAME}
            ${WB_FFMPEG_STATIC}
            z
            bz2
            iconv
            c++
            "-framework VideoToolbox"
            "-framework CoreMedia"
            "-framework CoreVideo"
            "-framework CoreFoundation"
            "-framework AudioToolbox"
            "-framework Security"
        )

    endif()

    message(STATUS "White Bot FFmpeg configured for ${WB_FFMPEG_TARGET}")

endif()
