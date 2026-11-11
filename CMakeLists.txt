cmake_minimum_required(VERSION 3.20)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GUSTO_RELEASE_VERSION LIMIT_COUNT 1)
if(NOT GUSTO_RELEASE_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "VERSION must contain a semantic version")
endif()
project(GUSTO VERSION ${CMAKE_MATCH_1} LANGUAGES CXX RC)
configure_file(cmake/version.hpp.in generated/gusto/version.hpp @ONLY)
configure_file(windows/gusto_version.rc.in generated/gusto_version.rc @ONLY)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")

if(NOT WIN32)
    message(FATAL_ERROR "GUSTO must be cross-compiled for Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "GUSTO requires a 32-bit Windows target.")
endif()

add_library(gusto SHARED
    "${CMAKE_CURRENT_BINARY_DIR}/generated/gusto_version.rc"
    src/client/rof2/compatibility.cpp
    src/client/rof2/bank.cpp
    src/client/rof2/mercenary_ui.cpp
    src/client/rof2/initialization.cpp
    src/client/rof2/globals.cpp
    src/client/rof2/client_state.cpp
    src/client/rof2/interpret_cmd_hook.cpp
    src/client/rof2/slow_turn_keys.cpp
    src/client/rof2/hook_utils.cpp
    src/client/rof2/nameplate_hooks.cpp
    src/client/rof2/tag_hooks.cpp
    src/client/rof2/tag_state.cpp
    src/client/rof2/tag_models.cpp
    src/client/rof2/hud_model.cpp
    src/client/rof2/life_fx_model.cpp
    src/client/rof2/life_fx.cpp
    src/client/rof2/hud_external_map.cpp
    src/client/rof2/named_pipe.cpp
    src/client/rof2/pipe_model.cpp
    src/client/rof2/pipe_snapshot.cpp
    src/client/rof2/gamepad.cpp
    src/client/rof2/hud_overlay.cpp
    )

target_include_directories(gusto
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gusto PRIVATE cxx_std_20)

target_compile_definitions(gusto PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    GUSTO_BUILD
)

target_link_libraries(gusto PRIVATE bcrypt gdi32)
if(MINGW)
    target_link_options(gusto PRIVATE -static-libgcc -static-libstdc++)
endif()

set_target_properties(gusto PROPERTIES
    OUTPUT_NAME "GUSTO"
    PREFIX ""
    SUFFIX ".dll"
)

add_executable(gusto_safety_tests
    test/gusto_safety_tests.cpp
    src/client/rof2/compatibility.cpp
    src/client/rof2/bank.cpp
    src/client/rof2/mercenary_ui.cpp
    src/client/rof2/initialization.cpp
    src/client/rof2/globals.cpp
    src/client/rof2/client_state.cpp
    src/client/rof2/interpret_cmd_hook.cpp
    src/client/rof2/slow_turn_keys.cpp
    src/client/rof2/hook_utils.cpp
    src/client/rof2/nameplate_hooks.cpp
    src/client/rof2/tag_hooks.cpp
    src/client/rof2/tag_state.cpp
    src/client/rof2/tag_models.cpp
    src/client/rof2/hud_model.cpp
    src/client/rof2/life_fx_model.cpp
    src/client/rof2/life_fx.cpp
    src/client/rof2/hud_external_map.cpp
    src/client/rof2/named_pipe.cpp
    src/client/rof2/pipe_model.cpp
    src/client/rof2/pipe_snapshot.cpp
    src/client/rof2/gamepad.cpp
    src/client/rof2/hud_overlay.cpp
)

target_include_directories(gusto_safety_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gusto_safety_tests PRIVATE cxx_std_20)

target_compile_definitions(gusto_safety_tests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(gusto_safety_tests PRIVATE bcrypt gdi32)

add_executable(gusto_tagging_tests
    test/tagging_tests.cpp
    src/client/rof2/tag_state.cpp
    src/client/rof2/tag_models.cpp
)
target_include_directories(gusto_tagging_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_tagging_tests PRIVATE cxx_std_20)

add_executable(gusto_hud_tests test/hud_model_tests.cpp src/client/rof2/hud_model.cpp)
target_include_directories(gusto_hud_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_hud_tests PRIVATE cxx_std_20)

# An opt-in rendering smoke test with synthetic map data (no game needed).
get_target_property(GUSTO_PREVIEW_SOURCES gusto SOURCES)
list(REMOVE_ITEM GUSTO_PREVIEW_SOURCES src/client/rof2/hud_overlay.cpp)
add_executable(gusto_hud_preview EXCLUDE_FROM_ALL test/hud_preview.cpp ${GUSTO_PREVIEW_SOURCES})
target_include_directories(gusto_hud_preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_hud_preview PRIVATE cxx_std_20)
target_compile_definitions(gusto_hud_preview PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gusto_hud_preview PRIVATE bcrypt gdi32 d3d9)

add_executable(gusto_life_tests test/life_fx_tests.cpp src/client/rof2/life_fx_model.cpp)
target_include_directories(gusto_life_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_life_tests PRIVATE cxx_std_20)

add_executable(gusto_life_preview EXCLUDE_FROM_ALL test/life_fx_preview.cpp src/client/rof2/life_fx_model.cpp src/client/rof2/life_fx.cpp)
target_include_directories(gusto_life_preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_life_preview PRIVATE cxx_std_20)
target_compile_definitions(gusto_life_preview PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gusto_life_preview PRIVATE d3d9)

add_executable(gusto_gamepad_tests test/gamepad_tests.cpp)
target_include_directories(gusto_gamepad_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_gamepad_tests PRIVATE cxx_std_20)

add_executable(gusto_pipe_model_tests test/pipe_model_tests.cpp src/client/rof2/pipe_model.cpp)
target_include_directories(gusto_pipe_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_pipe_model_tests PRIVATE cxx_std_20)

add_executable(gusto_pipe_tests test/pipe_tests.cpp src/client/rof2/named_pipe.cpp)
target_include_directories(gusto_pipe_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_pipe_tests PRIVATE cxx_std_20)
target_compile_definitions(gusto_pipe_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(gusto_gamepad_preview EXCLUDE_FROM_ALL test/gamepad_preview.cpp)
target_include_directories(gusto_gamepad_preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_gamepad_preview PRIVATE cxx_std_20)
target_compile_definitions(gusto_gamepad_preview PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gusto_gamepad_preview PRIVATE bcrypt)

add_executable(gusto_pipe_snapshot_tests test/pipe_snapshot_tests.cpp src/client/rof2/named_pipe.cpp src/client/rof2/pipe_model.cpp)
target_include_directories(gusto_pipe_snapshot_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_pipe_snapshot_tests PRIVATE cxx_std_20)
target_compile_definitions(gusto_pipe_snapshot_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(gusto_controller_probe EXCLUDE_FROM_ALL tools/controller_probe.cpp)
target_compile_features(gusto_controller_probe PRIVATE cxx_std_20)
target_compile_definitions(gusto_controller_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(gusto_windows_runtime_smoke EXCLUDE_FROM_ALL tools/windows_runtime_smoke.cpp)
target_compile_features(gusto_windows_runtime_smoke PRIVATE cxx_std_20)

add_executable(gusto_hook_chain_tests test/hook_chain_tests.cpp)
target_include_directories(gusto_hook_chain_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_hook_chain_tests PRIVATE cxx_std_20)
target_compile_definitions(gusto_hook_chain_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Runs against a privately mapped client image; never starts EQ or logs in.
get_target_property(GUSTO_BANK_TEST_SOURCES gusto SOURCES)
add_executable(gusto_bank_tests EXCLUDE_FROM_ALL test/bank_expansion_tests.cpp ${GUSTO_BANK_TEST_SOURCES})
target_include_directories(gusto_bank_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gusto_bank_tests PRIVATE cxx_std_20)
target_compile_definitions(gusto_bank_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gusto_bank_tests PRIVATE bcrypt gdi32)
if(MINGW)
    # Leave the game's preferred image base free for native instruction tests.
    target_link_options(gusto_bank_tests PRIVATE -Wl,--image-base,0x10000000)
endif()

# Generate the expanded Mercenary Management UI.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(GUSTO_MERC_UI_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/EQUI_MercenaryManageWnd.xml"
)

set(GUSTO_MERC_UI_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/uifiles/default/EQUI_MercenaryManageWnd.xml"
)

add_custom_target(gusto_mercenary_ui ALL
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_mercenary_ui.py"
        "${GUSTO_MERC_UI_SOURCE}"
        "${GUSTO_MERC_UI_OUTPUT}"
    BYPRODUCTS "${GUSTO_MERC_UI_OUTPUT}"
    COMMENT "Generating GUSTO mercenary UI"
    VERBATIM
)
