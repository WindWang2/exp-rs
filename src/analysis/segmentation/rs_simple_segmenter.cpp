// rs_simple_segmenter.cpp — Phase 10B Task 10B.3
#include "rs_simple_segmenter.h"
#include "sicnu_logging.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RsSegmentMap RsSimpleSegmenter::segment( const float *data, int width, int height,
                                          float nodata, const Params &params )
{
    if ( !data || width <= 0 || height <= 0 )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Segmentation, QString( "Invalid input: data=%1, width=%2, height=%3" )
            .arg( data ? "valid" : "null" ).arg( width ).arg( height ) );
        return {};
    }

    SICNU_LOG_INFO( SicnuLogTags::Segmentation, QString( "Starting segmentation: %1x%2, kernel=%3, bins=%4, minRegion=%5" )
        .arg( width ).arg( height ).arg( params.smoothKernel ).arg( params.quantizeBins ).arg( params.minRegionSize ) );

    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);

    // 1. Copy data for in-place smoothing
    QVector<float> smoothed( data, data + n );

    // 2. Gaussian smoothing
    gaussianSmooth( smoothed, width, height, params.smoothKernel );

    // 3. Quantize
    QVector<int> quantized = quantize( smoothed.data(), n, params.quantizeBins, nodata );

    // Find nodata bin value
    int nodataBin = -1;
    for ( size_t i = 0; i < n; ++i )
    {
        if ( data[i] == nodata || std::isnan( data[i] ) )
        {
            nodataBin = quantized[i];
            break;
        }
    }

    // 4. Connected components
    QVector<quint32> labels = connectedComponents( quantized, width, height, nodataBin );

    // 5. Merge small regions
    mergeSmallRegions( labels, width, height, params.minRegionSize );

    RsSegmentMap result( std::move( labels ), width, height );
    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation, QString( "Segmentation complete: %1 segments found" )
        .arg( result.segmentCount() ) );
    return result;
}

RsSegmentMap RsSimpleSegmenter::segmentMultiBand( const float *const *bandData,
                                                   int nBands, int width, int height,
                                                   float nodata, const Params &params )
{
    if ( !bandData || nBands <= 0 || width <= 0 || height <= 0 )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Segmentation, QString( "segmentMultiBand: invalid input (nBands=%1)" ).arg( nBands ) );
        return {};
    }

    SICNU_LOG_INFO( SicnuLogTags::Segmentation, QString( "Starting multi-band segmentation: %1 bands, %2x%3" )
        .arg( nBands ).arg( width ).arg( height ) );

    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Compute mean across bands
    QVector<float> meanBand( n, 0.0f );
    for ( size_t i = 0; i < n; ++i )
    {
        bool isNodata = false;
        float sum = 0;
        int count = 0;
        for ( int b = 0; b < nBands; ++b )
        {
            float v = bandData[b][i];
            if ( v == nodata || std::isnan( v ) )
            {
                isNodata = true;
                break;
            }
            sum += v;
            count++;
        }
        if ( isNodata || count == 0 )
            meanBand[i] = nodata;
        else
            meanBand[i] = sum / count;
    }

    return segment( meanBand.data(), width, height, nodata, params );
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void RsSimpleSegmenter::gaussianSmooth( QVector<float> &data, int w, int h, int kernelSize )
{
    if ( kernelSize < 3 )
        return;
    if ( kernelSize % 2 == 0 )
        kernelSize++;

    const int half = kernelSize / 2;

    // Build 1D Gaussian kernel
    QVector<float> kernel( kernelSize );
    float sum = 0;
    const float sigma = kernelSize / 6.0f; // kernel covers ±3σ
    for ( int i = 0; i < kernelSize; ++i )
    {
        float x = i - half;
        kernel[i] = std::exp( -x * x / ( 2 * sigma * sigma ) );
        sum += kernel[i];
    }
    for ( float &k : kernel )
        k /= sum;

    // Horizontal pass
    QVector<float> tmp( w * h );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            float val = 0;
            for ( int k = 0; k < kernelSize; ++k )
            {
                int cc = std::clamp( c + k - half, 0, w - 1 );
                val += data[r * w + cc] * kernel[k];
            }
            tmp[r * w + c] = val;
        }
    }

    // Vertical pass
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            float val = 0;
            for ( int k = 0; k < kernelSize; ++k )
            {
                int rr = std::clamp( r + k - half, 0, h - 1 );
                val += tmp[rr * w + c] * kernel[k];
            }
            data[r * w + c] = val;
        }
    }
}

QVector<int> RsSimpleSegmenter::quantize( const float *data, size_t n, int bins, float nodata )
{
    // Find min/max excluding nodata
    float vmin = std::numeric_limits<float>::max();
    float vmax = std::numeric_limits<float>::lowest();
    for ( size_t i = 0; i < n; ++i )
    {
        if ( data[i] == nodata || std::isnan( data[i] ) )
            continue;
        vmin = std::min( vmin, data[i] );
        vmax = std::max( vmax, data[i] );
    }

    if ( vmin >= vmax )
    {
        // All same value
        QVector<int> result( n, 0 );
        for ( size_t i = 0; i < n; ++i )
            result[i] = ( data[i] == nodata || std::isnan( data[i] ) ) ? 0 : 1;
        return result;
    }

    QVector<int> result( n );
    const float scale = ( bins - 1 ) / ( vmax - vmin );
    for ( size_t i = 0; i < n; ++i )
    {
        if ( data[i] == nodata || std::isnan( data[i] ) )
            result[i] = 0; // nodata bin
        else
            result[i] = 1 + static_cast<int>( ( data[i] - vmin ) * scale );
    }
    return result;
}

QVector<quint32> RsSimpleSegmenter::connectedComponents( const QVector<int> &quantized,
                                                          int w, int h, int nodataBin )
{
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    QVector<quint32> labels( n, 0 );
    quint32 nextLabel = 1;

    // 8-connectivity offsets
    const int dr[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    const int dc[] = { -1, 0, 1, -1, 1, -1, 0, 1 };

    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            const int idx = r * w + c;
            if ( quantized[idx] == nodataBin || quantized[idx] == 0 )
                continue;
            if ( labels[idx] != 0 )
                continue;

            // BFS flood fill
            const int targetBin = quantized[idx];
            std::queue<int> q;
            q.push( idx );
            labels[idx] = nextLabel;
            size_t segmentSize = 1;
            const size_t maxSegmentSize = 1000000; // Safeguard to prevent OOM on large homogeneous regions

            while ( !q.empty() && segmentSize < maxSegmentSize )
            {
                int cur = q.front();
                q.pop();
                int cr = cur / w;
                int cc = cur % w;

                for ( int d = 0; d < 8; ++d )
                {
                    int nr = cr + dr[d];
                    int nc = cc + dc[d];
                    if ( nr < 0 || nr >= h || nc < 0 || nc >= w )
                        continue;
                    int nidx = nr * w + nc;
                    if ( labels[nidx] != 0 )
                        continue;
                    if ( quantized[nidx] != targetBin )
                        continue;
                    labels[nidx] = nextLabel;
                    q.push( nidx );
                    segmentSize++;
                    if ( segmentSize >= maxSegmentSize )
                        break;
                }
            }
            nextLabel++;
        }
    }

    return labels;
}

void RsSimpleSegmenter::mergeSmallRegions( QVector<quint32> &labels, int w, int h,
                                            int minSize )
{
    // Count region sizes
    std::unordered_map<quint32, int> sizeMap;
    for ( quint32 l : labels )
    {
        if ( l != 0 )
            sizeMap[l]++;
    }

    // Find small regions
    std::unordered_set<quint32> smallRegions;
    for ( auto &[id, sz] : sizeMap )
    {
        if ( sz < minSize )
            smallRegions.insert( id );
    }

    if ( smallRegions.empty() )
        return;

    SICNU_LOG_DEBUG( SicnuLogTags::Segmentation, QString( "Merging %1 small regions (minSize=%2)" )
        .arg( smallRegions.size() ).arg( minSize ) );

    // HIGH #7 fix: Build adjacency in one image scan, then apply merges in one pass.
    // O(W*H + K*B) where B = boundary pixels per small region.
    const int dr4[] = { -1, 1, 0, 0 };
    const int dc4[] = { 0, 0, -1, 1 };

    // Pass 1: For each small region pixel, count its non-small neighbors
    // regionId -> { neighborId -> boundaryCount }
    std::unordered_map<quint32, std::unordered_map<quint32, int>> adjacency;
    for ( size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h); ++i )
    {
        quint32 segId = labels[i];
        if ( segId == 0 || smallRegions.find( segId ) == smallRegions.end() )
            continue;
        int r = i / w;
        int c = i % w;
        for ( int d = 0; d < 4; ++d )
        {
            int nr = r + dr4[d];
            int nc = c + dc4[d];
            if ( nr < 0 || nr >= h || nc < 0 || nc >= w )
                continue;
            quint32 neighbor = labels[nr * w + nc];
            if ( neighbor != 0 && neighbor != segId )
                adjacency[segId][neighbor]++;
        }
    }

    // Build merge map from adjacency
    std::unordered_map<quint32, quint32> mergeMap;
    for ( auto &[segId, neighbors] : adjacency )
    {
        quint32 bestNeighbor = 0;
        int bestCount = 0;
        for ( auto &[nid, cnt] : neighbors )
        {
            if ( cnt > bestCount )
            {
                bestCount = cnt;
                bestNeighbor = nid;
            }
        }
        if ( bestNeighbor != 0 )
            mergeMap[segId] = bestNeighbor;
    }

    // Apply all merges in a single pass over the full image
    for ( size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h); ++i )
    {
        auto it = mergeMap.find( labels[i] );
        if ( it != mergeMap.end() )
            labels[i] = it->second;
    }
}
