// src/workflow/workflow_runtime.cpp
#include "workflow_runtime.h"

#include "workflow_gate.h"
#include "workflow_runner.h"

#include <sstream>
#include <stdexcept>

namespace sicnu::workflow {

namespace {

std::string jsonValueToArtifactString( const Json::Value &v )
{
  if ( v.isString() )
    return v.asString();
  if ( v.isNull() )
    return {};
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString( builder, v );
}

std::string joinHints( const std::vector<std::string> &hints )
{
  std::ostringstream oss;
  for ( size_t i = 0; i < hints.size(); ++i )
  {
    if ( i > 0 )
      oss << "; ";
    oss << hints[i];
  }
  return oss.str();
}

} // namespace

WorkflowRuntime::WorkflowRuntime( WorkflowRegistry &registry )
  : m_registry( registry )
{
}

std::string WorkflowRuntime::open( const std::string &definitionId )
{
  const WorkflowDefinition *def = m_registry.find( definitionId );
  if ( !def )
    return {};

  const std::string sessionId = "wf-" + std::to_string( m_nextId++ );
  m_sessions[sessionId] = std::make_unique<WorkflowSession>( *def, sessionId );
  return sessionId;
}

SessionSnapshot WorkflowRuntime::state( const std::string &sessionId ) const
{
  const WorkflowSession *s = sessionConst( sessionId );
  if ( !s )
    throw std::runtime_error( "Session not found: " + sessionId );
  return s->snapshot();
}

bool WorkflowRuntime::gotoStep( const std::string &sessionId, const std::string &stepId )
{
  WorkflowSession *s = sessionMut( sessionId );
  if ( !s )
    return false;
  return s->gotoStep( stepId );
}

void WorkflowRuntime::setParams( const std::string &sessionId, const std::string &stepId, const Json::Value &params )
{
  WorkflowSession *s = sessionMut( sessionId );
  if ( !s )
    throw std::runtime_error( "Session not found: " + sessionId );
  s->setParams( stepId, params );
}

CanRunResult WorkflowRuntime::canRun( const std::string &sessionId, const std::string &stepId ) const
{
  CanRunResult result;
  const WorkflowSession *s = sessionConst( sessionId );
  if ( !s )
  {
    result.ok = false;
    result.hints.push_back( "Session not found: " + sessionId );
    return result;
  }

  const StepDef *step = s->stepById( stepId );
  if ( !step )
  {
    result.ok = false;
    result.hints.push_back( "Step not found: " + stepId );
    return result;
  }

  return evaluateGates( *s, step->gates );
}

Json::Value WorkflowRuntime::runStep( const std::string &sessionId, const std::string &stepId )
{
  WorkflowSession *s = sessionMut( sessionId );
  if ( !s )
    throw std::runtime_error( "Session not found: " + sessionId );

  const StepDef *step = s->stepById( stepId );
  if ( !step )
    throw std::runtime_error( "Step not found: " + stepId );

  const CanRunResult can = canRun( sessionId, stepId );
  if ( !can.ok )
  {
    const std::string msg = can.hints.empty()
                              ? std::string( "Step cannot run: gates failed" )
                              : joinHints( can.hints );
    throw std::runtime_error( msg );
  }

  if ( step->kind != StepKind::Operator )
  {
    throw std::runtime_error( "Step is not an Operator step: " + stepId );
  }

  if ( step->operatorId.empty() )
  {
    throw std::runtime_error( "Step has empty operatorId: " + stepId );
  }

  const Json::Value params = s->paramsFor( stepId );
  const Json::Value result = WorkflowRunner::run( step->operatorId, params );

  // Artifact side-effects from operator result
  if ( result.isMember( "output" ) && result["output"].isString() )
  {
    const std::string name = step->artifactOnSuccess.empty() ? "output" : step->artifactOnSuccess;
    s->setArtifact( name, result["output"].asString() );
  }
  else if ( result.isMember( "result" ) )
  {
    // Prefer "result" when artifactOnSuccess is the default "output" (non-file operators).
    const std::string name =
      ( step->artifactOnSuccess.empty() || step->artifactOnSuccess == "output" )
        ? "result"
        : step->artifactOnSuccess;
    s->setArtifact( name, jsonValueToArtifactString( result["result"] ) );
  }
  else if ( !step->artifactOnSuccess.empty() )
  {
    s->setArtifact( step->artifactOnSuccess, jsonValueToArtifactString( result ) );
  }

  s->markStepComplete( stepId );
  return result;
}

void WorkflowRuntime::markStepComplete( const std::string &sessionId, const std::string &stepId )
{
  WorkflowSession *s = sessionMut( sessionId );
  if ( !s )
    return;
  s->markStepComplete( stepId );
}

void WorkflowRuntime::setArtifact( const std::string &sessionId, const std::string &name, const std::string &value )
{
  WorkflowSession *s = sessionMut( sessionId );
  if ( !s )
    return;
  s->setArtifact( name, value );
}

void WorkflowRuntime::close( const std::string &sessionId )
{
  m_sessions.erase( sessionId );
}

WorkflowSession *WorkflowRuntime::sessionMut( const std::string &sessionId )
{
  const auto it = m_sessions.find( sessionId );
  if ( it == m_sessions.end() )
    return nullptr;
  return it->second.get();
}

const WorkflowSession *WorkflowRuntime::sessionConst( const std::string &sessionId ) const
{
  const auto it = m_sessions.find( sessionId );
  if ( it == m_sessions.end() )
    return nullptr;
  return it->second.get();
}

} // namespace sicnu::workflow
