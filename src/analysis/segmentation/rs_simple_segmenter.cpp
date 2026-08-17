// rs_simple_segmenter.cpp — Phase 10B Task 10B.3
#include "rs_simple_segmenter.h"
#include "sicnu_logging.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RsSegmentMap RsSimpleSegmenter::segment( const float *data, int width, int height,
                                          float nodata, const Params &params,
                                          const std::function<bool()> &isCanceled,
                                          const std::function<void(float)> &onProgress )
{
    if ( !data || width <= 0 || height <= 0 )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Segmentation, QString( "Invalid input: data=%1, width=%2, height=%3" )
            .arg( data ? "valid" : "null" ).arg( width ).arg( height ) );
        return {};
    }

    SICNU_LOG_INFO( SicnuLogTags::Segmentation, QString( "Starting segmentation: %1x%2, kernel=%3, bins=%4, minRegion=%5" )
        .arg( width ).arg( height ).arg( params.smoothKernel ).arg( params.quantizeBins ).arg( params.minRegionSize ) );

    auto canceled = [&isCanceled]() { return isCanceled && isCanceled(); };
    auto report = [&onProgress]( float f ) {
        if ( onProgress )
            onProgress( f );
    };

    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);

    if ( canceled() )
        return {};
    report( 0.0f );

    // 1. Copy data for in-place smoothing
    QVector<float> smoothed( data, data + n );

    // 2. Gaussian smoothing (line-buffered in-place, memory footprint < 200 KB)
    gaussianSmooth( smoothed, width, height, params.smoothKernel, nodata );
    if ( canceled() )
        return {};
    report( 0.3f );

    // 3. Quantize
    const int bins = std::max( 2, params.quantizeBins );
    QVector<int> quantized = quantize( smoothed.data(), n, bins, nodata );
    report( 0.5f );

    // Mask nodata pixels explicitly from original raw data
    for ( size_t i = 0; i < n; ++i )
    {
        if ( data[i] == nodata || std::isnan( data[i] ) )
            quantized[i] = 0;
    }

    // Release smoothed float buffer early to minimize peak RSS
    smoothed.clear();
    smoothed.squeeze();

    if ( canceled() )
        return {};
    report( 0.55f );

    // 4. Connected components
    QVector<quint32> labels = connectedComponents( quantized, width, height, 0, isCanceled );
    if ( labels.isEmpty() )
        return {}; // canceled during labeling

    // Release quantized buffer early
    quantized.clear();
    quantized.squeeze();

    report( 0.8f );

    // 5. Merge small regions (flat Union-Find)
    mergeSmallRegions( labels, width, height, params.minRegionSize, isCanceled );
    if ( canceled() )
        return {};

    report( 1.0f );

    RsSegmentMap result( std::move( labels ), width, height );
    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation, QString( "Segmentation complete: %1 segments found" )
        .arg( result.segmentCount() ) );
    return result;
}

RsSegmentMap RsSimpleSegmenter::segmentMultiBand( const float *const *bandData,
                                                   int nBands, int width, int height,
                                                   float nodata, const Params &params,
                                                   const std::function<bool()> &isCanceled,
                                                   const std::function<void(float)> &onProgress )
{
    if ( !bandData || nBands <= 0 || width <= 0 || height <= 0 )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Segmentation, QString( "segmentMultiBand: invalid input (nBands=%1)" ).arg( nBands ) );
        return {};
    }

    SICNU_LOG_INFO( SicnuLogTags::Segmentation, QString( "Starting multi-band segmentation: %1 bands, %2x%3" )
        .arg( nBands ).arg( width ).arg( height ) );

    auto canceled = [&isCanceled]() { return isCanceled && isCanceled(); };
    auto report = [&onProgress]( float f ) {
        if ( onProgress )
            onProgress( f );
    };

    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);

    if ( canceled() )
        return {};
    report( 0.0f );

    const int bins = std::max( 2, params.quantizeBins );
    QVector<int> compositeQuantized( n, 0 );

    // Sequential band processing: smooth & quantize 1 band at a time to minimize RSS
    for ( int b = 0; b < nBands; ++b )
    {
        if ( canceled() )
            return {};

        // 1. Copy single band for in-place smoothing
        QVector<float> curBand( bandData[b], bandData[b] + n );
        gaussianSmooth( curBand, width, height, params.smoothKernel, nodata );

        // 2. Quantize single smoothed band with sanitized bins
        QVector<int> curQuant = quantize( curBand.data(), n, bins, nodata );

        // curBand float buffer no longer needed for this band
        curBand.clear();
        curBand.squeeze();

        // 3. Accumulate into compositeQuantized on-the-fly
        if ( b == 0 )
        {
            for ( size_t i = 0; i < n; ++i )
            {
                if ( bandData[0][i] == nodata || std::isnan( bandData[0][i] ) || curQuant[i] == 0 )
                {
                    compositeQuantized[i] = 0; // nodata bin
                }
                else
                {
                    compositeQuantized[i] = curQuant[i];
                }
            }
        }
        else
        {
            for ( size_t i = 0; i < n; ++i )
            {
                if ( compositeQuantized[i] == 0 || bandData[b][i] == nodata || std::isnan( bandData[b][i] ) || curQuant[i] == 0 )
                {
                    compositeQuantized[i] = 0; // nodata in any band
                }
                else
                {
                    uint64_t h = static_cast<uint32_t>( compositeQuantized[i] );
                    // 64-bit hash combiner (SplitMix / Boost hash_combine)
                    h ^= static_cast<uint32_t>( curQuant[i] ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
                    int combined = static_cast<int>( h & 0x7FFFFFFF );
                    if ( combined == 0 )
                        combined = 1;
                    compositeQuantized[i] = combined;
                }
            }
        }

        if ( canceled() )
            return {};
        report( 0.1f + 0.5f * ( static_cast<float>(b + 1) / static_cast<float>(nBands) ) );
    }

    if ( canceled() )
        return {};
    report( 0.6f );

    // Connected components on composite multi-spectral grid
    QVector<quint32> labels = connectedComponents( compositeQuantized, width, height, 0, isCanceled );
    if ( labels.isEmpty() )
        return {};

    // Release compositeQuantized early
    compositeQuantized.clear();
    compositeQuantized.squeeze();

    report( 0.8f );

    // Merge small regions (flat Union-Find)
    mergeSmallRegions( labels, width, height, params.minRegionSize, isCanceled );
    if ( canceled() )
        return {};

    report( 1.0f );

    RsSegmentMap result( std::move( labels ), width, height );
    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation, QString( "Multi-band segmentation complete: %1 segments found" )
        .arg( result.segmentCount() ) );
    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void RsSimpleSegmenter::gaussianSmooth( QVector<float> &data, int w, int h, int kernelSize, float nodata )
{
    if ( kernelSize < 3 || w <= 0 || h <= 0 )
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

    auto isNoData = [nodata]( float v ) {
        return v == nodata || std::isnan( v );
    };

    // Line-buffered separable convolution:
    // Uses a circular ring buffer of `kernelSize` rows (size = kernelSize * w * 4 bytes).
    // Eliminates the full W*H temp buffer and keeps lines in L1/L2 cache.
    std::vector<float> ringBuf( static_cast<size_t>(kernelSize) * static_cast<size_t>(w) );

    for ( int r = 0; r < h + half; ++r )
    {
        if ( r < h )
        {
            // Horizontal 1D pass for row r
            const float *srcRow = &data[static_cast<size_t>(r) * w];
            float *ringRow = &ringBuf[static_cast<size_t>(r % kernelSize) * w];
            for ( int c = 0; c < w; ++c )
            {
                if ( isNoData( srcRow[c] ) )
                {
                    ringRow[c] = srcRow[c];
                    continue;
                }
                float val = 0.0f;
                float weightSum = 0.0f;
                for ( int k = 0; k < kernelSize; ++k )
                {
                    int cc = std::clamp( c + k - half, 0, w - 1 );
                    float sv = srcRow[cc];
                    if ( !isNoData( sv ) )
                    {
                        val += sv * kernel[k];
                        weightSum += kernel[k];
                    }
                }
                ringRow[c] = ( weightSum > 0.0f ) ? ( val / weightSum ) : nodata;
            }
        }

        // Vertical pass for output row y = r - half
        if ( r >= half )
        {
            const int y = r - half;
            float *outRow = &data[static_cast<size_t>(y) * w];
            for ( int c = 0; c < w; ++c )
            {
                if ( isNoData( outRow[c] ) )
                    continue;

                float val = 0.0f;
                float weightSum = 0.0f;
                for ( int k = 0; k < kernelSize; ++k )
                {
                    int srcR = std::clamp( y + k - half, 0, h - 1 );
                    const float *ringRow = &ringBuf[static_cast<size_t>(srcR % kernelSize) * w];
                    float rv = ringRow[c];
                    if ( !isNoData( rv ) )
                    {
                        val += rv * kernel[k];
                        weightSum += kernel[k];
                    }
                }
                outRow[c] = ( weightSum > 0.0f ) ? ( val / weightSum ) : nodata;
            }
        }
    }
}

QVector<int> RsSimpleSegmenter::quantize( const float *data, size_t n, int bins, float nodata )
{
    // Find min/max excluding nodata and NaN
    float vmin = std::numeric_limits<float>::infinity();
    float vmax = -std::numeric_limits<float>::infinity();
    for ( size_t i = 0; i < n; ++i )
    {
        float v = data[i];
        if ( v != nodata && !std::isnan( v ) )
        {
            if ( v < vmin )
                vmin = v;
            if ( v > vmax )
                vmax = v;
        }
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
                                                          int w, int h, int nodataBin,
                                                          const std::function<bool()> &isCanceled )
{
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    QVector<quint32> labels( n, 0 );
    quint32 nextLabel = 1;

    // 8-connectivity offsets
    const int dr[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    const int dc[] = { -1, 0, 1, -1, 1, -1, 0, 1 };

    // Cancellation is polled every 4096 scanned pixels (cheap enough to not
    // measurably slow the hot loops, fine-grained enough for cancel latency).
    size_t scanned = 0;
    auto cancelCheck = [&isCanceled, &scanned]() {
        if ( isCanceled && ( scanned & 0xFFFu ) == 0 && isCanceled() )
            return true;
        ++scanned;
        return false;
    };

    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            if ( cancelCheck() )
                return {};
            const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c);
            if ( quantized[idx] == nodataBin || quantized[idx] == 0 )
                continue;
            if ( labels[idx] != 0 )
                continue;

            // BFS flood fill with natural 8-connected wavefront
            const int targetBin = quantized[idx];
            std::queue<size_t> q;
            q.push( idx );
            labels[idx] = nextLabel;

            while ( !q.empty() )
            {
                size_t cur = q.front();
                q.pop();
                int cr = static_cast<int>( cur / static_cast<size_t>(w) );
                int cc = static_cast<int>( cur % static_cast<size_t>(w) );

                for ( int d = 0; d < 8; ++d )
                {
                    int nr = cr + dr[d];
                    int nc = cc + dc[d];
                    if ( nr < 0 || nr >= h || nc < 0 || nc >= w )
                        continue;
                    size_t nidx = static_cast<size_t>(nr) * static_cast<size_t>(w) + static_cast<size_t>(nc);
                    if ( labels[nidx] != 0 )
                        continue;
                    if ( quantized[nidx] != targetBin )
                        continue;
                    labels[nidx] = nextLabel;
                    q.push( nidx );
                }
                if ( ( scanned & 0xFFFu ) == 0 && isCanceled && isCanceled() )
                    return {};
                ++scanned;
            }
            nextLabel++;
        }
    }

    return labels;
}

void RsSimpleSegmenter::mergeSmallRegions( QVector<quint32> &labels, int w, int h,
                                            int minSize,
                                            const std::function<bool()> &isCanceled )
{
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    if ( n == 0 || minSize <= 1 )
        return;

    // Find max label
    quint32 maxLabel = 0;
    for ( size_t i = 0; i < n; ++i )
    {
        if ( labels[i] > maxLabel )
            maxLabel = labels[i];
    }

    if ( maxLabel == 0 )
        return;

    // Count region sizes using flat array
    std::vector<int> sizeVec( maxLabel + 1, 0 );
    for ( size_t i = 0; i < n; ++i )
    {
        if ( labels[i] != 0 )
            sizeVec[labels[i]]++;
    }

    // Find small regions
    std::vector<uint8_t> isSmall( maxLabel + 1, 0 );
    size_t smallCount = 0;
    for ( quint32 id = 1; id <= maxLabel; ++id )
    {
        if ( sizeVec[id] > 0 && sizeVec[id] < minSize )
        {
            isSmall[id] = 1;
            ++smallCount;
        }
    }

    if ( smallCount == 0 )
        return;

    SICNU_LOG_DEBUG( SicnuLogTags::Segmentation, QString( "Merging %1 small regions (minSize=%2)" )
        .arg( smallCount ).arg( minSize ) );

    // 4-connected boundary offsets
    const int dr4[] = { -1, 1, 0, 0 };
    const int dc4[] = { 0, 0, -1, 1 };

    // Pass 1: For each small region pixel, count its non-small / different neighbors.
    // Each small region (< minSize pixels) touches few distinct neighbors. A compact
    // pair vector per small region avoids nested hash-map heap churn.
    struct NeighborCount
    {
        quint32 neighbor = 0;
        int count = 0;
    };
    std::unordered_map<quint32, std::vector<NeighborCount>> smallAdjacency;

    for ( size_t i = 0; i < n; ++i )
    {
        if ( ( i & 0xFFFFu ) == 0 && isCanceled && isCanceled() )
            return;
        quint32 segId = labels[i];
        if ( segId == 0 || !isSmall[segId] )
            continue;
        int r = static_cast<int>( i / static_cast<size_t>(w) );
        int c = static_cast<int>( i % static_cast<size_t>(w) );
        for ( int d = 0; d < 4; ++d )
        {
            int nr = r + dr4[d];
            int nc = c + dc4[d];
            if ( nr < 0 || nr >= h || nc < 0 || nc >= w )
                continue;
            quint32 neighbor = labels[static_cast<size_t>(nr) * static_cast<size_t>(w) + static_cast<size_t>(nc)];
            if ( neighbor != 0 && neighbor != segId )
            {
                auto &vec = smallAdjacency[segId];
                bool found = false;
                for ( auto &entry : vec )
                {
                    if ( entry.neighbor == neighbor )
                    {
                        entry.count++;
                        found = true;
                        break;
                    }
                }
                if ( !found )
                {
                    vec.push_back( { neighbor, 1 } );
                }
            }
        }
    }

    if ( isCanceled && isCanceled() )
        return;

    // Build merge map from adjacency (small -> preferred neighbor)
    std::unordered_map<quint32, quint32> mergeMap;
    for ( const auto &[segId, neighbors] : smallAdjacency )
    {
        quint32 bestNeighbor = 0;
        int bestCount = 0;
        for ( const auto &entry : neighbors )
        {
            if ( entry.count > bestCount )
            {
                bestCount = entry.count;
                bestNeighbor = entry.neighbor;
            }
        }
        if ( bestNeighbor != 0 )
            mergeMap[segId] = bestNeighbor;
    }

    // Flat Union-Find with path compression
    std::vector<quint32> parent( maxLabel + 1 );
    std::iota( parent.begin(), parent.end(), 0 );

    auto findRoot = [&parent]( quint32 id ) -> quint32 {
        quint32 root = id;
        while ( root <= parent.size() - 1 && parent[root] != root )
            root = parent[root];
        // Path compression
        quint32 cur = id;
        while ( cur <= parent.size() - 1 && parent[cur] != root )
        {
            quint32 nxt = parent[cur];
            parent[cur] = root;
            cur = nxt;
        }
        return root;
    };

    for ( const auto &[segId, target] : mergeMap )
    {
        quint32 r1 = findRoot( segId );
        quint32 r2 = findRoot( target );
        if ( r1 != r2 )
        {
            parent[r1] = r2;
        }
    }

    // Apply fully-resolved merges in a single pass over the full image
    for ( size_t i = 0; i < n; ++i )
    {
        if ( ( i & 0xFFFFu ) == 0 && isCanceled && isCanceled() )
            return;
        const quint32 lab = labels[i];
        if ( lab == 0 )
            continue;
        const quint32 root = findRoot( lab );
        if ( root != lab )
            labels[i] = root;
    }
}
