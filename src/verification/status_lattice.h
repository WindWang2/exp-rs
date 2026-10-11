/***************************************************************************
  status_lattice.h — the three-valued verification lattice (RS14-10, Slice A)

  Everything the Unified Scientific Verifier concludes is expressed in ONE
  lattice: Pass / Fail / Indeterminate. The third value is not decoration and
  it is not a weaker Fail — "we could not obtain the evidence" must never be
  reported as either success or failure, because those two drive very
  different reactions (accept the result vs. redo the science).

  Precedents this deliberately mirrors, so the whole repo stays consistent:

    - `ReplayCheckStatus{Ok,Missing,Mismatched,Unknown}` (replay_readiness.h)
      already specifies that Unknown exists and DOWNGRADES the overall level.
    - artifact fact provenance has five grades with the explicit rule that
      "unknown" degrades to a warning and is "never fake pass/fail"
      (workflow_ir.h).
    - `ReproductionHooks` that are not wired degrade to BestEffort rather than
      reporting ok.

  Rules enforced here, and relied on by every later slice:

    1. Fail dominates: a single Fail makes the combination Fail.
    2. Indeterminate downgrades a would-be Pass; a Pass never absorbs it.
    3. The EMPTY combination is Indeterminate. Zero checks is absence of
       evidence, never a pass. This is the track's central anti-fail-open rule.
    4. Wire spelling is closed: "pass" | "fail" | "indeterminate". Look-alikes
       ("passed", "PASS", "") are refused rather than mapped.
 ***************************************************************************/
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sicnu::verification
{

/// Outcome of one check, and therefore of any roll-up of checks.
enum class CheckStatus
{
    Pass,
    Fail,
    Indeterminate,
};

/// Deterministic ordering used by comparisons and roll-ups. Higher is worse.
/// Exposed so callers never invent their own ranking.
int statusSeverity( CheckStatus status );

/// Wire spelling ("pass" | "fail" | "indeterminate"). Never nullptr.
const char *statusToWire( CheckStatus status );

/// Strict inverse of statusToWire. Returns false for anything outside the
/// closed vocabulary — including plausible-looking aliases — and leaves
/// @p out untouched in that case.
bool statusFromWire( const std::string &wire, CheckStatus &out );

/// Least-upper-bound in the lattice: Fail-dominant, Indeterminate-transparent.
CheckStatus combineStatus( CheckStatus left, CheckStatus right );

/// Roll-up over any number of statuses. An EMPTY vector is Indeterminate.
CheckStatus combineAll( const std::vector<CheckStatus> &statuses );

/// How a caller wants Indeterminate to be reported at the boundary.
///   Keep  — report Indeterminate (default; track semantics).
///   Fail  — strict mode: promote Indeterminate to Fail, but RECORD which
///           checks were promoted so nothing becomes a failure silently.
enum class IndeterminatePolicy
{
    Keep,
    Fail,
};

/// Result of a labelled roll-up.
struct RollupResult
{
    CheckStatus status = CheckStatus::Indeterminate;

    /// Ids whose Indeterminate was promoted to Fail by the strict policy.
    /// Empty in every other case — including the empty-input case, which is
    /// Indeterminate without anyone to blame.
    std::vector<std::string> promoted;
};

/// Roll-up over (id, status) pairs. Empty input stays Indeterminate even under
/// IndeterminatePolicy::Fail: there is no failure to point at, and pretending
/// otherwise would be exactly the silent degradation this module exists to
/// prevent.
RollupResult rollUp( const std::vector<std::pair<std::string, CheckStatus>> &labelled,
                     IndeterminatePolicy policy = IndeterminatePolicy::Keep );

} // namespace sicnu::verification
