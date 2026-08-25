// src/workflow/workflow_runtime.cpp
#include "workflow_runtime.h"

#include "builtin_definitions.h"
#include "workflow_gate.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

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

WorkflowRuntime::WorkflowRuntime( bool loadBuiltins )
{
  if ( loadBuiltins )
  {
    registerBuiltinWorkflows( *this );
  }
}

void WorkflowRuntime::registerDefinition( WorkflowDefinition def )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const std::string id = def.id;
  m_defs.insert_or_assign( id, std::move( def ) );
}

bool WorkflowRuntime::hasDefinition( const std::string &id ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_defs.find( id ) != m_defs.end();
}

const WorkflowDefinition *WorkflowRuntime::findDefinition( const std::string &id ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_defs.find( id );
  if ( it == m_defs.end() )
    return nullptr;
  return &it->second;
}

std::vector<std::string> WorkflowRuntime::registeredDefinitionIds() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  std::vector<std::string> out;
  out.reserve( m_defs.size() );
  for ( const auto &kv : m_defs )
    out.push_back( kv.first );
  return out;
}

std::string WorkflowRuntime::open( const std::string &definitionId )
{
  const WorkflowDefinition *def = findDefinition( definitionId );
  if ( !def )
    return {};

  const std::string sessionId = "wf-" + std::to_string( m_nextId++ );
  m_sessions[sessionId] = std::make_unique<WorkflowSession>( *def, sessionId );
  m_cancelFlags[sessionId] = std::make_shared<std::atomic<bool>>( false );
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

  const Json::Value params = s->resolveParams( stepId );

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( step->operatorId );
  if ( !op )
  {
    throw std::runtime_error( "Operator not found: " + step->operatorId );
  }

  sicnu::operators::RSOperatorContext context;
  // Wire the session's cooperative cancellation flag into the operator context
  // so requestCancel() aborts a long-running operator step mid-run. The
  // shared_ptr local keeps the flag alive for the whole step even if close()
  // erases the session's map entry concurrently (no use-after-free).
  const auto cancelFlagPtr = cancelFlag( sessionId );
  if ( cancelFlagPtr )
  {
    context.setCancelFlag( cancelFlagPtr.get() );
  }
  Json::Value result;
  try
  {
    result = op->execute( params, context );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    throw std::runtime_error( e.message() );
  }

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

void WorkflowRuntime::requestCancel( const std::string &sessionId )
{
  auto flag = cancelFlag( sessionId );
  if ( flag )
    flag->store( true, std::memory_order_release );
}

void WorkflowRuntime::close( const std::string &sessionId )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_sessions.erase( sessionId );
  m_cancelFlags.erase( sessionId );
}

std::shared_ptr<std::atomic<bool>> WorkflowRuntime::cancelFlag( const std::string &sessionId )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_cancelFlags.find( sessionId );
  if ( it == m_cancelFlags.end() )
    return nullptr;
  return it->second;
}

WorkflowSession *WorkflowRuntime::sessionMut( const std::string &sessionId )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_sessions.find( sessionId );
  if ( it == m_sessions.end() )
    return nullptr;
  return it->second.get();
}

const WorkflowSession *WorkflowRuntime::sessionConst( const std::string &sessionId ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_sessions.find( sessionId );
  if ( it == m_sessions.end() )
    return nullptr;
  return it->second.get();
}

} // namespace sicnu::workflow
