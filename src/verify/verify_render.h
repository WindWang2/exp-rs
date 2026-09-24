// src/verify/verify_render.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — presentation surfaces.
//
// Three deterministic renders of one report, zero policy inside the codes:
//
//   renderText     — one-line summary for logs and batch listings;
//   renderTeaching — student-facing multi-line view (status words, no jargon);
//   renderAgent    — machine-facing JSON with the blocking list and a
//                    suggestedAction per blocking check.
//
// suggestedAction is a total, deterministic, ADVISORY mapping from a wire
// code: the verify:e_* class reads as "the checked contract is violated —
// repair the output"; verify:i_* reads as "the verdict could not be reached
// — restore the missing capability". It never promises an optimal recovery
// and never upgrades a verdict.
//

#include <json/json.h>

#include <string>

#include "verify_types.h"

namespace sicnu::verify
{

/// One-line deterministic summary, e.g.
/// "verify(spec.preprocess.optical): FAIL (fail=1 indeterminate=0 pass=3)".
std::string renderText( const VerificationReport &report );

/// Student-facing multi-line view in declaration order. Deterministic:
/// identical reports render byte-identically (no wall clock).
std::string renderTeaching( const VerificationReport &report );

/// Machine-facing view: overall, counts, blocking checks (every non-pass, in
/// declaration order) with code + message + suggestedAction, and the
/// overall-level suggestedAction (the first blocking check's action; "none"
/// on a clean pass).
Json::Value renderAgent( const VerificationReport &report );

/// Total advisory mapping from a verifier wire code to a suggested action.
/// Unknown or empty codes map to the conservative "escalate_unverifiable" —
/// an unrecognized failure is never advised into a routine repair.
std::string suggestedAction( const std::string &code );

} // namespace sicnu::verify
