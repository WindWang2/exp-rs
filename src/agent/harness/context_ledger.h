// src/agent/harness/context_ledger.h
#pragma once

//
// Harness 7.0 typed context continuity (mission Area E).
//
// Bounded, process-scoped stores for the typed context slots the workspace
// snapshot cannot derive on its own: plan/run bindings with verification
// status, unresolved scientific decisions, and a dataset-understanding cache
// keyed by (path, asset revision).
//
// This is NOT a chat memory: every record is written by harness tools from
// authoritative events (a plan execution, an explicit agent decision record,
// a dataset inspection) and every store is bounded with oldest-first
// eviction. Nothing here plans or converses.
//

#include <json/json.h>
#include <QMutex>
#include <QString>

namespace sicnu::agent::harness {

class ContextLedger {
  public:
    static ContextLedger &instance();

    /// Binds a plan execution to its run and (later) verification status.
    /// Re-binding the same run id updates the record in place; the store
    /// keeps the most recent 8 bindings.
    void recordPlanBinding( const std::string &runId, const std::string &planId,
                            const std::string &goal, const std::string &intent,
                            const std::string &verificationStatus = "" );
    Json::Value planBindings() const; ///< [{run_id, plan_id, goal, intent, verification_status, bound_at}]

    /// Records a typed decision. kind: "ambiguity"|"alternative"|"parameter".
    /// status: "unresolved"|"resolved". Bounded to the most recent 20.
    /// Returns the assigned decision id ("decision-N").
    std::string recordDecision( const std::string &kind, const std::string &subject,
                                const std::string &status, const std::string &note,
                                const Json::Value &candidates = Json::Value() );
    /// Marks a decision resolved (records the chosen option when given).
    bool resolveDecision( const std::string &decisionId, const std::string &chosen );
    Json::Value decisions() const; ///< bounded list, unresolved first

    /// Caches a DatasetUnderstanding document under a caller-computed key
    /// ((path, revision) or (path, size, mtime) — see grounding_tools).
    /// A later call with the same key is a hit. Bounded to 32 entries,
    /// oldest-first eviction.
    void cacheUnderstanding( const QString &key, long long reserved,
                             const Json::Value &understanding );
    /// Returns the cached document when the key matches, else null.
    Json::Value cachedUnderstanding( const QString &key, long long reserved ) const;

  private:
    ContextLedger() = default;

    mutable QMutex mMutex;
    Json::Value mPlanBindings{Json::arrayValue};
    Json::Value mDecisions{Json::arrayValue};
    int mNextDecisionId = 1;
    // understanding cache: parallel arrays keyed by "path\u0001revision"
    Json::Value mUnderstandingKeys{Json::arrayValue};
    Json::Value mUnderstandingDocs{Json::arrayValue};
};

} // namespace sicnu::agent::harness
