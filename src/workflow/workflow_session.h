// src/workflow/workflow_session.h
#pragma once

#include "workflow_types.h"

#include <json/json.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::workflow {

// Session state is guarded by an internal mutex (#503, restored after a
// parallel-fix clobber): runStep()/requestCancel() may be used from worker
// threads while the GUI thread snapshots. m_def is immutable after
// construction, so definition()/stepById() read it without the lock.
class WorkflowSession
{
  public:
    WorkflowSession( WorkflowDefinition def, std::string sessionId );

    SessionSnapshot snapshot() const;

    bool gotoStep( const std::string &stepId );
    void setParams( const std::string &stepId, const Json::Value &params );
    Json::Value paramsFor( const std::string &stepId ) const;
    Json::Value resolveParams( const std::string &stepId ) const;

    void setArtifact( const std::string &name, const std::string &value );
    bool hasArtifact( const std::string &name ) const;
    std::string artifact( const std::string &name ) const;

    void markStepComplete( const std::string &stepId );
    void setMode( SessionMode mode );
    void setDirty( bool d );

    void setPipelineId( long pipelineId );
    long pipelineId() const;

    void setPipelineStatusResolver( PipelineStatusResolver resolver );

    const WorkflowDefinition &definition() const;
    const StepDef *currentStep() const;
    const StepDef *stepById( const std::string &id ) const;

  private:
    Json::Value paramsForUnlocked( const std::string &stepId ) const;
    SessionSnapshot snapshotUnlocked() const;

    WorkflowDefinition m_def;
    std::string m_sessionId;
    std::string m_currentStepId;
    std::vector<std::string> m_completed;
    long m_pipelineId = -1;
    SessionMode m_mode = SessionMode::Wizard;
    bool m_dirty = false;
    Json::Value m_paramsByStep;
    std::unordered_map<std::string, std::string> m_artifacts;
    PipelineStatusResolver m_pipelineResolver;
    mutable std::mutex m_mutex;
};

} // namespace sicnu::workflow
