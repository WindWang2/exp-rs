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
    observability.cpp
    observed_state.cpp
    planner_projection.cpp
    recipe_router.cpp
)

add_library( Sicnu::ScienceContext ALIAS sicnu_science_context )

target_include_directories( sicnu_science_context PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries( sicnu_science_context PUBLIC
    Sicnu::ScientificState
)

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
