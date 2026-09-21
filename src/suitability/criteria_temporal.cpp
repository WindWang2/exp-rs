#include "criteria_temporal.h"

#include "seasonality.h"

#include <QJsonArray>

#include <algorithm>

namespace sicnu::suitability
{

namespace
{

SuitabilityCriterion makeCriterion( const QString &id )
{
    SuitabilityCriterion criterion;
    criterion.id = id;
    return criterion;
}

/// A usable time point: present AND parseable. An optional that carries an
/// invalid QDateTime is treated as unknown, never as "outside the window".
bool sceneTime( const SceneCandidate &scene, QDateTime *out )
{
    if ( !scene.acquisitionTimeUtc.has_value() || !scene.acquisitionTimeUtc->isValid() )
        return false;
    if ( out )
        *out = *scene.acquisitionTimeUtc;
    return true;
}

bool factsRange( const std::optional<DatasetFacts> &facts, QDateTime *start, QDateTime *end )
{
    if ( !facts.has_value() || !facts->hasTemporalExtent )
        return false;
    if ( !facts->temporalStartUtc.isValid() || !facts->temporalEndUtc.isValid() )
        return false;
    if ( start )
        *start = facts->temporalStartUtc;
    if ( end )
        *end = facts->temporalEndUtc;
    return true;
}

bool intersects( const QDateTime &aStart, const QDateTime &aEnd,
                 const QDateTime &bStart, const QDateTime &bEnd )
{
    return aStart <= bEnd && bStart <= aEnd;
}

QStringList normalizedSeasons( const QStringList &seasons )
{
    QStringList normalized;
    for ( const QString &season : seasons )
    {
        const QString key = season.trimmed().toLower();
        if ( !key.isEmpty() && !normalized.contains( key ) )
            normalized.append( key );
    }
    return normalized;
}

} // namespace

SuitabilityCriterion assessTemporalCoverage( const ResolvedRequirements &req,
                                             const QVector<SceneCandidate> &scenes,
                                             const std::optional<DatasetFacts> &facts )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "temporal.coverage" ) );

    if ( !req.hasTimeWindow )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No time window; temporal coverage is not graded." );
        criterion.notes.append( QStringLiteral( "no time window" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "time_window_not_specified" ) );
        return criterion;
    }

    criterion.evidence.insert(
        QStringLiteral( "window" ),
        QJsonObject{ { QStringLiteral( "start_utc" ), req.windowStartUtc.toString( Qt::ISODate ) },
                     { QStringLiteral( "end_utc" ), req.windowEndUtc.toString( Qt::ISODate ) } } );

    int usableCount = 0;
    int timeUnknownCount = 0;
    int inWindowCount = 0;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( !scene.usable() )
            continue;
        ++usableCount;
        QDateTime acquired;
        if ( !sceneTime( scene, &acquired ) )
        {
            ++timeUnknownCount;
            continue;
        }
        if ( acquired >= req.windowStartUtc && acquired <= req.windowEndUtc )
            ++inWindowCount;
    }

    QDateTime factsStart;
    QDateTime factsEnd;
    const bool hasFactsRange = factsRange( facts, &factsStart, &factsEnd );
    const bool factsIntersects =
        hasFactsRange && intersects( req.windowStartUtc, req.windowEndUtc, factsStart, factsEnd );

    criterion.evidence.insert( QStringLiteral( "usable_scene_count" ), usableCount );
    criterion.evidence.insert( QStringLiteral( "time_unknown_count" ), timeUnknownCount );
    criterion.evidence.insert( QStringLiteral( "in_window_count" ), inWindowCount );
    criterion.evidence.insert( QStringLiteral( "facts_temporal_intersect" ), factsIntersects );

    if ( usableCount > 0 && timeUnknownCount == usableCount && !hasFactsRange )
    {
        // Nothing carries a usable time: unmeasured, not failed.
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "No scene carries an acquisition time and facts carry no temporal extent." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "temporal_evidence_absent" ) );
        return criterion;
    }
    if ( usableCount == 0 && !hasFactsRange )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "No usable scene and no facts temporal extent to check against the window." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "temporal_evidence_absent" ) );
        return criterion;
    }

    if ( inWindowCount == 0 && !factsIntersects )
    {
        criterion.level = SuitabilityLevel::Unsuitable;
        criterion.summary = QStringLiteral( "No temporal evidence falls inside the window." );
        SuitabilityGap gap;
        gap.id = QStringLiteral( "temporal.outside_window" );
        gap.criterionId = criterion.id;
        gap.description = QStringLiteral(
            "No scene acquisition time or dataset temporal extent intersects the window %1 .. %2." )
                              .arg( req.windowStartUtc.toString( Qt::ISODate ),
                                    req.windowEndUtc.toString( Qt::ISODate ) );
        gap.evidence.insert( QStringLiteral( "in_window_count" ), inWindowCount );
        gap.evidence.insert( QStringLiteral( "facts_temporal_intersect" ), factsIntersects );
        criterion.gaps.append( gap );
        return criterion;
    }

    criterion.level = SuitabilityLevel::Suitable;
    criterion.summary = QStringLiteral( "Temporal evidence falls inside the window." );
    return criterion;
}

SuitabilityCriterion assessTemporalDensity( const ResolvedRequirements &req,
                                            const QVector<SceneCandidate> &scenes,
                                            const std::optional<DatasetFacts> &facts )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "temporal.density" ) );

    if ( req.minScenesInWindow <= 0 )
    {
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No density requirement; temporal density is not assessed." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "not_applicable" ) );
        return criterion;
    }

    if ( !req.hasTimeWindow )
    {
        // A count "in the window" is undefined without a window.
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No time window; in-window density cannot be counted." );
        criterion.notes.append( QStringLiteral( "no time window" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "time_window_not_specified" ) );
        return criterion;
    }

    criterion.evidence.insert( QStringLiteral( "required_min_scenes" ),
                               static_cast< qint64 >( req.minScenesInWindow ) );

    int sceneInWindow = 0;
    int timeUnknownCount = 0;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( !scene.usable() )
            continue;
        QDateTime acquired;
        if ( !sceneTime( scene, &acquired ) )
        {
            ++timeUnknownCount;
            continue;
        }
        if ( acquired >= req.windowStartUtc && acquired <= req.windowEndUtc )
            ++sceneInWindow;
    }

    QDateTime factsStart;
    QDateTime factsEnd;
    const bool factsContributed =
        factsRange( facts, &factsStart, &factsEnd )
        && intersects( req.windowStartUtc, req.windowEndUtc, factsStart, factsEnd );

    // The facts temporal extent is a range, not a scene list: if it
    // intersects the window it contributes ONE estimated temporal cluster.
    // The estimate is stated in the note and split out in the evidence —
    // never silently blended into a scene count.
    const int inWindow = sceneInWindow + ( factsContributed ? 1 : 0 );

    criterion.evidence.insert( QStringLiteral( "scene_in_window_count" ), sceneInWindow );
    criterion.evidence.insert( QStringLiteral( "facts_temporal_contributed" ), factsContributed );
    criterion.evidence.insert( QStringLiteral( "time_unknown_count" ), timeUnknownCount );
    criterion.evidence.insert( QStringLiteral( "in_window_count" ), static_cast< qint64 >( inWindow ) );
    if ( factsContributed )
    {
        criterion.notes.append( QStringLiteral(
            "in_window_count is estimated: scene times plus 1 for the facts temporal extent "
            "intersecting the window (an extent is not a scene count)" ) );
    }

    if ( inWindow < req.minScenesInWindow )
    {
        criterion.level = SuitabilityLevel::Unsuitable;
        criterion.summary = QStringLiteral( "%1 in-window evidence unit(s) is below the required %2." )
                                .arg( inWindow )
                                .arg( req.minScenesInWindow );
        SuitabilityGap gap;
        gap.id = QStringLiteral( "temporal.below_min_density" );
        gap.criterionId = criterion.id;
        gap.description = QStringLiteral( "Only %1 in-window evidence unit(s) against a minimum of %2." )
                              .arg( inWindow )
                              .arg( req.minScenesInWindow );
        gap.evidence.insert( QStringLiteral( "in_window_count" ), static_cast< qint64 >( inWindow ) );
        gap.evidence.insert( QStringLiteral( "required_min_scenes" ),
                             static_cast< qint64 >( req.minScenesInWindow ) );
        gap.evidence.insert( QStringLiteral( "at_minimum" ), false );
        criterion.gaps.append( gap );
        return criterion;
    }

    if ( inWindow == req.minScenesInWindow )
    {
        // Exactly on the line: acceptable but with zero headroom.
        criterion.level = SuitabilityLevel::Marginal;
        criterion.summary = QStringLiteral( "In-window count exactly meets the minimum." );
        SuitabilityGap gap;
        gap.id = QStringLiteral( "temporal.below_min_density" );
        gap.criterionId = criterion.id;
        gap.description = QStringLiteral( "In-window count sits exactly at the minimum %1." )
                              .arg( req.minScenesInWindow );
        gap.evidence.insert( QStringLiteral( "in_window_count" ), static_cast< qint64 >( inWindow ) );
        gap.evidence.insert( QStringLiteral( "required_min_scenes" ),
                             static_cast< qint64 >( req.minScenesInWindow ) );
        gap.evidence.insert( QStringLiteral( "at_minimum" ), true );
        criterion.gaps.append( gap );
        return criterion;
    }

    criterion.level = SuitabilityLevel::Suitable;
    criterion.summary = QStringLiteral( "In-window count exceeds the required minimum." );
    return criterion;
}

SuitabilityCriterion assessTemporalSeasonality( const ResolvedRequirements &req,
                                                const QVector<SceneCandidate> &scenes,
                                                const std::optional<DatasetFacts> &facts )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "temporal.seasonality" ) );

    const QStringList required = normalizedSeasons( req.requiredSeasons );
    if ( required.isEmpty() )
    {
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No season requirement; seasonality is not assessed." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "not_applicable" ) );
        return criterion;
    }

    criterion.evidence.insert( QStringLiteral( "required_seasons" ),
                               QJsonArray::fromStringList( required ) );

    // Season pool: scene months mapped to meteorological seasons (northern-
    // hemisphere assumption, declared in seasonality.h) plus facts season
    // distributions with a positive count.
    QSet<QString> pool;
    int usableCount = 0;
    int timeUnknownCount = 0;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( !scene.usable() )
            continue;
        ++usableCount;
        QDateTime acquired;
        if ( !sceneTime( scene, &acquired ) )
        {
            ++timeUnknownCount;
            continue;
        }
        const QString season = seasonForMonth( acquired.date().month() );
        if ( !season.isEmpty() )
            pool.insert( season );
    }

    bool factsSeasonsUsed = false;
    if ( facts.has_value() )
    {
        QList<QString> factSeasons = facts->samplesBySeason.keys();
        std::sort( factSeasons.begin(), factSeasons.end() );
        for ( const QString &season : factSeasons )
        {
            if ( facts->samplesBySeason.value( season ) > 0 )
            {
                const QString key = season.trimmed().toLower();
                if ( !key.isEmpty() )
                {
                    pool.insert( key );
                    factsSeasonsUsed = true;
                }
            }
        }
    }

    QStringList observed = QStringList( pool.cbegin(), pool.cend() );
    std::sort( observed.begin(), observed.end() );

    criterion.evidence.insert( QStringLiteral( "usable_scene_count" ), usableCount );
    criterion.evidence.insert( QStringLiteral( "time_unknown_count" ), timeUnknownCount );
    criterion.evidence.insert( QStringLiteral( "facts_seasons_used" ), factsSeasonsUsed );
    criterion.evidence.insert( QStringLiteral( "observed_seasons" ),
                               QJsonArray::fromStringList( observed ) );

    if ( pool.isEmpty() )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No scene time and no facts season distribution; "
                                            "observed seasons cannot be derived." );
        criterion.notes.append( QStringLiteral( "no season evidence" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "season_evidence_absent" ) );
        return criterion;
    }

    QStringList missing;
    for ( const QString &season : required )
    {
        if ( !pool.contains( season ) )
            missing.append( season );
    }

    if ( missing.isEmpty() )
    {
        criterion.level = SuitabilityLevel::Suitable;
        criterion.summary = QStringLiteral( "Every required season is observed." );
        return criterion;
    }

    criterion.level = SuitabilityLevel::Unsuitable;
    criterion.summary = QStringLiteral( "%1 required season(s) are not observed." ).arg( missing.size() );
    for ( const QString &season : missing )
    {
        SuitabilityGap gap;
        gap.id = QStringLiteral( "season.missing.%1" ).arg( season );
        gap.criterionId = criterion.id;
        gap.description = QStringLiteral(
            "Required season '%1' is not observed in any scene time or facts season distribution." )
                              .arg( season );
        gap.evidence.insert( QStringLiteral( "required_season" ), season );
        criterion.gaps.append( gap );
    }
    return criterion;
}

} // namespace sicnu::suitability
