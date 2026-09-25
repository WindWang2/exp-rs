#include "criteria_grid.h"

#include "../data/raster_grid_compat.h"

#include <QJsonObject>

#include <cmath>
#include <exception>
#include <stdexcept>

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

/// A non-finite affine term is broken metadata, not a grid difference:
/// compareGrids cannot mean anything on such input.
bool abnormalGrid( const sicnu::data::RasterGrid &grid )
{
    if ( !grid.hasGeoTransform )
        return false;
    for ( const double value : grid.geoTransform )
    {
        if ( !std::isfinite( value ) )
            return true;
    }
    return false;
}

} // namespace

SuitabilityCriterion assessGridCompatibility( const ResolvedRequirements &req,
                                              const QVector<SceneCandidate> &scenes )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "grid.compatibility" ) );

    int usableCount = 0;
    QVector<const sicnu::data::RasterGrid *> grids;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( !scene.usable() )
            continue;
        ++usableCount;
        if ( scene.grid.has_value() )
        {
            grids.append( &*scene.grid );
        }
    }
    criterion.evidence.insert( QStringLiteral( "usable_scene_count" ), usableCount );
    criterion.evidence.insert( QStringLiteral( "grid_scene_count" ), grids.size() );

    if ( grids.size() < 2 )
    {
        if ( usableCount >= 2 )
        {
            // Scenes exist to combine, so grid fit is a live question that
            // the available metadata cannot answer — Unknown, never N/A.
            criterion.level = SuitabilityLevel::Unknown;
            criterion.summary = QStringLiteral(
                "Multiple usable scenes but fewer than two grid snapshots; grid fit is unmeasured." );
            criterion.notes.append( QStringLiteral( "scenes lack grid snapshots" ) );
            criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
            criterion.evidence.insert( QStringLiteral( "reason" ),
                                       QStringLiteral( "grid_snapshots_missing" ) );
            return criterion;
        }
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "Fewer than two usable scenes; there is no grid combination to judge." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "not_applicable" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "single_scene_or_no_grid" ) );
        return criterion;
    }

    for ( const sicnu::data::RasterGrid *grid : grids )
    {
        if ( !abnormalGrid( *grid ) )
            continue;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "A scene carries a non-finite geotransform; grid comparison is unmeasured." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "grid_abnormal_input" ) );
        criterion.diagnostics.append( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.grid_abnormal_input" ),
            QStringLiteral( "non-finite geotransform term in a grid snapshot" ),
            sicnu::data::DiagnosticSeverity::Warning } );
        return criterion;
    }

    const qint64 sceneCount = grids.size();
    const qint64 totalPairs = sceneCount * ( sceneCount - 1 ) / 2;
    const bool sampled = totalPairs > kMaxGridPairs;
    const qint64 pairsChecked = sampled ? static_cast< qint64 >( kMaxGridPairs ) : totalPairs;

    QJsonObject verdictCounts;
    qint64 blockingPairs = 0;
    QString firstBlockingCode;

    // Equal strides over the linear pair index space keep the sample
    // deterministic and spread across all scenes; linear -> (i, j), i < j.
    // The stride is tail-inclusive: the last sampled index is always the
    // final pair, so a lone mismatched scene at the end of the list can
    // never fall entirely outside the sample (a plain floor stride leaves
    // the last ~totalPairs/pairsChecked indices unjudged).
    for ( qint64 checked = 0; checked < pairsChecked; ++checked )
    {
        const qint64 linear =
            sampled ? ( checked + 1 ) * totalPairs / pairsChecked - 1 : checked;
        qint64 pairI = 0;
        qint64 rest = linear;
        while ( rest >= sceneCount - pairI - 1 )
        {
            rest -= sceneCount - pairI - 1;
            ++pairI;
        }
        const qint64 pairJ = pairI + 1 + rest;

        sicnu::data::GridCompatReport report;
        try
        {
            report = sicnu::data::compareGrids( *grids.at( static_cast< int >( pairI ) ),
                                                *grids.at( static_cast< int >( pairJ ) ) );
        }
        catch ( const std::exception &error )
        {
            criterion.level = SuitabilityLevel::Unknown;
            criterion.summary = QStringLiteral(
                "Grid comparison failed on a scene pair; grid fit is unmeasured." );
            criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
            criterion.evidence.insert( QStringLiteral( "reason" ),
                                       QStringLiteral( "grid_compare_failed" ) );
            criterion.diagnostics.append( sicnu::data::Diagnostic{
                QStringLiteral( "suitability.grid_compare_failed" ),
                QStringLiteral( "compareGrids threw: %1" ).arg( error.what() ),
                sicnu::data::DiagnosticSeverity::Error } );
            return criterion;
        }
        catch ( ... )
        {
            criterion.level = SuitabilityLevel::Unknown;
            criterion.summary = QStringLiteral(
                "Grid comparison failed on a scene pair; grid fit is unmeasured." );
            criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
            criterion.evidence.insert( QStringLiteral( "reason" ),
                                       QStringLiteral( "grid_compare_failed" ) );
            criterion.diagnostics.append( sicnu::data::Diagnostic{
                QStringLiteral( "suitability.grid_compare_failed" ),
                QStringLiteral( "compareGrids threw an unknown exception" ),
                sicnu::data::DiagnosticSeverity::Error } );
            return criterion;
        }

        bool pairBlocking = false;
        for ( const sicnu::data::GridCompatIssue &issue : report.issues )
        {
            const QString code = issue.code.isEmpty() ? QStringLiteral( "grid.issue" )
                                                      : issue.code;
            verdictCounts.insert( code, verdictCounts.value( code ).toInt() + 1 );
            if ( issue.blocking )
            {
                pairBlocking = true;
                if ( firstBlockingCode.isEmpty() )
                    firstBlockingCode = code;
            }
        }
        if ( pairBlocking )
            ++blockingPairs;
    }

    criterion.evidence.insert( QStringLiteral( "pairs_total" ), totalPairs );
    criterion.evidence.insert( QStringLiteral( "pairs_checked" ), pairsChecked );
    criterion.evidence.insert( QStringLiteral( "blocking_pair_count" ), blockingPairs );
    criterion.evidence.insert( QStringLiteral( "verdict_counts" ), verdictCounts );
    if ( sampled )
    {
        criterion.notes.append( QStringLiteral( "sampled %1 of %2 pairs" )
                                    .arg( pairsChecked )
                                    .arg( totalPairs ) );
    }

    if ( blockingPairs == 0 )
    {
        if ( sampled )
        {
            // Same truncation posture as the label-sampling criterion: a
            // pass over a sampled space is Marginal, never Suitable — the
            // pairs outside the sample were never judged.
            criterion.level = SuitabilityLevel::Marginal;
            criterion.summary = QStringLiteral(
                                     "No blocking pixel-grid mismatch in the judged scene pairs; %1 of %2 pairs were sampled." )
                                     .arg( pairsChecked )
                                     .arg( totalPairs );
            return criterion;
        }
        criterion.level = SuitabilityLevel::Suitable;
        criterion.summary = QStringLiteral( "No blocking pixel-grid mismatch across the compared scene pairs." );
        return criterion;
    }

    criterion.level = req.gridStrict ? SuitabilityLevel::Unsuitable : SuitabilityLevel::Marginal;
    criterion.summary = req.gridStrict
                            ? QStringLiteral( "%1 scene pair(s) carry a blocking pixel-grid mismatch, forbidden by the strict profile." )
                                  .arg( blockingPairs )
                            : QStringLiteral( "%1 scene pair(s) carry a blocking pixel-grid mismatch." )
                                  .arg( blockingPairs );
    SuitabilityGap gap;
    gap.id = QStringLiteral( "grid.blocking_mismatch.%1" ).arg( firstBlockingCode );
    gap.criterionId = criterion.id;
    gap.description = QStringLiteral(
        "%1 of %2 compared pair(s) are blocked by '%3'." )
                          .arg( blockingPairs )
                          .arg( pairsChecked )
                          .arg( firstBlockingCode );
    gap.evidence.insert( QStringLiteral( "first_blocking_code" ), firstBlockingCode );
    gap.evidence.insert( QStringLiteral( "blocking_pair_count" ), blockingPairs );
    gap.evidence.insert( QStringLiteral( "pairs_checked" ), pairsChecked );
    gap.evidence.insert( QStringLiteral( "grid_strict" ), req.gridStrict );
    gap.evidence.insert( QStringLiteral( "verdict_counts" ), verdictCounts );
    criterion.gaps.append( gap );
    return criterion;
}

} // namespace sicnu::suitability
