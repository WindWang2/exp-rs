/***************************************************************************
 * rs_classify_workflow_bridge.h
 *
 * Thin adapter: classification workspace ↔ WorkflowRuntime session for
 * lab.classify.supervised.
 *
 * Phase note (dual-write):
 *   RsClassifyWorkflowController remains the gate authority for
 *   classify-specific flags (class count, train pixels, evaluate-reviewed,
 *   post skipped, etc.). This bridge only mirrors current step id and
 *   completed step ids into the runtime session, plus optional pure
 *   artifacts (source_raster, classified_output) for GateDef soft hints.
 *   Do not dual-write long-term — move flags into session extension later.
 ***************************************************************************/
#pragma once

#include "rs_classify_workflow_controller.h"
#include "workflow/workflow_registry.h"
#include "workflow/workflow_runtime.h"

#include <string>

/// Lightweight open/sync helper for the classification main window.
class RsClassifyWorkflowBridge
{
  public:
    static constexpr const char *kDefinitionId = "lab.classify.supervised";

    RsClassifyWorkflowBridge();

    /// Register builtins (once) and open a lab.classify.supervised session.
    /// @return true if session id is non-empty.
    bool open();
    void close();

    bool isOpen() const { return !m_sessionId.empty(); }
    const std::string &sessionId() const { return m_sessionId; }

    /// Map RsClassifyStep ↔ definition step ids (classes…export).
    static const char *stepId( RsClassifyStep s );
    static bool stepFromId( const std::string &id, RsClassifyStep *out );

    void gotoStep( RsClassifyStep s );
    /// Mark complete steps according to controller.isStepComplete (additive).
    void syncCompletionsFromController( const RsClassifyWorkflowController &ctrl );

    void setSourceRasterArtifact( const std::string &path );
    void setClassifiedOutputArtifact( const std::string &path );

    sicnu::workflow::WorkflowRuntime &runtime() { return m_runtime; }
    const sicnu::workflow::WorkflowRegistry &registry() const { return m_registry; }

  private:
    // Member order: registry must outlive runtime (runtime holds a reference).
    sicnu::workflow::WorkflowRegistry m_registry;
    sicnu::workflow::WorkflowRuntime m_runtime;
    std::string m_sessionId;
    bool m_builtinsRegistered = false;
};
