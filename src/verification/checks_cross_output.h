/***************************************************************************
  checks_cross_output.h — do two outputs agree about the same thing? (RS14-10, C)

  The same tile is often produced twice by two routes (a re-tiled mosaic and a
  direct export, a CPU and a GPU path, a before and an after repair). When they
  disagree beyond tolerance, at least one of them is wrong, and until we know
  which, neither may be trusted — this is the cheapest strong signal a
  verification pass can produce.

  What it must NOT do is treat a half-observed comparison as an agreement.
  A single observed value is not a comparison: `|a - b| <= tolerance` with `b`
  unknown is not satisfiable, and returning Pass there would launder "we only
  looked at one output" into "the two outputs agree". Likewise a non-finite
  statistic makes the difference unordered, so the naive comparison silently
  reports consistency. Both are Indeterminate here.

  Rounding matters too: every number written into the evidence record goes
  through `roundSignificant`, because the record is what gets hashed and
  compared between runs.
 ***************************************************************************/
#pragma once

#include "verification/providers.h"
#include "verification/verification_types.h"

namespace sicnu::verification
{

/// Evaluates one `cross_output_consistency` check.
///
/// Expectation: `check.params` —
///   "artifacts"  array of exactly two artifact references (left, right)
///   "statistic"  name of the fact to compare on each side
///   "tolerance"  maximum acceptable absolute difference
/// Any of them missing or malformed is VERIFY.SPEC_INVALID with Indeterminate:
/// an unpinned tolerance does not mean "exact", it means "unspecified".
///
/// Subject: `check.subject.id` — what both outputs are about (the tile).
CheckResult runCrossOutputConsistencyCheck( const VerificationCheck &check,
                                            const VerificationInputs &inputs );

} // namespace sicnu::verification
