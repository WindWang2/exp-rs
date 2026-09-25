// src/agent_ops/repair_approval.h
#pragma once

//
// Repair approval token — the human gate for risky repairs is a typed,
// bound, single-use artifact, never a bare UI flag.
//
// A token binds four facts:
//   - the repair science it approves (`findings_digest` — the planner's
//     sha256/16 over the canonical findings; stable across re-planning of
//     the same evidence, so a re-projection of the SAME findings is the
//     same approval target while a different finding set never is);
//   - the coordinator instance that minted it (a process-unique id, so a
//     token captured from one coordinator cannot arm another);
//   - a driver-supplied clock window ([issued_at_ms, expires_at_ms]; the
//     module keeps no wall-clock — callers pass `now`, which keeps
//     verification deterministic and testable);
// and carries a sha256/16 digest over exactly those fields, so any field
// mutation is detected. The digest is tamper-EVIDENT (the repo's standard
// discipline — repair_planning_state, agent_plan fingerprints): it proves
// the document is intact, not the holder's identity.
//
// Single-use is the HOLDER's consumption record: an armed token is consumed
// by the next launch and a consumed token is refused as a replay.
//
// Verification vocabulary is closed and machine-readable; every refusal is
// a typed code, never a silent downgrade to "not approved".

#include <json/json.h>
#include <string>

namespace sicnu::agent_ops {

inline constexpr const char *kRepairApprovalKind = "sicnu.agent_ops.repair_approval";
inline constexpr const char *kRepairApprovalSchema = "1.0";

/// Closed check vocabulary (wire strings; never rename).
namespace approval_check {
inline constexpr const char *kOk = "ok";
inline constexpr const char *kMalformed = "malformed";
inline constexpr const char *kTampered = "tampered";
inline constexpr const char *kWrongPlan = "wrong_plan";
inline constexpr const char *kWrongCoordinator = "wrong_coordinator";
inline constexpr const char *kExpired = "expired";
} // namespace approval_check

/// Process-unique coordinator instance id (monotonic counter; runtime
/// identity only — never part of a science document's content).
long long nextRepairApprovalInstanceId();

/// Mints a token binding `findingsDigest` + coordinator instance + the clock
/// window [nowMs, nowMs + ttlMs]. Returns a null document when the arguments
/// are unusable (empty digest, non-positive ttl or clock) — minting never
/// invents an approval.
Json::Value mintRepairApprovalToken(const std::string &findingsDigest,
                                    long long coordinatorId, long long nowMs,
                                    long long ttlMs);

/// Re-derives and checks one token against the expected binding and clock.
/// Pure: no state; replay is detected through the holder's consumption
/// record, surfaced as the holder's typed error.
const char *verifyRepairApprovalToken(const Json::Value &tokenDoc,
                                      const std::string &findingsDigest,
                                      long long coordinatorId, long long nowMs);

} // namespace sicnu::agent_ops
