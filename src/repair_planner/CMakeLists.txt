# src/repair_planner — RS14-03 Scientific Repair Planner (ADR 0174)
#
# Explicit, auditable, rejectable repair candidate plans over preflight
# findings. Plain C++20 + jsoncpp; deliberately no Qt and no sicnu library
# dependencies so the planner stays a portable leaf (same discipline as
# src/contracts). Planning-only: nothing here executes a repair.

add_library( sicnu_repair_planner STATIC
    repair_sha256.cpp
    repair_schema.cpp
)

target_include_directories( sicnu_repair_planner PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_repair_planner PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Same jsoncpp resolution as sicnu_contracts: the canonical target when the
# vendored/built target exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_repair_planner PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_repair_planner SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_repair_planner PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_repair_planner PUBLIC jsoncpp_lib )
    endif()
endif()
