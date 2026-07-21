// src/workflow/workflow_session.cpp
#include "workflow_session.h"

#include <algorithm>

namespace sicnu::workflow {

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
  snap.paramsByStep = m_paramsByStep;
  snap.artifacts = m_artifacts;
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
  if ( !stepById( stepId ) )
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
