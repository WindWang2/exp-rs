// src/workflow/workflow_gate.cpp
#include "workflow_gate.h"

#include <string>

namespace sicnu::workflow {

namespace {

constexpr const char *kHasArtifactPrefix = "hasArtifact:";
constexpr const char *kParamNonEmptyPrefix = "paramNonEmpty:";

bool isParamValueNonEmpty( const Json::Value &params, const std::string &key )
{
  if ( !params.isObject() || !params.isMember( key ) )
    return false;

  const Json::Value &v = params[key];
  if ( v.isNull() )
    return false;
  if ( v.isString() )
    return !v.asString().empty();
  // Paths and other scalar-like values: treat as non-empty if convertible string is non-empty
  if ( v.isNumeric() || v.isBool() )
    return !v.asString().empty();
  if ( v.isArray() || v.isObject() )
    return !v.empty();
  return false;
}

bool artifactNonEmpty( const WorkflowSession &session, const std::string &name )
{
  return !session.artifact( name ).empty();
}

std::string failHint( const GateDef &gate, const std::string &fallback )
{
  if ( !gate.hint.empty() )
    return gate.hint;
  return fallback;
}

/// Returns true if the single gate predicate passes.
bool evaluateOne( const WorkflowSession &session, const GateDef &gate, std::string &outHint )
{
  const std::string &req = gate.require;

  if ( req.rfind( kHasArtifactPrefix, 0 ) == 0 )
  {
    const std::string name = req.substr( std::char_traits<char>::length( kHasArtifactPrefix ) );
    if ( name.empty() )
    {
      outHint = failHint( gate, "未知门禁条件: " + req );
      return false;
    }
    if ( !artifactNonEmpty( session, name ) )
    {
      outHint = failHint( gate, "需要产物: " + name );
      return false;
    }
    return true;
  }

  if ( req.rfind( kParamNonEmptyPrefix, 0 ) == 0 )
  {
    const std::string rest = req.substr( std::char_traits<char>::length( kParamNonEmptyPrefix ) );
    if ( rest.empty() )
    {
      outHint = failHint( gate, "未知门禁条件: " + req );
      return false;
    }

    std::string stepId;
    std::string key;
    const auto dot = rest.find( '.' );
    if ( dot != std::string::npos )
    {
      stepId = rest.substr( 0, dot );
      key = rest.substr( dot + 1 );
    }
    else
    {
      const StepDef *cur = session.currentStep();
      if ( !cur )
      {
        outHint = failHint( gate, "当前步骤未知，无法检查参数: " + rest );
        return false;
      }
      stepId = cur->id;
      key = rest;
    }

    if ( stepId.empty() || key.empty() )
    {
      outHint = failHint( gate, "未知门禁条件: " + req );
      return false;
    }

    if ( !isParamValueNonEmpty( session.paramsFor( stepId ), key ) )
    {
      outHint = failHint( gate, "参数不能为空: " + stepId + "." + key );
      return false;
    }
    return true;
  }

  // Unknown require form
  outHint = failHint( gate, "未知门禁条件: " + req );
  return false;
}

} // namespace

CanRunResult evaluateGates( const WorkflowSession &session, const std::vector<GateDef> &gates )
{
  CanRunResult result;
  result.ok = true;

  for ( const GateDef &gate : gates )
  {
    std::string hint;
    if ( !evaluateOne( session, gate, hint ) )
    {
      result.ok = false;
      if ( !hint.empty() )
        result.hints.push_back( std::move( hint ) );
    }
  }

  return result;
}

} // namespace sicnu::workflow
