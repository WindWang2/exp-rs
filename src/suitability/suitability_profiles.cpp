#include "suitability_profiles.h"

namespace sicnu::suitability
{

namespace
{

QVector<SuitabilityProfile> makeBuiltinProfiles()
{
    QVector<SuitabilityProfile> profiles;

    profiles.append( SuitabilityProfile{
        QStringLiteral( "classification" ),
        QStringLiteral( "Land-cover classification" ),
        QStringLiteral( "classification" ),
        /*requireLabels*/ true,
        /*minSamples*/ 200,
        /*minCoverageFraction*/ 0.95,
        /*minScenesInWindow*/ {},
        /*minGsdM*/ {},
        /*maxGsdM*/ {},
        /*requiredSeasons*/ {},
        /*pseudoLabelsAllowed*/ true,
        /*gridStrict*/ {} } );

    profiles.append( SuitabilityProfile{
        QStringLiteral( "segmentation" ),
        QStringLiteral( "Segmentation" ),
        QStringLiteral( "segmentation" ),
        true,
        50,
        0.95,
        {},
        {},
        {},
        {},
        true,
        {} } );

    profiles.append( SuitabilityProfile{
        QStringLiteral( "change_detection" ),
        QStringLiteral( "Change detection" ),
        QStringLiteral( "change_detection" ),
        true,
        100,
        0.95,
        2,
        {},
        {},
        {},
        /*pseudoLabelsAllowed*/ false,
        {} } );

    profiles.append( SuitabilityProfile{
        QStringLiteral( "object_detection" ),
        QStringLiteral( "Object detection" ),
        QStringLiteral( "object_detection" ),
        true,
        300,
        0.9,
        {},
        {},
        2.0,
        {},
        true,
        {} } );

    profiles.append( SuitabilityProfile{
        QStringLiteral( "regression" ),
        QStringLiteral( "Regression" ),
        QStringLiteral( "regression" ),
        true,
        100,
        {},
        {},
        {},
        {},
        {},
        true,
        {} } );

    profiles.append( SuitabilityProfile{
        QStringLiteral( "temporal_prediction" ),
        QStringLiteral( "Temporal prediction" ),
        QStringLiteral( "temporal_prediction" ),
        false,
        {},
        {},
        4,
        {},
        {},
        {},
        true,
        {} } );

    profiles.append( SuitabilityProfile{
        QStringLiteral( "spectral_matching" ),
        QStringLiteral( "Spectral matching" ),
        QStringLiteral( "spectral_matching" ),
        false,
        {},
        {},
        {},
        {},
        {},
        {},
        true,
        {} } );

    profiles.append( SuitabilityProfile{
        QStringLiteral( "phenology" ),
        QStringLiteral( "Phenology (full-year temporal curve)" ),
        QStringLiteral( "temporal_prediction" ),
        false,
        {},
        {},
        6,
        {},
        {},
        /*requiredSeasons*/ { QStringLiteral( "spring" ), QStringLiteral( "summer" ),
                              QStringLiteral( "autumn" ), QStringLiteral( "winter" ) },
        true,
        {} } );

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
