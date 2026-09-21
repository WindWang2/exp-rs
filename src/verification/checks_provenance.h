/***************************************************************************
  checks_provenance.h — is this result citable? (RS14-10, Slice C)

  A number is not a result. "0.83" is a result only if someone can later say
  which algorithm produced it, from which inputs, under which parameters and
  in which coordinate reference — otherwise it is a plausible-looking number
  that nobody can cite, reproduce or refute. That is why provenance sits in
  the verification core rather than in a report decorator: an incomplete
  provenance set is a defect of the RESULT, not of its documentation.

  The check therefore answers exactly three things, and the third is the one
  that is usually lost:

    Pass           every required dimension is declared present.
    Fail           at least one required dimension is declared absent.
    Indeterminate  we could not obtain the completeness set at all
                   (provider missing / refused / unwired), or a required
                   dimension was never mentioned by the provider.

  "Never mentioned" is deliberately NOT the same as "absent". providers.h
  states the rule outright — a dimension not mentioned is unknown — and
  collapsing it into either Pass or Fail would assert a fact nobody observed.
  Absence of evidence is not evidence of absence.

  `ProvenanceDimension` mirrors `experiment::EvidenceCompleteness::Dimension`
  (name / present / detail) so the future adapter moves fields rather than
  reinterpreting them.
 ***************************************************************************/
#pragma once

#include "verification/providers.h"
#include "verification/verification_types.h"

namespace sicnu::verification
{

/// Evaluates one `provenance_completeness` check.
///
/// Expectation: `check.params["required_dimensions"]` — a non-empty array of
/// dimension names. Absent or malformed is a caller defect
/// (VERIFY.SPEC_INVALID) and yields Indeterminate: a check that never says
/// what "complete" means cannot be called complete.
///
/// Subject: `check.subject.id`, passed straight to the provenance provider.
CheckResult runProvenanceCompletenessCheck( const VerificationCheck &check,
                                            const VerificationInputs &inputs );

} // namespace sicnu::verification
