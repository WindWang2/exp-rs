// src/workflow/placeholder_grammar.h — Unified placeholder grammar module for task pipelines
#pragma once

#include "workflow_types.h"

#include <functional>
#include <string>
#include <vector>

namespace sicnu::workflow {

struct PlaceholderRef
{
  std::string rawRef;           // e.g. "$step1.output" or "${step1.output}"
  std::string stepId;           // e.g. "step1", or empty if task id / env var ref
  long parentTaskId = -1;       // e.g. 12 if ${task.12.output}, else -1
  bool isParentKeyword = false; // true if ${task.parent.output}
  bool isEnvVar = false;        // true if ${ENV_VAR} or ${env.VAR}
  std::string envVarName;       // name of the environment variable e.g. "WORK"
  std::string portName = "output"; // e.g. "output" or custom port name

  bool isValid() const
  {
    return !stepId.empty() || parentTaskId >= 0 || isParentKeyword || isEnvVar;
  }
};

/**
 * Parses all placeholder references from an input string value.
 * Supports: $stepId.output, ${stepId.output}, ${stepId.portName},
 * ${task.12.output}, ${task.parent.output}.
 */
std::vector<PlaceholderRef> parsePlaceholders( const std::string &text );

/**
 * Resolves the port a placeholder references against ONE completed step's
 * result payload and canonical output path. This is the single port
 * resolution policy for both fresh dispatch (TaskCenter) and crash resume
 * (WorkflowRunCoordinator) — they must never drift (#727):
 *   1. resultPayload[portName] (exact key, string, non-empty);
 *   2. the canonical/default output: @p canonicalOutputPath when non-empty,
 *      else resultPayload["output"];
 *   3. case-insensitive portName scan of the payload's string values.
 * Returns an empty string when nothing matches — the caller leaves the
 * placeholder unresolved, identical on both paths.
 */
std::string resolvePlaceholderPort( const Json::Value &resultPayload,
                                    const std::string &canonicalOutputPath,
                                    const std::string &portName );

/**
 * Replaces placeholders in text using a resolver callback.
 */
std::string substitutePlaceholders( const std::string &text,
                                    const std::function<std::string( const PlaceholderRef &ref )> &resolver );

/**
 * Infers StepConnection objects for a given parameter key and string value containing placeholders.
 */
std::vector<StepConnection> inferStepConnections( const std::string &paramKey, const std::string &paramValue );

} // namespace sicnu::workflow
