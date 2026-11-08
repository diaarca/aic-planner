cmake_minimum_required(VERSION 3.15)
project(aic_planner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_compile_options("$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>")

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(planner_tests tests/planner_tests.cpp)
    target_link_libraries(planner_tests PRIVATE aic_planner_lib)
    add_test(NAME planner_regressions COMMAND planner_tests "${PROJECT_SOURCE_DIR}")
    set_tests_properties(planner_regressions PROPERTIES TIMEOUT 150)
    add_test(NAME catalog_importer COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${PROJECT_SOURCE_DIR}/tests" -p "test_*.py")
    add_test(NAME validate_fixture COMMAND aic_planner --validate "${PROJECT_SOURCE_DIR}/data/test")
    add_test(NAME validate_catalog_region COMMAND aic_planner --validate "${PROJECT_SOURCE_DIR}/data/valley_iv")
    add_test(NAME valley_blueprints COMMAND aic_planner --blueprints-only --no-layouts "${PROJECT_SOURCE_DIR}/data/valley_iv")
    set_tests_properties(valley_blueprints PROPERTIES TIMEOUT 240)
endif()
