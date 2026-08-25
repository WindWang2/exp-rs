// rs_multires_segmenter.cpp — Native Baatz-Schäpe (FNEA) Multiresolution Segmentation Engine.
#include "rs_multires_segmenter.h"
#include "sicnu_logging.h"

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

namespace
{

struct DisjointSet
{
    std::vector<uint32_t> parent;
    explicit DisjointSet( size_t n ) : parent( n )
    {
        for ( size_t i = 0; i < n; ++i )
            parent[i] = static_cast<uint32_t>( i );
    }

    uint32_t find( uint32_t i )
    {
        uint32_t root = i;
        while ( root != parent[root] )
            root = parent[root];
        uint32_t curr = i;
        while ( curr != root )
        {
            uint32_t next = parent[curr];
            parent[curr] = root;
            curr = next;
        }
        return root;
    }

    void unite( uint32_t root1, uint32_t root2 )
    {
        parent[root2] = root1;
    }
};

inline double calculateStdDev( double sum, double sumSq, double area )
{
    if ( area <= 1.0 )
        return 0.0;
    const double mean = sum / area;
    const double variance = ( sumSq / area ) - ( mean * mean );
    return variance > 0.0 ? std::sqrt( variance ) : 0.0;
}

inline double calculateFusionCost(
    const RsMultiresSegmenter::SegmentNode &r1,
    const RsMultiresSegmenter::SegmentNode &r2,
    uint32_t sharedBorder,
    double wShape,
    double wComp,
    const std::vector<double> &normBandWeights,
    int bandCount )
{
    const double n1 = static_cast<double>( r1.area );
    const double n2 = static_cast<double>( r2.area );
    const double nMerge = n1 + n2;

    // 1. Spectral color heterogeneity
    double deltaHColor = 0.0;
    if ( wShape < 1.0 )
    {
        for ( int b = 0; b < bandCount; ++b )
        {
            const double s1 = r1.sumB[b];
            const double ss1 = r1.sumSqB[b];
            const double s2 = r2.sumB[b];
            const double ss2 = r2.sumSqB[b];

            const double sMerge = s1 + s2;
            const double ssMerge = ss1 + ss2;

            const double sigma1 = calculateStdDev( s1, ss1, n1 );
            const double sigma2 = calculateStdDev( s2, ss2, n2 );
            const double sigmaMerge = calculateStdDev( sMerge, ssMerge, nMerge );

            const double deltaHB = ( nMerge * sigmaMerge ) - ( n1 * sigma1 + n2 * sigma2 );
            deltaHColor += normBandWeights[b] * deltaHB;
        }
    }

    // 2. Shape heterogeneity
    double deltaHShape = 0.0;
    if ( wShape > 0.0 )
    {
        const double l1 = static_cast<double>( r1.perimeter );
        const double l2 = static_cast<double>( r2.perimeter );
        const double lMerge = l1 + l2 - 2.0 * static_cast<double>( sharedBorder );

        // a) Compactness
        const double hComp1 = l1 / std::sqrt( n1 );
        const double hComp2 = l2 / std::sqrt( n2 );
        const double hCompMerge = lMerge / std::sqrt( nMerge );
        const double deltaHComp = nMerge * hCompMerge - ( n1 * hComp1 + n2 * hComp2 );

        // b) Smoothness
        const double b1 = 2.0 * ( static_cast<double>( r1.maxX - r1.minX + 1 ) + static_cast<double>( r1.maxY - r1.minY + 1 ) );
        const double b2 = 2.0 * ( static_cast<double>( r2.maxX - r2.minX + 1 ) + static_cast<double>( r2.maxY - r2.minY + 1 ) );

        const uint32_t minXMerge = std::min( r1.minX, r2.minX );
        const uint32_t maxXMerge = std::max( r1.maxX, r2.maxX );
        const uint32_t minYMerge = std::min( r1.minY, r2.minY );
        const uint32_t maxYMerge = std::max( r1.maxY, r2.maxY );
        const double bMerge = 2.0 * ( static_cast<double>( maxXMerge - minXMerge + 1 ) + static_cast<double>( maxYMerge - minYMerge + 1 ) );

        const double hSmooth1 = l1 / ( b1 > 0.0 ? b1 : 1.0 );
        const double hSmooth2 = l2 / ( b2 > 0.0 ? b2 : 1.0 );
        const double hSmoothMerge = lMerge / ( bMerge > 0.0 ? bMerge : 1.0 );
        const double deltaHSmooth = nMerge * hSmoothMerge - ( n1 * hSmooth1 + n2 * hSmooth2 );

        deltaHShape = wComp * deltaHComp + ( 1.0 - wComp ) * deltaHSmooth;
    }

    const double totalCost = ( 1.0 - wShape ) * deltaHColor + wShape * deltaHShape;
    return totalCost;
}

void mergeRegions(
    uint32_t r1Id,
    uint32_t r2Id,
    std::vector<RsMultiresSegmenter::SegmentNode> &nodes,
    DisjointSet &dsu,
    int bandCount )
{
    RsMultiresSegmenter::SegmentNode &r1 = nodes[r1Id];
    RsMultiresSegmenter::SegmentNode &r2 = nodes[r2Id];

    // Find shared border between r1 and r2
    uint32_t sharedBorder = 0;
    for ( const auto &nb : r1.neighbors )
    {
        if ( nb.first == r2Id )
        {
            sharedBorder = nb.second;
            break;
        }
    }

    // 1. Update stats of r1
    r1.area += r2.area;
    r1.perimeter = r1.perimeter + r2.perimeter - 2 * sharedBorder;
    r1.minX = std::min( r1.minX, r2.minX );
    r1.maxX = std::max( r1.maxX, r2.maxX );
    r1.minY = std::min( r1.minY, r2.minY );
    r1.maxY = std::max( r1.maxY, r2.maxY );

    for ( int b = 0; b < bandCount; ++b )
    {
        r1.sumB[b] += r2.sumB[b];
        r1.sumSqB[b] += r2.sumSqB[b];
    }

    // 2. Update DSU
    dsu.unite( r1Id, r2Id );
    r2.active = false;

    // 3. Remove r2 from r1's neighbor list
    auto itR2InR1 = std::remove_if( r1.neighbors.begin(), r1.neighbors.end(),
                                   [r2Id]( const std::pair<uint32_t, uint32_t> &p ) { return p.first == r2Id; } );
    r1.neighbors.erase( itR2InR1, r1.neighbors.end() );

    // 4. Update neighbors of r2: redirect them to r1
    for ( const auto &edge : r2.neighbors )
    {
        uint32_t kId = edge.first;
        if ( kId == r1Id )
            continue;

        uint32_t borderK2 = edge.second;
        RsMultiresSegmenter::SegmentNode &kNode = nodes[kId];

        // In kNode: replace r2 with r1, or if r1 already present, merge border
        bool foundR1InK = false;
        for ( auto &kNb : kNode.neighbors )
        {
            if ( kNb.first == r1Id )
            {
                kNb.second += borderK2;
                foundR1InK = true;
                break;
            }
        }
        // Remove r2 from kNode
        auto itR2InK = std::remove_if( kNode.neighbors.begin(), kNode.neighbors.end(),
                                      [r2Id]( const std::pair<uint32_t, uint32_t> &p ) { return p.first == r2Id; } );
        kNode.neighbors.erase( itR2InK, kNode.neighbors.end() );

        if ( !foundR1InK )
        {
            kNode.neighbors.push_back( { r1Id, borderK2 } );
        }

        // In r1: add or update kId
        bool foundKInR1 = false;
        for ( auto &r1Nb : r1.neighbors )
        {
            if ( r1Nb.first == kId )
            {
                r1Nb.second += borderK2;
                foundKInR1 = true;
                break;
            }
        }
        if ( !foundKInR1 )
        {
            r1.neighbors.push_back( { kId, borderK2 } );
        }
    }

    r2.neighbors.clear();
}

uint32_t findBestNeighbor(
    uint32_t u,
    const std::vector<RsMultiresSegmenter::SegmentNode> &nodes,
    double wShape,
    double wComp,
    const std::vector<double> &normBandWeights,
    int bandCount,
    double &outBestCost )
{
    const auto &r1 = nodes[u];
    uint32_t bestNeighbor = std::numeric_limits<uint32_t>::max();
    double bestCost = std::numeric_limits<double>::infinity();

    for ( const auto &nb : r1.neighbors )
    {
        uint32_t v = nb.first;
        if ( v == u || !nodes[v].active )
            continue;

        double cost = calculateFusionCost( r1, nodes[v], nb.second, wShape, wComp, normBandWeights, bandCount );
        if ( cost < bestCost )
        {
            bestCost = cost;
            bestNeighbor = v;
        }
    }

    outBestCost = bestCost;
    return bestNeighbor;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RsMultiresSegmenter Implementation
// ---------------------------------------------------------------------------

RsSegmentMap RsMultiresSegmenter::segment(
    const float *const *bandData,
    int bandCount,
    int width,
    int height,
    float nodata,
    const RsMultiresParams &params,
    const std::function<bool()> &isCanceled,
    const std::function<void(float)> &onProgress )
{
    if ( !bandData || bandCount <= 0 || width <= 0 || height <= 0 )
    {
        return RsSegmentMap();
    }

    std::vector<const float*> bands( bandCount );
    for ( int b = 0; b < bandCount; ++b )
    {
        bands[b] = bandData[b];
    }

    return segment( bands, static_cast<int64_t>( width ), static_cast<int64_t>( height ),
                    nodata, params, isCanceled, onProgress );
}

RsSegmentMap RsMultiresSegmenter::segment(
    const std::vector<const float*> &bands,
    int64_t width,
    int64_t height,
    float nodata,
    const RsMultiresParams &params,
    const std::function<bool()> &isCanceled,
    const std::function<void(float)> &onProgress )
{
    if ( bands.empty() || width <= 0 || height <= 0 )
    {
        return RsSegmentMap();
    }

    int bandCount = static_cast<int>( bands.size() );
    for ( int b = 0; b < bandCount; ++b )
    {
        if ( !bands[b] )
            return RsSegmentMap();
    }

    if ( isCanceled && isCanceled() )
        return RsSegmentMap();

    if ( onProgress )
        onProgress( 0.05f );

    // 1. Normalize band weights
    std::vector<double> normBandWeights( bandCount, 1.0 / bandCount );
    if ( !params.bandWeights.empty() )
    {
        double sumW = 0.0;
        for ( int b = 0; b < bandCount && b < static_cast<int>( params.bandWeights.size() ); ++b )
        {
            double w = std::max( 0.0, params.bandWeights[b] );
            normBandWeights[b] = w;
            sumW += w;
        }
        if ( sumW > 1e-12 )
        {
            for ( int b = 0; b < bandCount; ++b )
                normBandWeights[b] /= sumW;
        }
        else
        {
            for ( int b = 0; b < bandCount; ++b )
                normBandWeights[b] = 1.0 / bandCount;
        }
    }

    const double wShape = params.effectiveShapeWeight();
    const double wComp = params.effectiveCompactnessWeight();
    const double scaleThreshold = params.scale * params.scale;

    // 2. Identify valid pixels and allocate RAG nodes
    const size_t totalPixels = static_cast<size_t>( width ) * static_cast<size_t>( height );
    std::vector<uint32_t> pixelToNode( totalPixels, std::numeric_limits<uint32_t>::max() );
    std::vector<SegmentNode> nodes;
    nodes.reserve( totalPixels );

    for ( int64_t y = 0; y < height; ++y )
    {
        for ( int64_t x = 0; x < width; ++x )
        {
            size_t idx = static_cast<size_t>( y * width + x );
            bool isPixelNodata = false;

            for ( int b = 0; b < bandCount; ++b )
            {
                float val = bands[b][idx];
                if ( std::isnan( val ) || ( !std::isnan( nodata ) && std::abs( val - nodata ) < 1e-6f ) )
                {
                    isPixelNodata = true;
                    break;
                }
            }

            if ( isPixelNodata )
                continue;

            uint32_t nodeId = static_cast<uint32_t>( nodes.size() );
            pixelToNode[idx] = nodeId;

            SegmentNode node;
            node.id = nodeId;
            node.area = 1;
            node.perimeter = 4;
            node.minX = static_cast<uint32_t>( x );
            node.maxX = static_cast<uint32_t>( x );
            node.minY = static_cast<uint32_t>( y );
            node.maxY = static_cast<uint32_t>( y );
            node.sumB.resize( bandCount );
            node.sumSqB.resize( bandCount );
            for ( int b = 0; b < bandCount; ++b )
            {
                double val = static_cast<double>( bands[b][idx] );
                node.sumB[b] = val;
                node.sumSqB[b] = val * val;
            }
            node.active = true;
            nodes.push_back( std::move( node ) );
        }
    }

    if ( nodes.empty() )
    {
        return RsSegmentMap();
    }

    // 3. Build initial 4-connected adjacency edges
    for ( int64_t y = 0; y < height; ++y )
    {
        for ( int64_t x = 0; x < width; ++x )
        {
            size_t idx = static_cast<size_t>( y * width + x );
            uint32_t u = pixelToNode[idx];
            if ( u == std::numeric_limits<uint32_t>::max() )
                continue;

            // Right neighbor (x+1, y)
            if ( x + 1 < width )
            {
                size_t rightIdx = static_cast<size_t>( y * width + ( x + 1 ) );
                uint32_t v = pixelToNode[rightIdx];
                if ( v != std::numeric_limits<uint32_t>::max() )
                {
                    nodes[u].neighbors.push_back( { v, 1 } );
                    nodes[v].neighbors.push_back( { u, 1 } );
                }
            }

            // Down neighbor (x, y+1)
            if ( y + 1 < height )
            {
                size_t downIdx = static_cast<size_t>( ( y + 1 ) * width + x );
                uint32_t v = pixelToNode[downIdx];
                if ( v != std::numeric_limits<uint32_t>::max() )
                {
                    nodes[u].neighbors.push_back( { v, 1 } );
                    nodes[v].neighbors.push_back( { u, 1 } );
                }
            }
        }
    }

    DisjointSet dsu( nodes.size() );

    if ( onProgress )
        onProgress( 0.10f );

    // 4. Local Mutual Best Fit (LMBF) Bottom-Up Merging Loop
    const int maxPasses = 200;
    int pass = 0;

    while ( pass < maxPasses )
    {
        if ( isCanceled && isCanceled() )
            return RsSegmentMap();

        std::vector<uint32_t> activeList;
        activeList.reserve( nodes.size() );
        for ( size_t i = 0; i < nodes.size(); ++i )
        {
            if ( nodes[i].active && !nodes[i].neighbors.empty() )
                activeList.push_back( static_cast<uint32_t>( i ) );
        }

        if ( activeList.empty() )
            break;

        std::mt19937 rng( 42 + pass );
        std::shuffle( activeList.begin(), activeList.end(), rng );

        int mergesInPass = 0;

        for ( uint32_t u : activeList )
        {
            if ( !nodes[u].active || nodes[u].neighbors.empty() )
                continue;

            // Follow search chain
            std::vector<uint32_t> chain;
            chain.reserve( 16 );
            chain.push_back( u );

            while ( chain.size() <= 50 )
            {
                uint32_t curr = chain.back();
                if ( !nodes[curr].active || nodes[curr].neighbors.empty() )
                    break;

                double minCost = 0.0;
                uint32_t bestNb = findBestNeighbor( curr, nodes, wShape, wComp, normBandWeights, bandCount, minCost );

                if ( bestNb == std::numeric_limits<uint32_t>::max() || minCost > scaleThreshold )
                {
                    // Cannot merge curr with any neighbor below scale threshold
                    break;
                }

                if ( chain.size() >= 2 && bestNb == chain[chain.size() - 2] )
                {
                    // Local Mutual Best Fit found!
                    uint32_t r1 = chain[chain.size() - 2];
                    uint32_t r2 = curr;
                    mergeRegions( r1, r2, nodes, dsu, bandCount );
                    mergesInPass++;
                    break;
                }

                // Detect cycles in chain
                auto it = std::find( chain.begin(), chain.end(), bestNb );
                if ( it != chain.end() )
                {
                    break;
                }

                chain.push_back( bestNb );
            }
        }

        if ( onProgress )
        {
            float p = 0.10f + 0.75f * std::min( 1.0f, static_cast<float>( pass + 1 ) / 25.0f );
            onProgress( p );
        }

        if ( mergesInPass == 0 )
            break;

        pass++;
    }

    // 5. Post-Processing: Eliminate Small Regions (< minRegionSize)
    const int minSize = params.effectiveMinRegionSize();
    if ( minSize > 1 )
    {
        if ( onProgress )
            onProgress( 0.85f );

        int cleanupPass = 0;
        while ( cleanupPass < 50 )
        {
            if ( isCanceled && isCanceled() )
                return RsSegmentMap();

            int mergesInCleanup = 0;
            for ( size_t i = 0; i < nodes.size(); ++i )
            {
                if ( !nodes[i].active || nodes[i].area >= static_cast<uint32_t>( minSize ) )
                    continue;
                if ( nodes[i].neighbors.empty() )
                    continue;

                double minCost = 0.0;
                uint32_t bestNb = findBestNeighbor( static_cast<uint32_t>( i ), nodes, wShape, wComp, normBandWeights, bandCount, minCost );
                if ( bestNb != std::numeric_limits<uint32_t>::max() )
                {
                    mergeRegions( bestNb, static_cast<uint32_t>( i ), nodes, dsu, bandCount );
                    mergesInCleanup++;
                }
            }

            if ( mergesInCleanup == 0 )
                break;
            cleanupPass++;
        }
    }

    // 6. Assign contiguous 1-based labels (0 = NoData)
    if ( onProgress )
        onProgress( 0.95f );

    std::unordered_map<uint32_t, quint32> rootToLabel;
    quint32 nextLabel = 1;

    // qsizetype keeps >2^31-pixel label buffers valid on 64-bit builds; the
    // map is constructed through the int64_t overload so width/height never
    // round-trip through int.
    QVector<quint32> labels( static_cast<qsizetype>( totalPixels ), 0 );

    for ( int64_t y = 0; y < height; ++y )
    {
        for ( int64_t x = 0; x < width; ++x )
        {
            size_t idx = static_cast<size_t>( y * width + x );
            uint32_t nodeIdx = pixelToNode[idx];
            if ( nodeIdx == std::numeric_limits<uint32_t>::max() )
            {
                labels[static_cast<qsizetype>( idx )] = 0;
            }
            else
            {
                uint32_t root = dsu.find( nodeIdx );
                auto it = rootToLabel.find( root );
                if ( it == rootToLabel.end() )
                {
                    rootToLabel[root] = nextLabel;
                    labels[static_cast<qsizetype>( idx )] = nextLabel;
                    nextLabel++;
                }
                else
                {
                    labels[static_cast<qsizetype>( idx )] = it->second;
                }
            }
        }
    }

    if ( onProgress )
        onProgress( 1.0f );

    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation,
        QString( "Baatz-Schäpe MRS complete: %1 segments generated (scale=%2, shapeWeight=%3, compWeight=%4)" )
            .arg( nextLabel - 1 )
            .arg( params.scale )
            .arg( wShape )
            .arg( wComp ) );

    return RsSegmentMap( std::move( labels ), width, height );
}

RsSegmentMap RsMultiresSegmenter::segment(
    const std::vector<const float*> &bands,
    int64_t width,
    int64_t height,
    const RsMultiresParams &params,
    std::atomic<bool> *cancelFlag,
    std::function<void(float)> progressCallback )
{
    auto isCanceled = [cancelFlag]() -> bool {
        return cancelFlag && cancelFlag->load( std::memory_order_relaxed );
    };

    const float nodata = std::numeric_limits<float>::quiet_NaN();
    return segment( bands, width, height, nodata, params, isCanceled, progressCallback );
}

RsSegmentMap RsMultiresSegmenter::segmentRasterFile(
    const QString &rasterPath,
    const QVector<int> &bandIndices,
    const RsMultiresParams &params,
    const std::function<bool()> &isCanceled,
    const std::function<void(float)> &onProgress,
    QString *errorMessage )
{
    if ( rasterPath.isEmpty() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Empty raster path" );
        return RsSegmentMap();
    }

    GDALDatasetH ds = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
    {
        if ( errorMessage )
            *errorMessage = QString( "Failed to open raster: %1" ).arg( rasterPath );
        return RsSegmentMap();
    }
    std::unique_ptr<void, decltype(&GDALClose)> dsGuard( ds, &GDALClose );

    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );
    const int totalBands = GDALGetRasterCount( ds );

    if ( w <= 0 || h <= 0 || totalBands <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QString( "Invalid raster dimensions: %1x%2, bands=%3" ).arg( w ).arg( h ).arg( totalBands );
        return RsSegmentMap();
    }

    QVector<int> effectiveBands = bandIndices;
    if ( effectiveBands.isEmpty() )
    {
        for ( int b = 1; b <= totalBands; ++b )
            effectiveBands.append( b );
    }

    const int nBands = effectiveBands.size();
    const size_t nPixels = static_cast<size_t>( w ) * static_cast<size_t>( h );

    std::vector<std::vector<float>> bandBuffers( nBands );
    std::vector<const float*> bandPointers( nBands );
    float nodata = std::numeric_limits<float>::quiet_NaN();

    for ( int i = 0; i < nBands; ++i )
    {
        int bandIdx = effectiveBands[i];
        if ( bandIdx < 1 || bandIdx > totalBands )
        {
            if ( errorMessage )
                *errorMessage = QString( "Invalid band index: %1 (total: %2)" ).arg( bandIdx ).arg( totalBands );
            return RsSegmentMap();
        }

        GDALRasterBandH band = GDALGetRasterBand( ds, bandIdx );
        if ( !band )
        {
            if ( errorMessage )
                *errorMessage = QString( "Failed to get GDAL band: %1" ).arg( bandIdx );
            return RsSegmentMap();
        }

        bandBuffers[i].resize( nPixels );
        CPLErr err = GDALRasterIO( band, GF_Read, 0, 0, w, h,
                                   bandBuffers[i].data(), w, h, GDT_Float32, 0, 0 );
        if ( err != CE_None )
        {
            if ( errorMessage )
                *errorMessage = QString( "GDALRasterIO read error on band %1" ).arg( bandIdx );
            return RsSegmentMap();
        }

        int hasNoData = 0;
        double ndVal = GDALGetRasterNoDataValue( band, &hasNoData );
        if ( hasNoData && std::isnan( nodata ) )
        {
            nodata = static_cast<float>( ndVal );
        }

        bandPointers[i] = bandBuffers[i].data();
    }

    return segment( bandPointers, static_cast<int64_t>( w ), static_cast<int64_t>( h ),
                    nodata, params, isCanceled, onProgress );
}

RsSegmenterResult RsMultiresSegmenter::segment(
    const QString &rasterPath,
    const RsLevelSpec &spec,
    const std::function<bool()> &isCanceled )
{
    RsSegmenterResult result;
    RsMultiresParams params;

    params.scale = spec.rangeRadius > 0.0 ? spec.rangeRadius : ( spec.spatialRadius > 0 ? spec.spatialRadius * 4.0 : 20.0 );
    params.minRegionSize = spec.minRegionSize > 0 ? spec.minRegionSize : 1;
    params.shapeWeight = 0.2;
    params.compactnessWeight = 0.5;

    QString errorMsg;
    RsSegmentMap segMap = segmentRasterFile( rasterPath, {}, params, isCanceled, nullptr, &errorMsg );

    if ( segMap.isEmpty() )
    {
        result.ok = false;
        result.errorMessage = errorMsg.isEmpty() ? QStringLiteral( "Multiresolution segmentation failed" ) : errorMsg;
        return result;
    }

    result.ok = true;
    result.segMap = std::move( segMap );
    return result;
}
