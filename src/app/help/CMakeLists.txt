# GUI projections of the Unified Help System 6.0: Help Center dialog, F1
# context resolution, action/widget help binders, availability fact adapter
# and workbench guidance widget. Kept as a small library (Qt Widgets +
# Sicnu::help) so tests and future surfaces can link help GUI without the
# full shell. Business rules stay in ContextRules / CommandRegistry — these
# are presentation adapters only.

qt_add_library(sicnu_help_gui STATIC
  help_center_dialog.cpp
  help_center_dialog.h
  help_event_filter.cpp
  help_event_filter.h
  help_system_controller.cpp
  help_system_controller.h
  command_help_source.cpp
  command_help_source.h
  availability_facts_adapter.cpp
  availability_facts_adapter.h
  workbench_guidance.cpp
  workbench_guidance.h
)

add_library(Sicnu::help_gui ALIAS sicnu_help_gui)

target_include_directories(sicnu_help_gui
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
)

target_link_libraries(sicnu_help_gui
  PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Sicnu::help
    Sicnu::help_adapters
)

set_target_properties(sicnu_help_gui PROPERTIES
  AUTOMOC ON
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
