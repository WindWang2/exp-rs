// src/workflow/workflow_types.h
#pragma once
#include <json/json.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace sicnu::workflow {

enum class StepKind { Operator, Interactive, Review, Composite };
enum class HostKind { TaskPanel, Workspace };
enum class SessionMode { Wizard, Expert };

struct GateDef {
  std::string require; // e.g. "hasArtifact:output", "paramNonEmpty:input"
  std::string hint;
};

struct StepUiMeta {
  double x = 0.0;
  double y = 0.0;
};

struct StepConnection {
  std::string fromStepId;
  std::string fromPort; // e.g. "output" or "result"
  std::string toPort;   // e.g. "input" or "A"
};

struct StepDef {
  std::string id;
  std::string title;
  StepKind kind = StepKind::Operator;
  std::string operatorId; // when kind == Operator
  std::vector<GateDef> gates;
  std::string artifactOnSuccess = "output"; // default artifact name from operator result["output"]
  StepUiMeta uiMeta;
  std::vector<StepConnection> inputs; // incoming dependency edges
};

struct WorkflowDefinition {
  std::string id;
  std::string title;
  HostKind host = HostKind::TaskPanel;
  std::string workspaceKind; // classify | georef | obia | empty
  std::vector<StepDef> steps;
};

struct CanRunResult {
  bool ok = true;
  std::vector<std::string> hints;
};

struct SessionSnapshot {
  std::string sessionId;
  std::string definitionId;
  std::string currentStepId;
  std::vector<std::string> completedStepIds;
  SessionMode mode = SessionMode::Wizard;
  bool dirty = false;
  Json::Value paramsByStep; // object: stepId -> params object
  std::unordered_map<std::string, std::string> artifacts; // name -> path/value
};

} // namespace sicnu::workflow
