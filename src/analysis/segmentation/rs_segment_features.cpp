// rs_segment_features.cpp — Phase 10B Task 10B.1
#include "rs_segment_features.h"
#include "../../processing/algorithms/math_utils.h"
#include "sicnu_logging.h"

#include <gdal.h>
#include <cpl_error.h>

#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>

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

    // Collect per-segment accumulators.
    // Labels from connectedComponents are contiguous starting at 1, so we can
    // index by label directly with a vector (O(1) per pixel) instead of QMap
    // (O(log N) red-black-tree lookup + node allocation per pixel). If the
    // max label is pathologically large relative to the pixel count (sparse /
    // corrupt labels), fall back to the map to avoid a huge allocation.
    struct Acc
    {
        QVector<double> sum;
        QVector<double> sumSq;
        QVector<double> minVal;
        QVector<double> maxVal;
        int count = 0;
    };

    const auto labels = segMap.labels();

    quint32 maxLabel = 0;
    for ( int i = 0; i < nPixels; ++i )
        maxLabel = std::max( maxLabel, labels[i] );

    const bool useVector = maxLabel > 0
                           && static_cast<size_t>( maxLabel ) <= nPixels * 10;
    std::vector<Acc> accVec;
    QMap<quint32, Acc> accMap; // fallback for sparse labels

    if ( useVector )
        accVec.resize( maxLabel + 1 );

    auto accFor = [&]( quint32 segId ) -> Acc * {
        if ( useVector )
            return &accVec[segId];
        return &accMap[segId];
    };

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

        Acc &acc = *accFor( segId );
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

    // Compute perimeter and bounding box per segment (same vector-vs-map split).
    struct SegBBox
    {
        int minR = std::numeric_limits<int>::max();
        int maxR = std::numeric_limits<int>::lowest();
        int minC = std::numeric_limits<int>::max();
        int maxC = std::numeric_limits<int>::lowest();
    };
    std::vector<SegBBox> bboxVec;
    QMap<quint32, SegBBox> bboxMap;
    std::vector<int> perimeterVec;
    QMap<quint32, int> perimeterMap;

    if ( useVector )
    {
        bboxVec.resize( maxLabel + 1 );
        perimeterVec.resize( maxLabel + 1, 0 );
    }

    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            const quint32 segId = labels[r * w + c];
            if ( segId == 0 )
                continue;

            if ( useVector )
            {
                SegBBox &box = bboxVec[segId];
                box.minR = std::min( box.minR, r );
                box.maxR = std::max( box.maxR, r );
                box.minC = std::min( box.minC, c );
                box.maxC = std::max( box.maxC, c );
            }
            else
            {
                SegBBox &box = bboxMap[segId];
                box.minR = std::min( box.minR, r );
                box.maxR = std::max( box.maxR, r );
                box.minC = std::min( box.minC, c );
                box.maxC = std::max( box.maxC, c );
            }

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
            {
                if ( useVector )
                    perimeterVec[segId]++;
                else
                    perimeterMap[segId]++;
            }
        }
    }

    // Build final stats: iterate labels 1..maxLabel (vector) or QMap keys (map).
    auto buildStat = [&]( quint32 segId, const Acc &acc, const SegBBox &box, int perimeter ) {
        if ( acc.count == 0 )
            return;

        SegmentStat stat;
        stat.area = acc.count;
        stat.perimeter = perimeter;
        stat.shapeIndex = computeShapeIndex( stat.area, stat.perimeter );

        // Extended geometric shape descriptors
        stat.compactness = ( stat.area > 0 ) ? ( ( stat.perimeter * stat.perimeter ) / ( 4.0 * M_PI * stat.area ) ) : 0.0;
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

                    const float val1 = bandData[b][r * w + c];
                    if ( std::isnan( val1 ) )
                        continue;

                    const size_t level1 = static_cast<size_t>( std::clamp( static_cast<int>( ( val1 - minV ) / rangeV * ( nLevels - 1 ) ), 0, nLevels - 1 ) );

                    const int drs[] = { 0, 1 };
                    const int dcs[] = { 1, 0 };
                    for ( int d = 0; d < 2; ++d )
                    {
                        const int nr = r + drs[d];
                        const int nc = c + dcs[d];
                        if ( nr <= box.maxR && nc <= box.maxC && labels[nr * w + nc] == segId )
                        {
                            const float val2 = bandData[b][nr * w + nc];
                            if ( std::isnan( val2 ) )
                                continue;
                            const size_t level2 = static_cast<size_t>( std::clamp( static_cast<int>( ( val2 - minV ) / rangeV * ( nLevels - 1 ) ), 0, nLevels - 1 ) );
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
    };

    if ( useVector )
    {
        for ( quint32 segId = 1; segId <= maxLabel; ++segId )
        {
            if ( accVec[segId].count > 0 )
                buildStat( segId, accVec[segId], bboxVec[segId], perimeterVec[segId] );
        }
    }
    else
    {
        for ( auto it = accMap.constBegin(); it != accMap.constEnd(); ++it )
        {
            const quint32 segId = it.key();
            buildStat( segId, it.value(), bboxMap.value( segId ), perimeterMap.value( segId, 0 ) );
        }
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation, QString( "Feature extraction complete: %1 segment stats computed" ).arg( result.size() ) );
    return result;
}

#ifdef SICNU_HAS_OPENCV
cv::Mat RsSegmentFeatures::toFeatureMatrix(
    const QMap<quint32, SegmentStat> &stats,
    QVector<quint32> &segmentIds,
    const RsFeatureSelection &selection )
{
    if ( stats.isEmpty() )
        return cv::Mat();

    auto first = stats.constBegin();
    const int nBands = first->mean.size();
    const int nFeatures = selection.activeFeatureCount( nBands );
    if ( nFeatures <= 0 )
        return cv::Mat();

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

        if ( selection.useMean )
            for ( int b = 0; b < nBands; ++b ) X.at<float>( row, col++ ) = static_cast<float>( s.mean[b] );
        if ( selection.useStdDev )
            for ( int b = 0; b < nBands; ++b ) X.at<float>( row, col++ ) = static_cast<float>( s.stddev[b] );
        if ( selection.useMin )
            for ( int b = 0; b < nBands; ++b ) X.at<float>( row, col++ ) = static_cast<float>( s.min[b] );
        if ( selection.useMax )
            for ( int b = 0; b < nBands; ++b ) X.at<float>( row, col++ ) = static_cast<float>( s.max[b] );

        if ( selection.useGlcmContrast )
            for ( int b = 0; b < nBands; ++b ) X.at<float>( row, col++ ) = static_cast<float>( s.glcmContrast.value( b, 0.0 ) );
        if ( selection.useGlcmCorrelation )
            for ( int b = 0; b < nBands; ++b ) X.at<float>( row, col++ ) = static_cast<float>( s.glcmCorrelation.value( b, 0.0 ) );
        if ( selection.useGlcmEnergy )
            for ( int b = 0; b < nBands; ++b ) X.at<float>( row, col++ ) = static_cast<float>( s.glcmEnergy.value( b, 0.0 ) );
        if ( selection.useGlcmHomogeneity )
            for ( int b = 0; b < nBands; ++b ) X.at<float>( row, col++ ) = static_cast<float>( s.glcmHomogeneity.value( b, 0.0 ) );

        if ( selection.useArea ) X.at<float>( row, col++ ) = static_cast<float>( s.area );
        if ( selection.usePerimeter ) X.at<float>( row, col++ ) = static_cast<float>( s.perimeter );
        if ( selection.useShapeIndex ) X.at<float>( row, col++ ) = static_cast<float>( s.shapeIndex );
        if ( selection.useCompactness ) X.at<float>( row, col++ ) = static_cast<float>( s.compactness );
        if ( selection.useRectangularity ) X.at<float>( row, col++ ) = static_cast<float>( s.rectangularity );
        if ( selection.useAspectRatio ) X.at<float>( row, col++ ) = static_cast<float>( s.aspectRatio );
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
