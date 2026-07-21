/***************************************************************************
 * rs_georef_workflow_bridge.h
 *
 * Thin adapter: georeferencer I2M workspace ↔ WorkflowRuntime session for
 * lab.georef.image_to_map.
 *
 * Dual-write note (same phase pattern as classify):
 *   UI / RsWarpTask remain authority for warp, fit, and tool enablement.
 *   This bridge only opens a Runtime session and mirrors pure artifacts
 *   (source_raster, gcp_count, output) plus optional current step.
 ***************************************************************************/
#pragma once

#include "workflow/workflow_registry.h"
#include "workflow/workflow_runtime.h"

#include <string>

/// Lightweight open/sync helper for the Image-to-Map georef window.
class RsGeorefWorkflowBridge
{
  public:
    static constexpr const char *kDefinitionId = "lab.georef.image_to_map";

    RsGeorefWorkflowBridge();

    /// Register builtins (once) and open a lab.georef.image_to_map session.
    /// @return true if session id is non-empty.
    bool open();
    void close();

    bool isOpen() const { return !m_sessionId.empty(); }
    const std::string &sessionId() const { return m_sessionId; }

    void gotoStep( const std::string &stepId );
    void markStepComplete( const std::string &stepId );

    void setSourceRasterArtifact( const std::string &path );
    /// Soft-gate artifact: non-empty string when count > 0 (e.g. "3").
    void setGcpCountArtifact( int count );
    void setOutputArtifact( const std::string &path );

    sicnu::workflow::WorkflowRuntime &runtime() { return m_runtime; }
    const sicnu::workflow::WorkflowRegistry &registry() const { return m_registry; }

  private:
    // Member order: registry must outlive runtime (runtime holds a reference).
    sicnu::workflow::WorkflowRegistry m_registry;
    sicnu::workflow::WorkflowRuntime m_runtime;
    std::string m_sessionId;
    bool m_builtinsRegistered = false;
};
