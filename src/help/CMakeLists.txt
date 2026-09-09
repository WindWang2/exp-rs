# Unified Help, Hint & Diagnostic Knowledge System 6.0.
#
# sicnu_help is the single authority for help/diagnostic knowledge: descriptor
# model, registry, search index, diagnostic catalog, JSON knowledge store and
# surface projections (tooltips, compact agent summaries, generated Markdown).
# It depends on nothing but Qt Core, the shared data vocabulary and jsoncpp —
# the authoritative catalogs (CommandRegistry, RSOperatorRegistry, ContextRules)
# are read through narrow source interfaces (help_catalog_source.h) implemented
# by consumers, so no dependency cycle can form and no business logic can leak
# into the help layer.

qt_add_library(sicnu_help STATIC
  help_id.cpp
  help_id.h
  help_descriptor.h
  help_registry.cpp
  help_registry.h
  help_search_index.cpp
  help_search_index.h
  diagnostic_catalog.cpp
  diagnostic_catalog.h
  help_content_store.cpp
  help_content_store.h
  help_composition.cpp
  help_composition.h
  help_catalog_source.h
  command_help_provider.cpp
  command_help_provider.h
  operator_help_provider.cpp
  operator_help_provider.h
  availability_facts.cpp
  availability_facts.h
  help_presenter.cpp
  help_presenter.h
  help_topic_text.cpp
  help_topic_text.h
  help_markdown_writer.cpp
  help_markdown_writer.h
  # NOTE: help_content.qrc is intentionally NOT compiled into this static
  # library — resource initializers in static libs are dropped by the linker.
  # Executables that need the embedded knowledge layer list
  # ${CMAKE_SOURCE_DIR}/data/help/help_content.qrc in their own sources.
)

add_library(Sicnu::help ALIAS sicnu_help)

target_include_directories(sicnu_help
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_help
  PUBLIC
    Qt6::Core
    Sicnu::data
    jsoncpp
)

# Layer guard: the help layer must stay GUI/network/operator free. Anything
# else couples hover-time help to the shell or the processing engines.
foreach(SICNU_HELP_FORBIDDEN_LINK Qt6::Widgets Qt6::Network qgis_gui Sicnu::app Sicnu::operators)
  get_target_property(SICNU_HELP_DIRECT_LINKS sicnu_help LINK_LIBRARIES)
  if(SICNU_HELP_FORBIDDEN_LINK IN_LIST SICNU_HELP_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_help must not depend on forbidden target ${SICNU_HELP_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_help PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
