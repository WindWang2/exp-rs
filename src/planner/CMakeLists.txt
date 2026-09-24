# src/planner — RS14-09 Scientific Task Planner (recovery of PR #1193)
#
# Goal → explainable ScientificPlan decision layer. Plain C++20 + jsoncpp;
# links ONLY sicnu_contracts (per-operator scientific semantics: state
# transitions, seed policy) — no Qt, no QGIS, no agent, no operators.
# Planning-only: nothing here executes an operator or opens a dataset
# (same Qt-free-leaf discipline as src/preflight and src/repair_planner).
# All capability facts enter through the injected provider seam.

add_library( sicnu_planner STATIC
    planner_vocab.cpp
    sha256_util.cpp
    scientific_goal.cpp
    planning_context.cpp
    scientific_plan.cpp
    planner_rules.cpp
    planner_core.cpp
    planner_constraints.cpp
    planner_proposal.cpp
    plan_ir_projection.cpp
    plan_teaching.cpp
)

target_include_directories( sicnu_planner PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_planner PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries( sicnu_planner PUBLIC sicnu_contracts )

# Same jsoncpp resolution as sicnu_contracts: the canonical target when the
# vendored/built target exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_planner PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_planner SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_planner PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_planner PUBLIC jsoncpp_lib )
    endif()
endif()
