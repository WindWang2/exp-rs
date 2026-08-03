// src/workflow/workflow_session.h
#pragma once

#include "workflow_types.h"

#include <json/json.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::workflow {

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

    void markStepComplete( const std::string &stepId );
    void setMode( SessionMode mode );
    void setDirty( bool d );

    void setPipelineId( long pipelineId ) { m_pipelineId = pipelineId; }
    long pipelineId() const { return m_pipelineId; }

    const WorkflowDefinition &definition() const;
    const StepDef *currentStep() const;
    const StepDef *stepById( const std::string &id ) const;

  private:
    WorkflowDefinition m_def;
    std::string m_sessionId;
    std::string m_currentStepId;
    std::vector<std::string> m_completed;
    long m_pipelineId = -1;
    SessionMode m_mode = SessionMode::Wizard;
    bool m_dirty = false;
    Json::Value m_paramsByStep;
    std::unordered_map<std::string, std::string> m_artifacts;
};

} // namespace sicnu::workflow
