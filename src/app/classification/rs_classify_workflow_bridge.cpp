/***************************************************************************
 * rs_classify_workflow_bridge.cpp
 ***************************************************************************/
#include "rs_classify_workflow_bridge.h"

#include "workflow/builtin_definitions.h"

namespace {

const char *const kStepIds[] = {
  "classes",
  "samples",
  "evaluate",
  "train",
  "accuracy",
  "post",
  "export",
};

static_assert( sizeof( kStepIds ) / sizeof( kStepIds[0] )
                 == static_cast<int>( RsClassifyStep::Count ),
               "step id count must match RsClassifyStep::Count" );

} // namespace

RsClassifyWorkflowBridge::RsClassifyWorkflowBridge()
  : m_runtime( m_registry )
{
}

bool RsClassifyWorkflowBridge::open()
{
  if ( !m_builtinsRegistered )
  {
    sicnu::workflow::registerBuiltinWorkflows( m_registry );
    m_builtinsRegistered = true;
  }

  if ( !m_sessionId.empty() )
    m_runtime.close( m_sessionId );

  m_sessionId = m_runtime.open( kDefinitionId );
  return !m_sessionId.empty();
}

void RsClassifyWorkflowBridge::close()
{
  if ( m_sessionId.empty() )
    return;
  m_runtime.close( m_sessionId );
  m_sessionId.clear();
}

const char *RsClassifyWorkflowBridge::stepId( RsClassifyStep s )
{
  const int i = static_cast<int>( s );
  if ( i < 0 || i >= static_cast<int>( RsClassifyStep::Count ) )
    return nullptr;
  return kStepIds[i];
}

bool RsClassifyWorkflowBridge::stepFromId( const std::string &id, RsClassifyStep *out )
{
  for ( int i = 0; i < static_cast<int>( RsClassifyStep::Count ); ++i )
  {
    if ( id == kStepIds[i] )
    {
      if ( out )
        *out = static_cast<RsClassifyStep>( i );
      return true;
    }
  }
  return false;
}

void RsClassifyWorkflowBridge::gotoStep( RsClassifyStep s )
{
  if ( m_sessionId.empty() )
    return;
  const char *id = stepId( s );
  if ( !id )
    return;
  m_runtime.gotoStep( m_sessionId, id );
}

void RsClassifyWorkflowBridge::syncCompletionsFromController(
  const RsClassifyWorkflowController &ctrl )
{
  if ( m_sessionId.empty() )
    return;
  for ( int i = 0; i < static_cast<int>( RsClassifyStep::Count ); ++i )
  {
    const auto step = static_cast<RsClassifyStep>( i );
    if ( !ctrl.isStepComplete( step ) )
      continue;
    const char *id = stepId( step );
    if ( id )
      m_runtime.markStepComplete( m_sessionId, id );
  }
}

void RsClassifyWorkflowBridge::setSourceRasterArtifact( const std::string &path )
{
  if ( m_sessionId.empty() || path.empty() )
    return;
  m_runtime.setArtifact( m_sessionId, "source_raster", path );
}

void RsClassifyWorkflowBridge::setClassifiedOutputArtifact( const std::string &path )
{
  if ( m_sessionId.empty() || path.empty() )
    return;
  m_runtime.setArtifact( m_sessionId, "classified_output", path );
}
