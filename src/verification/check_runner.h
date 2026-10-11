/***************************************************************************
  check_runner.h — deterministic evaluation of a spec against supplied facts

  The runner is the ONLY place that turns declarations into verdicts, and it is
  deliberately dull: no caching, no state, no threading, no clock. Given the
  same spec and the same provider answers it must produce byte-identical output,
  because a report is content-addressed and gets compared across runs.

  Its most important job is refusing to be helpful in the wrong direction.
  Four inputs have no honest verdict available, and every one of them must land
  on Indeterminate with a typed code rather than quietly contributing nothing:

    0 checks              -> Indeterminate + VERIFY.NO_CHECKS
    unknown check kind    -> Indeterminate + VERIFY.UNSUPPORTED_CHECK_KIND
    over budget           -> Indeterminate + VERIFY.BUDGET_EXCEEDED
    declared evidence absent -> Indeterminate (+ the spec's own required set)

  The temptation in each case is to "continue with what we have", which a
  roll-up then reads as success. A verification layer whose failure mode is
  silence is worse than none.
 ***************************************************************************/
#pragma once

#include "verification/providers.h"
#include "verification/report.h"
#include "verification/spec.h"

#include <string>
#include <vector>

namespace sicnu::verification
{

/// Evaluates @p spec against @p inputs and rolls the results up into a report.
///
/// Results appear in the spec's declaration order — the order a human wrote
/// them in — so a report is readable in the same terms as the spec.
///
/// @param nodeChecks nodeId -> ids of the checks forming that node's
///        postcondition. Empty means "single unnamed scope": the report then
///        has no node level, and the task outcome is rolled up from the checks
///        directly.
VerificationReport runSpec( const VerificationSpec &spec, const VerificationInputs &inputs,
                            const NodeCheckMap &nodeChecks = {} );

/// Evaluates without any node structure: one implicit scope named after the
/// spec. Convenience for callers that verify a single artifact.
VerificationReport runSpecFlat( const VerificationSpec &spec, const VerificationInputs &inputs );

} // namespace sicnu::verification
