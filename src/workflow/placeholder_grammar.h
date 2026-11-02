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
 *   2. for the canonical "output" port only: @p canonicalOutputPath when
 *      non-empty, else resultPayload["output"] (#1775/WE-14 — a named port
 *      the producer never declared no longer falls back to the default
 *      output; it stays unresolved so the caller can refuse).
 * Returns an empty string when nothing matches — the caller leaves the
 * placeholder unresolved, identical on both paths.
 */
std::string resolvePlaceholderPort( const Json::Value &resultPayload,
                                    const std::string &canonicalOutputPath,
                                    const std::string &portName );

/**
 * #1780 (WE-21): true when @p text still carries a step/task placeholder
 * ($step.port, ${task.parent.output}, …). The TaskCenter pre-launch gate
 * refuses tasks whose substituted parameters test positive — the literal
 * reference must never be dispatched to an operator.
 */
bool containsUnresolvedStepPlaceholder( const std::string &text );

/**
 * #1776 (WE-15): true when the env name is secret-shaped (key/token/secret/
 * password/credential fragments) and must never be expanded, even under an
 * allow-listed prefix.
 */
bool envNameLooksSecret( const std::string &name );

/**
 * #1776 (WE-15): true when a workflow parameter may expand the env var
 * @p name (shipped pipeline conventions + SICNU_/EXP_RS_ namespaces).
 * Everything else stays unresolved instead of reading arbitrary process
 * env into resolvedParams/provenance.
 */
bool isEnvExpansionAllowed( const std::string &name );

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
