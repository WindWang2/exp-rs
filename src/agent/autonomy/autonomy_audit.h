// src/agent/autonomy/autonomy_audit.h
#pragma once

//
// RS14-12: the autonomy decision audit log.
//
// Every allow/deny/downgrade the engine decides is recorded with its typed
// reason, so "why was this capability forbidden" is answerable after the
// fact — for a student asking why the assistant refused, a teacher auditing
// an exam session, and a future agent reading what a session was permitted
// to do. The log is the ONLY durable-ish trace of policy decisions; there is
// no second place decisions are remembered.
//
// Contract:
//   * bounded — kMaxRecords entries, FIFO eviction (memory has an upper
//     bound no matter how long a session runs);
//   * deterministic — sequence numbers start at 1 per process and are never
//     reused after eviction; the JSON carries NO wall-clock, so replaying an
//     identical request sequence yields byte-identical output;
//   * thread-safe — gates may fire from MCP, CLI and GUI threads.

#include <json/json.h>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"

namespace sicnu::agent::autonomy {

inline constexpr const char *kAutonomyDecisionSchema = "sicnu.autonomy-decision/1";

struct AutonomyAuditRecord
{
    std::string schema = kAutonomyDecisionSchema;
    std::uint64_t sequence = 0;
    std::string domain;
    std::string role;
    std::string mode;
    std::string capability;
    std::string intent;
    std::string actionKey;
    std::string toolId;
    std::string riskClass;
    std::string decision;   ///< "allow" | "deny" | "downgrade"
    std::string reasonCode; ///< closed autonomy_reason_codes::*
    std::string downgradeTo;
    AutonomyLevel effectiveLevel = AutonomyLevel::L0;

    Json::Value toJson() const;
};

class AutonomyAuditLog
{
  public:
    /// Upper bound on retained records (same bounded-ledger pattern as the
    /// harness repair ledger).
    static constexpr std::size_t kMaxRecords = 1000;

    static AutonomyAuditLog &instance();

    /// Records one decision. The request supplies identity (domain/role/
    /// action/tool), the policy supplies the mode it was decided under.
    void record( const AutonomyRequest &request, const AutonomyPolicy &policy,
                 const AutonomyDecision &decision );

    /// Oldest-first snapshot (bounded).
    std::vector<AutonomyAuditRecord> records() const;

    /// Deterministic wire document: {schema, count, records[]}.
    Json::Value toJson() const;

    std::size_t size() const;
    void clear();

  private:
    AutonomyAuditLog() = default;

    mutable std::mutex mMutex;
    std::vector<AutonomyAuditRecord> mRecords;
    std::uint64_t mNextSequence = 1;
};

} // namespace sicnu::agent::autonomy
