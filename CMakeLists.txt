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


# Android, iOS, macOS and Windows: FFmpeg is embedded into the mod.
# Replaces the eclipse.ffmpeg-api dependency on all platforms.
#
# Package layout:
#   include/libavcodec/...
#   include/libavformat/...
#   include/libavutil/...
#
#   lib/arm64-v8a/
#   lib/armeabi-v7a/
#   lib/ios-arm64/
#   lib/macos-universal/
#   lib/windows-x64/
#
# Required libraries:
#   avformat, avcodec, avfilter, swscale, swresample, avutil
#
# Optional libraries:
#   aom, vpx, x264, x265
#
# Windows libraries must use the MSVC ABI and dynamic CRT (/MD).
# Windows keeps ffmpeg.exe for Legacy Render.
#
# GPL note:
# Linking GPL libraries such as x264/x265 makes the resulting
# distribution subject to GPL requirements.

set(WB_IS_IOS FALSE)
if("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(WB_IS_IOS TRUE)
endif()

set(WB_IS_MACOS FALSE)
if(APPLE AND NOT WB_IS_IOS)
    set(WB_IS_MACOS TRUE)
endif()

if(ANDROID OR WB_IS_IOS OR WB_IS_MACOS OR WIN32)

    # Enable embedded FFmpeg backend.
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WB_FFMPEG_EMBEDDED
    )

    # Select FFmpeg platform target.
    if(ANDROID)
        if(NOT ANDROID_ABI STREQUAL "arm64-v8a"
           AND NOT ANDROID_ABI STREQUAL "armeabi-v7a")
            message(FATAL_ERROR
                "Unsupported Android ABI '${ANDROID_ABI}'. "
                "Expected arm64-v8a or armeabi-v7a."
            )
        endif()

        set(WB_FFMPEG_TARGET "${ANDROID_ABI}")

    elseif(WB_IS_IOS)
        set(WB_FFMPEG_TARGET "ios-arm64")

    elseif(WIN32)
        set(WB_FFMPEG_TARGET "windows-x64")

    else()
        set(WB_FFMPEG_TARGET "macos-universal")
    endif()

    # Default FFmpeg package.
    set(
        WB_FFMPEG_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg"
        CACHE PATH
        "Prebuilt FFmpeg package directory"
    )

    set(
        WB_FFMPEG_URL
        ""
        CACHE STRING
        "URL of the prebuilt FFmpeg package"
    )

    # Default to the official White Bot FFmpeg v1.0.0 package.
    # A URL passed through -DWB_FFMPEG_URL takes priority.
    if("${WB_FFMPEG_URL}" STREQUAL "")
        set(
            WB_FFMPEG_URL
            "https://github.com/NeonGD1667/White-Bot-FFMPEG/releases/download/v1.0.0/white-bot-ffmpeg-8.1.2-r1-gpl.tar.gz"
        )
    endif()

    message(STATUS "White Bot FFmpeg target: ${WB_FFMPEG_TARGET}")
    message(STATUS "White Bot FFmpeg directory: ${WB_FFMPEG_DIR}")

    # Download and extract FFmpeg if it is not available locally.
    if(NOT EXISTS "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}")

        if("${WB_FFMPEG_URL}" STREQUAL "")
            message(FATAL_ERROR
                "FFmpeg package not found. "
                "Set WB_FFMPEG_DIR or WB_FFMPEG_URL."
            )
        endif()

        message(STATUS "Downloading White Bot FFmpeg package...")
        message(STATUS "FFmpeg URL: ${WB_FFMPEG_URL}")

        CPMAddPackage(
            NAME wb_ffmpeg
            URL "${WB_FFMPEG_URL}"
            DOWNLOAD_ONLY YES
        )

        if(NOT wb_ffmpeg_SOURCE_DIR)
            message(FATAL_ERROR
                "Failed to download or extract White Bot FFmpeg package."
            )
        endif()

        set(WB_FFMPEG_DIR "${wb_ffmpeg_SOURCE_DIR}")

        message(STATUS
            "FFmpeg package extracted to: ${WB_FFMPEG_DIR}"
        )
    endif()

    # Validate package directory.
    if(NOT EXISTS "${WB_FFMPEG_DIR}/include")
        message(FATAL_ERROR
            "FFmpeg headers not found: ${WB_FFMPEG_DIR}/include"
        )
    endif()

    if(NOT EXISTS "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}")
        message(FATAL_ERROR
            "Missing prebuilt FFmpeg for ${WB_FFMPEG_TARGET}: "
            "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}"
        )
    endif()

    # Find static FFmpeg libraries.
    function(wb_find_ffmpeg_lib out name)
        set(found "")

        foreach(candidate
            "lib${name}.a"
            "lib${name}.lib"
            "${name}.lib"
        )
            if(EXISTS
                "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}/${candidate}"
            )
                set(
                    found
                    "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}/${candidate}"
                )
                break()
            endif()
        endforeach()

        set(${out} "${found}" PARENT_SCOPE)
    endfunction()

    set(WB_FFMPEG_STATIC)

    # Required libraries.
    foreach(lib IN ITEMS
        avformat
        avcodec
        avfilter
        swscale
        swresample
        avutil
    )
        wb_find_ffmpeg_lib(lib_path "${lib}")

        if("${lib_path}" STREQUAL "")
            message(FATAL_ERROR
                "Missing required FFmpeg library '${lib}' in "
                "${WB_FFMPEG_DIR}/lib/${WB_FFMPEG_TARGET}"
            )
        endif()

        list(APPEND WB_FFMPEG_STATIC "${lib_path}")
    endforeach()

    # Optional codec libraries.
    foreach(lib IN ITEMS aom vpx x264 x265)
        wb_find_ffmpeg_lib(lib_path "${lib}")

        if(NOT "${lib_path}" STREQUAL "")
            list(APPEND WB_FFMPEG_STATIC "${lib_path}")
        endif()
    endforeach()

    message(STATUS "FFmpeg static libraries:")
    foreach(lib_path IN LISTS WB_FFMPEG_STATIC)
        message(STATUS "  ${lib_path}")
    endforeach()

    # FFmpeg source files.
    target_sources(${PROJECT_NAME} PRIVATE
        src/renderer/ffmpeg/recorder.cpp
        src/renderer/ffmpeg/audio_mixer.cpp
        src/renderer/ffmpeg/ffmpeg_utils.cpp
    )

    # Use headers matching the prebuilt libraries.
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${WB_FFMPEG_DIR}/include"
    )

    # Platform-specific linking.
    if(ANDROID)

        target_link_libraries(${PROJECT_NAME}
            -Wl,--start-group
            ${WB_FFMPEG_STATIC}
            -Wl,--end-group
            z
            android
            mediandk
            log
        )

        # Hide FFmpeg symbols to prevent conflicts with other mods.
        target_link_options(${PROJECT_NAME} PRIVATE
            -Wl,--exclude-libs,ALL
        )

        if(ANDROID_ABI STREQUAL "armeabi-v7a")

            # 64-bit file offsets on 32-bit ARM.
            target_compile_definitions(${PROJECT_NAME} PRIVATE
                __USE_FILE_OFFSET64
            )

        else()

            # Android 16 KB page-size compatibility.
            target_link_options(${PROJECT_NAME} PRIVATE
                -Wl,-z,max-page-size=16384
            )

        endif()

    elseif(WIN32)

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

        # iOS / macOS.
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

    message(STATUS
        "White Bot FFmpeg successfully configured for ${WB_FFMPEG_TARGET}"
    )

endif()
