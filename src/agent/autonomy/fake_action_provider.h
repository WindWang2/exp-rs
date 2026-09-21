// src/agent/autonomy/fake_action_provider.h
#pragma once

//
// RS14-12: fake action providers for policy verification.
//
// The policy layer must be verifiable WITHOUT a real LLM, a real model
// session, or the network. These providers supply deterministic, canned
// turns — the assistance a fake assistant would produce and the actions a
// fake agent would execute — and run them through the same decision engine
// the live gates use. The corpus is closed and ordered: identical input,
// identical decisions, byte-identical audit output.
//
// Action turns carry the risk class already resolved by the caller (the risk
// table lives in sicnu_agent — tool_manifest — and must stay the single
// source of truth); an unresolved turn classifies as unknown and the engine
// denies it.

#include <string>
#include <vector>

#include "agent/autonomy/autonomy_audit.h"
#include "agent/autonomy/autonomy_classification.h"
#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_policy.h"
#include "agent/autonomy/autonomy_projection.h"

namespace sicnu::agent::autonomy {

/// One canned assistance turn (what a student would ask).
struct FakeAssistanceTurn
{
    std::string message;
    std::string intent; ///< classified offline by the test/caller
    std::string role;   ///< session role
};

/// One canned action turn (what an assistant/agent would execute).
struct FakeActionTurn
{
    std::string actionKey; ///< harness action key (audit only)
    std::string toolId;    ///< tool id (audit only)
    std::string riskClass; ///< resolved via tool_manifest by the caller
    std::string role;
    std::string domain = "lab";
};

/// Deterministic assistance corpus covering the ladder (ordered).
inline const std::vector<FakeAssistanceTurn> &fakeAssistanceTurns()
{
    static const std::vector<FakeAssistanceTurn> kTurns = {
        { "什么是对比度拉伸？", "lab_concept", "student" },
        { "我的分类结果kappa接近0，为什么？", "lab_troubleshoot", "student" },
        { "我卡在第3步了，下一步做什么？", "lab_hint", "student" },
        { "帮我把实验3做完", "lab_execute", "student" },
        { "帮我评分", "lab_grade_request", "student" },
        { "帮我把实验3做完", "lab_execute", "teacher" },
    };
    return kTurns;
}

/// Deterministic action corpus covering every risk class (ordered).
inline const std::vector<FakeActionTurn> &fakeActionTurns()
{
    static const std::vector<FakeActionTurn> kTurns = {
        { "check_dataset", "spatial:understand", autonomy_risk_classes::kReadOnly, "student", "lab" },
        { "inspect_bands", "spatial:raster_inspect", autonomy_risk_classes::kReadOnly, "student", "lab" },
        { "add_source_note", "cartography:repair", autonomy_risk_classes::kCreatesArtifact, "student", "lab" },
        { "resume_run", "harness:run_status", autonomy_risk_classes::kModifiesProject, "student", "lab" },
        { "resume_run", "harness:run_status", autonomy_risk_classes::kModifiesProject, "teacher", "lab" },
        { "", "harness:execute_plan", autonomy_risk_classes::kCreatesArtifact, "teacher", "agent" },
        { "", "", "", "student", "lab" }, // unresolved risk class: fail-closed
    };
    return kTurns;
}

/// Capability a fake action turn would exercise; empty when the turn's risk
/// class is unresolved (the engine denies unknown capabilities).
inline std::string fakeActionCapability( const FakeActionTurn &turn )
{
    const ActionClassification classification = classifyActionRisk( turn.riskClass );
    return classification.known ? classification.capability : std::string();
}

/// Runs a fake assistance turn through the engine (and the audit log).
inline AutonomyDecision decideFakeAssistance( const AutonomyPolicy &policy,
                                              const FakeAssistanceTurn &turn )
{
    AutonomyRequest request;
    request.domain = "lab";
    request.role = turn.role;
    request.intent = turn.intent;
    request.capability = classifyAssistanceIntent( turn.intent ).capability;
    const AutonomyDecision decision = decideAutonomy( policy, request );
    AutonomyAuditLog::instance().record( request, policy, decision );
    return decision;
}

/// Runs a fake action turn through the engine (and the audit log).
inline AutonomyDecision decideFakeAction( const AutonomyPolicy &policy, const FakeActionTurn &turn )
{
    AutonomyRequest request;
    request.domain = turn.domain;
    request.role = turn.role;
    request.actionKey = turn.actionKey;
    request.toolId = turn.toolId;
    request.riskClass = turn.riskClass;
    request.capability = fakeActionCapability( turn );
    const AutonomyDecision decision = decideAutonomy( policy, request );
    AutonomyAuditLog::instance().record( request, policy, decision );
    return decision;
}

} // namespace sicnu::agent::autonomy
