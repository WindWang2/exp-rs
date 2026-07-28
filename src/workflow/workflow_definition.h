// src/workflow/workflow_definition.h
#pragma once

// Workflow definition types live in workflow_types.h (StepDef, WorkflowDefinition, …).
// This header re-exports them so callers can include workflow_definition.h as planned.
#include "workflow_types.h"

namespace sicnu::workflow {

/// Converts WorkflowDefinition to JSON representation including step positions and connections.
Json::Value workflowDefinitionToJson( const WorkflowDefinition &def );

/// Parses WorkflowDefinition from JSON. Returns false on invalid schema.
bool workflowDefinitionFromJson( const Json::Value &json, WorkflowDefinition &def, std::string &error );

/// Performs topological sort on workflow steps. Returns false and sets error if a cycle is detected.
bool topologicalSortSteps( const WorkflowDefinition &def, std::vector<std::string> &orderedStepIds, std::string &error );

/// Checks if connecting sourcePortType to targetPortType is valid.
bool validatePortConnection( const std::string &sourcePortType, const std::string &targetPortType );

} // namespace sicnu::workflow
