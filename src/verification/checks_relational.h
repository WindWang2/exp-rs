/***************************************************************************
  checks_relational.h — CheckKind::RelationalConsistency (RS14-10, Slice C)

  A single fact can be plausible while a PAIR of facts is impossible: a raster
  that reports 512 x 512 pixels and 262656 pixel-count says nothing wrong about
  either number separately, yet together they cannot describe one grid. This
  family exists for claims that only exist BETWEEN facts.

  The rule it must never break: NEITHER SIDE MAY BE ASSUMED. If one operand's
  evidence is absent, the relation is unevaluated — Indeterminate with
  VERIFY.EVIDENCE_UNAVAILABLE — rather than read as zero, as absent, or as
  "everything else matched so this probably did too". Assuming a value for the
  missing side is how a check about consistency becomes a check that agrees
  with itself.

  params:

    relation    string   one of allRelations(): "equals" | "product_equals" |
                         "sum_equals"
    operands    array    exactly two operand descriptors:
                           { "source": "artifact", "ref": <ref>, "fact": "size.width" }
                           { "source": "metric",   "name": <metric name> }
                         a missing `ref`/`name` falls back to check.subject.id,
                         so the subject is the default artifact under test
    expect.value  number the quantity the combination must equal (required for
                         the arithmetic relations)
    expect.tolerance number absolute tolerance override

  `product_equals` is the workhorse: width x height == declared pixel count.
  `equals` covers "these two facts must agree", including the unit/magnitude
  trap — 1024 (an integer fact) and 1024.0 (a real-valued metric) are one
  quantity, and 1e3 and 1000 are another spelling of the same one.
 ***************************************************************************/
#pragma once

#include "verification/providers.h"
#include "verification/verification_types.h"

#include <string>
#include <vector>

namespace sicnu::verification
{

/// The closed relation vocabulary, sorted. An unknown relation is refused
/// (VERIFY.SPEC_INVALID) rather than quietly evaluated as something else.
std::vector<std::string> allRelations();

CheckResult runRelationalCheck( const VerificationCheck &check, const VerificationInputs &inputs );

} // namespace sicnu::verification
