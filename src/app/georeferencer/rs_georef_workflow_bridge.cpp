/***************************************************************************
 * rs_georef_workflow_bridge.cpp
 ***************************************************************************/
#include "rs_georef_workflow_bridge.h"

#include "workflow/builtin_definitions.h"

RsGeorefWorkflowBridge::RsGeorefWorkflowBridge()
  : m_runtime( m_registry )
{
}

bool RsGeorefWorkflowBridge::open()
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

void RsGeorefWorkflowBridge::close()
{
  if ( m_sessionId.empty() )
    return;
  m_runtime.close( m_sessionId );
  m_sessionId.clear();
}

void RsGeorefWorkflowBridge::gotoStep( const std::string &stepId )
{
  if ( m_sessionId.empty() || stepId.empty() )
    return;
  m_runtime.gotoStep( m_sessionId, stepId );
}

void RsGeorefWorkflowBridge::markStepComplete( const std::string &stepId )
{
  if ( m_sessionId.empty() || stepId.empty() )
    return;
  m_runtime.markStepComplete( m_sessionId, stepId );
}

void RsGeorefWorkflowBridge::setSourceRasterArtifact( const std::string &path )
{
  if ( m_sessionId.empty() || path.empty() )
    return;
  m_runtime.setArtifact( m_sessionId, "source_raster", path );
}

void RsGeorefWorkflowBridge::setGcpCountArtifact( int count )
{
  if ( m_sessionId.empty() )
    return;
  if ( count > 0 )
    m_runtime.setArtifact( m_sessionId, "gcp_count", std::to_string( count ) );
  else
    m_runtime.setArtifact( m_sessionId, "gcp_count", std::string() );
}

void RsGeorefWorkflowBridge::setOutputArtifact( const std::string &path )
{
  if ( m_sessionId.empty() || path.empty() )
    return;
  m_runtime.setArtifact( m_sessionId, "output", path );
}
