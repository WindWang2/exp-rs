#pragma once

// criteria_spatial.h — spatial coverage and resolution criteria.
//
// Pure functions: requirements and scene candidates in, one criterion out.
// The assessor core does not reproject — scenes in a different CRS than the
// AOI are excluded (with a note), never transformed.

#include "suitability_goal.h"
#include "suitability_types.h"

#include <QVector>

#include "scene_candidate.h"

namespace sicnu::suitability
{

/// AOI ∩ ⋃(usable scene rectangles), same-CRS scenes only, measured by
/// coordinate-compressed rectangle union (safe at the assessor's scene cap).
SuitabilityCriterion assessSpatialCoverage( const ResolvedRequirements &req,
                                            const QVector<SceneCandidate> &scenes );

/// Meter-GSD range fit over usable scenes. Scenes with unknown GSD are
/// counted as unknown evidence and never judged.
SuitabilityCriterion assessResolution( const ResolvedRequirements &req,
                                       const QVector<SceneCandidate> &scenes );

} // namespace sicnu::suitability
