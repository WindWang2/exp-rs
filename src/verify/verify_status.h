// src/verify/verify_status.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — status lattice.
//
// The closed tri-state every verification check and every report resolves
// to. The lattice is FAIL-CLOSED: an aggregate with any Fail is Fail, else
// any Indeterminate is Indeterminate, else Pass. "Could not verify" is
// never allowed to read as "verified" — Slice G's adversarial matrix is
// built around that invariant.
//
// Distinct from (and never replacing) the existing vocabularies:
//   - sicnu::agent::harness::Verdict   (artifact structural gate, PASS/PASS_WITH_WARNINGS/FAIL)
//   - lab grading verdicts             (pass/fail/unverifiable, educational scoring)
//   - ReplayCheckStatus                (reproduction dependency readiness)
// Projections to those shapes live in projections.h; the words never mix.
//

#include <string>
#include <vector>

namespace sicnu::verify
{

enum class VerificationStatus
{
    Pass,
    Fail,
    Indeterminate,
};

/// Wire-stable lowercase forms: "pass" | "fail" | "indeterminate".
const char *statusToWire( VerificationStatus status );

/// Human-readable form for teaching renderers: "Pass" | "Fail" | "Indeterminate".
const char *statusToString( VerificationStatus status );

/// Aggregation lattice: any Fail -> Fail; else any Indeterminate ->
/// Indeterminate; else Pass. An EMPTY input is Indeterminate — nothing
/// verified must never aggregate to pass.
VerificationStatus aggregateStatus( const std::vector<VerificationStatus> &statuses );

} // namespace sicnu::verify
