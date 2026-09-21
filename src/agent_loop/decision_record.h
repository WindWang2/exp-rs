// src/agent_loop/decision_record.h
#pragma once

//
// RS14-11 Evidence-first Agent Loop: the typed DecisionRecord.
//
// Every key decision an agent session takes — which intent, which repair,
// whether to proceed after a warning, why a goal was refused — is recorded
// with its inputs, the alternatives that were NOT taken (and why), the
// selected action, the reason, the evidence it rested on, and the policy
// that governed the choice. The record is the unit of auditability: a
// session can be replayed and every step justified without chat memory.
//
// This is deliberately NOT the ContextLedger decision row
// (src/agent/harness/context_ledger.h): that ledger is a manual,
// process-scoped continuity store with a shallow shape. This record is
// richer, session-scoped, immutable once written, and versioned. Nothing
// here writes to ContextLedger — the session journal is the single source
// for orchestration decisions.
//

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::agent_loop {

inline constexpr const char *kDecisionRecordSchemaVersion = "1.0";

struct DecisionAlternative {
    std::string id;
    std::string description;
    std::string whyNot;
};

struct DecisionEvidence {
    std::string kind; ///< "fact" | "preflight" | "verification" | "policy" | "engine" | "diagnosis"
    std::string ref;  ///< stable pointer: sidecar path, check name, code, fact key
};

struct DecisionRecord {
    std::string schemaVersion = kDecisionRecordSchemaVersion;
    std::string decisionId;
    std::string sessionId;
    std::string stage;
    long long recordedAt = 0;
    Json::Value inputs{ Json::objectValue };
    std::vector< DecisionAlternative > alternatives;
    Json::Value selected{ Json::objectValue };
    std::string reason;
    std::vector< DecisionEvidence > evidence;
    Json::Value policy{ Json::objectValue };

    Json::Value toJson() const;

    /// Fail-closed reader: unknown schema_version, unknown stage, missing
    /// or empty reason/selected/decision_id/session_id all return nullopt
    /// with a message in `error` (never a partially-trusted record).
    static std::optional< DecisionRecord > fromJson( const Json::Value &doc,
                                                     std::string *error = nullptr );
};

} // namespace sicnu::agent_loop
