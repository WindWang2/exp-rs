# Explainable Scientific Workflow / Why-this-step (RS14-15, ADR 0167).
#
# sicnu_explain is the single authority for per-step explanations: the
# versioned StepExplanation schema (exp.step_explanation.v1), the
# fact-provenance taxonomy (system_fact / authored_guidance / inferred),
# the authored-guidance store, the explanation builder and the
# hallucination-guard validator. It composes facts that other layers own
# (operator registry, workflow IR, provenance, derivation records) through
# narrow source interfaces — it owns no scientific truth itself.
#
# The core is pure C++20 + jsoncpp: no Qt, no QGIS, no engine linkage, so
# explanation contracts are testable on the lightest lane. Authoritative
# sources are attached by sicnu_explain_adapters (see adapters/), which is
# the only place allowed to link the operator registry / workflow runtime.

add_library(sicnu_explain STATIC
  explain_provenance.cpp
  explain_provenance.h
  explanation_builder.cpp
  explanation_builder.h
  explanation_validator.cpp
  explanation_validator.h
  guidance_store.cpp
  guidance_store.h
  state_vocabulary.cpp
  state_vocabulary.h
  step_explanation.cpp
  step_explanation.h
  step_explanation_view_model.cpp
  step_explanation_view_model.h
)

add_library(Sicnu::explain ALIAS sicnu_explain)

target_include_directories(sicnu_explain
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sicnu_explain
  PUBLIC
    jsoncpp
)

# Layer guard: the explanation core must stay GUI/engine/operator free, and
# it must never link the verifier or the grader — explanations cite their
# reports as facts, they do not re-run verification or grading (the three
# evidence semantics stay separate authorities; only a layer above may
# compose them).
foreach(SICNU_EXPLAIN_FORBIDDEN_LINK Qt6::Core Qt6::Widgets Qt6::Network
    qgis_core qgis_gui Sicnu::app Sicnu::operators Sicnu::help
    Sicnu::workflow_runtime Sicnu::data sicnu_verifier sicnu_grader)
  get_target_property(SICNU_EXPLAIN_DIRECT_LINKS sicnu_explain LINK_LIBRARIES)
  if(SICNU_EXPLAIN_FORBIDDEN_LINK IN_LIST SICNU_EXPLAIN_DIRECT_LINKS)
    message(FATAL_ERROR
      "sicnu_explain must not depend on forbidden target ${SICNU_EXPLAIN_FORBIDDEN_LINK}"
    )
  endif()
endforeach()

set_target_properties(sicnu_explain PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
