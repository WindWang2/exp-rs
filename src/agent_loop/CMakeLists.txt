# src/agent_loop — Evidence-first Agent Session (RS14-11)
#
# The orchestration core of the evidence-first agent loop: session state
# machine, typed decision records, the replayable journal, seam interfaces
# and the bounded session policy. Plain C++20 + jsoncpp; deliberately no Qt
# so the loop stays testable headless in seconds (the Qt-capable production
# adapter lives in src/agent/tools/agent_session_adapter.*).

add_library( sicnu_agent_loop STATIC
    session_state.cpp
    decision_record.cpp
    session_journal.cpp
    session_seams.cpp
    session_policy.cpp
)

target_include_directories( sicnu_agent_loop PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_agent_loop PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Same jsoncpp resolution as sicnu_contracts: the canonical target when the
# vendored/built target exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_agent_loop PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_agent_loop SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_agent_loop PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_agent_loop PUBLIC jsoncpp_lib )
    endif()
endif()
