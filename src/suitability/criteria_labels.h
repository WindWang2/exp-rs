#pragma once

// criteria_labels.h — label availability criterion.
//
// Labels are dataset-level evidence (facts), not per-scene: the criterion
// grades the declared schema, the observed class vocabulary, the sample
// volume and the pseudo-label policy against the resolved requirements.

#include "dataset_facts.h"
#include "suitability_goal.h"
#include "suitability_types.h"

#include <optional>

namespace sicnu::suitability
{

/// Label evidence fit. A declared label requirement with NO label evidence
/// anywhere (no schema, no classes) is Unsuitable ("labels.schema_missing"):
/// the requirement is explicit, so its absence is a verdict, not a shrug.
/// sampleCount == -1 keeps the volume check Unknown — an unknown count never
/// poses as zero. Pseudo labels under a forbidding policy downgrade an
/// otherwise-fitting dataset to Marginal ("labels.pseudo_present").
SuitabilityCriterion assessLabelAvailability(
    const ResolvedRequirements &req, const std::optional<DatasetFacts> &facts );

} // namespace sicnu::suitability
