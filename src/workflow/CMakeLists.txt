# SICNU GEO RS — Workflow engine.
#
# Two targets:
#  - sicnu_workflow (SHARED): the self-contained core - definitions, gates,
#    sessions, placeholder grammar, the Workflow Engine 2.0 run/checkpoint
#    aggregate, and ArtifactGC. ArtifactGC consumes WorkflowRun and used to
#    live in sicnu_processing, which cannot link sicnu_workflow (cycle:
#    workflow -> operators -> processing), so executables linking only
#    sicnu_processing could not resolve the run accessors; it now ships in
#    this library, which sits above processing in the layer stack.
#  - sicnu_workflow_runtime (STATIC): WorkflowRuntime + built-in workflow
#    registrations. These reference ExecutionPlane (sicnu_task_center) and
#    OutputVerifier (sicnu_agent) and deliberately keep the historical
#    static-deferral contract: symbols resolve at final link time in the
#    executables/tests, which link those libs directly.
set(SICNU_WORKFLOW_CORE_SOURCES
  artifact_gc.cpp
  workflow_definition.cpp
  workflow_gate.cpp
  workflow_session.cpp
  placeholder_grammar.cpp
  workflow_run.cpp
  workflow_checkpoint.cpp
)
set(SICNU_WORKFLOW_RUNTIME_SOURCES
  workflow_runtime.cpp
  builtin_definitions.cpp
)
add_library(sicnu_workflow SHARED ${SICNU_WORKFLOW_CORE_SOURCES})
add_library(sicnu_workflow_runtime STATIC ${SICNU_WORKFLOW_RUNTIME_SOURCES})
target_link_libraries(sicnu_workflow_runtime PUBLIC sicnu_workflow)
target_include_directories(sicnu_workflow PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/processing
  ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(sicnu_workflow PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)
install(TARGETS sicnu_workflow
  RUNTIME DESTINATION ${QGIS_BIN_DIR}
  LIBRARY DESTINATION ${QGIS_LIB_SUBDIR}
  ARCHIVE DESTINATION ${QGIS_LIB_DIR}
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

# Deliberately NO link from sicnu_workflow itself to task_center/processing/
# agent: task_center PUBLIC-links sicnu_workflow, so adding the reverse edge
# would form a cycle. workflow_runtime.cpp's ExecutionPlane/OutputVerifier
# references stay unresolved in that archive and are resolved at final link
# time; the PUBLIC provider links below propagate the resolving libraries to
# every executable that uses the runtime (the "final executables link those
# libs directly" contract, encoded once instead of per-consumer). Static ->
# static propagation only puts archive/library paths on exe link lines:
# nothing is embedded into a shared library (no state duplication), and
# task_center never references sicnu_workflow_runtime, so the graph stays
# acyclic.
if(TARGET sicnu_task_center)
  target_link_libraries(sicnu_workflow_runtime PUBLIC sicnu_task_center sicnu_processing)
endif()
if(TARGET sicnu_agent)
  target_link_libraries(sicnu_workflow_runtime PUBLIC sicnu_agent)
endif()


set_target_properties(sicnu_workflow PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)
