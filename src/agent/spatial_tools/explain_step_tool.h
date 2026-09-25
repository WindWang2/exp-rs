// src/agent/spatial_tools/explain_step_tool.h
#pragma once

// RS14-15 Explainable Workflow (Slice G): the `explain:step` agent surface.
//
// One tool, two closed modes, fail-closed everywhere:
//   mode "operator"          — explain one operator call (live registry
//                              knowledge + authored guidance);
//   mode "workflow_document" — explain one node of an IR 2.0 document
//                              (projected through the adapters layer).
//
// The tool composes the explanation through StepExplanationBuilder, runs the
// hallucination-guard validator over its own output, and returns the
// canonical exp.step_explanation.v1 JSON plus a deterministic markdown
// rendering under an explicit serialized-bytes budget: when the budget is
// exceeded the markdown rendering is omitted and the truncation is reported
// honestly — the explanation document itself is never cut mid-fact.
//
// Registration lives with the other built-ins in spatial_tool.cpp
// (registerBuiltinTools); the agent surface shows the tool through the
// "explain:" allow-prefix (tool_catalog/surface_registry.cpp) and the
// spatial tool provider's namespace table.

#include "spatial_tool.h"

#include "explain/guidance_store.h"

#include <memory>

namespace sicnu::agent::spatial_tools {

/// The explain:step tool instance (also used by the registry registration in
/// spatial_tool.cpp; exposed so the narrow test lane can exercise execute()
/// without the full agent link set).
SpatialToolPtr createExplainStepTool();

/// Overrides the guidance-corpus directory (tests; empty restores the
/// default resolution policy). Thread-safe; resets the cached store.
void setExplainGuidanceDirectory( const std::string &directory );

/// Where the guidance corpus was last loaded from plus any load problems
/// (diagnostic honesty for the tool response).
std::string explainGuidanceLoadReport();

/// The same cached guidance store the tool answers from (default directory
/// resolution or the setExplainGuidanceDirectory override). UI explain
/// surfaces share this instance instead of re-resolving the corpus
/// directory, so agent and panel can never disagree about authored text.
/// Thread-safe; the returned raw pointer stays valid for the synchronous
/// render that borrows it (the shared_ptr cache keeps the store alive).
std::shared_ptr<const sicnu::explain::GuidanceStore> sharedExplainGuidanceStore();

} // namespace sicnu::agent::spatial_tools
