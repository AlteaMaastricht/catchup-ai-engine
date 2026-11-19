cmake_minimum_required(VERSION 3.16)
project(catchup LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CATCHUP_LIB_IN_PACKAGE
       "Write the shared library into catchup/ so the Python package finds it" ON)
option(CATCHUP_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(CATCHUP_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

set(CATCHUP_CORE_SOURCES
    src/state.cpp
    src/search.cpp
    src/ui.cpp
    src/position.cpp
)

add_library(catchup_core STATIC ${CATCHUP_CORE_SOURCES})
set_target_properties(catchup_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(catchup_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(catchup src/main.cpp)
target_link_libraries(catchup PRIVATE catchup_core)

add_executable(catchup-bench src/bench.cpp)
target_link_libraries(catchup-bench PRIVATE catchup_core)

find_package(Threads REQUIRED)
add_executable(catchup-match src/match.cpp)
target_link_libraries(catchup-match PRIVATE catchup_core Threads::Threads)

add_executable(catchup-test
    tests/test_main.cpp
    tests/test_board.cpp
    tests/test_state.cpp
    tests/test_rules.cpp
    tests/test_search.cpp
    tests/test_ui.cpp
    tests/test_reference.cpp
)
target_include_directories(catchup-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(catchup-test PRIVATE catchup_core)

add_library(catchup_capi SHARED src/c_api.cpp)
target_include_directories(catchup_capi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(catchup_capi PRIVATE catchup_core)
set_target_properties(catchup_capi PROPERTIES OUTPUT_NAME catchup)
if(CATCHUP_LIB_IN_PACKAGE)
    set_target_properties(catchup_capi PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/catchup
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/catchup
    )
endif()

if(APPLE)
    set_target_properties(catchup_capi PROPERTIES
        MACOSX_RPATH ON
        INSTALL_NAME_DIR "@loader_path"
    )
endif()

enable_testing()
add_test(NAME catchup-test COMMAND catchup-test)

find_package(Python3 COMPONENTS Interpreter QUIET)
# A sanitizer-instrumented shared library cannot be loaded into an
# uninstrumented Python interpreter, so the Python suite is skipped there.
if(Python3_Interpreter_FOUND AND NOT CATCHUP_SANITIZE)
    add_test(NAME python-unittest
             COMMAND ${Python3_EXECUTABLE} -B -m unittest discover -s tests -t .
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(python-unittest PROPERTIES
        ENVIRONMENT "CATCHUP_LIB=$<TARGET_FILE:catchup_capi>")
endif()
