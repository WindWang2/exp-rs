// src/agent_ops/recovery_bridge.h
#pragma once

//
// Feature D: Recovery / Replan Bridge.
// diagnostic → (optional RepairPlan projection) → autonomy gate → action
// {retry|repair|replan|ask|abort}. Enforces max retries/replans/repairs,
// no-progress, wall-clock, cancel. No infinite self-heal.
//

#include "agent_ops/ops_types.h"
#include "agent/autonomy/autonomy_holder.h"
#include "agent/autonomy/autonomy_policy.h"

#include <optional>
#include <string>

namespace sicnu::agent_ops {

struct RecoveryContext {
    OpsBudget budgets;
    int attempt = 1;
    int replanCount = 0;
    int repairCount = 0;
    int retryCount = 0;
    int identicalFailureCount = 0;
    bool cancelRequested = false;
    long long elapsedMs = 0;
    std::string domain;
    std::string role;
    std::string intent;
    std::string leadingRiskClass = "shape_preserving";
    bool humanApprovedRepair = false;
    sicnu::agent::autonomy::AutonomyPolicy autonomyPolicy{
        sicnu::agent::autonomy::AutonomyPolicyHolder::researchDefaultPolicy()};
};

class RecoveryBridge {
  public:
    RecoveryDecision decide(const OpDiagnostic &diagnostic, const RecoveryContext &ctx) const;

    /// Hints for the planning-only repair plan projection. Everything here
    /// is auditable context — nothing turns a science-changing candidate
    /// auto (evaluateRepairPolicy keeps the ceiling).
    struct PlanHints {
        std::string leadingRiskClass;       ///< ctx.leadingRiskClass
        bool scienceChangeApproved = false; ///< recorded human approval
        std::string domain;                 ///< teaching-gate context ("lab")
        std::string role;
    };

    Json::Value projectRepairPlan(const OpDiagnostic &diagnostic,
                                  const std::string &intent) const;
    Json::Value projectRepairPlan(const OpDiagnostic &diagnostic, const std::string &intent,
                                  const PlanHints &hints) const;
};

} // namespace sicnu::agent_ops
