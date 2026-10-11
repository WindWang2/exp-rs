/***************************************************************************
  checks_reproducibility.h — can this result be produced again? (RS14-10, C)

  Reproducibility is the claim that re-running the same plan over the same
  inputs yields the same bytes. It is checked by comparing two content
  digests: the one the specification pinned, and the one the run recorded.

  Two digests are only comparable when they were produced by the SAME
  algorithm. A sha256 and an md5 over the same bytes are different strings,
  and calling that a mismatch is a lie: it asserts the outputs differ when the
  truth is that the comparison was never possible. It is equally wrong to call
  it a match. So a differing algorithm id is a THIRD outcome — Indeterminate —
  carrying both digests so a human can see what could not be compared.

  Same shape as `ReplayCheckStatus::Unknown` (src/experiment/replay_readiness.h):
  unknown lowers the grade, it never passes it.

  And when only one side exists there is nothing to compare at all: one
  digest is not evidence of reproducibility, nor of irreproducibility.
 ***************************************************************************/
#pragma once

#include "verification/providers.h"
#include "verification/verification_types.h"

namespace sicnu::verification
{

/// Evaluates one `reproducibility_digest` check.
///
/// Expectation: `check.params["expected"]` — `{ "algorithm": str,
/// "digest": str }`. Omitting it is legal and means "no pin declared"; the
/// check then has one side only and is Indeterminate.
///
/// Subject: `check.subject.id`, passed straight to the digest provider.
CheckResult runReproducibilityDigestCheck( const VerificationCheck &check,
                                           const VerificationInputs &inputs );

} // namespace sicnu::verification
