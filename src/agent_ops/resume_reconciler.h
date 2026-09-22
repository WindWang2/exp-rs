// src/agent_ops/resume_reconciler.h
#pragma once

//
// Feature F: Resume / Crash Recovery reconciliation.
// Uses existing journal/checkpoint; no duplicate successful submits;
// unknown ≠ success.
//

#include "agent_ops/ops_types.h"
#include "agent_loop/session_journal.h"

#include <optional>
#include <set>
#include <string>

namespace sicnu::agent_ops {

struct ReconcileResult {
    bool ok = false;
    bool resumable = false;
    bool duplicateSubmitRisk = false;
    std::string reasonCode;
    std::string stage;
    std::string terminalState;
    std::set<std::string> successfulRunIds;
    Json::Value details{Json::objectValue};

    Json::Value toJson() const;
};

class ResumeReconciler {
  public:
    /// Inspect journal for safe resume. Corrupted/unknown → not ok, not success.
    ReconcileResult reconcile(const sicnu::agent_loop::SessionJournal &journal) const;

    /// Load journal from disk; corrupted file → typed failure (not success).
    ReconcileResult reconcileFile(const std::string &directory, const std::string &sessionId,
                                  std::string *loadError = nullptr) const;
};

} // namespace sicnu::agent_ops
