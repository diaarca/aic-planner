set(PLANNER_LIB_SOURCES
    area.cpp
    facility.cpp
    fuel.cpp
    mineral.cpp
    product.cpp
    region.cpp
    solver.cpp
)

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
)

add_executable(aic_planner main.cpp)

target_link_libraries(aic_planner PRIVATE aic_planner_lib)

install(TARGETS aic_planner RUNTIME DESTINATION bin)
