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
    VERSION 3.1.0
    LANGUAGES CXX
)

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
    src/gdr/gdr.cpp
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gdr/slc"
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

# MSVC-specific compiler settings
if (MSVC)
    # Disable Link-Time Code Generation (WholeProgramOptimization)
    # which can cause CL.exe crashes.
    set_property(
        TARGET ${PROJECT_NAME}
        PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE
    )

    set_property(
        TARGET ${PROJECT_NAME}
        PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE
    )

    set_property(
        TARGET ${PROJECT_NAME}
        PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
    )

    target_compile_options(${PROJECT_NAME} PRIVATE
        /Zm200
        /bigobj
        /d2ReducedOptimizeHugeFunctions
        /Od
    )
endif()