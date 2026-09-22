// src/agent_ops/ops_projection.h
#pragma once

//
// Feature H (model): OpsProjection — timeline stages, current decision,
// resources, evidence for Control Center / MCP. Does not bypass Loop state.
//

#include "agent_ops/ops_types.h"
#include "agent_loop/session_journal.h"
#include "agent_loop/scientific_agent_session.h"

#include <string>

namespace sicnu::agent_ops {

struct OpsProjection {
    std::string kind = kOpsProjectionKind;
    std::string sessionId;
    std::string currentStage;
    std::string terminalState;
    std::string stopReason;
    Json::Value header{Json::objectValue};
    Json::Value timeline{Json::arrayValue}; ///< [{seq, stage, event, summary}]
    Json::Value currentDecision{Json::objectValue};
    Json::Value resources{Json::objectValue};
    Json::Value evidence{Json::objectValue};
    Json::Value controls{Json::objectValue}; ///< pause/cancel/resume/approve/export availability

    Json::Value toJson() const;
};

class OpsProjector {
  public:
    OpsProjection project(const sicnu::agent_loop::SessionJournal &journal,
                          const OpsBudget &budgets = {},
                          bool pauseAvailable = true) const;

    OpsProjection projectResult(const sicnu::agent_loop::SessionResult &result,
                                const OpsBudget &budgets = {}) const;
};

} // namespace sicnu::agent_ops
