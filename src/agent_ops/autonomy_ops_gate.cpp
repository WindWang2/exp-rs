// src/agent_ops/autonomy_ops_gate.cpp
#include "agent_ops/autonomy_ops_gate.h"
#include "agent/autonomy/autonomy_capability.h"

namespace sicnu::agent_ops {

std::string capabilityForMutate(const std::string &mutateKind)
{
    using namespace sicnu::agent::autonomy::assistance_capabilities;
    if (mutateKind == ops_mutate::kExecute || mutateKind == ops_mutate::kWorkflowMutate ||
        mutateKind == ops_mutate::kPublish || mutateKind == ops_mutate::kOverwrite ||
        mutateKind == ops_mutate::kCleanup || mutateKind == ops_mutate::kModelOut ||
        mutateKind == ops_mutate::kRepair)
        return kAutonomousExecution;
    return kReadOnlyQuery;
}

OpsAutonomyVerdict gateMutatingOp(const sicnu::agent::autonomy::AutonomyPolicy &policy,
                                  const OpsAutonomyRequest &request)
{
    OpsAutonomyVerdict verdict;
    verdict.capability = capabilityForMutate(request.mutateKind);
    sicnu::agent::autonomy::AutonomyRequest ar;
    ar.domain = request.domain.empty() ? "research" : request.domain;
    ar.role = request.role;
    ar.capability = verdict.capability;
    ar.intent = request.intent;
    ar.actionKey = request.actionKey.empty() ? request.mutateKind : request.actionKey;
    ar.riskClass = request.riskClass;
    ar.toolId = "agent_ops:" + request.mutateKind;
    verdict.decision = sicnu::agent::autonomy::decideAutonomy(policy, ar);
    verdict.reasonCode = verdict.decision.reasonCode;
    verdict.allowed =
        verdict.decision.kind == sicnu::agent::autonomy::AutonomyDecisionKind::Allow;
    return verdict;
}

} // namespace sicnu::agent_ops
