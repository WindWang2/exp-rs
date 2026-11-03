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
// and carries a per-mint random `nonce` plus an HMAC-SHA256/16 MAC over
// exactly those fields, keyed by a process-local secret that never leaves
// the minting coordinator (#1733). The MAC is an AUTHENTICITY bound, not
// just integrity: a peer that can observe every field on the wire (the
// model, an MCP client) still cannot mint or alter a token without the
// secret. The mint path itself is human-only — the session surface refuses
// `approve_repair` (APPROVAL_HUMAN_CHANNEL_ONLY); only the trusted local
// UI (the Control Center dock) asks its coordinator to mint.
//
// Single-use is the HOLDER's consumption record: an armed token is consumed
// by the next launch and a consumed token is refused as a replay. The
// consumed-digest ring only evicts entries whose token window has expired,
// so a still-live token can never be replayed through ring pressure.
//
// Verification vocabulary is closed and machine-readable; every refusal is
// a typed code, never a silent downgrade to "not approved".
//

#include <json/json.h>
#include <string>

namespace sicnu::agent_ops {

inline constexpr const char *kRepairApprovalKind = "sicnu.agent_ops.repair_approval";
inline constexpr const char *kRepairApprovalSchema = "2.0";

/// Upper bound for a token's TTL (#1733: an unbounded ttl plus a bounded
/// consumed ring reopens a replay window). 30 minutes of wall clock is far
/// beyond any human "look at the plan, then approve" latency and short
/// enough that the consumed ring cannot be aged out by waiting.
inline constexpr long long kRepairApprovalMaxTtlMs = 30LL * 60 * 1000;

/// TTL used when the operator channel does not name one.
inline constexpr long long kRepairApprovalDefaultTtlMs = 5LL * 60 * 1000;

/// Minimum hex length of a minting secret (32 bytes of entropy).
inline constexpr std::size_t kRepairApprovalMinSecretHex = 64;

/// Closed check vocabulary (wire strings; never rename).
namespace approval_check {
inline constexpr const char *kOk = "ok";
inline constexpr const char *kMalformed = "malformed";
inline constexpr const char *kTampered = "tampered";
inline constexpr const char *kWrongPlan = "wrong_plan";
inline constexpr const char *kWrongCoordinator = "wrong_coordinator";
inline constexpr const char *kExpired = "expired";
inline constexpr const char *kTtlExcessive = "ttl_excessive";
} // namespace approval_check

/// Process-unique coordinator instance id (monotonic counter; runtime
/// identity only — never part of a science document's content).
long long nextRepairApprovalInstanceId();

/// High-entropy hex secret for HMAC keying (32 bytes from the OS entropy
/// source; never serialized, never returned on any wire surface).
std::string generateRepairApprovalSecretHex();

/// Mints a token binding `findingsDigest` + coordinator instance + the clock
/// window [nowMs, nowMs + ttlMs], MACed with `secretHex`. Returns a null
/// document when the arguments are unusable (empty digest, non-positive
/// clock, non-positive or excessive ttl — see kRepairApprovalMaxTtlMs — or
/// a too-short secret). Minting never invents an approval.
Json::Value mintRepairApprovalToken(const std::string &findingsDigest,
                                    long long coordinatorId, long long nowMs,
                                    long long ttlMs, const std::string &secretHex);

/// Re-derives and checks one token against the expected binding, MAC and
/// clock. Pure: no state; replay is detected through the holder's consumption
/// record, surfaced as the holder's typed error.
const char *verifyRepairApprovalToken(const Json::Value &tokenDoc,
                                      const std::string &findingsDigest,
                                      long long coordinatorId, long long nowMs,
                                      const std::string &secretHex);

/// Secret-free structural validation: field presence/types, hex shapes, and
/// a within-bound ttl window. Catches garbled/legacy tokens without any
/// coordinator state, so a trusted UI can refuse nonsense immediately
/// (#1760). Returns an empty string when the shape is acceptable, else a
/// check-vocabulary code (never the MAC verdict — that needs the secret).
const char *validateRepairApprovalTokenShape(const Json::Value &tokenDoc);

} // namespace sicnu::agent_ops
