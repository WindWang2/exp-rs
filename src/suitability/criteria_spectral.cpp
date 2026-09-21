#include "criteria_spectral.h"

#include <QJsonArray>

#include <algorithm>
#include <limits>

namespace sicnu::suitability
{

namespace
{

/// band_role.h vocabulary is lowercase with underscores; goal authors may
/// write "NIR"/"Red Edge" — normalize to the vocabulary form for matching
/// and for stable gap ids.
QString normalizedRole( const QString &role )
{
    return role.trimmed().toLower().replace( QLatin1Char( ' ' ), QLatin1Char( '_' ) );
}

SuitabilityCriterion makeCriterion( const QString &id )
{
    SuitabilityCriterion criterion;
    criterion.id = id;
    return criterion;
}

} // namespace

SuitabilityCriterion assessSpectralBands( const ResolvedRequirements &req,
                                          const QVector<SceneCandidate> &scenes,
                                          const std::optional<DatasetFacts> &facts )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "spectral.bands" ) );

    QStringList required;
    for ( const QString &role : req.requiredBandRoles )
    {
        const QString normalized = normalizedRole( role );
        if ( !normalized.isEmpty() && !required.contains( normalized ) )
            required.append( normalized );
    }
    if ( required.isEmpty() )
    {
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No band requirement; spectral fit is not assessed." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "not_applicable" ) );
        return criterion;
    }

    // Band pool: usable scenes and dataset facts are one fused union.
    QStringList pool;
    int scenePoolContributors = 0;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( !scene.usable() )
            continue;
        bool contributed = false;
        for ( const QString &role : scene.bandRoles )
        {
            const QString normalized = normalizedRole( role );
            if ( normalized.isEmpty() )
                continue;
            if ( !pool.contains( normalized ) )
                pool.append( normalized );
            contributed = true;
        }
        if ( contributed )
            ++scenePoolContributors;
    }
    const bool factsPoolPresent = facts.has_value() && !facts->bandRoles.isEmpty();
    if ( factsPoolPresent )
    {
        for ( const QString &role : facts->bandRoles )
        {
            const QString normalized = normalizedRole( role );
            if ( !normalized.isEmpty() && !pool.contains( normalized ) )
                pool.append( normalized );
        }
    }

    criterion.evidence.insert( QStringLiteral( "required_band_roles" ),
                               QJsonArray::fromStringList( required ) );
    std::sort( pool.begin(), pool.end() );
    criterion.evidence.insert( QStringLiteral( "available_roles" ),
                               QJsonArray::fromStringList( pool ) );
    criterion.evidence.insert( QStringLiteral( "scene_role_contributors" ), scenePoolContributors );
    criterion.evidence.insert( QStringLiteral( "facts_band_roles_used" ), factsPoolPresent );

    if ( pool.isEmpty() )
    {
        // No source carries band metadata: the requirement cannot be judged.
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No band role metadata is available to match against." );
        criterion.notes.append( QStringLiteral( "no band role metadata" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "band_metadata_absent" ) );
        return criterion;
    }

    QStringList matched;
    QStringList missing;
    for ( const QString &role : required )
    {
        if ( pool.contains( role ) )
            matched.append( role );
        else
            missing.append( role );
    }
    criterion.evidence.insert( QStringLiteral( "matched_roles" ),
                               QJsonArray::fromStringList( matched ) );
    criterion.evidence.insert( QStringLiteral( "missing_roles" ),
                               QJsonArray::fromStringList( missing ) );

    if ( missing.isEmpty() )
    {
        criterion.level = SuitabilityLevel::Suitable;
        criterion.summary = QStringLiteral( "Every required band role is available." );
        return criterion;
    }

    // Any missing required band makes the task infeasible — this is graded
    // Unsuitable even when most roles are present.
    criterion.level = SuitabilityLevel::Unsuitable;
    criterion.summary = QStringLiteral( "%1 required band role(s) are not available." )
                            .arg( missing.size() );
    for ( const QString &role : missing )
    {
        SuitabilityGap gap;
        gap.id = QStringLiteral( "band.missing.%1" ).arg( role );
        gap.criterionId = criterion.id;
        gap.description = QStringLiteral( "Required band role '%1' is not available in any usable scene or dataset facts." )
                              .arg( role );
        gap.evidence.insert( QStringLiteral( "required_role" ), role );
        criterion.gaps.append( gap );
    }
    return criterion;
}

SuitabilityCriterion assessCloudCover( const ResolvedRequirements &req,
                                       const QVector<SceneCandidate> &scenes )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "quality.cloud" ) );

    if ( req.maxCloudCoverPercent < 0.0 )
    {
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No cloud limit; cloud cover is not assessed." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "not_applicable" ) );
        return criterion;
    }

    QVector<SceneCandidate> usableScenes;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( scene.usable() )
            usableScenes.append( scene );
    }
    criterion.evidence.insert( QStringLiteral( "required_max_cloud_cover_percent" ),
                               req.maxCloudCoverPercent );
    criterion.evidence.insert( QStringLiteral( "usable_scene_count" ), usableScenes.size() );

    if ( usableScenes.isEmpty() )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No usable scene to assess cloud cover against." );
        criterion.notes.append( QStringLiteral( "no usable scene" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        return criterion;
    }

    // A value outside [0, 100] is broken metadata: counted as unknown with a
    // diagnostic — clamping it into range would fabricate evidence.
    int unknownCount = 0;
    int outOfRangeCount = 0;
    int knownCount = 0;
    int clearCount = 0;
    double worstCloud = -std::numeric_limits<double>::infinity();
    for ( const SceneCandidate &scene : usableScenes )
    {
        if ( !scene.cloudCoverPercent.has_value() )
        {
            ++unknownCount;
            continue;
        }
        const double cloud = *scene.cloudCoverPercent;
        if ( cloud < 0.0 || cloud > 100.0 )
        {
            ++unknownCount;
            ++outOfRangeCount;
            continue;
        }
        ++knownCount;
        worstCloud = std::max( worstCloud, cloud );
        if ( cloud <= req.maxCloudCoverPercent )
            ++clearCount;
    }
    if ( outOfRangeCount > 0 )
    {
        criterion.diagnostics.append( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.cloud_out_of_range" ),
            QStringLiteral( "%1 scene(s) carry cloud cover outside [0, 100]; treated as unknown" )
                .arg( outOfRangeCount ),
            sicnu::data::DiagnosticSeverity::Warning } );
    }

    criterion.evidence.insert( QStringLiteral( "known_count" ), knownCount );
    criterion.evidence.insert( QStringLiteral( "unknown_count" ), unknownCount );

    if ( knownCount == 0 )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No scene carries a usable cloud cover value." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "cloud_all_unknown" ) );
        return criterion;
    }

    criterion.evidence.insert( QStringLiteral( "clear_count" ), clearCount );
    criterion.evidence.insert( QStringLiteral( "worst_cloud_cover_percent" ), worstCloud );

    if ( clearCount == knownCount )
    {
        criterion.level = SuitabilityLevel::Suitable;
        criterion.summary = QStringLiteral( "Every scene is within the cloud cover limit." );
        return criterion;
    }

    criterion.level = clearCount > 0 ? SuitabilityLevel::Marginal : SuitabilityLevel::Unsuitable;
    criterion.summary = clearCount > 0
                            ? QStringLiteral( "Some scenes exceed the cloud cover limit." )
                            : QStringLiteral( "Every scene exceeds the cloud cover limit." );
    SuitabilityGap gap;
    gap.id = QStringLiteral( "cloud.cover_exceeded" );
    gap.criterionId = criterion.id;
    gap.description = QStringLiteral( "%1 of %2 measured scene(s) exceed the cloud cover limit %3." )
                          .arg( knownCount - clearCount )
                          .arg( knownCount )
                          .arg( req.maxCloudCoverPercent, 0, 'g', 4 );
    gap.evidence.insert( QStringLiteral( "required_max_cloud_cover_percent" ),
                         req.maxCloudCoverPercent );
    gap.evidence.insert( QStringLiteral( "known_count" ), knownCount );
    gap.evidence.insert( QStringLiteral( "clear_count" ), clearCount );
    gap.evidence.insert( QStringLiteral( "unknown_count" ), unknownCount );
    gap.evidence.insert( QStringLiteral( "worst_cloud_cover_percent" ), worstCloud );
    criterion.gaps.append( gap );
    return criterion;
}

} // namespace sicnu::suitability
