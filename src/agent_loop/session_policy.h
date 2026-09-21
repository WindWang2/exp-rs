// src/agent_loop/session_policy.h
#pragma once

//
// RS14-11 Evidence-first Agent Loop: the session policy (budgets & modes).
//
// One value object that bounds the loop before it starts:
//
//   mode                — dry_run | plan_only | execute_with_verify
//   max_replans         — how many diagnose→replan rounds may run
//   no_progress_threshold — identical (plan fingerprint, failure code)
//                         repetitions tolerated before an abort
//   resource_budget_mb  — 0 = unbounded; otherwise the plan's declared
//                         estimates must fit before execution
//   max_steps           — hard loop bound (belt & braces)
//   executor_timeout_ms — per-poll bound; the executor seam must honor it
//   repair_approval     — which repair risk classes may be auto-approved
//   teaching            — the role/domain the teaching gate applies
//
// Validation is fail-closed: a policy that cannot be bounded is rejected
// before the session starts, never mid-loop.
//

#include <string>
#include <vector>

namespace sicnu::agent_loop {

enum class RunMode {
    DryRun,
    PlanOnly,
    ExecuteWithVerify,
};

bool parseRunMode( const std::string &text, RunMode &out );
std::string runModeToString( RunMode mode );

struct RepairApprovalPolicy {
    /// Risk classes (repair_risk::* in the harness rule table) that may be
    /// auto-approved without a recorded human/agent decision. Defaults to
    /// the shape-preserving class only — geometry/format repairs that leave
    /// pixel semantics untouched. Radiometric and science-changing repairs
    /// NEVER auto-apply; they become decision-required refusals.
    std::vector< std::string > autoApproveRiskClasses{ "shape_preserving" };
    /// When false (default), a fixable preflight whose proposals are NOT
    /// auto-approved refuses the run; nothing silently proceeds unfixed.
    bool allowUnapprovedFixable = false;

    bool classAutoApproved( const std::string &riskClass ) const;
};

struct TeachingPolicy {
    /// "lab" while serving the teaching surface; anything else leaves the
    /// gate inert (agent semantics). Mirrors harness_actions::TeachingContext.
    std::string intentDomain;
    /// Session role, normalized by the adapter through harness_actions
    /// ("" degrades to student). Never parsed from message content.
    std::string role;
};

struct SessionPolicy {
    RunMode mode = RunMode::ExecuteWithVerify;
    int maxReplans = 3;
    int noProgressThreshold = 2;
    long resourceBudgetMb = 0; ///< 0 = unbounded
    int maxSteps = 64;
    long long executorTimeoutMs = 300000;
    RepairApprovalPolicy repairApproval;
    TeachingPolicy teaching;

    static SessionPolicy defaults();

    /// Returns true when every bound is sane; on false, `error` names the
    /// offending field.
    bool validate( std::string *error = nullptr ) const;

    /// Identity stamped into every DecisionRecord this session writes.
    std::string policyId() const { return "session_policy"; }
    std::string policyVersion() const { return "1.0"; }
};

} // namespace sicnu::agent_loop
