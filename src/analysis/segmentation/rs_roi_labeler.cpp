// rs_roi_labeler.cpp — ADR 0060 (see header for design notes).
//
// Canonical ROI-majority labeling: each training polygon is rasterized to a
// pixel-index set with RsPixelRasterizer (center-of-pixel membership, the
// same rule the training-sample extraction module uses), every covered pixel
// votes for its segment, and the per-segment class decision goes through
// majorityKeyWithTieBreak (ties → smaller class id). This replaces the
// point-in-polygon helper in rs:obia_hierarchy and the ALL_TOUCHED mask loop
// in rs:obia_classify.
#include "rs_roi_labeler.h"

#include "rs_majority_vote.h"

#include "classification/rs_pixel_rasterizer.h"
#include "classification/rs_training_data_extraction.h"

#include "qgsogrutils.h"

#include <gdal.h>
#include <ogr_api.h>

#include <cmath>

QMap<quint32, int> RsRoiLabeler::labelByMajority( const RsSegmentMap &segMap,
                                                  const QString &rasterPath,
                                                  const QString &trainingPath,
                                                  const QString &classField,
                                                  int minLabelPixels,
                                                  QString *error,
                                                  const std::function<bool()> &isCanceled )
{
    QMap<quint32, int> result;
    auto fail = [error]( const QString &msg ) {
        if ( error )
            *error = msg;
        return QMap<quint32, int>();
    };

    if ( segMap.isEmpty() )
        return fail( QStringLiteral( "labelByMajority: empty segment map" ) );

    GDALAllRegister();
    OGRRegisterAll();

    GDALDatasetH vecDs = GDALOpenEx( trainingPath.toUtf8().constData(),
                                     GDAL_OF_VECTOR, nullptr, nullptr, nullptr );
    if ( !vecDs )
        return fail( QStringLiteral( "labelByMajority: cannot open training vector: %1" )
                         .arg( trainingPath ) );

    OGRLayerH layer = GDALDatasetGetLayer( vecDs, 0 );
    if ( !layer )
    {
        GDALClose( vecDs );
        return fail( QStringLiteral( "labelByMajority: training has no layers" ) );
    }

    // Shared fallback chain (classField → "class" → "id") owned by the
    // analysis classification layer (ADR 0019 S4 / ADR 0055).
    const int fieldIdx = RsTrainingDataExtraction::classFieldIndex(
        OGR_L_GetLayerDefn( layer ), classField );
    if ( fieldIdx < 0 )
    {
        GDALClose( vecDs );
        return fail( QStringLiteral( "labelByMajority: classField not found: %1" )
                         .arg( classField ) );
    }

    // Geotransform of the reference raster; the training polygons live in its
    // map space and the segment map must cover the same grid. Fail closed on
    // a missing raster or a degenerate transform (like RsSegmentMap::toGeoTIFF).
    double gt[6] = { 0, 1, 0, 0, 0, 1 };
    GDALDatasetH rds = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( !rds )
    {
        GDALClose( vecDs );
        return fail( QStringLiteral( "labelByMajority: cannot open reference raster: %1" )
                         .arg( rasterPath ) );
    }
    const bool gtOk = GDALGetGeoTransform( rds, gt ) == CE_None;
    GDALClose( rds );
    if ( !gtOk || std::abs( gt[1] ) < 1e-12 || std::abs( gt[5] ) < 1e-12 )
    {
        GDALClose( vecDs );
        return fail( QStringLiteral( "labelByMajority: degenerate geotransform in %1" )
                         .arg( rasterPath ) );
    }

    const int w = segMap.width();
    const int h = segMap.height();
    const auto &labels = segMap.labels();

    // segmentId → classId → covered pixel count.
    QHash<quint32, QHash<int, int>> votes;

    OGR_L_ResetReading( layer );
    OGRFeatureH feat = nullptr;
    while ( ( feat = OGR_L_GetNextFeature( layer ) ) != nullptr )
    {
        if ( isCanceled && isCanceled() )
        {
            OGR_F_Destroy( feat );
            GDALClose( vecDs );
            return fail( QStringLiteral( "canceled" ) );
        }
        const int classId = OGR_F_GetFieldAsInteger( feat, fieldIdx );
        OGRGeometryH geom = OGR_F_GetGeometryRef( feat );
        if ( classId <= 0 || !geom )
        {
            OGR_F_Destroy( feat );
            continue;
        }
        const QgsGeometry qg = QgsOgrUtils::ogrGeometryToQgsGeometry( geom );
        OGR_F_Destroy( feat );
        if ( qg.isNull() || qg.isEmpty() )
            continue;

        const QSet<quint64> px = RsPixelRasterizer::rasterize( qg, gt, w, h );
        for ( quint64 idx : px )
        {
            if ( idx >= static_cast<quint64>( labels.size() ) )
                continue;
            const quint32 sid = labels[static_cast<qsizetype>( idx )];
            if ( sid != 0 )
                ++votes[sid][classId];
        }
    }
    GDALClose( vecDs );

    for ( auto it = votes.constBegin(); it != votes.constEnd(); ++it )
    {
        const auto &segVotes = it.value();
        int total = 0;
        for ( auto cit = segVotes.constBegin(); cit != segVotes.constEnd(); ++cit )
            total += cit.value();
        if ( total < minLabelPixels )
            continue;
        const int bestClass = majorityKeyWithTieBreak( segVotes );
        if ( bestClass > 0 )
            result.insert( it.key(), bestClass );
    }
    return result;
}
