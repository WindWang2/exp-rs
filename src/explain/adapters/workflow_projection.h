/***************************************************************************
 * workflow_projection.h — workflow documents → ExplanationRequest
 *
 * Normalizes the two workflow representations the platform owns into the
 * explain layer's ExplanationRequest (the builder only ever sees the
 * normalization):
 *
 *   - WorkflowDocument  (IR 2.0, the D17 designer document, Qt-based):
 *     node facts project 1:1, ports carry crs/radiometricState/resolution/
 *     bandCount and the radiometric vocabulary is shared with the explain
 *     state vocabulary (pinned by test_explain_projection).
 *   - WorkflowDefinition/StepDef (Engine 2.0 guided pipeline, jsoncpp):
 *     steps carry no typed port facts, so ports stay empty — facts the
 *     representation does not declare stay unknown.
 *
 * Fail-closed: an unknown node id answers nullopt; an operator-less
 * operator node or a state token outside the closed vocabulary yields a
 * typed projection problem and degrades honestly (empty field), never an
 * invented token.
 ***************************************************************************/
#pragma once

#include "explain/explanation_request.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::workflow
{
class WorkflowDocument;
struct WorkflowDefinition;
struct StepDef;
}

namespace sicnu::explain::adapters
{

struct ProjectionProblem
{
  std::string code; // typed: node_unknown | operator_missing | state_token_unknown
  std::string field;
  std::string message;
};

// D17 designer projection: WorkflowDocument + node id → request.
// Returns nullopt only for an unknown node id (typed problem recorded).
std::optional<ExplanationRequest> projectWorkflowDocument( const sicnu::workflow::WorkflowDocument &document,
                                                           const std::string &nodeId,
                                                           std::vector<ProjectionProblem> &problems );

// Engine 2.0 projection: WorkflowDefinition step + optional execution status
// (a StepPlan status string; empty when the step was never executed).
// Returns nullopt only for an unknown step id (typed problem recorded).
std::optional<ExplanationRequest> projectEngine2Step( const sicnu::workflow::WorkflowDefinition &definition,
                                                      const std::string &stepId,
                                                      const std::string &executionStatus,
                                                      std::vector<ProjectionProblem> &problems );

} // namespace sicnu::explain::adapters
