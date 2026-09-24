# src/planner/live — production capability seam for the scientific planner.
#
# Projects the live capability knowledge mirror (data/agent/capabilities,
# read through the shared Qt-free read path in sicnu_preflight) onto
# sicnu::planner::CapabilityProvider. Plain C++20 + jsoncpp; links the two
# Qt-free leaves it composes (sicnu_planner, sicnu_preflight) — no Qt, no
# QGIS, no agent layer. The planner core stays provider-interface-only; this
# library is what an embedding installs when the mirror is the authority.

add_library( sicnu_planner_live STATIC
    capability_live.cpp
)

add_library( Sicnu::PlannerLive ALIAS sicnu_planner_live )

target_include_directories( sicnu_planner_live PUBLIC ${CMAKE_SOURCE_DIR}/src )

target_link_libraries( sicnu_planner_live PUBLIC
    sicnu_planner
    sicnu_preflight
)

set_target_properties( sicnu_planner_live PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
