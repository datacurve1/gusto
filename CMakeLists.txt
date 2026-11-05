cmake_minimum_required(VERSION 3.20)

project(GUSTO VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "GUSTO must be cross-compiled for Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "GUSTO requires a 32-bit Windows target.")
endif()

add_library(gusto SHARED
    src/client/rof2/compatibility.cpp
    src/client/rof2/initialization.cpp
    src/client/rof2/globals.cpp
    src/client/rof2/client_state.cpp
    src/client/rof2/diagnostics.cpp
    src/client/rof2/interpret_cmd_hook.cpp
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

target_link_libraries(gusto PRIVATE bcrypt)

set_target_properties(gusto PROPERTIES
    OUTPUT_NAME "GUSTO"
    PREFIX ""
    SUFFIX ".dll"
)

add_executable(gusto_safety_tests
    test/gusto_safety_tests.cpp
    src/client/rof2/compatibility.cpp
    src/client/rof2/initialization.cpp
    src/client/rof2/globals.cpp
    src/client/rof2/client_state.cpp
)

target_include_directories(gusto_safety_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gusto_safety_tests PRIVATE cxx_std_20)

target_compile_definitions(gusto_safety_tests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(gusto_safety_tests PRIVATE bcrypt)
