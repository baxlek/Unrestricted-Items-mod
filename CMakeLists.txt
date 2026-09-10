cmake_minimum_required(VERSION 3.26)
project(unrestricted_items CXX)

# The Dusklight version to build against: a release tag (e.g. "v1.5.0") or a commit SHA.
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "8caab1a6ba4aad5513bc9fe402a76dce53964e9c")
endif ()
# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
# Use -DDUSKLIGHT_DIR= to point at an existing Dusklight checkout.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

# Current Android builds in use may predate the SDK's automatic GameService import for
# FEATURES game. Keep the game headers/hooks surface, but drop the extra sdk/src/game_feature.cpp
# source so the mod doesn't hard-require that loader service at startup.
get_target_property(_unrestricted_game_feature_sources dusklight_mod_feature_game INTERFACE_SOURCES)
if (_unrestricted_game_feature_sources)
    list(REMOVE_ITEM _unrestricted_game_feature_sources "${DUSKLIGHT_DIR}/sdk/src/game_feature.cpp")
    set_property(TARGET dusklight_mod_feature_game PROPERTY INTERFACE_SOURCES
            "${_unrestricted_game_feature_sources}")
endif ()

add_mod(unrestricted_items
        FEATURES game          # remove for service/asset-only mods; add webgpu for GfxService
        SOURCES src/mod.cpp
        MOD_JSON mod.json
        RES_DIR res            # mod resources, including icon.png and banner.png
        OVERLAY_DIR overlay    # game file overlays; remove if unused
        TEXTURES_DIR textures  # texture replacements; remove if unused
)
