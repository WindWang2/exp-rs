#pragma once

// criteria_uncertainty.h — the uncertainty roll-up criterion.
//
// Every assumption the other criteria declared (notes), every diagnostic
// they raised (warnings/errors), every truncation the facts provider
// reported and every invalid measurement they met becomes one machine-
// readable uncertainty source here. The criterion answers a different
// question than the others: not "does the data fit", but "how much of this
// report rests on unverified statements" — a report with undigested
// assumptions does not earn a clean Suitable.

#include "dataset_facts.h"
#include "suitability_types.h"

#include <QVector>

#include <optional>

namespace sicnu::suitability
{

/// Collects the uncertainty sources of one assessment:
///  - every note of every prior criterion ("uncertainty.note.<criterion_id>",
///    non-blocking — a declared assumption or skipped check);
///  - every Warning/Error diagnostic ("uncertainty.diagnostic", non-blocking);
///  - facts truncation ("uncertainty.facts_truncated", non-blocking — the
///    counts the verdicts rest on are partial);
///  - every invalid-measurement diagnostic ("suitability.gsd_invalid",
///    "suitability.cloud_out_of_range", "suitability.grid_abnormal_input",
///    "suitability.aoi_area_not_representable") as
///    "uncertainty.measurement_conflict", BLOCKING — a verdict computed while
///    raw values were broken rests on contaminated evidence.
///
/// Level: any blocking source -> Unsuitable; otherwise any source ->
/// Marginal; no sources -> Suitable. The criterion is always applicable and
/// never Unknown: the sources array always exists (possibly empty), so the
/// answer is always decidable.
SuitabilityCriterion assessUncertaintySources(
    const QVector<SuitabilityCriterion> &priorCriteria,
    const std::optional<DatasetFacts> &facts );

} // namespace sicnu::suitability
