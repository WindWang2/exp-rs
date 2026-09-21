/***************************************************************************
  checks_numeric.h — NumericRange family, plus the numeric primitives every
  other check family shares (RS14-10, Slice C)

  Why NaN gets its own branch instead of falling through the interval test:

    `!(x < lower) && !(x > upper)` is TRUE for NaN. A range check written the
    obvious way therefore answers "in range" for an observation that carries no
    magnitude at all — and since the whole point of this module is deciding
    whether a RESULT may be trusted, that single line converts "the science
    produced garbage" into "the science passed". The same trap exists in every
    family that compares two numbers, which is why `numericSame()` lives here
    and is shared rather than re-derived per family.

  What a number means here:
    - Finite is POLICY: NaN and +-inf fail with VERIFY.NUMERIC_NOT_FINITE.
    - The expectation may itself be unusable (a NaN pin). An unusable
      expectation judges neither Pass nor Fail — it is VERIFY.SPEC_INVALID,
      because "we cannot form the question" is not "the answer is wrong".
    - Equality is resolved, not bit-exact: 1024 observed and 1024.0 declared
      are the same quantity, as are 1e3 and 1000, and so are 0.1+0.2 and 0.3.
      Both the tolerance below and the 12-significant-digit canonical text are
      consulted, because a disagreement between the two would let one report
      state that two measurements are and are not the same quantity.

  params (everything under `expect`; any other member is REFUSED rather than
  ignored — silently dropping an expectation manufactures a pass):

    expect.value            number  exact pin (within tolerance)
    expect.lower            number  lower bound (default -inf)
    expect.upper            number  upper bound (default +inf)
    expect.inclusive_lower  bool    default true
    expect.inclusive_upper  bool    default true
    expect.tolerance        number  absolute tolerance >= 0, default see
                                    numericTolerance()

  At least one of value/lower/upper must be declared. The observed quantity is
  asked of `MetricProvider` under `check.subject.id`.
 ***************************************************************************/
#pragma once

#include "verification/providers.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <string>

namespace sicnu::verification
{

// ---------------------------------------------------------------------------
// Primitives shared with the other check families
// ---------------------------------------------------------------------------
//
// They are declared next to the numeric check because that is where they are
// exercised, but they are NOT private to it: state / artifact / relational (and
// later provenance / cross-output) include this header and reuse them.
//
// The alternative — one small private copy of "are these two numbers the same"
// per file — is worse than the coupling, because a tolerance that drifts
// between two families is invisible: both keep passing while the module says
// two contradictory things about one measurement.

/// Result skeleton every family starts from: identity copied from the check,
/// evidence stamped with the kind and with the bundle that produced it. Doing
/// it in one place is what guarantees no family can forget sourceId — and
/// "who told you that?" is unanswerable after the fact if it is missing.
CheckResult beginResult( const VerificationCheck &check, const VerificationInputs &inputs );

/// Closes a result and enforces the invariant every consumer relies on:
///   Pass  -> no failure code; anything else -> a KNOWN, non-empty one.
/// The default status of CheckResult is Indeterminate, so leaving the status
/// untouched is always the conservative reading; never reach for Pass here.
void finishResult( CheckResult &result, CheckStatus status, const std::string &failureCode,
                   const std::string &message );

/// Prefixes @p body with the check title when there is one, so a report line
/// reads as a sentence about THIS check instead of an anonymous verdict.
std::string titleScopedMessage( const VerificationCheck &check, const std::string &body );

/// False for NaN and +-inf. Called before ANY comparison, always.
bool numericFinite( double value );

/// Absolute comparison tolerance for @p left and @p right: 1e-9 relative to the
/// larger magnitude, floored at 1e-9 absolute. `numericSame` also accepts an
/// explicit `expect.tolerance` override supplied by the caller.
double numericTolerance( double left, double right );

/// True when two finite measurements denote the same quantity, at either the
/// canonical 12-significant-digit spelling or within tolerance. Never true when
/// either side is not finite — that case belongs to `numericFinite`.
bool numericSame( double left, double right, double tolerance = -1.0 );

/// The ONLY way a number reaches an evidence record: its canonical 12
/// significant digit TEXT, following the grading-preview discipline documented
/// in canonical_json.h. Storing the text rather than the double means a report
/// has exactly one spelling of every measurement (no int/real disagreement, no
/// platform-dependent last digit) and can be hashed reproducibly. A NaN written
/// this way stays legible instead of poisoning canonicalization.
Json::Value canonicalNumber( double value );

// ---------------------------------------------------------------------------
// The check itself
// ---------------------------------------------------------------------------

CheckResult runNumericRangeCheck( const VerificationCheck &check, const VerificationInputs &inputs );

} // namespace sicnu::verification
