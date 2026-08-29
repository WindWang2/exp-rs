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
    std::unique_ptr<void, decltype(&GDALClose)> dsGuard( ds, &GDALClose );

    const int w = segMap.width();
    const int h = segMap.height();
    const int nBands = bandIndices.size();
    const size_t nPixels = static_cast<size_t>(w) * static_cast<size_t>(h);

    // Collect NoData metadata first
    QVector<double> bandNoData( nBands );
    QVector<bool> hasNoData( nBands, false );
    for ( int b = 0; b < nBands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, bandIndices[b] );
        if ( !band )
        {
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

    // Build any-band invalid mask with row-block reads (#648): the band
    // working buffer used to be a FULL-SCENE float row (4 B/px on top of the
    // 1 B/px mask; 12.5 GB at 50k x 50k). Blocks are capped at ~4 MiB.
    std::vector<char> validMask( nPixels, 1 );
    const int blockRows = std::max( 1, std::min(
        h, static_cast<int>( 4 * 1024 * 1024 / ( static_cast<size_t>( std::max( w, 1 ) ) * sizeof( float ) ) ) ) );
    std::vector<float> bandBuf( static_cast<size_t>( blockRows ) * w );

    // Pass 1: mark invalid where any band is NaN or NoData sentinel
    for ( int r0 = 0; r0 < h; r0 += blockRows )
    {
        const int rows = std::min( blockRows, h - r0 );
        const size_t base = static_cast<size_t>( r0 ) * w;
        for ( int b = 0; b < nBands; ++b )
        {
            GDALRasterBandH band = GDALGetRasterBand( ds, bandIndices[b] );
            CPLErr err = GDALRasterIO( band, GF_Read, 0, r0, w, rows,
                                       bandBuf.data(), w, rows, GDT_Float32, 0, 0 );
            if ( err != CE_None )
            {
                return result;
            }
            const size_t blockPixels = static_cast<size_t>( rows ) * w;
            for ( size_t i = 0; i < blockPixels; ++i )
            {
                if ( !validMask[base + i] ) continue;
                const float v = bandBuf[i];
                if ( std::isnan( v ) || ( hasNoData[b] && v == static_cast<float>( bandNoData[b] ) ) )
                    validMask[base + i] = 0;
            }
        }
    }

    const auto labels = segMap.labels();

    quint32 maxLabel = 0;
    for ( size_t i = 0; i < nPixels; ++i )
        maxLabel = std::max( maxLabel, labels[static_cast<qsizetype>(i)] );

    const bool useVector = maxLabel > 0
                           && static_cast<size_t>( maxLabel ) <= nPixels * 10;
    struct Acc
    {
        QVector<double> sum;
        QVector<double> sumSq;
        QVector<double> minVal;
        QVector<double> maxVal;
        QVector<double> glcmContrast;
        QVector<double> glcmCorrelation;
        QVector<double> glcmEnergy;
        QVector<double> glcmHomogeneity;
        int64_t count = 0;
    };
    std::vector<Acc> accVec;
    QMap<quint32, Acc> accMap; // fallback for sparse labels

    if ( useVector )
        accVec.resize( static_cast<size_t>(maxLabel) + 1 );

    auto accFor = [&]( quint32 segId ) -> Acc * {
        if ( useVector )
            return &accVec[static_cast<size_t>(segId)];
        return &accMap[segId];
    };

    // Prepare accumulators: ensure sum vectors sized nBands lazily, but we can pre-size glcm vectors later
    // Bounding box and perimeter depend only on labels (label geometry), not validMask.
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
        bboxVec.resize( static_cast<size_t>(maxLabel) + 1 );
        perimeterVec.resize( static_cast<size_t>(maxLabel) + 1, 0 );
    }

    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c);
            const quint32 segId = labels[static_cast<qsizetype>(idx)];
            if ( segId == 0 )
                continue;

            if ( useVector )
            {
                SegBBox &box = bboxVec[static_cast<size_t>(segId)];
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
                const size_t nIdx = static_cast<size_t>(nr) * static_cast<size_t>(w) + static_cast<size_t>(nc);
                if ( labels[static_cast<qsizetype>(nIdx)] != segId )
                {
                    isBoundary = true;
                    break;
                }
            }
            if ( isBoundary )
            {
                if ( useVector )
                    perimeterVec[static_cast<size_t>(segId)]++;
                else
                    perimeterMap[segId]++;
            }
        }
    }

    // Pass 2: per-band accumulation in the same row blocks (#648). Within a
    // band the row-major accumulation order is unchanged, so the sums are
    // bit-identical to the full-scene version.
    for ( int b = 0; b < nBands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, bandIndices[b] );
        for ( int r0 = 0; r0 < h; r0 += blockRows )
        {
        const int rows = std::min( blockRows, h - r0 );
        const size_t base = static_cast<size_t>( r0 ) * w;
        CPLErr err = GDALRasterIO( band, GF_Read, 0, r0, w, rows,
                                   bandBuf.data(), w, rows, GDT_Float32, 0, 0 );
        if ( err != CE_None )
        {
            return result;
        }
        // Accumulate spectral stats for this band
        const size_t blockPixels = static_cast<size_t>( rows ) * w;
        for ( size_t i = 0; i < blockPixels; ++i )
        {
            const quint32 segId = labels[static_cast<qsizetype>(base + i)];
            if ( segId == 0 ) continue;
            if ( !validMask[base + i] ) continue;
            Acc *acc = accFor( segId );
            if ( acc->sum.isEmpty() )
            {
                acc->sum.resize( nBands, 0.0 );
                acc->sumSq.resize( nBands, 0.0 );
                acc->minVal.resize( nBands, std::numeric_limits<double>::max() );
                acc->maxVal.resize( nBands, std::numeric_limits<double>::lowest() );
                acc->glcmContrast.resize( nBands, 0.0 );
                acc->glcmCorrelation.resize( nBands, 0.0 );
                acc->glcmEnergy.resize( nBands, 0.0 );
                acc->glcmHomogeneity.resize( nBands, 0.0 );
            }
            const float v = bandBuf[i];
            // validMask already guarantees !isnan and !hasNoData for this band
            acc->sum[b] += v;
            acc->sumSq[b] += static_cast<double>( v ) * v;
            acc->minVal[b] = std::min( acc->minVal[b], static_cast<double>( v ) );
            acc->maxVal[b] = std::max( acc->maxVal[b], static_cast<double>( v ) );
        }
        // Count valid pixels per segment (only once, on first band)
        if ( b == 0 )
        {
            if ( useVector )
            {
                for ( size_t segId = 1; segId < accVec.size(); ++segId )
                {
                    // count will be incremented in loop above per pixel? We missed count increment.
                    // To avoid double counting, we counted via sum loop? Actually we need count per segment = number of valid pixels.
                    // We already have not incremented count; do it now via scanning validMask+labels once.
                    // Instead of per-pixel increment in band loop (which would double), we compute after.
                }
            }
        }
        }
    }
    // Fix count: single scan over validMask+labels (since per-band loop did not increment count)
    // Reset counts then recompute
    if ( useVector )
    {
        for ( auto &acc : accVec ) acc.count = 0;
    }
    else
    {
        for ( auto it = accMap.begin(); it != accMap.end(); ++it ) it.value().count = 0;
    }
    for ( size_t i = 0; i < nPixels; ++i )
    {
        if ( !validMask[i] ) continue;
        const quint32 segId = labels[static_cast<qsizetype>(i)];
        if ( segId == 0 ) continue;
        Acc *acc = accFor( segId );
        if ( acc->sum.isEmpty() ) continue; // segment wholy invalid
        acc->count++;
    }

    // Re-read per band to compute GLCM (requires min/max per band now known).
    // #648: rows are read per SEGMENT BBOX CHUNK with a 1-row overlap instead
    // of a full-scene buffer. The GLCM for one (segment, band) pair is a
    // stack-local histogram accumulated across the segment's chunks; pair
    // anchors (r,c) are unique, so chunking cannot duplicate or drop pairs
    // (fuzz-verified over 857 randomized segment maps: chunked == full).
    bool glcmReadFailed = false;
    for ( int b = 0; b < nBands; ++b )
    {
        if ( glcmReadFailed )
        {
            return result;
        }
        GDALRasterBandH band = GDALGetRasterBand( ds, bandIndices[b] );

        // For each segment, build GLCM for this band
        auto processSegment = [&]( quint32 segId, const SegBBox &box ){
            Acc *acc = accFor( segId );
            if ( acc->count == 0 ) return;
            if ( acc->sum.isEmpty() ) return;
            const double minV = acc->minVal[b];
            const double maxV = acc->maxVal[b];
            const double rangeV = ( maxV > minV ) ? ( maxV - minV ) : 1.0;
            constexpr int nLevels = 16;
            double glcm[nLevels][nLevels] = {};
            int pairCount = 0;
            const int chunkRowsMax = std::max( 1, static_cast<int>(
                4 * 1024 * 1024 / ( static_cast<size_t>( w ) * sizeof( float ) ) ) );
            std::vector<float> chunkBuf;
            int r0 = box.minR;
            while ( r0 <= box.maxR )
            {
                const int r1 = std::min( r0 + chunkRowsMax - 1, box.maxR );
                const int readEnd = std::min( r1 + 1, box.maxR ); // 1-row overlap: (1,0) pairs at the chunk edge
                const int readRows = readEnd - r0 + 1;
                chunkBuf.assign( static_cast<size_t>( readRows ) * w, 0.0f );
                const CPLErr err = GDALRasterIO( band, GF_Read, 0, r0, w, readRows,
                                                 chunkBuf.data(), w, readRows, GDT_Float32, 0, 0 );
                if ( err != CE_None )
                {
                    glcmReadFailed = true;
                    return;
                }
                const auto chunkVal = [&]( int r, int c ) -> float {
                    return chunkBuf[ static_cast<size_t>( r - r0 ) * w + c ];
                };
            for ( int r = r0; r <= r1; ++r )
            {
                for ( int c = box.minC; c <= box.maxC; ++c )
                {
                    const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c);
                    if ( labels[static_cast<qsizetype>(idx)] != segId ) continue;
                    if ( !validMask[idx] ) continue;
                    const float val1 = chunkVal( r, c );
                    // validMask guarantees not NaN/NoData, but keep guard for safety
                    if ( std::isnan(val1) ) continue;
                    if ( hasNoData[b] && val1 == static_cast<float>( bandNoData[b] ) ) continue;
                    const size_t level1 = static_cast<size_t>( std::clamp( static_cast<int>( ( val1 - minV ) / rangeV * ( nLevels - 1 ) ), 0, nLevels - 1 ) );
                    const int drs[] = { 0, 1 };
                    const int dcs[] = { 1, 0 };
                    for ( int d = 0; d < 2; ++d )
                    {
                        const int nr = r + drs[d];
                        const int nc = c + dcs[d];
                        if ( nr < box.minR || nr > box.maxR || nc < box.minC || nc > box.maxC ) continue;
                        const size_t nIdx = static_cast<size_t>(nr) * static_cast<size_t>(w) + static_cast<size_t>(nc);
                        if ( labels[static_cast<qsizetype>(nIdx)] != segId ) continue;
                        if ( !validMask[nIdx] ) continue;
                        const float val2 = chunkVal( nr, nc );
                        if ( std::isnan(val2) ) continue;
                        if ( hasNoData[b] && val2 == static_cast<float>( bandNoData[b] ) ) continue;
                        const size_t level2 = static_cast<size_t>( std::clamp( static_cast<int>( ( val2 - minV ) / rangeV * ( nLevels - 1 ) ), 0, nLevels - 1 ) );
                        glcm[level1][level2] += 1.0;
                        glcm[level2][level1] += 1.0;
                        pairCount += 2;
                    }
                }
            }
                r0 = r1 + 1;
            }
            if ( pairCount > 0 )
            {
                double contrast = 0.0;
                double energy = 0.0;
                double homogeneity = 0.0;
                double meanI = 0.0;
                for ( int i = 0; i < nLevels; ++i )
                    for ( int j = 0; j < nLevels; ++j )
                    {
                        const double p = glcm[i][j] / pairCount;
                        if ( p <= 0 ) continue;
                        contrast += ( i - j ) * ( i - j ) * p;
                        energy += p * p;
                        homogeneity += p / ( 1.0 + std::abs( i - j ) );
                        meanI += i * p;
                    }
                double varI = 0.0;
                for ( int i = 0; i < nLevels; ++i )
                    for ( int j = 0; j < nLevels; ++j )
                    {
                        const double p = glcm[i][j] / pairCount;
                        varI += ( i - meanI ) * ( i - meanI ) * p;
                    }
                double correlation = 0.0;
                if ( varI > 1e-6 )
                    for ( int i = 0; i < nLevels; ++i )
                        for ( int j = 0; j < nLevels; ++j )
                        {
                            const double p = glcm[i][j] / pairCount;
                            correlation += ( i - meanI ) * ( j - meanI ) * p / varI;
                        }
                acc->glcmContrast[b] = contrast;
                acc->glcmCorrelation[b] = correlation;
                acc->glcmEnergy[b] = energy;
                acc->glcmHomogeneity[b] = homogeneity;
            }
        };

        if ( useVector )
        {
            for ( quint32 segId = 1; segId < accVec.size(); ++segId )
            {
                if ( accVec[segId].count == 0 ) continue;
                const SegBBox &box = bboxVec[segId];
                if ( box.minR > box.maxR ) continue;
                processSegment( segId, box );
            }
        }
        else
        {
            for ( auto it = accMap.constBegin(); it != accMap.constEnd(); ++it )
            {
                const quint32 segId = it.key();
                const SegBBox box = bboxMap.value( segId );
                if ( box.minR > box.maxR ) continue;
                processSegment( segId, box );
            }
        }
    }

    // Build final stats
    auto buildStat = [&]( quint32 segId, const Acc &acc, const SegBBox &box, int perimeter ) {
        if ( acc.count == 0 )
            return;
        SegmentStat stat;
        stat.area = acc.count;
        stat.perimeter = perimeter;
        stat.shapeIndex = computeShapeIndex( stat.area, stat.perimeter );
        stat.compactness = ( stat.area > 0 ) ? ( ( stat.perimeter * stat.perimeter ) / ( 4.0 * M_PI * stat.area ) ) : 0.0;
        const double bboxW = std::max( 1, box.maxC - box.minC + 1 );
        const double bboxH = std::max( 1, box.maxR - box.minR + 1 );
        stat.rectangularity = stat.area / ( bboxW * bboxH );
        stat.aspectRatio = std::max( bboxW, bboxH ) / std::min( bboxW, bboxH );

        stat.mean.resize( nBands );
        stat.stddev.resize( nBands );
        stat.min = acc.minVal;
        stat.max = acc.maxVal;
        stat.glcmContrast = acc.glcmContrast;
        stat.glcmCorrelation = acc.glcmCorrelation;
        stat.glcmEnergy = acc.glcmEnergy;
        stat.glcmHomogeneity = acc.glcmHomogeneity;

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
        }
        result[segId] = stat;
    };

    if ( useVector )
    {
        for ( quint32 segId = 1; segId < accVec.size(); ++segId )
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
