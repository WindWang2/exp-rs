#pragma once

// criteria_temporal.h — temporal coverage, density and seasonality criteria.
//
// Pure functions over requirements, scene candidates and dataset facts.
// Time evidence is fused: per-scene acquisition times first, the facts
// temporal extent as a coarse dataset-level fallback (an extent proves a
// range was covered, not how many scenes).

#include "dataset_facts.h"
#include "scene_candidate.h"
#include "suitability_goal.h"
#include "suitability_types.h"

#include <QVector>

#include <optional>

namespace sicnu::suitability
{

/// Usable scenes (or the facts extent) with acquisition time inside the
/// window. No window -> Unknown; no time evidence at all -> Unknown;
/// nothing inside the window -> Unsuitable.
SuitabilityCriterion assessTemporalCoverage( const ResolvedRequirements &req,
                                             const QVector<SceneCandidate> &scenes,
                                             const std::optional<DatasetFacts> &facts );

/// In-window scene count against minScenesInWindow; a facts extent that
/// intersects the window contributes ONE estimated temporal cluster (stated
/// in a note — an extent is not a scene count). Exactly at the minimum the
/// verdict is Marginal with at_minimum evidence.
SuitabilityCriterion assessTemporalDensity( const ResolvedRequirements &req,
                                            const QVector<SceneCandidate> &scenes,
                                            const std::optional<DatasetFacts> &facts );

/// Observed seasons (scene month -> meteorological season, northern-
/// hemisphere assumption; facts samplesBySeason keys with value > 0) against
/// requiredSeasons. Every missing season yields one gap
/// "season.missing.<season>"; any missing season is Unsuitable.
SuitabilityCriterion assessTemporalSeasonality( const ResolvedRequirements &req,
                                                const QVector<SceneCandidate> &scenes,
                                                const std::optional<DatasetFacts> &facts );

} // namespace sicnu::suitability
