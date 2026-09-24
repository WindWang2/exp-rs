# Science Context Broker & Skill Router — Qt-free, jsoncpp + scientific_state.
# Projects passport / capability / recipe truths into exp.science_context.v1.
# No GUI, no network, no Control Center UI.

add_library( sicnu_science_context STATIC
    agent_adapter.cpp
    asset_state_provider.cpp
    broker.cpp
    bundle.cpp
    capability_router.cpp
    context_budget.cpp
    context_cache.cpp
    live_asset_resolver.cpp
    observability.cpp
    observed_state.cpp
    planner_projection.cpp
    planner_goal_projection.cpp
    recipe_router.cpp
)

add_library( Sicnu::ScienceContext ALIAS sicnu_science_context )

target_include_directories( sicnu_science_context PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries( sicnu_science_context PUBLIC
    Sicnu::ScientificState
    # R3 track 14: bundle → ScientificGoal/PlanningContext projection. The
    # planner is a Qt-free leaf (contracts + jsoncpp), so this stays inside
    # the module's no-GUI discipline.
    sicnu_planner
)

# Live recipe authority: the registry stays the only store; the broker's
# RecipeRouter is a bounded projection synced from it (Qt-free, jsoncpp only).
if ( TARGET sicnu_recipes )
    target_link_libraries( sicnu_science_context PUBLIC sicnu_recipes )
endif()

# Live GDAL facts collector (Qt-free; the target is added unconditionally by
# src/scientific_state/gdal alongside this module).
if ( TARGET Sicnu::ScientificStateGdal )
    target_sources( sicnu_science_context PRIVATE gdal_asset_source.cpp )
    target_link_libraries( sicnu_science_context PUBLIC Sicnu::ScientificStateGdal )
endif()

if ( TARGET jsoncpp )
    target_link_libraries( sicnu_science_context PUBLIC jsoncpp )
elseif ( JSONCPP_LIBRARIES )
    target_include_directories( sicnu_science_context SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
    target_link_libraries( sicnu_science_context PUBLIC ${JSONCPP_LIBRARIES} )
else()
    target_link_libraries( sicnu_science_context PUBLIC jsoncpp )
endif()

set_target_properties( sicnu_science_context PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
)
