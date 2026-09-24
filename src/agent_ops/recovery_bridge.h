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
#include "repair_planner/repair_provider.h"

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
    /// The capability provider is optional: candidates must come from real
    /// capability knowledge (live adapter or test fake) through this seam —
    /// the bridge never ships a second operator table. Without a provider
    /// the bridge plans nothing (typed no_safe_repair) instead of
    /// fabricating candidates.
    explicit RecoveryBridge(
        const sicnu::repair::RepairCapabilityProvider *provider = nullptr);

    RecoveryDecision decide(const OpDiagnostic &diagnostic, const RecoveryContext &ctx) const;

    /// Plans the repair over the diagnostic's typed findings (preflight
    /// issues in `sources`) with the wired capability provider. Deterministic;
    /// always returns a repair_plan/1.0 envelope — `no_safe_repair` with a
    /// counted cause when planning honestly is impossible.
    Json::Value projectRepairPlan(const OpDiagnostic &diagnostic,
                                  const RecoveryContext &ctx) const;

  private:
    const sicnu::repair::RepairCapabilityProvider *mProvider = nullptr;
};

} // namespace sicnu::agent_ops
