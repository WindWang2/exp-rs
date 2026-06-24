// rs_segment_features.cpp — Phase 10B Task 10B.1
#include "rs_segment_features.h"
#include "sicnu_logging.h"

#include <gdal.h>
#include <cpl_error.h>

#include <cmath>
#include <algorithm>
#include <numeric>

QMap<quint32, RsSegmentFeatures::SegmentStat>
RsSegmentFeatures::extract( const QString &rasterPath,
                            const RsSegmentMap &segMap,
                            const QVector<int> &bandIndices )
{
    QMap<quint32, SegmentStat> result;
    if ( segMap.isEmpty() || bandIndices.isEmpty() )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Segmentation, "Feature extraction: empty segment map or band indices" );
        return result;
    }

    SICNU_LOG_INFO( SicnuLogTags::Segmentation, QString( "Extracting features: %1 bands, %2 segments" )
        .arg( bandIndices.size() ).arg( segMap.segmentCount() ) );

    // Open raster
    GDALDatasetH ds = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Segmentation, QString( "Failed to open raster for feature extraction: %1" ).arg( rasterPath ) );
        return result;
    }

    const int w = segMap.width();
    const int h = segMap.height();
    const int nBands = bandIndices.size();
    const size_t nPixels = static_cast<size_t>(w) * static_cast<size_t>(h);

    // Read all requested bands into contiguous buffers
    QVector<QVector<float>> bandData( nBands );
    QVector<double> bandNoData( nBands );
    QVector<bool> hasNoData( nBands, false );
    for ( int b = 0; b < nBands; ++b )
    {
        bandData[b].resize( nPixels );
        GDALRasterBandH band = GDALGetRasterBand( ds, bandIndices[b] );
        if ( !band )
        {
            GDALClose( ds );
            return result;
        }
        CPLErr err = GDALRasterIO( band, GF_Read, 0, 0, w, h,
                                   bandData[b].data(), w, h, GDT_Float32, 0, 0 );
        if ( err != CE_None )
        {
            GDALClose( ds );
            return result;
        }
        int pbSuccess = 0;
        double noDataValue = GDALGetRasterNoDataValue( band, &pbSuccess );
        if ( pbSuccess )
        {
            bandNoData[b] = noDataValue;
            hasNoData[b] = true;
        }
    }
    GDALClose( ds );

    // Collect per-segment accumulators
    // We use a temporary map to accumulate sums
    struct Acc
    {
        QVector<double> sum;
        QVector<double> sumSq;
        QVector<double> minVal;
        QVector<double> maxVal;
        int count = 0;
    };

    QMap<quint32, Acc> accMap;
    const auto labels = segMap.labels();

    for ( int i = 0; i < nPixels; ++i )
    {
        const quint32 segId = labels[i];
        if ( segId == 0 )
            continue; // skip nodata

        // Check if any band has NoData or NaN at this pixel
        bool isPixelNodata = false;
        for ( int b = 0; b < nBands; ++b )
        {
            const float v = bandData[b][i];
            if ( std::isnan( v ) || ( hasNoData[b] && static_cast<double>( v ) == bandNoData[b] ) )
            {
                isPixelNodata = true;
                break;
            }
        }
        if ( isPixelNodata )
            continue;

        Acc &acc = accMap[segId];
        if ( acc.sum.isEmpty() )
        {
            acc.sum.resize( nBands, 0.0 );
            acc.sumSq.resize( nBands, 0.0 );
            acc.minVal.resize( nBands, std::numeric_limits<double>::max() );
            acc.maxVal.resize( nBands, std::numeric_limits<double>::lowest() );
        }

        for ( int b = 0; b < nBands; ++b )
        {
            const float v = bandData[b][i];
            acc.sum[b] += v;
            acc.sumSq[b] += static_cast<double>( v ) * v;
            acc.minVal[b] = std::min( acc.minVal[b], static_cast<double>( v ) );
            acc.maxVal[b] = std::max( acc.maxVal[b], static_cast<double>( v ) );
        }
        acc.count++;
    }

    // Compute perimeter: count boundary pixels (pixels where at least one
    // 4-neighbour has a different label or is out of bounds).
    QMap<quint32, int> perimeterMap;
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            const quint32 segId = labels[r * w + c];
            if ( segId == 0 )
                continue;

            bool isBoundary = false;
            const int dr[] = { -1, 1, 0, 0 };
            const int dc[] = { 0, 0, -1, 1 };
            for ( int d = 0; d < 4; ++d )
            {
                const int nr = r + dr[d];
                const int nc = c + dc[d];
                if ( nr < 0 || nr >= h || nc < 0 || nc >= w )
                {
                    isBoundary = true;
                    break;
                }
                if ( labels[nr * w + nc] != segId )
                {
                    isBoundary = true;
                    break;
                }
            }
            if ( isBoundary )
                perimeterMap[segId]++;
        }
    }

    // Build final stats
    for ( auto it = accMap.constBegin(); it != accMap.constEnd(); ++it )
    {
        const quint32 segId = it.key();
        const Acc &acc = it.value();
        if ( acc.count == 0 )
            continue;

        SegmentStat stat;
        stat.area = acc.count;
        stat.perimeter = perimeterMap.value( segId, 0 );
        stat.shapeIndex = computeShapeIndex( stat.area, stat.perimeter );
        stat.mean.resize( nBands );
        stat.stddev.resize( nBands );
        stat.min = acc.minVal;
        stat.max = acc.maxVal;

        for ( int b = 0; b < nBands; ++b )
        {
            stat.mean[b] = acc.sum[b] / acc.count;
            const double variance = acc.sumSq[b] / acc.count
                                    - stat.mean[b] * stat.mean[b];
            stat.stddev[b] = std::sqrt( std::max( 0.0, variance ) );
        }

        result[segId] = stat;
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation, QString( "Feature extraction complete: %1 segment stats computed" ).arg( result.size() ) );
    return result;
}

#ifdef SICNU_HAS_OPENCV
cv::Mat RsSegmentFeatures::toFeatureMatrix(
    const QMap<quint32, SegmentStat> &stats,
    QVector<quint32> &segmentIds )
{
    if ( stats.isEmpty() )
        return cv::Mat();

    // Determine feature count from first entry
    auto first = stats.constBegin();
    const int nBands = first->mean.size();
    // Features per segment: mean + stddev + min + max per band + area + perimeter + shapeIndex
    const int nFeatures = nBands * 4 + 3;

    const int nSegments = stats.size();
    cv::Mat X( nSegments, nFeatures, CV_32F );

    segmentIds.clear();
    segmentIds.reserve( nSegments );

    int row = 0;
    for ( auto it = stats.constBegin(); it != stats.constEnd(); ++it, ++row )
    {
        segmentIds.append( it.key() );
        const SegmentStat &s = it.value();
        int col = 0;
        for ( int b = 0; b < nBands; ++b )
            X.at<float>( row, col++ ) = static_cast<float>( s.mean[b] );
        for ( int b = 0; b < nBands; ++b )
            X.at<float>( row, col++ ) = static_cast<float>( s.stddev[b] );
        for ( int b = 0; b < nBands; ++b )
            X.at<float>( row, col++ ) = static_cast<float>( s.min[b] );
        for ( int b = 0; b < nBands; ++b )
            X.at<float>( row, col++ ) = static_cast<float>( s.max[b] );
        X.at<float>( row, col++ ) = static_cast<float>( s.area );
        X.at<float>( row, col++ ) = static_cast<float>( s.perimeter );
        X.at<float>( row, col++ ) = static_cast<float>( s.shapeIndex );
    }

    return X;
}
#endif

double RsSegmentFeatures::computeShapeIndex( double area, double perimeter )
{
    if ( area <= 0 )
        return 0;
    return perimeter / ( 4.0 * std::sqrt( area ) );
}
