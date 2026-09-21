#include "criteria_spatial.h"

#include "../data/raster_grid_compat.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::suitability
{

namespace
{

constexpr int kMaxEvidenceSceneIds = 20;

/// Same-CRS test for coverage. An empty WKT never matches (not even two
/// empties): without a CRS the assessor cannot claim two things share one,
/// and it does not reproject.
bool crsCompatible( const QString &sceneWkt, const QString &aoiWkt )
{
    if ( sceneWkt.trimmed().isEmpty() || aoiWkt.trimmed().isEmpty() )
        return false;
    return sicnu::data::isSameCrs( sceneWkt, aoiWkt );
}

/// Union area of rectangles already guaranteed to lie inside the AOI.
/// Coordinate compression over y-strips + a sorted sweep over x-intervals:
/// O(n^2 log n) worst case, safe at the assessor's 1000-scene cap.
double clippedUnionArea( const QVector<SceneCandidate> &scenes,
                         const sicnu::data::SpatialExtent &aoi )
{
    struct ClippedRect
    {
        double minX = 0.0;
        double minY = 0.0;
        double maxX = 0.0;
        double maxY = 0.0;
    };

    QVector<ClippedRect> rects;
    rects.reserve( scenes.size() );
    for ( const SceneCandidate &scene : scenes )
    {
        const double minX = std::max( scene.extent.minimumX, aoi.minimumX );
        const double minY = std::max( scene.extent.minimumY, aoi.minimumY );
        const double maxX = std::min( scene.extent.maximumX, aoi.maximumX );
        const double maxY = std::min( scene.extent.maximumY, aoi.maximumY );
        if ( maxX <= minX || maxY <= minY )
            continue;
        rects.append( ClippedRect{ minX, minY, maxX, maxY } );
    }
    if ( rects.isEmpty() )
        return 0.0;

    QVector<double> ys;
    ys.reserve( rects.size() * 2 );
    for ( const ClippedRect &rect : rects )
    {
        ys.append( rect.minY );
        ys.append( rect.maxY );
    }
    std::sort( ys.begin(), ys.end() );
    ys.erase( std::unique( ys.begin(), ys.end() ),
              ys.end() );

    double unionArea = 0.0;
    for ( int i = 0; i + 1 < ys.size(); ++i )
    {
        const double y0 = ys.at( i );
        const double y1 = ys.at( i + 1 );
        QVector<QPair<double, double>> intervals;
        for ( const ClippedRect &rect : rects )
        {
            if ( rect.minY <= y0 && rect.maxY >= y1 )
                intervals.append( { rect.minX, rect.maxX } );
        }
        if ( intervals.isEmpty() )
            continue;
        std::sort( intervals.begin(), intervals.end() );
        double covered = 0.0;
        double sweepEnd = intervals.first().first;
        for ( const auto &interval : intervals )
        {
            if ( interval.first > sweepEnd )
            {
                covered += interval.second - interval.first;
                sweepEnd = interval.second;
            }
            else if ( interval.second > sweepEnd )
            {
                covered += interval.second - sweepEnd;
                sweepEnd = interval.second;
            }
        }
        unionArea += covered * ( y1 - y0 );
    }
    return unionArea;
}

SuitabilityCriterion makeCriterion( const QString &id )
{
    SuitabilityCriterion criterion;
    criterion.id = id;
    return criterion;
}

} // namespace

SuitabilityCriterion assessSpatialCoverage( const ResolvedRequirements &req,
                                            const QVector<SceneCandidate> &scenes )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "spatial.coverage" ) );

    if ( !req.hasAoi || !req.aoi.valid )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "Coverage cannot be measured without an AOI." );
        criterion.notes.append( QStringLiteral( "AOI not specified" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   req.hasAoi ? QStringLiteral( "aoi_extent_invalid" )
                                              : QStringLiteral( "aoi_not_specified" ) );
        return criterion;
    }

    const double aoiWidth = req.aoi.maximumX - req.aoi.minimumX;
    const double aoiHeight = req.aoi.maximumY - req.aoi.minimumY;
    // A degenerate (zero/negative/non-finite) AOI is a malformed goal, not a
    // data deficiency: refuse to grade it rather than divide by zero.
    if ( !std::isfinite( aoiWidth ) || !std::isfinite( aoiHeight ) || aoiWidth <= 0.0 || aoiHeight <= 0.0 )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "The AOI extent is degenerate; coverage is undefined." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ), QStringLiteral( "aoi_degenerate" ) );
        criterion.diagnostics.append( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.aoi_degenerate" ),
            QStringLiteral( "AOI extent has non-positive area" ),
            sicnu::data::DiagnosticSeverity::Warning } );
        return criterion;
    }

    // A usable coverage extent is declared AND representable: NaN/inf corners
    // are broken metadata that would poison the rectangle union (NaN poisons
    // the coordinate sort), so they fold into the unknown-extent count.
    const auto extentFinite = []( const sicnu::data::SpatialExtent &extent )
    {
        return std::isfinite( extent.minimumX ) && std::isfinite( extent.minimumY )
               && std::isfinite( extent.maximumX ) && std::isfinite( extent.maximumY );
    };

    int notReadyCount = 0;
    int extentUnknownCount = 0;
    QVector<SceneCandidate> coverageScenes;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( !scene.usable() )
        {
            ++notReadyCount;
            continue;
        }
        if ( !scene.extent.valid || !extentFinite( scene.extent ) )
        {
            ++extentUnknownCount;
            continue;
        }
        coverageScenes.append( scene );
    }

    if ( coverageScenes.isEmpty() )
    {
        // A goal with a place to cover and no usable geo-referenced data is
        // infeasible, not merely unknown.
        criterion.level = SuitabilityLevel::Unsuitable;
        criterion.summary = QStringLiteral( "No usable scene covers the assessment." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unsuitable" ) );
        criterion.evidence.insert( QStringLiteral( "scene_count" ), scenes.size() );
        criterion.evidence.insert( QStringLiteral( "not_ready_count" ), notReadyCount );
        criterion.evidence.insert( QStringLiteral( "extent_unknown_count" ), extentUnknownCount );
        SuitabilityGap gap;
        gap.id = QStringLiteral( "coverage.no_usable_scene" );
        gap.criterionId = criterion.id;
        gap.description = QStringLiteral(
            "No scene is both Ready and geo-referenced, so the AOI cannot be covered at all." );
        gap.evidence.insert( QStringLiteral( "usable_scene_count" ), 0 );
        gap.evidence.insert( QStringLiteral( "scene_count" ), scenes.size() );
        criterion.gaps.append( gap );
        return criterion;
    }

    int crsExcludedCount = 0;
    int crsExcludedOmitted = 0;
    QVector<SceneCandidate> sameCrsScenes;
    for ( const SceneCandidate &scene : coverageScenes )
    {
        if ( crsCompatible( scene.crsWkt, req.aoiCrsWkt ) )
        {
            sameCrsScenes.append( scene );
            continue;
        }
        if ( crsExcludedCount < kMaxEvidenceSceneIds )
        {
            criterion.notes.append( QStringLiteral(
                "CRS mismatch, scene '%1' excluded; assessor core does not reproject" )
                                        .arg( scene.id ) );
        }
        else
        {
            ++crsExcludedOmitted;
        }
        ++crsExcludedCount;
    }
    if ( crsExcludedCount > 0 && crsExcludedOmitted > 0 )
    {
        criterion.notes.append( QStringLiteral(
            "%1 further CRS-mismatched scenes excluded (ids omitted)" )
                                    .arg( crsExcludedOmitted ) );
    }

    criterion.evidence.insert( QStringLiteral( "scene_count" ), scenes.size() );
    criterion.evidence.insert( QStringLiteral( "not_ready_count" ), notReadyCount );
    criterion.evidence.insert( QStringLiteral( "extent_unknown_count" ), extentUnknownCount );
    criterion.evidence.insert( QStringLiteral( "crs_mismatch_excluded_count" ), crsExcludedCount );

    if ( sameCrsScenes.isEmpty() )
    {
        // Everything was excluded before anything was measured: honest answer
        // is "unknown", not "unsuitable".
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "Every usable scene is in a different CRS than the AOI; nothing was measured." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "all_scenes_excluded_crs_mismatch" ) );
        criterion.notes.append( QStringLiteral(
            "No coverage measured: all usable scenes excluded by CRS mismatch" ) );
        return criterion;
    }

    const double aoiArea = aoiWidth * aoiHeight;
    const double measuredFraction = clippedUnionArea( sameCrsScenes, req.aoi ) / aoiArea;
    // A ratio the double range cannot represent (area under-/overflowed to
    // 0/inf, e.g. a 1e-300 m AOI) is not a measurement: honest unknown instead
    // of a garbage verdict, and non-finite numbers never enter the evidence.
    if ( !std::isfinite( measuredFraction ) )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "The AOI area is too extreme to measure coverage against; coverage is undefined." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "aoi_area_not_representable" ) );
        criterion.diagnostics.append( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.aoi_area_not_representable" ),
            QStringLiteral( "AOI area or coverage ratio overflowed/underflowed the double range" ),
            sicnu::data::DiagnosticSeverity::Warning } );
        return criterion;
    }
    criterion.evidence.insert( QStringLiteral( "measured_fraction" ), measuredFraction );
    criterion.evidence.insert( QStringLiteral( "required_fraction" ),
                               req.minCoverageFractionResolved );
    criterion.evidence.insert( QStringLiteral( "usable_scene_count" ), sameCrsScenes.size() );
    criterion.evidence.insert(
        QStringLiteral( "aoi" ),
        QJsonObject{ { QStringLiteral( "min_x" ), req.aoi.minimumX },
                     { QStringLiteral( "min_y" ), req.aoi.minimumY },
                     { QStringLiteral( "max_x" ), req.aoi.maximumX },
                     { QStringLiteral( "max_y" ), req.aoi.maximumY } } );

    if ( measuredFraction >= req.minCoverageFractionResolved )
    {
        criterion.level = SuitabilityLevel::Suitable;
        criterion.summary = QStringLiteral( "Scene coverage meets the required fraction of the AOI." );
        return criterion;
    }

    criterion.level = measuredFraction > 0.0
                          && measuredFraction >= 0.5 * req.minCoverageFractionResolved
                          ? SuitabilityLevel::Marginal
                          : SuitabilityLevel::Unsuitable;
    criterion.summary = QStringLiteral( "Scene coverage is below the required fraction of the AOI." );
    SuitabilityGap gap;
    gap.id = QStringLiteral( "coverage.below_minimum" );
    gap.criterionId = criterion.id;
    gap.description = QStringLiteral(
        "Coverage %1 is below the required fraction %2." )
                          .arg( measuredFraction, 0, 'g', 4 )
                          .arg( req.minCoverageFractionResolved, 0, 'g', 4 );
    gap.evidence.insert( QStringLiteral( "measured_fraction" ), measuredFraction );
    gap.evidence.insert( QStringLiteral( "required_fraction" ), req.minCoverageFractionResolved );
    criterion.gaps.append( gap );
    return criterion;
}

SuitabilityCriterion assessResolution( const ResolvedRequirements &req,
                                       const QVector<SceneCandidate> &scenes )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "spatial.resolution" ) );

    if ( req.minGsdM <= 0.0 && req.maxGsdM <= 0.0 )
    {
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No GSD requirement; resolution is not assessed." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "not_applicable" ) );
        return criterion;
    }

    QVector<SceneCandidate> usableScenes;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( scene.usable() )
            usableScenes.append( scene );
    }
    if ( usableScenes.isEmpty() )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No usable scene to assess resolution against." );
        criterion.notes.append( QStringLiteral( "no usable scene" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        return criterion;
    }

    int unknownCount = 0;
    int invalidGsdCount = 0;
    QVector<const SceneCandidate *> knownScenes;
    for ( const SceneCandidate &scene : usableScenes )
    {
        if ( scene.gsdM.has_value() )
        {
            // Non-finite or non-positive GSD is broken metadata, not a range
            // verdict: NaN would silently satisfy every comparison below, so
            // such values count as unknown evidence with a diagnostic (same
            // posture as cloud cover outside [0, 100]).
            const double gsd = *scene.gsdM;
            if ( !std::isfinite( gsd ) || gsd <= 0.0 )
            {
                ++invalidGsdCount;
                ++unknownCount;
                continue;
            }
            knownScenes.append( &scene );
        }
        else
        {
            ++unknownCount;
        }
    }
    if ( invalidGsdCount > 0 )
    {
        criterion.diagnostics.append( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.gsd_invalid" ),
            QStringLiteral( "%1 scene(s) carry a non-finite or non-positive GSD; treated as unknown" )
                .arg( invalidGsdCount ),
            sicnu::data::DiagnosticSeverity::Warning } );
    }
    criterion.evidence.insert( QStringLiteral( "usable_scene_count" ), usableScenes.size() );
    criterion.evidence.insert( QStringLiteral( "gsd_unknown_count" ), unknownCount );
    criterion.evidence.insert( QStringLiteral( "gsd_invalid_count" ), invalidGsdCount );
    if ( req.minGsdM > 0.0 )
        criterion.evidence.insert( QStringLiteral( "required_min_gsd_m" ), req.minGsdM );
    if ( req.maxGsdM > 0.0 )
        criterion.evidence.insert( QStringLiteral( "required_max_gsd_m" ), req.maxGsdM );

    if ( knownScenes.isEmpty() )
    {
        // Nothing measurable: unknown evidence must not pose as a verdict.
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No scene carries a meter GSD; resolution is unmeasured." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ), QStringLiteral( "gsd_all_unknown" ) );
        return criterion;
    }

    auto inRange = [ &req ]( double gsd )
    {
        if ( req.minGsdM > 0.0 && gsd < req.minGsdM )
            return false;
        if ( req.maxGsdM > 0.0 && gsd > req.maxGsdM )
            return false;
        return true;
    };

    double measuredMin = std::numeric_limits<double>::infinity();
    double measuredMax = -std::numeric_limits<double>::infinity();
    int inRangeCount = 0;
    int outOfRangeCount = 0;
    int omittedIds = 0;
    QJsonArray outOfRangeIds;
    for ( const SceneCandidate *scene : knownScenes )
    {
        measuredMin = std::min( measuredMin, *scene->gsdM );
        measuredMax = std::max( measuredMax, *scene->gsdM );
        if ( inRange( *scene->gsdM ) )
        {
            ++inRangeCount;
            continue;
        }
        ++outOfRangeCount;
        if ( outOfRangeIds.size() < kMaxEvidenceSceneIds )
            outOfRangeIds.append( scene->id );
        else
            ++omittedIds;
    }

    criterion.evidence.insert( QStringLiteral( "measured_min_gsd_m" ), measuredMin );
    criterion.evidence.insert( QStringLiteral( "measured_max_gsd_m" ), measuredMax );
    criterion.evidence.insert( QStringLiteral( "in_range_count" ), inRangeCount );
    criterion.evidence.insert( QStringLiteral( "out_of_range_count" ), outOfRangeCount );

    if ( outOfRangeCount == 0 )
    {
        criterion.level = SuitabilityLevel::Suitable;
        criterion.summary = QStringLiteral( "Every measured scene GSD lies inside the required range." );
        return criterion;
    }

    criterion.level = inRangeCount > 0 ? SuitabilityLevel::Marginal : SuitabilityLevel::Unsuitable;
    criterion.summary = inRangeCount > 0
                            ? QStringLiteral( "Some scenes fall outside the required GSD range." )
                            : QStringLiteral( "Every measured scene falls outside the required GSD range." );
    SuitabilityGap gap;
    gap.id = QStringLiteral( "resolution.out_of_range" );
    gap.criterionId = criterion.id;
    gap.description = QStringLiteral( "%1 scene(s) fall outside the required GSD range." )
                          .arg( outOfRangeCount );
    gap.evidence.insert( QStringLiteral( "out_of_range_scene_ids" ), outOfRangeIds );
    if ( omittedIds > 0 )
        gap.evidence.insert( QStringLiteral( "omitted_count" ), omittedIds );
    gap.evidence.insert( QStringLiteral( "measured_min_gsd_m" ), measuredMin );
    gap.evidence.insert( QStringLiteral( "measured_max_gsd_m" ), measuredMax );
    if ( req.minGsdM > 0.0 )
        gap.evidence.insert( QStringLiteral( "required_min_gsd_m" ), req.minGsdM );
    if ( req.maxGsdM > 0.0 )
        gap.evidence.insert( QStringLiteral( "required_max_gsd_m" ), req.maxGsdM );
    criterion.gaps.append( gap );
    return criterion;
}

} // namespace sicnu::suitability
