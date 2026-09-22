// src/agent_ops/autonomy_ops_gate.h
#pragma once

//
// Feature E: Autonomy gate on ops mutating actions.
// Prefer dispatch over invasive hooks: call sites invoke gateMutatingOp
// before execute/repair/overwrite/publish/cleanup/model_out/workflow_mutate.
// Non-Agent GUI paths are untouched (they simply never call this).
//

#include "agent_ops/ops_types.h"
#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_policy.h"

#include <functional>
#include <string>

namespace sicnu::agent_ops {

struct OpsAutonomyRequest {
    std::string mutateKind; ///< ops_mutate::*
    std::string domain;     ///< "lab" or research
    std::string role;
    std::string intent;
    std::string actionKey;
    std::string riskClass;
};

struct OpsAutonomyVerdict {
    bool allowed = false;
    std::string reasonCode;
    std::string capability;
    sicnu::agent::autonomy::AutonomyDecision decision;
};

/// Map ops mutate kind → assistance capability for the autonomy engine.
std::string capabilityForMutate(const std::string &mutateKind);

OpsAutonomyVerdict gateMutatingOp(const sicnu::agent::autonomy::AutonomyPolicy &policy,
                                  const OpsAutonomyRequest &request);

/// Decorator helper: run `fn` only when the gate allows; otherwise return
/// the deny reason without invoking fn.
template <typename Fn>
auto withAutonomyGate(const sicnu::agent::autonomy::AutonomyPolicy &policy,
                      const OpsAutonomyRequest &request, Fn &&fn)
    -> decltype(fn())
{
    const OpsAutonomyVerdict v = gateMutatingOp(policy, request);
    if (!v.allowed)
        return decltype(fn()){};
    return fn();
}

} // namespace sicnu::agent_ops
