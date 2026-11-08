set(PLANNER_LIB_SOURCES
    area.cpp
    cli.cpp
    blueprint_layout.cpp
    blueprint_solver.cpp
    facility.cpp
    fuel.cpp
    mineral.cpp
    product.cpp
    region.cpp
    conveyor_router.cpp
    dataset.cpp
    rendering.cpp
    recipe_graph.cpp
    production_network.cpp
    plant_module.cpp
    solver.cpp
)

# The Nix OR-Tools package exports these solver dependencies in its link interface.
find_package(PkgConfig REQUIRED)
pkg_check_modules(Cbc REQUIRED IMPORTED_TARGET GLOBAL cbc)
pkg_check_modules(OsiCbc REQUIRED IMPORTED_TARGET GLOBAL osi-cbc)
pkg_check_modules(Clp REQUIRED IMPORTED_TARGET GLOBAL clp)
pkg_check_modules(OsiClp REQUIRED IMPORTED_TARGET GLOBAL osi-clp)
pkg_check_modules(CoinUtils REQUIRED IMPORTED_TARGET GLOBAL coinutils)
find_package(nlohmann_json REQUIRED)
find_package(ortools REQUIRED)

add_library(aic_planner_lib STATIC ${PLANNER_LIB_SOURCES})

target_include_directories(aic_planner_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/include/aic-planner"
)

target_link_libraries(aic_planner_lib PUBLIC
    ortools::ortools
    nlohmann_json::nlohmann_json
)

add_executable(aic_planner main.cpp)

target_link_libraries(aic_planner PRIVATE aic_planner_lib)

include(GNUInstallDirs)
install(TARGETS aic_planner RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/aic-planner/data"
    FILES_MATCHING PATTERN "*.json"
)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/NOTICE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/aic-planner"
)
