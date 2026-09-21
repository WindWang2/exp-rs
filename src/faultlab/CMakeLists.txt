# src/faultlab — Scientific Fault Injection Lab Framework (RS14-13)
#
# Qt-free C++20 leaf library: the framework's semantics (scenario schema,
# sandbox contract, fault transforms, observables, expectations, runner)
# must stay unit-testable in a small pure C++ binary, so the core links
# jsoncpp only. GeoTIFF fixture I/O lives in the gdal/ sub-library.

add_library( sicnu_faultlab STATIC
    deterministic.cpp
    fault_expectations.cpp
    fault_observables.cpp
    fault_report.cpp
    fault_registry.cpp
    fault_sandbox.cpp
    fault_transforms.cpp
    fault_types.cpp
    fault_scenario.cpp
    util/sha256.cpp
    util/canonical_json.cpp
)

add_library( Sicnu::faultlab ALIAS sicnu_faultlab )

target_include_directories( sicnu_faultlab PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_faultlab PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Same jsoncpp resolution as sicnu_contracts: the canonical target when the
# vendored/built target exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_faultlab PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_faultlab SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_faultlab PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_faultlab PUBLIC jsoncpp_lib )
    endif()
endif()
