# sicnu_explain_adapters — the only layer allowed to attach authoritative
# sources to the explain core (see the layer guard in src/explain/CMakeLists.txt).
#
#   registry_operator_knowledge  RSOperatorRegistry → IOperatorKnowledge
#                                (live schema projection + drift gate)
#   workflow_projection          WorkflowDocument (IR 2.0 / D17) and
#                                WorkflowDefinition/StepDef (Engine 2.0)
#                                → ExplanationRequest
#   provenance_file_evidence     provenance_<runId>.json (d17_provenance)
#                                → IExecutionEvidence
#
# The target pulls in Qt6::Core (Qt-native sources), sicnu_operators (live
# registry) and sicnu_workflow (IR 2.0 + provenance reader); it never links
# the app/GUI layer and adds no reverse edges into sicnu_explain.

add_library(sicnu_explain_adapters STATIC
  provenance_file_evidence.cpp
  registry_operator_knowledge.cpp
  workflow_projection.cpp
)

add_library(Sicnu::explain_adapters ALIAS sicnu_explain_adapters)

target_include_directories(sicnu_explain_adapters
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_explain_adapters
  PUBLIC
    Sicnu::explain
    Qt6::Core
    sicnu_operators
    sicnu_workflow
)

set_target_properties(sicnu_explain_adapters PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
