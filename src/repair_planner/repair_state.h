// src/repair_planner/repair_state.h
#pragma once

//
// RS14-03 completion slice G: the planning-state ledger.
//
// A bounded, tamper-evident, reloadable record of what was PLANNED for a
// (subject, findings digest) pair. It lets callers detect re-planning the
// same findings into a different plan (conflict), verify a stored plan
// against its recorded fingerprint (tamper evidence), and resume after a
// restart from the serialized form (which carries a digest of its own).
//
// It is NOT an execution record: nothing here runs a repair. It records
// planning outcomes only.
//
// Determinism: the serialized form is canonical JSON with a sha256/16 state
// digest over the records; eviction is by lowest sequence number and counted.

#include <json/json.h>
#include <string>
#include <vector>

#include "repair_schema.h"

namespace sicnu::repair {

struct RepairPlanningRecord
{
    std::string subject;
    std::string findingsDigest;   ///< sha256/16 over the canonical findings
    std::string planId;           ///< "srp-<fingerprint>"
    std::string planFingerprint;  ///< sha256/16 over the canonical plan content
    std::string status;           ///< plan_status::*
    long long sequence = 0;       ///< caller-supplied monotonically growing order
};

class RepairPlanningState
{
  public:
    enum class Verify
    {
        Unknown,   ///< no record for this plan id
        Match,     ///< recorded fingerprint matches the document's content
        Tampered,  ///< the document's content differs from the record
    };

    /// Records a planning outcome. Idempotent on (subject, findingsDigest):
    /// the same fingerprint records nothing new and returns true; a
    /// DIFFERENT fingerprint for the same key is a typed conflict
    /// (error.code "invalid_state"). Bounded: when the ledger is full the
    /// lowest-sequence record is evicted and the eviction is counted.
    bool record( const RepairPlanningRecord &record, RepairError &error );

    bool contains( const std::string &subject, const std::string &findingsDigest ) const;

    /// Tamper check of a repair_plan document against the recorded
    /// fingerprint for its plan id.
    Verify verifyPlan( const Json::Value &planDoc ) const;

    /// True when `result` (a repair_result document) carries the plan id and
    /// findings digest of a recorded plan.
    bool resultDigestMatches( const Json::Value &resultDoc ) const;

    std::size_t size() const;
    std::size_t capacity() const;
    long long evictedCount() const;

    /// Canonical, digest-bearing serialization. Deterministic bytes.
    Json::Value toJson() const;

    /// Fail-closed reload: validates shape, statuses and the state digest.
    /// A digest mismatch is a typed tampered_state error, never a silent
    /// acceptance.
    static bool fromJson( const Json::Value &doc, RepairPlanningState &out,
                          RepairError &error );

    static constexpr std::size_t kCapacity = 8;

  private:
    std::vector<RepairPlanningRecord> mRecords; ///< ascending sequence order
    long long mEvicted = 0;

    void evictIfNeeded();
};

} // namespace sicnu::repair
