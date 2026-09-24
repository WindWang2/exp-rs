// src/verify/projections.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — shape projections.
//
// Pure adapters from a VerificationReport into the DOCUMENT SHAPES of the
// existing consumers. These are projections, not a second verifier: the
// verdict words of the target shapes are computed here from the report's
// fail-closed lattice, and the verifier vocabulary never mixes with theirs.
//
// The harness tri-state (PASS / PASS_WITH_WARNINGS / FAIL) cannot express
// "unknown". The projection is conservative: anything that is not a Pass
// projects to FAIL. Folding an Indeterminate into a success class would let
// "could not verify" pass the run — the exact failure the lattice forbids.
// Indeterminate checks therefore project with severity "error" (not
// "warning"): a caller re-aggregating the projected checks with the harness
// aggregateVerdict stays fail-closed.
//
// These headers stay Qt-free on purpose (the real OutputVerifier/
// harness_verification types live behind Qt/sicnu_agent links); the shapes
// here are the JSON documents those types serialize to.
//

#include <json/json.h>

#include "verify_types.h"

namespace sicnu::verify
{

/// Harness 4.0 ArtifactVerification-shaped document:
/// { "verdict": "PASS"|"FAIL", "checks": [ { "check", "passed", "severity",
///   "code", "details" } ... ] } — check id in "check", verdict Fail for any
/// non-Pass overall, per-check severity "info" on pass else "error",
/// "details" carrying the evidence observed/expected when present.
Json::Value harnessVerificationFromReport( const VerificationReport &report );

/// Teaching LabEvidence-shaped document:
/// { "evidence": [ { "assertion_id", "kind", "passed", "observed",
///   "expected" } ... ] } — field names mirror OutputVerifier::LabEvidence
/// (assertion_id = checkId; observed/expected empty objects when the check
/// carried no evidence). Records in report declaration order.
Json::Value labEvidenceFromReport( const VerificationReport &report );

} // namespace sicnu::verify
