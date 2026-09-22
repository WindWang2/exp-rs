#pragma once

// criteria_spectral.h — spectral band availability and cloud cover quality
// criteria.
//
// Pure functions over requirements, scene candidates and (where defined)
// dataset facts. Band evidence from scenes and facts is fused — the union is
// the dataset's band pool; grading itself never invents metadata.

#include "dataset_facts.h"
#include "scene_candidate.h"
#include "suitability_goal.h"
#include "suitability_types.h"

#include <QVector>

#include <optional>

namespace sicnu::suitability
{

/// requiredBandRoles ⊆ ⋃(usable scene bandRoles ∪ facts bandRoles). Every
/// missing role yields one gap "band.missing.<role>"; ANY missing required
/// role makes the criterion Unsuitable (a task without its required bands is
/// infeasible, not merely weakened).
SuitabilityCriterion assessSpectralBands( const ResolvedRequirements &req,
                                          const QVector<SceneCandidate> &scenes,
                                          const std::optional<DatasetFacts> &facts );

/// Cloud cover fit for scenes with a known 0-100 value. Unknown or
/// out-of-range values count as unknown evidence (diagnostic, no clamping);
/// a known-value set that is entirely above the limit is Unsuitable, a
/// partially clear set Marginal.
SuitabilityCriterion assessCloudCover( const ResolvedRequirements &req,
                                       const QVector<SceneCandidate> &scenes );

} // namespace sicnu::suitability
