/***************************************************************************
 * explanation_request.h — normalized per-step projection target
 *
 * One request shape serves every workflow representation: a D17 designer
 * node, a Workflow Engine 2.0 StepDef, a LabSpec lab stage or a bare
 * agent operator call. Representation-specific adapters (see
 * explain/adapters/) map their native types onto this value object; the
 * explanation builder only ever sees the normalization.
 ***************************************************************************/
#pragma once

#include "explain/explain_provenance.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::explain
{

// Read-only view of one workflow port fact. `stateToken` must be a member
// of the state vocabulary (state_vocabulary.h); empty means "not declared".
struct PortFactView
{
  std::string portName;
  std::string dataType;
  std::string stateToken;
  std::string crs;
  std::optional<double> resolutionX;
  std::optional<double> resolutionY;
  std::optional<int> bandCount;
};

struct ExplanationRequest
{
  std::string workflowKind; // closed vocabulary, explain_provenance.h
  std::string workflowId;
  std::string workflowTitle;
  std::string stepId;
  std::string stepTitle;
  std::string stepKind; // closed vocabulary, explain_provenance.h

  std::string operatorId;      // empty for non-operator steps
  Json::Value parameters;      // configured parameter values (object)

  std::vector<PortFactView> inputPorts;
  std::vector<PortFactView> outputPorts;

  // Optional execution context: when present, the builder attaches
  // execution facts and (for Skipped steps) runtime skip evidence.
  std::string runId;
  std::string executionStatus;

  bool isOperatorStep() const { return stepKind == StepKindOperator && !operatorId.empty(); }
};

// Universal factory for ad-hoc operator explanations (agent single-step
// calls, lab stages constructed from LabSpec fields, tests). Every workflow
// representation normalizes into the same request shape; representation
// specific adapters (see explain/adapters/) build on this.
inline ExplanationRequest makeOperatorRequest( const std::string &workflowKind,
                                               const std::string &workflowId,
                                               const std::string &stepId,
                                               const std::string &stepTitle,
                                               const std::string &operatorId,
                                               Json::Value parameters = Json::Value() )
{
  ExplanationRequest request;
  request.workflowKind = workflowKind;
  request.workflowId = workflowId;
  request.stepId = stepId;
  request.stepTitle = stepTitle;
  request.stepKind = StepKindOperator;
  request.operatorId = operatorId;
  request.parameters = parameters.isObject() ? parameters : Json::Value( Json::objectValue );
  return request;
}

} // namespace sicnu::explain
