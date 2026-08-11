// src/processing/framework/schema_validator.h
#pragma once

#include "algorithm_descriptor.h"

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::processing {

/// Policy for parameters that are not declared by the algorithm schema.
enum class UnknownParameterPolicy
{
  Ignore,  ///< Unknown parameters pass silently (not reported at all).
  Warn,    ///< Unknown parameters are reported as warnings.
  Error    ///< Unknown parameters are validation errors.
};

/// A single structured parameter issue (code/parameter/expected/actual/message).
struct ParameterIssue
{
  std::string code;
  std::string parameter;
  std::string expected;
  std::string actual;
  std::string message;

  Json::Value toJson() const;
};

struct ParameterValidationResult
{
  std::vector<ParameterIssue> errors;
  std::vector<ParameterIssue> warnings;

  bool ok() const { return errors.empty(); }
  Json::Value toJson() const;
};

/**
 * Validates @a params against the descriptor's input ports. Shared by the
 * ToolCallDispatcher, MCP and any agent-facing surface so parameter checking
 * is implemented exactly once.
 *
 * Checks: required presence, JSON type, enum membership, numeric
 * minimum/maximum, array item types, basic raster/vector file shape, and the
 * unknown-parameter policy. Does NOT touch the filesystem — file existence and
 * dataset-level contracts belong to preflight.
 */
ParameterValidationResult validateParameters( const Json::Value &params,
                                              const AlgorithmDescriptor &desc,
                                              UnknownParameterPolicy unknown =
                                                UnknownParameterPolicy::Error );

} // namespace sicnu::processing
