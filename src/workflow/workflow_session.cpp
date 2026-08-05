#include "workflow_session.h"
#include "placeholder_grammar.h"

#include <algorithm>

namespace sicnu::workflow {

namespace {

Json::Value resolveValuePlaceholders(
  const Json::Value &val,
  const std::function<std::string( const PlaceholderRef &ref )> &resolver )
{
  if ( val.isString() )
  {
    return substitutePlaceholders( val.asString(), resolver );
  }
  else if ( val.isArray() )
  {
    Json::Value arr( Json::arrayValue );
    for ( const auto &item : val )
    {
      arr.append( resolveValuePlaceholders( item, resolver ) );
    }
    return arr;
  }
  else if ( val.isObject() )
  {
    Json::Value obj( Json::objectValue );
    for ( auto it = val.begin(); it != val.end(); ++it )
    {
      obj[it.name()] = resolveValuePlaceholders( *it, resolver );
    }
    return obj;
  }
  return val;
}

} // namespace

WorkflowSession::WorkflowSession( WorkflowDefinition def, std::string sessionId )
  : m_def( std::move( def ) )
  , m_sessionId( std::move( sessionId ) )
  , m_paramsByStep( Json::objectValue )
{
  if ( !m_def.steps.empty() )
    m_currentStepId = m_def.steps.front().id;
}

SessionSnapshot WorkflowSession::snapshot() const
{
  SessionSnapshot snap;
  snap.sessionId = m_sessionId;
  snap.definitionId = m_def.id;
  snap.currentStepId = m_currentStepId;
  snap.completedStepIds = m_completed;
  snap.mode = m_mode;
  snap.dirty = m_dirty;
  snap.pipelineId = m_pipelineId;
  snap.paramsByStep = m_paramsByStep;
  snap.artifacts = m_artifacts;

  // ── Authoritative pipeline step status enrichment ──────────────────
  // When bound to a TaskCenter pipeline, query the resolver for
  // real-time step statuses and merge completed steps that the local
  // m_completed list may not yet know about (async DAG execution).
  if ( m_pipelineId >= 0 && m_pipelineResolver )
  {
    const auto statuses = m_pipelineResolver( m_pipelineId );
    for ( const auto &[stepId, status] : statuses )
    {
      if ( status == PipelineStepStatus::Completed && stepById( stepId ) )
      {
        if ( std::find( snap.completedStepIds.begin(), snap.completedStepIds.end(), stepId ) == snap.completedStepIds.end() )
          snap.completedStepIds.push_back( stepId );
      }
    }
  }

  return snap;
}

bool WorkflowSession::gotoStep( const std::string &stepId )
{
  if ( !stepById( stepId ) )
    return false;
  m_currentStepId = stepId;
  return true;
}

void WorkflowSession::setParams( const std::string &stepId, const Json::Value &params )
{
  m_paramsByStep[stepId] = params;
  m_dirty = true;
}

Json::Value WorkflowSession::paramsFor( const std::string &stepId ) const
{
  if ( !m_paramsByStep.isObject() || !m_paramsByStep.isMember( stepId ) )
    return Json::Value( Json::objectValue );
  return m_paramsByStep[stepId];
}

Json::Value WorkflowSession::resolveParams( const std::string &stepId ) const
{
  const Json::Value raw = paramsFor( stepId );
  if ( raw.isNull() || !raw.isObject() )
    return raw;

  auto resolver = [this]( const PlaceholderRef &ref ) -> std::string {
    if ( !ref.stepId.empty() )
    {
      const StepDef *s = stepById( ref.stepId );
      if ( s && !s->artifactOnSuccess.empty() )
      {
        auto it = m_artifacts.find( s->artifactOnSuccess );
        if ( it != m_artifacts.end() )
          return it->second;
      }
      std::string qualifiedKey = ref.stepId + "." + ref.portName;
      auto itKey = m_artifacts.find( qualifiedKey );
      if ( itKey != m_artifacts.end() )
        return itKey->second;
    }
    auto itPort = m_artifacts.find( ref.portName );
    if ( itPort != m_artifacts.end() )
      return itPort->second;

    auto itRaw = m_artifacts.find( ref.rawRef );
    if ( itRaw != m_artifacts.end() )
      return itRaw->second;

    return ref.rawRef;
  };

  return resolveValuePlaceholders( raw, resolver );
}

void WorkflowSession::setArtifact( const std::string &name, const std::string &value )
{
  m_artifacts[name] = value;
}

bool WorkflowSession::hasArtifact( const std::string &name ) const
{
  return m_artifacts.find( name ) != m_artifacts.end();
}

void WorkflowSession::markStepComplete( const std::string &stepId )
{
  if ( !stepById( stepId ) || m_completed.size() >= m_def.steps.size() )
    return;
  if ( std::find( m_completed.begin(), m_completed.end(), stepId ) == m_completed.end() )
    m_completed.push_back( stepId );
}

void WorkflowSession::setMode( SessionMode mode )
{
  m_mode = mode;
}

void WorkflowSession::setDirty( bool d )
{
  m_dirty = d;
}

const WorkflowDefinition &WorkflowSession::definition() const
{
  return m_def;
}

const StepDef *WorkflowSession::currentStep() const
{
  return stepById( m_currentStepId );
}

const StepDef *WorkflowSession::stepById( const std::string &id ) const
{
  for ( const auto &step : m_def.steps )
  {
    if ( step.id == id )
      return &step;
  }
  return nullptr;
}

} // namespace sicnu::workflow
