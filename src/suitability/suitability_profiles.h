#pragma once

// suitability_profiles.h — the built-in task profile table.
//
// A profile carries DEFAULTS only (an empty optional / empty list means
// "the profile does not set this"): explicit goal values always win and
// profiles never compute. The empty profileKey selects the task family's
// same-named default profile; "phenology" is an extra profile that only
// resolves in combination with the TemporalPrediction family.
//
// Applicability rationale (the scientific prior each profile encodes):
//  - classification: land-cover category mapping needs a declared label
//    vocabulary and enough samples per class to train and validate; 95%
//    AOI coverage avoids extrapolating classes from unseen area.
//  - segmentation: pixel/object masks are denser than points, so fewer
//    samples (50) support a model, but the labeled area must still cover
//    95% of the AOI.
//  - change_detection: bi-temporal by definition — at least 2 scenes in
//    the window (one per date); 100 labeled samples; pseudo-labels are
//    forbidden because benchmark evaluation must not score a model
//    against labels derived from a model.
//  - object_detection: targets must be resolvable — a 2 m GSD ceiling
//    keeps objects more than a few pixels wide; 300 targets as a floor
//    for stable per-class evaluation; 90% coverage (detection tolerates
//    small uncovered margins).
//  - regression: continuous targets need a declared label field and at
//    least 100 samples to constrain the response surface.
//  - temporal_prediction: labels are future observations, not annotated
//    samples — no label requirement, but at least 4 scenes so a time
//    series has structure to fit.
//  - spectral_matching: band requirements are scene/library-specific and
//    come from the goal; no label requirement (matching is unsupervised).
//  - phenology (TemporalPrediction family + profileKey "phenology"):
//    phenological metrics need full-year coverage — at least 6 scenes
//    spread over ALL FOUR meteorological seasons; missing one season
//    (e.g. winter dormancy) invalidates the curve.

#include "../dataset/dataset_types.h"
#include "suitability_goal.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace sicnu::suitability
{

struct SuitabilityProfile
{
    QString key;        ///< non-empty stable name
    QString displayName;
    /// BenchmarkTaskFamily vocabulary string this profile belongs to.
    QString taskFamily;

    std::optional<bool> requireLabels;
    std::optional<qint64> minSamples;
    std::optional<double> minCoverageFraction;
    std::optional<qint64> minScenesInWindow;
    std::optional<double> minGsdM;
    std::optional<double> maxGsdM;
    QStringList requiredSeasons; ///< empty = profile does not set seasons
    /// Set false only by change_detection (benchmark semantics: pseudo
    /// labels never enter an evaluation).
    std::optional<bool> pseudoLabelsAllowed;
    std::optional<bool> gridStrict;
};

/// Table order: classification, segmentation, change_detection,
/// object_detection, regression, temporal_prediction, spectral_matching,
/// phenology.
const QVector<QString> &builtinProfileKeys();

/// True only for keys in the built-in table.
bool isBuiltinProfile( const QString &key );

/// Table lookup; nullopt for keys outside the built-in table.
std::optional<SuitabilityProfile> builtinProfile( const QString &key );

/// The default profile key of a task family ("classification" for
/// Classification, ...); empty for a family with no default.
QString defaultProfileKeyForFamily( sicnu::dataset::BenchmarkTaskFamily family );

} // namespace sicnu::suitability
