#pragma once

// suitability_goal.h — the experiment goal as a versioned value object plus
// its resolution into the concrete requirements the criteria consume.
//
// resolveRequirements is where goal-level policy (built-in defaults) meets
// explicit goal values. This slice ships the default profile only; the named
// profile table lands in the profiles slice, so any non-empty profileKey is
// an honest typed "unknown profile" failure today.

#include "../data/data_asset.h"
#include "../data/data_result.h"
#include "../dataset/dataset_types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::suitability
{

inline constexpr int kSuitabilityGoalSerializationVersion = 1;

/// Coverage fraction applied when the goal does not pin one (0 = default).
inline constexpr double kDefaultMinCoverageFraction = 0.95;

struct SuitabilityGoal
{
    sicnu::dataset::BenchmarkTaskFamily taskFamily =
        sicnu::dataset::BenchmarkTaskFamily::Classification;
    /// Empty = task-family default. No profile table exists yet; a non-empty
    /// key fails resolveRequirements typed ("suitability.profile_unknown").
    QString profileKey;

    bool hasAoi = false;
    sicnu::data::SpatialExtent aoi;
    QString aoiCrsWkt;

    bool hasTimeWindow = false;
    QDateTime windowStartUtc;
    QDateTime windowEndUtc;

    /// "spring"|"summer"|"autumn"|"winter".
    QStringList requiredSeasons;

    /// 0 = no limit; when both are set minGsdM <= maxGsdM must hold.
    double minGsdM = 0.0;
    double maxGsdM = 0.0;

    /// band_role.h vocabulary.
    QStringList requiredBandRoles;

    /// <0 = no limit; otherwise must lie in [-1, 100].
    double maxCloudCoverPercent = -1.0;

    qint64 minSamples = 0;
    QStringList requiredClasses;
    bool requireLabels = false;

    // Model-fit requirements (optional; hasModel gates them).
    bool hasModel = false;
    QStringList modelRequiredBandRoles;
    double modelMinGsdM = 0.0;
    double modelMaxGsdM = 0.0;
    QString modelModality;

    /// 0 = use kDefaultMinCoverageFraction; otherwise must lie in (0, 1].
    double minCoverageFraction = 0.0;

    /// 0 = no density requirement (temporal density not applicable).
    qint64 minScenesInWindow = 0;

    QJsonObject toJson() const;
    static sicnu::data::Result<SuitabilityGoal> fromJson( const QJsonObject &json );

    /// SHA-256 over the canonical compact JSON — the replay/pairing handle a
    /// report carries as its goalDigest.
    QString contentDigest() const;
};

/// The immutable requirement set the criteria consume: explicit goal values
/// with policy defaults resolved (minCoverageFractionResolved etc.). Only the
/// fields the criteria actually read are exposed; label/model requirements
/// join when their criteria do.
struct ResolvedRequirements
{
    // spatial.coverage
    bool hasAoi = false;
    sicnu::data::SpatialExtent aoi;
    QString aoiCrsWkt;
    double minCoverageFractionResolved = kDefaultMinCoverageFraction;

    // spatial.resolution
    double minGsdM = 0.0;
    double maxGsdM = 0.0;

    // spectral.bands / quality.cloud
    QStringList requiredBandRoles;
    double maxCloudCoverPercent = -1.0;

    // temporal.coverage / density / seasonality
    bool hasTimeWindow = false;
    QDateTime windowStartUtc;
    QDateTime windowEndUtc;
    QStringList requiredSeasons;
    qint64 minScenesInWindow = 0;

    // labels.availability
    bool requireLabels = false;
    QStringList requiredClasses;
    qint64 minSamples = 0;
    /// Profile-level policy (no goal field): true unless a profile forbids
    /// pseudo labels (benchmark semantics — change_detection).
    bool pseudoLabelsAllowed = true;

    // grid.compatibility
    /// Profile-level strictness (no goal field): blocking grid mismatches
    /// grade Unsuitable when strict, Marginal otherwise.
    bool gridStrict = false;

    // model.compatibility
    bool hasModel = false;
    QStringList modelRequiredBandRoles;
    double modelMinGsdM = 0.0;
    double modelMaxGsdM = 0.0;
    QString modelModality;
};

/// Validates the goal (typed failure "suitability.profile_unknown" for an
/// unknown profileKey, "suitability.goal_invalid" for contradictory values)
/// and resolves defaults.
sicnu::data::Result<ResolvedRequirements> resolveRequirements( const SuitabilityGoal &goal );

} // namespace sicnu::suitability
