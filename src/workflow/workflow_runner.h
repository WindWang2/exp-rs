// src/workflow/workflow_runner.h
#pragma once

#include <json/json.h>

#include <string>

namespace sicnu::workflow {

/// Synchronous operator execution bridge (no Qt Widgets).
/// Creates operators from RSOperatorRegistry and runs them with RSOperatorContext.
class WorkflowRunner
{
  public:
    /// Run operator by id with JSON params.
    /// @throws std::runtime_error if operator missing or RSOperatorError occurs
    static Json::Value run( const std::string &operatorId, const Json::Value &params );
};

} // namespace sicnu::workflow
