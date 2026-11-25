# ---------------------------------------------------------------------------
# Dependency pin verification (Geode SDK v6.0.0 / GD 2.2091)
# ---------------------------------------------------------------------------
# White Bot is pinned to immutable revisions. This module fails configuration
# early when the SDK or the 2.2091 bindings do not match the audited revisions,
# instead of silently building against an alpha SDK or a moving branch.
#
#   Geode SDK  v6.0.0  -> fcbf84c15a18924df29c01636d46450f935c220c
#   bindings           -> c46bcdfc511b992200bb3818f7c1de1f41dc8fde
#
# Override (only for intentional local experiments):
#   -DWB_ALLOW_SDK_REVISION_MISMATCH=ON
#   -DWB_ALLOW_BINDINGS_REVISION_MISMATCH=ON
# ---------------------------------------------------------------------------

find_package(Git QUIET)

set(WB_EXPECTED_GEODE_VERSION "6.0.0")
set(WB_EXPECTED_GEODE_REVISION "fcbf84c15a18924df29c01636d46450f935c220c")
set(WB_EXPECTED_BINDINGS_REVISION "c46bcdfc511b992200bb3818f7c1de1f41dc8fde")

# --- Geode SDK -------------------------------------------------------------
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR
        "GEODE_SDK is not set. Point it at a Geode SDK v6.0.0 checkout "
        "(commit ${WB_EXPECTED_GEODE_REVISION})."
    )
endif()

set(WB_SDK_DIR "$ENV{GEODE_SDK}")

if (NOT EXISTS "${WB_SDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "GEODE_SDK (${WB_SDK_DIR}) does not look like a Geode SDK checkout: "
        "missing CMakeLists.txt."
    )
endif()

# The SDK version must be the stable v6.0.0 release, never an alpha/beta or a
# newer major that would ship different headers.
if (EXISTS "${WB_SDK_DIR}/VERSION")
    file(STRINGS "${WB_SDK_DIR}/VERSION" WB_SDK_VERSION LIMIT_COUNT 1)
    string(STRIP "${WB_SDK_VERSION}" WB_SDK_VERSION)
    if (NOT WB_SDK_VERSION STREQUAL WB_EXPECTED_GEODE_VERSION)
        message(FATAL_ERROR
            "Geode SDK version mismatch: found '${WB_SDK_VERSION}', expected "
            "'${WB_EXPECTED_GEODE_VERSION}' (stable). White Bot targets Geode "
            "v6.0.0; do not build it against alpha/beta SDKs."
        )
    endif()
    message(STATUS "Geode SDK version: ${WB_SDK_VERSION} (pinned)")
else()
    message(WARNING "Geode SDK has no VERSION file (${WB_SDK_DIR}); "
                    "cannot verify the stable release version.")
endif()

# When the SDK is a git checkout, require the exact audited commit.
if (EXISTS "${WB_SDK_DIR}/.git")
    if (Git_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${WB_SDK_DIR}"
            OUTPUT_VARIABLE WB_SDK_HEAD
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if (WB_SDK_HEAD AND NOT WB_SDK_HEAD STREQUAL WB_EXPECTED_GEODE_REVISION)
            if (WB_ALLOW_SDK_REVISION_MISMATCH)
                message(WARNING
                    "Geode SDK revision mismatch allowed by "
                    "WB_ALLOW_SDK_REVISION_MISMATCH: found ${WB_SDK_HEAD}, "
                    "expected ${WB_EXPECTED_GEODE_REVISION}."
                )
            else()
                message(FATAL_ERROR
                    "Geode SDK revision mismatch: found ${WB_SDK_HEAD}, "
                    "expected ${WB_EXPECTED_GEODE_REVISION} (v6.0.0). "
                    "Use the pinned checkout or pass "
                    "-DWB_ALLOW_SDK_REVISION_MISMATCH=ON."
                )
            endif()
        else()
            message(STATUS "Geode SDK revision: ${WB_SDK_HEAD} (pinned)")
        endif()
    else()
        message(WARNING "Git not found; cannot verify the Geode SDK commit.")
    endif()
else()
    message(STATUS
        "Geode SDK is not a git checkout; commit verification skipped "
        "(version ${WB_SDK_VERSION} was verified above)."
    )
endif()

# --- bindings --------------------------------------------------------------
# Geode downloads bindings from branch 'main' via CPM when no override is
# given. That is a moving target, so for a reproducible build the bindings
# must come from a pinned checkout.
if (DEFINED ENV{GEODE_BINDINGS_REPO_PATH} AND NOT "$ENV{GEODE_BINDINGS_REPO_PATH}" STREQUAL "")
    set(WB_BINDINGS_DIR "$ENV{GEODE_BINDINGS_REPO_PATH}")

    if (NOT EXISTS "${WB_BINDINGS_DIR}/bindings/2.2091")
        message(FATAL_ERROR
            "Bindings at ${WB_BINDINGS_DIR} do not contain bindings/2.2091. "
            "White Bot targets GD 2.2091 and cannot build with these bindings."
        )
    endif()

    if (EXISTS "${WB_BINDINGS_DIR}/.git")
        if (Git_FOUND)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                WORKING_DIRECTORY "${WB_BINDINGS_DIR}"
                OUTPUT_VARIABLE WB_BINDINGS_HEAD
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if (WB_BINDINGS_HEAD AND NOT WB_BINDINGS_HEAD STREQUAL WB_EXPECTED_BINDINGS_REVISION)
                if (WB_ALLOW_BINDINGS_REVISION_MISMATCH)
                    message(WARNING
                        "Bindings revision mismatch allowed by "
                        "WB_ALLOW_BINDINGS_REVISION_MISMATCH: found "
                        "${WB_BINDINGS_HEAD}, expected "
                        "${WB_EXPECTED_BINDINGS_REVISION}."
                    )
                else()
                    message(FATAL_ERROR
                        "Bindings revision mismatch: found ${WB_BINDINGS_HEAD}, "
                        "expected ${WB_EXPECTED_BINDINGS_REVISION}. Use the "
                        "pinned checkout or pass "
                        "-DWB_ALLOW_BINDINGS_REVISION_MISMATCH=ON."
                    )
                endif()
            else()
                message(STATUS "Bindings revision: ${WB_BINDINGS_HEAD} (pinned)")
            endif()
        endif()
    else()
        message(STATUS
            "Bindings are not a git checkout; commit verification skipped "
            "(bindings/2.2091 exists)."
        )
    endif()
else()
    message(WARNING
        "GEODE_BINDINGS_REPO_PATH is not set: Geode will clone the bindings "
        "'main' branch. This is NOT reproducible. Set it to a checkout at "
        "${WB_EXPECTED_BINDINGS_REVISION} for pinned builds."
    )
endif()
