// lab_completion_ledger.h — the teaching-side authority for lab completion.
//
// Why this exists (R18-E T103 / audit D1): the shipped cockpit builds its
// course-home progress from CurriculumProgress::emptyDoc() on every start
// and nothing in the app ever calls markCompleted — the prerequisite DAG
// is unsatisfiable in the released UI. The fix has two halves:
//
//   1. THIS module (src/teaching, the track's write zone): a durable
//      completion ledger whose entries are only created from a VERIFIED
//      pass verdict (the session's verdict stamp, verdict_stamp.h), never
//      from self-report. It projects into the
//      `sicnu.curriculum.progress/1` document the course home already
//      consumes (evidence = the ledger entry, not a claim).
//   2. Dock wiring (src/app — R18-D's zone, recorded in
//      CROSS_TRACK_FINDINGS.md): replace emptyDoc() with the ledger's
//      projection and record on verdict stamp success.
//
// Trust discipline mirrors the session file: strict schema, unknown keys
// refused, entries re-verified against the referenced session's recorded
// verdict digest on load. This catches INDEPENDENT tampering — a stale
// session verdict, a vanished session, a copy-pasted entry — the same way
// the verdict stamp does. It is not a trust root: an attacker who edits
// the ledger AND the session file consistently (recomputing the public
// unkeyed digests) still passes; binding verdict provenance needs a keyed
// MAC, recorded as future work in DECISIONS.md.
#pragma once

#include "teaching/lab_session_state.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::teaching
{

inline constexpr const char *kLabCompletionLedgerSchema =
  "sicnu.teaching.completion_ledger/1";

struct LabCompletionEntry
{
  std::string labId;
  /// Session file the verified verdict was recorded in (evidence ref).
  std::string sessionRef;
  /// Digest of the summary document that passed (verdict_stamp binding).
  std::string verdictDigest;
  /// The artifact content stamp at verdict time (drift evidence).
  std::string artifactStamp;
};

struct LabCompletionLedger
{
  std::string schema = kLabCompletionLedgerSchema;
  bool ok = false;
  std::vector<LabCompletionEntry> completed;
  std::vector<std::string> issuesZh;

  /// Records the verified pass verdict carried by @p session. Refuses
  /// (returns false, adds an issue) when the session has no stamp — an
  /// unbound verdict is a claim, not completion evidence.
  bool recordVerdict( const LabSessionState &session, const std::string &sessionRef );

  /// Strict parse; unknown schema/keys/types → ok=false + issues. Entries
  /// whose session file still exists must re-verify (verdict digest
  /// unchanged) or the entry is dropped with an issue.
  static LabCompletionLedger fromJson( const Json::Value &doc );

  /// Re-verification variant: checks every entry against the session file
  /// it references (when readable). Called by the load path; fromJson
  /// alone trusts the recorded digests (shape-level honesty).
  static LabCompletionLedger loadVerified( const std::string &ledgerPath );

  Json::Value toJson() const;

  /// Durable save through the platform sidecar authority.
  bool saveToFile( const std::string &path ) const;

  /// Projects into a `sicnu.curriculum.progress/1` document for the
  /// course-home consumer (evidence = the ledger entry; no self-report).
  /// completed_at_iso stays empty: the ledger reads no clock, the wiring
  /// caller may inject one.
  Json::Value toProgressDoc() const;
};

} // namespace sicnu::teaching
