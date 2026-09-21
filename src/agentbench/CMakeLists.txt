# src/agentbench — RS14 Agent Benchmark & Evaluation Harness
#
# Offline, traces-in trajectory benchmark: versioned cases, deterministic
# fake agent, recorded-trace replay, metric evaluation and reporting.
# Plain C++20 + jsoncpp; deliberately no Qt so the library stays usable
# headless and portable. Evaluation never executes tools, never opens a
# store, never schedules work (no second scheduler, no third truth source).

add_library( sicnu_agentbench STATIC
    case_schema.cpp
    case_schema.h
    errors.h
    failure_taxonomy.cpp
    failure_taxonomy.h
    json_writer.cpp
    json_writer.h
    trace.cpp
    trace.h
    version.h
)

add_library( Sicnu::agentbench ALIAS sicnu_agentbench )

target_include_directories( sicnu_agentbench PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_agentbench PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Same jsoncpp resolution as sicnu_contracts: the canonical target when the
# vendored/built target exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_agentbench PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_agentbench SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_agentbench PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_agentbench PUBLIC jsoncpp_lib )
    endif()
endif()
