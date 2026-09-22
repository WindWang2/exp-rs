# RS14-01 Scientific Data Passport — catalog adapter (Qt-typed).
# Thin mapping from AssetSnapshot/DerivationRecord into passport facts;
# linked only into Qt-enabled targets (CLI, app, Qt test lane).

qt_add_library( sicnu_scientific_state_catalog STATIC
    catalog_state_facts.cpp
)

add_library( Sicnu::ScientificStateCatalog ALIAS sicnu_scientific_state_catalog )

target_include_directories( sicnu_scientific_state_catalog PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries( sicnu_scientific_state_catalog PUBLIC
    Sicnu::ScientificState
    Sicnu::data
)

set_target_properties( sicnu_scientific_state_catalog PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
)
