/***************************************************************************
  checks_state.h — CheckKind::StateInvariant (RS14-10, Slice B)

  Why this is its own family rather than a field comparison inside a general
  checker: a scientific operator is written against ONE numeric domain, and
  feeding dB magnitudes to an operator calibrated for linear power does not
  produce a slightly wrong number — it produces a radiometrically meaningless
  one whose bias changes with local incidence angle. That is a claim about the
  RESULT, so it belongs to verification rather than to argument plumbing.

  The rule that makes this check three-valued is the one the whole track exists
  to enforce:

      a DECLARED-WRONG state  -> Fail          (VERIFY.STATE_INVARIANT_VIOLATION)
      an UNDECLARED     state -> Indeterminate (VERIFY.STATE_TOKEN_MISSING)

  Undeclared is common and legitimate: plenty of intermediate products carry no
  domain token at all. Reporting that as a violation would invent evidence, and
  inventing evidence is this module's cardinal sin. The same distinction is
  already the repo's own discipline in workflow_ir.h, whose five fact-provenance
  grades downgrade `unknown` to a warning and "never fake pass/fail".

  A provider that could not answer at all is likewise Indeterminate: Missing
  becomes VERIFY.STATE_TOKEN_MISSING and Refused becomes
  VERIFY.EVIDENCE_REFUSED (carrying the provider's reason), never Fail.

  params (everything under `expect`; any unlisted member is REFUSED rather than
  ignored — a dropped expectation would silently manufacture a pass):

    expect.numeric_domain      string  e.g. "linear" | "db"
    expect.radiometric_state   string  e.g. "sigma0" | "gamma0" | "beta0"

  At least one of the two must be declared. The state is asked of
  `StateProvider` under `check.subject.id`.
 ***************************************************************************/
#pragma once

#include "verification/providers.h"
#include "verification/verification_types.h"

namespace sicnu::verification
{

CheckResult runStateInvariantCheck( const VerificationCheck &check, const VerificationInputs &inputs );

} // namespace sicnu::verification
