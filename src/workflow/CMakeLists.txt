# SICNU GEO RS — Workflow engine (Qt-free runtime)
set(SICNU_WORKFLOW_SOURCES
  workflow_definition.cpp
  workflow_gate.cpp
  workflow_session.cpp
  workflow_runtime.cpp
  builtin_definitions.cpp
  placeholder_grammar.cpp
)
add_library(sicnu_workflow STATIC ${SICNU_WORKFLOW_SOURCES})
target_include_directories(sicnu_workflow PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)
# Link jsoncpp the same way sicnu_operators does in this repo
if(TARGET jsoncpp)
  target_link_libraries(sicnu_workflow PUBLIC jsoncpp)
elseif(JSONCPP_LIBRARIES)
  target_include_directories(sicnu_workflow PUBLIC ${JSONCPP_INCLUDE_DIRS})
  target_link_libraries(sicnu_workflow PUBLIC ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(sicnu_workflow PUBLIC jsoncpp)
endif()

# Runner executes RSOperators; operators pull Qt6::Core (OK — no Widgets in headers).
if(TARGET sicnu_operators)
  target_link_libraries(sicnu_workflow PUBLIC sicnu_operators)
endif()

set_target_properties(sicnu_workflow PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
