set(PLANNER_LIB_SOURCES
    area.cpp
    blueprint_solver.cpp
    facility.cpp
    fuel.cpp
    mineral.cpp
    product.cpp
    region.cpp
    solver.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(Cbc REQUIRED IMPORTED_TARGET GLOBAL cbc)
pkg_check_modules(OsiCbc REQUIRED IMPORTED_TARGET GLOBAL osi-cbc)
pkg_check_modules(Clp REQUIRED IMPORTED_TARGET GLOBAL clp)
pkg_check_modules(OsiClp REQUIRED IMPORTED_TARGET GLOBAL osi-clp)
pkg_check_modules(CoinUtils REQUIRED IMPORTED_TARGET GLOBAL coinutils)
find_package(nlohmann_json REQUIRED)

find_package(ortools REQUIRED)

find_path(FastCppCsvParser_INCLUDE_DIR
    NAMES csv.h
    HINTS "$ENV{CSV_PARSER_DIR}"
    PATH_SUFFIXES include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FastCppCsvParser
    REQUIRED_VARS FastCppCsvParser_INCLUDE_DIR
)

if(FastCppCsvParser_FOUND)
    set(FastCppCsvParser_INCLUDE_DIRS ${FastCppCsvParser_INCLUDE_DIR})
    if(NOT TARGET FastCppCsvParser::FastCppCsvParser)
        add_library(FastCppCsvParser::FastCppCsvParser INTERFACE IMPORTED)
        set_target_properties(FastCppCsvParser::FastCppCsvParser PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${FastCppCsvParser_INCLUDE_DIRS}"
        )
    endif()
endif()

add_library(aic_planner_lib STATIC ${PLANNER_LIB_SOURCES})

target_include_directories(aic_planner_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/include/aic-planner"
)

target_link_libraries(aic_planner_lib PUBLIC
    ortools::ortools
    FastCppCsvParser::FastCppCsvParser
    nlohmann_json::nlohmann_json
)

add_executable(aic_planner main.cpp)

target_link_libraries(aic_planner PRIVATE aic_planner_lib)
