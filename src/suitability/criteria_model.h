#pragma once

// criteria_model.h — model-input compatibility criterion.
//
// The model side is the resolved requirement (trained-on band roles, GSD
// range, modality); the data side is the fused usable-scene ∪ facts
// evidence pool. Neither side is ever guessed: absent evidence is Unknown.

#include "dataset_facts.h"
#include "scene_candidate.h"
#include "suitability_goal.h"
#include "suitability_types.h"

#include <QVector>

#include <optional>

namespace sicnu::suitability
{

/// Model-input fit. A required model band role missing from the fused pool
/// ("model.band_missing.<role>"), an empty GSD-range intersection
/// ("model.resolution_out_of_range") and a modality no data source carries
/// ("model.modality_mismatch") are each Unsuitable — the model cannot
/// consume the data. A GSD range that only partially covers the usable
/// scenes is Marginal; full coverage is Suitable.
SuitabilityCriterion assessModelCompatibility(
    const ResolvedRequirements &req, const QVector<SceneCandidate> &scenes,
    const std::optional<DatasetFacts> &facts );

} // namespace sicnu::suitability
