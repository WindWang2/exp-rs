# Read-only adapter from RSOperatorRegistry to the help layer's
# OperatorCatalogSource. Lives between sicnu_help (must stay operator-free)
# and consumers (GUI shell, CLI, tests): whoever can see the operator
# registry snapshots it through this tiny library.

qt_add_library(sicnu_help_adapters STATIC
  operator_help_source.cpp
  operator_help_source.h
)

add_library(Sicnu::help_adapters ALIAS sicnu_help_adapters)

target_include_directories(sicnu_help_adapters
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_help_adapters
  PUBLIC
    Qt6::Core
    Sicnu::help
  PRIVATE
    sicnu_operators
)

set_target_properties(sicnu_help_adapters PROPERTIES
  AUTOMOC ON
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
