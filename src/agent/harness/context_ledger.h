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
                            const std::string &verificationStatus = "",
                            const std::string &planFingerprint = "" );
    Json::Value planBindings() const; ///< [{run_id, plan_id, goal, intent, verification_status, plan_fingerprint, bound_at}]

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

    /// Harness 8.0 (typed context 2.0): records the latest observed typed
    /// context for one dataset — identity (entity ids), revision, and the
    /// stat identity (size/mtime) the facts were observed at, folded into
    /// `observed_key` by the caller (same convention as cacheUnderstanding).
    /// Re-recording the same path replaces the record and un-stales it.
    /// Bounded to 32 paths, oldest-first eviction. `summary` must be a
    /// caller-bounded projection of the understanding document.
    void recordAssetContext( const QString &path, const Json::Value &entity,
                             const QString &observedKey, const Json::Value &summary );
    /// The bounded asset-context list. Each record carries a `stale` flag
    /// computed at read time from the recorded observation key: true when
    /// the file is gone or its current (size, mtime) identity no longer
    /// matches — i.e. the facts predate the current bytes.
    Json::Value assetContexts() const;

    /// Harness 8.0 (typed context 2.0): records the contract/readiness facts
    /// of a model the harness observed (spatial:select_model, inference
    /// preflight). Re-recording the same model id replaces the record.
    /// Bounded to 8 models; `contract` must be caller-bounded.
    void recordModelContract( const std::string &modelId, const Json::Value &contract );
    /// The bounded model-contract list.
    Json::Value modelContracts() const;

    // Harness 9.0 (M6): evidence-aware run summaries. The continuity record
    // for long conversations: what a run produced, what verified, what failed,
    // what rests on assumptions. Written ONLY from authoritative run events
    // (verification verdicts, step failures) by the plan lifecycle — never
    // chat memory. Bounded to kMaxRunSummaries entries AND a total
    // approximate-token budget: the oldest summaries evict first until the
    // store fits the budget again.
    static constexpr int kMaxRunSummaries = 12;
    static constexpr int kRunSummaryTokenBudget = 8192;
    /// Records (or replaces) the summary for `runId`. `approxTokens` is the
    /// caller-computed cost (~ bytes/4) of the bounded summary.
    void recordRunSummary( const std::string &runId, const Json::Value &summary,
                           int approxTokens );
    /// The bounded summaries, newest first.
    Json::Value runSummaries() const;
    /// Current total approximate token cost of the store.
    int runSummaryTokens() const;

  private:
    ContextLedger() = default;

    mutable QMutex mMutex;
    Json::Value mPlanBindings{Json::arrayValue};
    Json::Value mDecisions{Json::arrayValue};
    int mNextDecisionId = 1;
    // understanding cache: parallel arrays keyed by "path\u0001revision"
    Json::Value mUnderstandingKeys{Json::arrayValue};
    Json::Value mUnderstandingDocs{Json::arrayValue};
    // Harness 8.0 typed context: asset facts keyed by path, model contracts
    // keyed by model id (both bounded arrays of objects).
    Json::Value mAssetContexts{Json::arrayValue};
    Json::Value mModelContracts{Json::arrayValue};
    // Harness 9.0: run summaries keyed by run id, newest last; parallel
    // per-entry token costs keep the budget check O(1) amortized.
    Json::Value mRunSummaries{Json::arrayValue};
    int mRunSummaryTokens = 0;
};

} // namespace sicnu::agent::harness
