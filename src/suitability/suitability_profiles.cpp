#include "suitability_profiles.h"

namespace sicnu::suitability
{

namespace
{

// Designated initializers (C++20) on purpose: a positional aggregate here
// once needed 12 arguments in exact member order, and a silently shifted
// field would rewrite a task family's scientific prior. Named fields make a
// future member addition a compile-safe edit.
SuitabilityProfile baseProfile( const char *key, const char *displayName,
                                const char *taskFamily, bool requireLabels )
{
    SuitabilityProfile profile;
    profile.key = QLatin1String( key );
    profile.displayName = QLatin1String( displayName );
    profile.taskFamily = QLatin1String( taskFamily );
    profile.requireLabels = requireLabels;
    profile.pseudoLabelsAllowed = true;
    return profile;
}

QVector<SuitabilityProfile> makeBuiltinProfiles()
{
    QVector<SuitabilityProfile> profiles;

    // Land-cover category mapping: declared vocabulary + enough samples to
    // train/validate + 95% AOI coverage (no extrapolating classes).
    profiles.append( baseProfile( "classification", "Land-cover classification",
                                  "classification", true ) );
    profiles.last().minSamples = 200;
    profiles.last().minCoverageFraction = 0.95;

    // Pixel/object masks are denser than points: fewer samples support a
    // model, but the labeled area must still cover 95% of the AOI.
    profiles.append( baseProfile( "segmentation", "Segmentation", "segmentation", true ) );
    profiles.last().minSamples = 50;
    profiles.last().minCoverageFraction = 0.95;

    // Bi-temporal by definition: at least one scene per date. Pseudo-labels
    // are forbidden — benchmark evaluation must not score a model against
    // labels derived from a model.
    profiles.append( baseProfile( "change_detection", "Change detection",
                                  "change_detection", true ) );
    profiles.last().minSamples = 100;
    profiles.last().minCoverageFraction = 0.95;
    profiles.last().minScenesInWindow = 2;
    profiles.last().pseudoLabelsAllowed = false;

    // Targets must be resolvable: a 2 m GSD ceiling keeps objects more than
    // a few pixels wide; 300 targets floor stable per-class evaluation;
    // 90% coverage (detection tolerates small uncovered margins).
    profiles.append( baseProfile( "object_detection", "Object detection",
                                  "object_detection", true ) );
    profiles.last().minSamples = 300;
    profiles.last().minCoverageFraction = 0.9;
    profiles.last().maxGsdM = 2.0;

    // Continuous targets need a declared label field and enough samples to
    // constrain the response surface.
    profiles.append( baseProfile( "regression", "Regression", "regression", true ) );
    profiles.last().minSamples = 100;

    // Labels are future observations, not annotated samples — no label
    // requirement, but at least 4 scenes so a time series has structure.
    profiles.append( baseProfile( "temporal_prediction", "Temporal prediction",
                                  "temporal_prediction", false ) );
    profiles.last().minScenesInWindow = 4;

    // Band requirements are scene/library-specific and come from the goal;
    // matching is unsupervised (no label requirement).
    profiles.append( baseProfile( "spectral_matching", "Spectral matching",
                                  "spectral_matching", false ) );

    // Phenological metrics need full-year coverage: at least 6 scenes spread
    // over ALL FOUR meteorological seasons; a missing season (e.g. winter
    // dormancy) invalidates the curve.
    profiles.append( baseProfile( "phenology", "Phenology (full-year temporal curve)",
                                  "temporal_prediction", false ) );
    profiles.last().minScenesInWindow = 6;
    profiles.last().requiredSeasons = { QStringLiteral( "spring" ), QStringLiteral( "summer" ),
                                        QStringLiteral( "autumn" ), QStringLiteral( "winter" ) };

    return profiles;
}

const QVector<SuitabilityProfile> &builtinProfiles()
{
    static const QVector<SuitabilityProfile> profiles = makeBuiltinProfiles();
    return profiles;
}

} // namespace

const QVector<QString> &builtinProfileKeys()
{
    static const QVector<QString> keys = []()
    {
        QVector<QString> result;
        result.reserve( builtinProfiles().size() );
        for ( const SuitabilityProfile &profile : builtinProfiles() )
            result.append( profile.key );
        return result;
    }();
    return keys;
}

bool isBuiltinProfile( const QString &key )
{
    if ( key.isEmpty() )
        return false;
    for ( const SuitabilityProfile &profile : builtinProfiles() )
    {
        if ( profile.key == key )
            return true;
    }
    return false;
}

std::optional<SuitabilityProfile> builtinProfile( const QString &key )
{
    if ( key.isEmpty() )
        return std::nullopt;
    for ( const SuitabilityProfile &profile : builtinProfiles() )
    {
        if ( profile.key == key )
            return profile;
    }
    return std::nullopt;
}

QString defaultProfileKeyForFamily( sicnu::dataset::BenchmarkTaskFamily family )
{
    switch ( family )
    {
        case sicnu::dataset::BenchmarkTaskFamily::Classification:
            return QStringLiteral( "classification" );
        case sicnu::dataset::BenchmarkTaskFamily::Segmentation:
            return QStringLiteral( "segmentation" );
        case sicnu::dataset::BenchmarkTaskFamily::ChangeDetection:
            return QStringLiteral( "change_detection" );
        case sicnu::dataset::BenchmarkTaskFamily::ObjectDetection:
            return QStringLiteral( "object_detection" );
        case sicnu::dataset::BenchmarkTaskFamily::Regression:
            return QStringLiteral( "regression" );
        case sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction:
            return QStringLiteral( "temporal_prediction" );
        case sicnu::dataset::BenchmarkTaskFamily::SpectralMatching:
            return QStringLiteral( "spectral_matching" );
    }
    return QString();
}

} // namespace sicnu::suitability
