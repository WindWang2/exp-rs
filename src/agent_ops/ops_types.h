// src/agent_ops/ops_types.h
#pragma once

//
// Agent Operations & Recovery Control Center — shared wire types.
// Compose-only: these documents project authorities (agent_loop journal,
// agentbench trace, autonomy decisions, repair schema). They invent no
// science and no second scheduler.
//

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::agent_ops {

inline constexpr const char *kOpsSchemaVersion = "1.0";
inline constexpr const char *kFinalDeliveryKind = "sicnu.agent_ops.final_delivery/v1";
inline constexpr const char *kOpDiagnosticKind = "sicnu.agent_ops.diagnostic/v1";
inline constexpr const char *kRecoveryDecisionKind = "sicnu.agent_ops.recovery/v1";
inline constexpr const char *kOpsProjectionKind = "sicnu.agent_ops.projection/v1";

/// Closed recovery action vocabulary (wire strings; never rename).
namespace recovery_action {
inline constexpr const char *kRetry = "retry";
inline constexpr const char *kRepair = "repair";
inline constexpr const char *kReplan = "replan";
inline constexpr const char *kAsk = "ask";
inline constexpr const char *kAbort = "abort";
} // namespace recovery_action

/// Closed mutating ops that MUST pass the autonomy gate.
namespace ops_mutate {
inline constexpr const char *kExecute = "execute";
inline constexpr const char *kRepair = "repair";
inline constexpr const char *kOverwrite = "overwrite";
inline constexpr const char *kPublish = "publish";
inline constexpr const char *kCleanup = "cleanup";
inline constexpr const char *kModelOut = "model_out";
inline constexpr const char *kWorkflowMutate = "workflow_mutate";
} // namespace ops_mutate

struct OpDiagnostic {
    std::string schemaVersion = kOpsSchemaVersion;
    std::string kind = kOpDiagnosticKind;
    std::string code;              ///< typed machine code (never prose-only)
    std::string rootCauseCode;     ///< closed; LLM text is never sole root cause
    double confidence = 0.0;       ///< 0..1; structured evidence drives this
    bool repairable = false;
    bool retryable = false;
    std::string advisoryNext;      ///< recovery_action::* or empty
    Json::Value evidence{Json::objectValue};
    Json::Value sources{Json::objectValue}; ///< verifier/preflight/runtime/debugger/...
    std::string summary;           ///< human-readable; not authoritative alone
    std::vector<std::string> proposals; ///< repair rule ids / action keys

    Json::Value toJson() const;
    static std::optional<OpDiagnostic> fromJson(const Json::Value &doc, std::string *error = nullptr);
};

struct RecoveryDecision {
    std::string schemaVersion = kOpsSchemaVersion;
    std::string kind = kRecoveryDecisionKind;
    std::string action; ///< recovery_action::*
    std::string reasonCode;
    bool autonomyAllowed = false;
    std::string autonomyReasonCode;
    bool needsApproval = false;
    int attempt = 0;
    int replanCount = 0;
    int repairCount = 0;
    int retryCount = 0;
    /// True when the decision authorizes a repair: a repair execution
    /// returning success is NOT a repaired claim — fresh preflight and
    /// fresh verification must run before anything is called repaired.
    bool requiresReverification = false;
    /// Non-empty when a presented repair approval was refused or did not
    /// bind the plan at hand (expired/tampered/replayed/wrong plan). The
    /// decision stays safe (ask), the field says why.
    std::string approvalError;
    Json::Value repairPlan{Json::Value()}; ///< RepairPlan JSON when present
    Json::Value diagnostic{Json::objectValue};
    Json::Value budgets{Json::objectValue};

    Json::Value toJson() const;
};

struct FinalDelivery {
    std::string schemaVersion = kOpsSchemaVersion;
    std::string kind = kFinalDeliveryKind;
    std::string sessionId;
    std::string goal;
    std::string outcome;    ///< delivered | refused | aborted
    std::string stopReason;
    Json::Value plan{Json::objectValue};
    Json::Value outputs{Json::arrayValue};
    Json::Value verifier{Json::objectValue};
    Json::Value warnings{Json::arrayValue};
    Json::Value questions{Json::arrayValue};
    Json::Value provenance{Json::objectValue};
    Json::Value runIds{Json::arrayValue};
    Json::Value capsule{Json::objectValue};
    Json::Value benchmarkRefs{Json::arrayValue};
    Json::Value claims{Json::arrayValue}; ///< [{claim, value, confidence,
                                          ///  evidence_ref, stop_reason?,
                                          ///  evidence_missing?}]
    Json::Value budgets{Json::objectValue};
    Json::Value stages{Json::arrayValue};
    Json::Value decisions{Json::arrayValue};
    Json::Value traceRef{Json::objectValue};
    Json::Value journalReplay{Json::objectValue};

    Json::Value toJson() const;
    static std::optional<FinalDelivery> fromJson(const Json::Value &doc, std::string *error = nullptr);
};

struct OpsBudget {
    int maxRetries = 3;
    int maxReplans = 3;
    int maxRepairs = 3;
    int noProgressThreshold = 2;
    long long wallClockMs = 0; ///< 0 = unbounded
    long resourceBudgetMb = 0; ///< 0 = unbounded
    std::size_t maxTraceBytes = 512 * 1024;
    std::size_t maxPayloadChars = 4096;
};

} // namespace sicnu::agent_ops
