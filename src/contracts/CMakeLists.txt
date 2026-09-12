# src/contracts — Contract Platform 9.0
#
# Canonical descriptors, contract graph and the mechanical scanners used by
# the contract guards. Plain C++20 + jsoncpp; deliberately no Qt so the
# library stays usable headless and portable.

add_library( sicnu_contracts STATIC
    text_scan.cpp
    operator_param_scanner.cpp
    contract_descriptor.cpp
    contract_graph.cpp
    command_ref_scanner.cpp
    error_code_scanner.cpp
    graph_assembly.cpp
)

target_include_directories( sicnu_contracts PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_contracts PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Same jsoncpp resolution as sicnu_operators: the canonical target when the
# vendored/built target exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_contracts PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_contracts SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_contracts PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_contracts PUBLIC jsoncpp_lib )
    endif()
endif()

# Snapshot regeneration/verification tool. Needs the live operator
# registry target, which is defined before this directory in the root
# CMakeLists.
add_executable( contract_inventory tool/contract_inventory_main.cpp )
target_link_libraries( contract_inventory PRIVATE
    sicnu_contracts
    sicnu_operators
)
