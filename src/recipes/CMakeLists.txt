# src/recipes — RS14-20 Scientific Recipe Compiler (#1206)
#
# LabSpec → versioned machine-readable recipes (Qt-free): lab documents and
# sources, the scientific_recipe model, the compiler/validator/lookup and the
# recipe registry. Plain C++20 + jsoncpp; deliberately no Qt/QGIS/GDAL so the
# library stays a testable leaf (sicnu_grader precedent). It compiles and
# validates recipes over the existing lab contract chain and registers no
# operators, stores or registries of its own.

add_library( sicnu_recipes STATIC
    lab_document.cpp
    lab_source.cpp
    provider_interfaces.cpp
    recipe_compiler.cpp
    recipe_diagnostics.cpp
    recipe_lookup.cpp
    recipe_registry.cpp
    recipe_validator.cpp
    scientific_recipe.cpp
)

target_include_directories( sicnu_recipes PUBLIC ${CMAKE_SOURCE_DIR}/src )

set_target_properties( sicnu_recipes PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Same jsoncpp resolution as sicnu_contracts: the canonical target when the
# vendored/built target exists, pkg-config fallback otherwise.
if ( TARGET jsoncpp )
    target_link_libraries( sicnu_recipes PUBLIC jsoncpp )
else()
    find_package( PkgConfig QUIET )
    if ( PkgConfig_FOUND )
        pkg_check_modules( JSONCPP QUIET jsoncpp )
    endif()
    if ( JSONCPP_FOUND )
        target_include_directories( sicnu_recipes SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} )
        target_link_libraries( sicnu_recipes PUBLIC ${JSONCPP_LIBRARIES} )
    else()
        find_package( jsoncpp CONFIG QUIET )
        target_link_libraries( sicnu_recipes PUBLIC jsoncpp_lib )
    endif()
endif()
