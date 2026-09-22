#pragma once

// criteria_grid.h — pixel-grid compatibility criterion.
//
// The single source of truth for grid judgment is sicnu::data::compareGrids;
// this criterion only samples scene pairs, counts blocking issues and
// grades. It never re-derives grid semantics.

#include "scene_candidate.h"
#include "suitability_goal.h"
#include "suitability_types.h"

#include <QVector>

namespace sicnu::suitability
{

/// Upper bound on compared pairs; beyond it pairs are equal-stride sampled
/// and the sampling is stated in a note.
inline constexpr int kMaxGridPairs = 200;

/// compareGrids verdict over usable scenes that carry a grid snapshot.
/// Fewer than two usable scenes is not applicable (nothing to combine);
/// two or more usable scenes with fewer than two grid snapshots stay
/// Unknown — a possible grid problem must not pass unexamined. Blocking
/// mismatches grade Unsuitable under a strict profile, Marginal otherwise.
SuitabilityCriterion assessGridCompatibility(
    const ResolvedRequirements &req, const QVector<SceneCandidate> &scenes );

} // namespace sicnu::suitability
