// rs_segment_features.cpp — Phase 10B Task 10B.1
#include "rs_segment_features.h"
#include "../../processing/algorithms/math_utils.h"
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

    // Compute perimeter and bounding box per segment
    struct SegBBox
    {
        int minR = std::numeric_limits<int>::max();
        int maxR = std::numeric_limits<int>::lowest();
        int minC = std::numeric_limits<int>::max();
        int maxC = std::numeric_limits<int>::lowest();
    };
    QMap<quint32, SegBBox> bboxMap;
    QMap<quint32, int> perimeterMap;
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            const quint32 segId = labels[r * w + c];
            if ( segId == 0 )
                continue;

            SegBBox &box = bboxMap[segId];
            box.minR = std::min( box.minR, r );
            box.maxR = std::max( box.maxR, r );
            box.minC = std::min( box.minC, c );
            box.maxC = std::max( box.maxC, c );

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

        // Extended geometric shape descriptors
        stat.compactness = ( stat.area > 0 ) ? ( ( stat.perimeter * stat.perimeter ) / ( 4.0 * M_PI * stat.area ) ) : 0.0;
        const SegBBox &box = bboxMap[segId];
        const double bboxW = std::max( 1, box.maxC - box.minC + 1 );
        const double bboxH = std::max( 1, box.maxR - box.minR + 1 );
        stat.rectangularity = stat.area / ( bboxW * bboxH );
        stat.aspectRatio = std::max( bboxW, bboxH ) / std::min( bboxW, bboxH );

        stat.mean.resize( nBands );
        stat.stddev.resize( nBands );
        stat.min = acc.minVal;
        stat.max = acc.maxVal;
        stat.glcmContrast.resize( nBands, 0.0 );
        stat.glcmCorrelation.resize( nBands, 0.0 );
        stat.glcmEnergy.resize( nBands, 0.0 );
        stat.glcmHomogeneity.resize( nBands, 0.0 );

        for ( int b = 0; b < nBands; ++b )
        {
            MathUtils::AccumulatorStats accStats;
            accStats.count = acc.count;
            accStats.sum = acc.sum[b];
            accStats.sumSq = acc.sumSq[b];
            accStats.min = static_cast<float>(acc.minVal[b]);
            accStats.max = static_cast<float>(acc.maxVal[b]);
            MathUtils::Stats s = MathUtils::computeStatsFromAccumulators(accStats);
            stat.mean[b] = s.mean;
            stat.stddev[b] = s.stddev;

            // Simplified GLCM calculation (16 quantized levels)
            constexpr int nLevels = 16;
            const double minV = acc.minVal[b];
            const double maxV = acc.maxVal[b];
            const double rangeV = ( maxV > minV ) ? ( maxV - minV ) : 1.0;

            double glcm[nLevels][nLevels] = {};
            int pairCount = 0;

            for ( int r = box.minR; r <= box.maxR; ++r )
            {
                for ( int c = box.minC; c <= box.maxC; ++c )
                {
                    if ( labels[r * w + c] != segId )
                        continue;

                    const int level1 = std::clamp( static_cast<int>( ( bandData[b][r * w + c] - minV ) / rangeV * ( nLevels - 1 ) ), 0, nLevels - 1 );

                    const int drs[] = { 0, 1 };
                    const int dcs[] = { 1, 0 };
                    for ( int d = 0; d < 2; ++d )
                    {
                        const int nr = r + drs[d];
                        const int nc = c + dcs[d];
                        if ( nr <= box.maxR && nc <= box.maxC && labels[nr * w + nc] == segId )
                        {
                            const int level2 = std::clamp( static_cast<int>( ( bandData[b][nr * w + nc] - minV ) / rangeV * ( nLevels - 1 ) ), 0, nLevels - 1 );
                            glcm[level1][level2] += 1.0;
                            glcm[level2][level1] += 1.0;
                            pairCount += 2;
                        }
                    }
                }
            }

            if ( pairCount > 0 )
            {
                double contrast = 0.0;
                double energy = 0.0;
                double homogeneity = 0.0;
                double meanI = 0.0;
                for ( int i = 0; i < nLevels; ++i )
                {
                    for ( int j = 0; j < nLevels; ++j )
                    {
                        const double p = glcm[i][j] / pairCount;
                        if ( p <= 0 ) continue;
                        contrast += ( i - j ) * ( i - j ) * p;
                        energy += p * p;
                        homogeneity += p / ( 1.0 + std::abs( i - j ) );
                        meanI += i * p;
                    }
                }
                double varI = 0.0;
                for ( int i = 0; i < nLevels; ++i )
                {
                    for ( int j = 0; j < nLevels; ++j )
                    {
                        const double p = glcm[i][j] / pairCount;
                        varI += ( i - meanI ) * ( i - meanI ) * p;
                    }
                }
                double correlation = 0.0;
                if ( varI > 1e-6 )
                {
                    for ( int i = 0; i < nLevels; ++i )
                    {
                        for ( int j = 0; j < nLevels; ++j )
                        {
                            const double p = glcm[i][j] / pairCount;
                            correlation += ( i - meanI ) * ( j - meanI ) * p / varI;
                        }
                    }
                }

                stat.glcmContrast[b] = contrast;
                stat.glcmCorrelation[b] = correlation;
                stat.glcmEnergy[b] = energy;
                stat.glcmHomogeneity[b] = homogeneity;
            }
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
    // Features per segment: (mean + stddev + min + max + glcmContrast + glcmCorr + glcmEnergy + glcmHomogeneity) per band + 6 shape descriptors
    const int nFeatures = nBands * 8 + 6;

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
        for ( int b = 0; b < nBands; ++b )
            X.at<float>( row, col++ ) = static_cast<float>( s.glcmContrast.value(b, 0.0) );
        for ( int b = 0; b < nBands; ++b )
            X.at<float>( row, col++ ) = static_cast<float>( s.glcmCorrelation.value(b, 0.0) );
        for ( int b = 0; b < nBands; ++b )
            X.at<float>( row, col++ ) = static_cast<float>( s.glcmEnergy.value(b, 0.0) );
        for ( int b = 0; b < nBands; ++b )
            X.at<float>( row, col++ ) = static_cast<float>( s.glcmHomogeneity.value(b, 0.0) );
        X.at<float>( row, col++ ) = static_cast<float>( s.area );
        X.at<float>( row, col++ ) = static_cast<float>( s.perimeter );
        X.at<float>( row, col++ ) = static_cast<float>( s.shapeIndex );
        X.at<float>( row, col++ ) = static_cast<float>( s.compactness );
        X.at<float>( row, col++ ) = static_cast<float>( s.rectangularity );
        X.at<float>( row, col++ ) = static_cast<float>( s.aspectRatio );
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
