// image_enhancement_streaming.cpp — tile-streaming enhancement kernels.
//
// Every kernel here is a formula-for-formula replica of the full-frame
// counterpart in image_enhancement.cpp (see the header for the numerical
// contract). Window statistics are accumulated directly over the halo tile
// instead of a whole-raster summed-area table, which removes the 20 B/px
// IntegralImage cost (2.7 GiB at 12000²) and the 2×B full-raster frames from
// the dialog paths.
#include "image_enhancement_streaming.h"

#include "image_enhancement.h"
#include "math_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ImageEnhancementStreaming
{

namespace
{

constexpr int kEqualizeBins = 256;

float quietNan()
{
    return std::numeric_limits<float>::quiet_NaN();
}

/**
 * Window mean/variance over the halo buffer — the tile equivalent of
 * image_enhancement.cpp's IntegralImage::computeRegion + localStats (finite
 * values only; variance = sumSq/count - mean², clamped at 0). The halo buffer
 * already replicate-clamps raster borders, exactly matching the full-frame
 * kernels' std::clamp window clamping, so no clamping is needed here.
 */
void windowStats( const GdalBlockStream::Tile &tile, const float *haloBuf,
                  int cx, int cy, int kernelSize, float &mean, float &variance )
{
    const int half = kernelSize / 2;
    const int bufW = tile.bufferWidth;
    const float *center = haloBuf + static_cast<size_t>( cy + tile.halo ) * bufW + ( cx + tile.halo );
    const int rasterX = tile.xOffset + cx;
    const int rasterY = tile.yOffset + cy;
    double sum = 0.0;
    double sumSq = 0.0;
    int count = 0;
    for ( int dy = -half; dy <= half; ++dy )
    {
        // The full-frame kernels compute window statistics over the
        // rect CLAMPED to the raster (localStats): border windows have
        // fewer samples. Halo positions outside the raster are
        // edge-replicated fill and must NOT be counted (review P1).
        if ( rasterY + dy < 0 || ( tile.rasterHeight > 0 && rasterY + dy >= tile.rasterHeight ) )
            continue;
        const float *row = center + dy * bufW; // signed offset: dy may be negative
        for ( int dx = -half; dx <= half; ++dx )
        {
            if ( rasterX + dx < 0 || ( tile.rasterWidth > 0 && rasterX + dx >= tile.rasterWidth ) )
                continue;
            const float v = row[dx];
            if ( std::isfinite( v ) )
            {
                sum += v;
                sumSq += static_cast<double>( v ) * v;
                ++count;
            }
        }
    }
    if ( count <= 0 )
    {
        mean = 0.0f;
        variance = 0.0f;
        return;
    }
    mean = static_cast<float>( sum / count );
    variance = static_cast<float>( sumSq / count - mean * mean );
    if ( variance < 0.0f )
        variance = 0.0f;
}

// Separable convolution over the halo tile: formula replica of
// image_enhancement.cpp's separableConvolve restricted to the tile core (the
// halo provides every neighbour the two passes read).
void separableConvolveTile( const GdalBlockStream::Tile &tile, const float *haloBuf,
                            float *coreOut, const float *kernel1D, int kernelSize )
{
    const int half = kernelSize / 2;
    const int bufW = tile.bufferWidth;
    const int bufH = tile.bufferHeight;
    const int halo = tile.halo;

    // Zero-sum derivative kernels must not be weight-renormalized (see
    // ImageEnhancement::convolve); averaging kernels renormalize over finite
    // neighbours.
    float kernelSum = 0.0f;
    for ( int i = 0; i < kernelSize; ++i )
        kernelSum += kernel1D[i];
    const bool isAveragingKernel = kernelSum > 1e-6f;

    // Horizontal pass: core columns × all buffer rows (the vertical pass also
    // reads the halo rows' results).
    std::vector<float> temp( static_cast<size_t>( bufW ) * bufH );
    for ( int by = 0; by < bufH; ++by )
    {
        const float *row = haloBuf + static_cast<size_t>( by ) * bufW;
        float *tempRow = temp.data() + static_cast<size_t>( by ) * bufW;
        for ( int cx = 0; cx < tile.width; ++cx )
        {
            const int bx = cx + halo;
            // NoData in -> NoData out (preserve mask)
            if ( !std::isfinite( row[bx] ) )
            {
                tempRow[bx] = quietNan();
                continue;
            }
            float sum = 0.0f;
            float wSum = 0.0f;
            bool hasFinite = false;
            for ( int k = -half; k <= half; ++k )
            {
                const float val = row[bx + k];
                if ( std::isfinite( val ) )
                {
                    const float w = kernel1D[k + half];
                    sum += val * w;
                    wSum += w;
                    hasFinite = true;
                }
            }
            if ( isAveragingKernel )
                tempRow[bx] = ( wSum > 1e-6f ) ? ( sum / wSum ) : quietNan();
            else
                tempRow[bx] = hasFinite ? sum : quietNan();
        }
    }

    // Vertical pass: core region only.
    for ( int y = 0; y < tile.height; ++y )
    {
        for ( int cx = 0; cx < tile.width; ++cx )
        {
            const int bx = cx + halo;
            const int by = y + halo;
            if ( !std::isfinite( haloBuf[static_cast<size_t>( by ) * bufW + bx] ) )
            {
                coreOut[static_cast<size_t>( y ) * tile.width + cx] = quietNan();
                continue;
            }
            float sum = 0.0f;
            float wSum = 0.0f;
            bool hasFinite = false;
            for ( int k = -half; k <= half; ++k )
            {
                const float val = temp[static_cast<size_t>( by + k ) * bufW + bx];
                if ( std::isfinite( val ) )
                {
                    const float w = kernel1D[k + half];
                    sum += val * w;
                    wSum += w;
                    hasFinite = true;
                }
            }
            float &out = coreOut[static_cast<size_t>( y ) * tile.width + cx];
            if ( isAveragingKernel )
                out = ( wSum > 1e-6f ) ? ( sum / wSum ) : quietNan();
            else
                out = hasFinite ? sum : quietNan();
        }
    }
}

/// Replica of image_enhancement.cpp's private generateGaussianKernel1D.
void gaussianKernel1D( float *kernel, int size, float sigma )
{
    if ( sigma <= 0.0f )
        sigma = 1e-6f;
    const int half = size / 2;
    float sum = 0.0f;
    const float twoSigmaSq = 2.0f * sigma * sigma;
    for ( int i = -half; i <= half; ++i )
    {
        const float val = std::exp( -( i * i ) / twoSigmaSq );
        kernel[i + half] = val;
        sum += val;
    }
    for ( int i = 0; i < size; ++i )
        kernel[i] /= sum;
}

/// Replica of histogramEqualize's clamped bin computation (float arithmetic).
int equalizeBinOf( float v, float minVal, float binWidth, int bins )
{
    int bin = static_cast<int>( ( v - minVal ) / binWidth );
    if ( bin >= bins )
        bin = bins - 1;
    if ( bin < 0 )
        bin = 0;
    return bin;
}

} // namespace

// ---------------------------------------------------------------------------
// Halo-tile windowed filtering
// ---------------------------------------------------------------------------

bool streamBandWindowed( const GdalDatasetWrapper &src, int bandNum,
                         GdalStreamingOutput &dst, int tileDim, int halo,
                         const WindowedTileFn &fn )
{
    if ( tileDim < 1 || halo < 0 )
        return false;
    GdalBlockStream stream( src, bandNum, tileDim, tileDim, halo );
    std::vector<float> core( static_cast<size_t>( tileDim ) * tileDim );
    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *haloBuf ) {
        fn( tile, haloBuf, core.data() );
        return dst.writeTile( bandNum, tile, core.data() );
    } );
    return ok;
}

void speckleTileLee( const GdalBlockStream::Tile &tile, const float *haloBuf,
                     float *coreOut, int kernelSize, float noiseVariance )
{
    const int bufW = tile.bufferWidth;
    const int halo = tile.halo;
    for ( int y = 0; y < tile.height; ++y )
    {
        for ( int x = 0; x < tile.width; ++x )
        {
            const float pixel = haloBuf[static_cast<size_t>( y + halo ) * bufW + ( x + halo )];
            float &out = coreOut[static_cast<size_t>( y ) * tile.width + x];
            // Preserve NaN center pixels
            if ( std::isnan( pixel ) )
            {
                out = pixel;
                continue;
            }
            float mean = 0.0f;
            float localVar = 0.0f;
            windowStats( tile, haloBuf, x, y, kernelSize, mean, localVar );
            if ( localVar <= 0.0f || mean <= 0.0f )
            {
                out = mean;
            }
            else
            {
                const float cuSq = noiseVariance;
                const float clSq = localVar / ( mean * mean );
                // LEE's additive-noise weight (#678, review P0): the
                // /(1+Cu^2) denominator is KUAN's — it made Lee and Kuan
                // bit-identical. Mirror the corrected full-frame kernel.
                const float weight = ( clSq <= cuSq ) ? 0.0f : std::max( 0.0f, 1.0f - cuSq / clSq );
                out = mean + weight * ( pixel - mean );
            }
        }
    }
}

void speckleTileFrost( const GdalBlockStream::Tile &tile, const float *haloBuf,
                       float *coreOut, int kernelSize, float damping )
{
    const int half = kernelSize / 2;
    const int bufW = tile.bufferWidth;
    const int halo = tile.halo;

    // Precompute distance lookup table (only depends on kernel geometry), as
    // frostFilter does.
    std::vector<float> distLut( static_cast<size_t>( kernelSize ) * kernelSize );
    for ( int dy = -half; dy <= half; ++dy )
    {
        for ( int dx = -half; dx <= half; ++dx )
        {
            distLut[static_cast<size_t>( dy + half ) * kernelSize + ( dx + half )] =
                std::sqrt( static_cast<float>( dx * dx + dy * dy ) );
        }
    }

    for ( int y = 0; y < tile.height; ++y )
    {
        for ( int x = 0; x < tile.width; ++x )
        {
            const float pixel = haloBuf[static_cast<size_t>( y + halo ) * bufW + ( x + halo )];
            float &out = coreOut[static_cast<size_t>( y ) * tile.width + x];
            // Preserve NaN center pixels
            if ( std::isnan( pixel ) )
            {
                out = pixel;
                continue;
            }
            float mean = 0.0f;
            float localVar = 0.0f;
            windowStats( tile, haloBuf, x, y, kernelSize, mean, localVar );
            // Coefficient of variation squared
            const float cvSq = ( mean > 0.0f ) ? ( localVar / ( mean * mean ) ) : 0.0f;

            double sumWeighted = 0.0;
            double sumWeight = 0.0;
            for ( int dy = -half; dy <= half; ++dy )
            {
                const float *row = haloBuf + static_cast<size_t>( y + halo + dy ) * bufW + ( x + halo );
                for ( int dx = -half; dx <= half; ++dx )
                {
                    const float neighbor = row[dx];
                    // Skip NaN neighbours
                    if ( std::isnan( neighbor ) )
                        continue;
                    // Weight: exp(-damping * distance * CV²)
                    const float dist = distLut[static_cast<size_t>( dy + half ) * kernelSize + ( dx + half )];
                    const float weight = std::exp( -damping * dist * cvSq );
                    sumWeighted += weight * neighbor;
                    sumWeight += weight;
                }
            }
            out = ( sumWeight > 0.0 ) ? static_cast<float>( sumWeighted / sumWeight ) : mean;
        }
    }
}

void speckleTileKuan( const GdalBlockStream::Tile &tile, const float *haloBuf,
                      float *coreOut, int kernelSize, float noiseVariance )
{
    const int bufW = tile.bufferWidth;
    const int halo = tile.halo;
    for ( int y = 0; y < tile.height; ++y )
    {
        for ( int x = 0; x < tile.width; ++x )
        {
            const float pixel = haloBuf[static_cast<size_t>( y + halo ) * bufW + ( x + halo )];
            float &out = coreOut[static_cast<size_t>( y ) * tile.width + x];
            // Preserve NaN center pixels
            if ( std::isnan( pixel ) )
            {
                out = pixel;
                continue;
            }
            float mean = 0.0f;
            float localVar = 0.0f;
            windowStats( tile, haloBuf, x, y, kernelSize, mean, localVar );
            if ( localVar <= 0.0f || mean <= 0.0f )
            {
                out = mean;
                continue;
            }
            // Cu²: noise variance; Cl²: local coefficient of variation squared
            const float cuSq = noiseVariance;
            const float clSq = localVar / ( mean * mean );
            float weight = 0.0f;
            if ( clSq > cuSq )
                weight = ( 1.0f - cuSq / clSq ) / ( 1.0f + cuSq );
            out = mean + weight * ( pixel - mean );
        }
    }
}

void speckleTileGammaMap( const GdalBlockStream::Tile &tile, const float *haloBuf,
                          float *coreOut, int kernelSize, float noiseVariance )
{
    const int bufW = tile.bufferWidth;
    const int halo = tile.halo;
    for ( int y = 0; y < tile.height; ++y )
    {
        for ( int x = 0; x < tile.width; ++x )
        {
            const float pixel = haloBuf[static_cast<size_t>( y + halo ) * bufW + ( x + halo )];
            float &out = coreOut[static_cast<size_t>( y ) * tile.width + x];
            // Preserve NaN center pixels
            if ( std::isnan( pixel ) )
            {
                out = pixel;
                continue;
            }
            float mean = 0.0f;
            float localVar = 0.0f;
            windowStats( tile, haloBuf, x, y, kernelSize, mean, localVar );
            if ( localVar <= 0.0f || mean <= 0.0f )
            {
                out = mean;
                continue;
            }
            const float cuSq = noiseVariance;
            const float clSq = localVar / ( mean * mean );
            if ( clSq <= cuSq )
            {
                // Homogeneous region — smooth fully
                out = mean;
            }
            else
            {
                // Heterogeneous region — preserve structure
                const float alpha = ( 1.0f + cuSq ) / ( clSq - cuSq );
                if ( alpha <= 1e-6f )
                {
                    // Extreme variance / point target -> preserve original pixel
                    out = pixel;
                }
                else
                {
                    // MAP estimate
                    const float a = alpha;
                    const float b = ( alpha - 1.0f ) * mean;
                    float discriminant = b * b + 4.0f * a * pixel;
                    if ( discriminant < 0.0f )
                        discriminant = 0.0f;
                    out = ( b + std::sqrt( discriminant ) ) / ( 2.0f * a );
                }
            }
        }
    }
}

void convolveTileMean( const GdalBlockStream::Tile &tile, const float *haloBuf,
                       float *coreOut, int kernelSize )
{
    // Mean filter is separable: 1D kernel is [1/n, ..., 1/n]
    std::vector<float> kernel1D( static_cast<size_t>( kernelSize ), 1.0f / static_cast<float>( kernelSize ) );
    separableConvolveTile( tile, haloBuf, coreOut, kernel1D.data(), kernelSize );
}

void convolveTileGaussian( const GdalBlockStream::Tile &tile, const float *haloBuf,
                           float *coreOut, int kernelSize, float sigma )
{
    if ( kernelSize < 1 )
        kernelSize = 1;
    if ( kernelSize % 2 == 0 )
        kernelSize++; // Force odd
    std::vector<float> kernel1D( static_cast<size_t>( kernelSize ) );
    gaussianKernel1D( kernel1D.data(), kernelSize, sigma );
    separableConvolveTile( tile, haloBuf, coreOut, kernel1D.data(), kernelSize );
}

void convolveTileMedian( const GdalBlockStream::Tile &tile, const float *haloBuf,
                         float *coreOut, int kernelSize )
{
    const int half = kernelSize / 2;
    const int bufW = tile.bufferWidth;
    const int halo = tile.halo;
    std::vector<float> validNeighbors;
    validNeighbors.reserve( static_cast<size_t>( kernelSize ) * kernelSize );

    for ( int y = 0; y < tile.height; ++y )
    {
        for ( int x = 0; x < tile.width; ++x )
        {
            const float pixel = haloBuf[static_cast<size_t>( y + halo ) * bufW + ( x + halo )];
            float &out = coreOut[static_cast<size_t>( y ) * tile.width + x];
            if ( !std::isfinite( pixel ) )
            {
                out = quietNan();
                continue;
            }
            validNeighbors.clear();
            for ( int dy = -half; dy <= half; ++dy )
            {
                const float *row = haloBuf + static_cast<size_t>( y + halo + dy ) * bufW + ( x + halo );
                for ( int dx = -half; dx <= half; ++dx )
                {
                    const float v = row[dx];
                    if ( std::isfinite( v ) )
                        validNeighbors.push_back( v );
                }
            }
            if ( validNeighbors.empty() )
            {
                out = quietNan();
            }
            else
            {
                const size_t mid = validNeighbors.size() / 2;
                std::nth_element( validNeighbors.begin(),
                                  validNeighbors.begin() + static_cast<std::ptrdiff_t>( mid ),
                                  validNeighbors.end() );
                out = validNeighbors[mid];
            }
        }
    }
}

void convolveTileSobel( const GdalBlockStream::Tile &tile, const float *haloBuf,
                        float *coreOut )
{
    const int bufW = tile.bufferWidth;
    const int halo = tile.halo;
    // Closed-form 3×3 Sobel gradient magnitude; the halo's replicate-clamped
    // borders reproduce the full-frame kernel's explicit border branches.
    for ( int y = 0; y < tile.height; ++y )
    {
        for ( int x = 0; x < tile.width; ++x )
        {
            const int bx = x + halo;
            const int by = y + halo;
            const float p00 = haloBuf[static_cast<size_t>( by - 1 ) * bufW + bx - 1];
            const float p01 = haloBuf[static_cast<size_t>( by - 1 ) * bufW + bx];
            const float p02 = haloBuf[static_cast<size_t>( by - 1 ) * bufW + bx + 1];
            const float p10 = haloBuf[static_cast<size_t>( by ) * bufW + bx - 1];
            const float p12 = haloBuf[static_cast<size_t>( by ) * bufW + bx + 1];
            const float p20 = haloBuf[static_cast<size_t>( by + 1 ) * bufW + bx - 1];
            const float p21 = haloBuf[static_cast<size_t>( by + 1 ) * bufW + bx];
            const float p22 = haloBuf[static_cast<size_t>( by + 1 ) * bufW + bx + 1];
            const float gx = ( p02 + 2.0f * p12 + p22 ) - ( p00 + 2.0f * p10 + p20 );
            const float gy = ( p20 + 2.0f * p21 + p22 ) - ( p00 + 2.0f * p01 + p02 );
            coreOut[static_cast<size_t>( y ) * tile.width + x] = std::sqrt( gx * gx + gy * gy );
        }
    }
}

void convolveTileLaplacian( const GdalBlockStream::Tile &tile, const float *haloBuf,
                            float *coreOut )
{
    const int bufW = tile.bufferWidth;
    const int halo = tile.halo;
    // up + left + right + down - 4·centre (same operand order as every border
    // branch of the full-frame laplacianFilter).
    for ( int y = 0; y < tile.height; ++y )
    {
        for ( int x = 0; x < tile.width; ++x )
        {
            const int bx = x + halo;
            const int by = y + halo;
            const float *curr = haloBuf + static_cast<size_t>( by ) * bufW + bx;
            const float val = curr[-bufW] + curr[-1] + curr[1] + curr[bufW] - 4.0f * curr[0];
            coreOut[static_cast<size_t>( y ) * tile.width + x] = val;
        }
    }
}

// ---------------------------------------------------------------------------
// Streaming band statistics
// ---------------------------------------------------------------------------

float populationStddev( double centeredSumSq, size_t validCount )
{
    if ( validCount <= 1 )
        return 0.0f;
    return static_cast<float>( std::sqrt( centeredSumSq / validCount ) );
}

// ---------------------------------------------------------------------------
// Percent-clip endpoint selection
// ---------------------------------------------------------------------------

void PercentClipSelector::beginHistogram( float minVal, float maxVal )
{
    m_minVal = minVal;
    m_maxVal = maxVal;
    m_minD = static_cast<double>( minVal );
    m_step = ( static_cast<double>( maxVal ) - m_minD ) / kBins;
    m_hist.assign( static_cast<size_t>( kBins ), 0 );
    m_degenerate = !( m_step > 0.0 );
    m_needsRefinement = false;
    m_binLo = -1;
    m_binHi = -1;
    m_cumLo = 0;
    m_cumHi = 0;
    m_valsLo.clear();
    m_valsHi.clear();
}

int PercentClipSelector::binOf( float v ) const
{
    const int bin = static_cast<int>( ( static_cast<double>( v ) - m_minD ) / m_step );
    return std::clamp( bin, 0, kBins - 1 );
}

void PercentClipSelector::addValue( float v )
{
    if ( m_degenerate )
        return;
    ++m_hist[static_cast<size_t>( binOf( v ) )];
}

void PercentClipSelector::finalize( size_t validCount, float pct )
{
    // Same rank arithmetic as percentClipStretch, float truncation included.
    const size_t clipCount =
        static_cast<size_t>( static_cast<float>( validCount ) * pct / 100.0f );
    if ( validCount == 0 || m_degenerate )
    {
        m_endpoints = { m_minVal, m_maxVal };
        m_needsRefinement = false;
        return;
    }
    const size_t lo = clipCount;
    const size_t hi = validCount - 1 - clipCount;
    if ( hi <= lo )
    {
        // Degenerate clip: the full-frame path falls back to the extremes.
        m_endpoints = { m_minVal, m_maxVal };
        return;
    }
    // Walk the cumulative histogram to the bins containing both ranks. Bin
    // edges are exact double comparisons, so this partitions [min, max].
    uint64_t cum = 0;
    for ( int k = 0; k < kBins; ++k )
    {
        const uint64_t count = m_hist[static_cast<size_t>( k )];
        if ( m_binLo < 0 && cum + count > static_cast<uint64_t>( lo ) )
        {
            m_binLo = k;
            m_cumLo = static_cast<size_t>( cum );
        }
        if ( m_binHi < 0 && cum + count > static_cast<uint64_t>( hi ) )
        {
            m_binHi = k;
            m_cumHi = static_cast<size_t>( cum );
            break;
        }
        cum += count;
    }
    if ( m_binLo < 0 || m_binHi < 0 )
    {
        // Unreachable with consistent counts; stay at the extremes.
        m_endpoints = { m_minVal, m_maxVal };
        return;
    }
    m_loRank = lo;
    m_hiRank = hi;
    m_needsRefinement = true;
}

void PercentClipSelector::collectValue( float v )
{
    if ( !m_needsRefinement )
        return;
    const int bin = binOf( v );
    if ( bin == m_binLo )
        m_valsLo.push_back( v );
    if ( bin == m_binHi && bin != m_binLo )
        m_valsHi.push_back( v );
}

PercentileEndpoints PercentClipSelector::endpoints() const
{
    if ( !m_needsRefinement )
        return m_endpoints;
    const auto nthOf = []( std::vector<float> &vals, size_t rank, float fallback ) {
        if ( vals.empty() || rank >= vals.size() || vals.size() > kRefineCap )
            return fallback;
        std::nth_element( vals.begin(), vals.begin() + static_cast<std::ptrdiff_t>( rank ),
                          vals.end() );
        return vals[rank];
    };
    PercentileEndpoints out;
    if ( m_binLo == m_binHi )
    {
        // Both ranks in one bin: capture lo before the second nth_element
        // reorders the vector.
        std::vector<float> vals = m_valsLo;
        out.lo = nthOf( vals, m_loRank - m_cumLo, binLowEdge( m_binLo ) );
        out.hi = nthOf( vals, m_hiRank - m_cumLo, binLowEdge( m_binLo ) );
    }
    else
    {
        std::vector<float> loVals = m_valsLo;
        std::vector<float> hiVals = m_valsHi;
        out.lo = nthOf( loVals, m_loRank - m_cumLo, binLowEdge( m_binLo ) );
        out.hi = nthOf( hiVals, m_hiRank - m_cumHi, binLowEdge( m_binHi ) );
    }
    return out;
}

// ---------------------------------------------------------------------------
// Stretch tile kernels
// ---------------------------------------------------------------------------

void linearStretchApplyTile( const float *in, float *out, size_t n,
                             float minVal, float maxVal, float nodata )
{
    float range = maxVal - minVal;
    if ( range == 0 )
        range = 1.0f;

    for ( size_t i = 0; i < n; i++ )
    {
        if ( in[i] == nodata || std::isnan( in[i] ) )
        {
            out[i] = nodata;
            continue;
        }
        float normalized = ( in[i] - minVal ) / range;
        out[i] = std::clamp( normalized * 255.0f, 0.0f, 255.0f );
    }
}

void piecewiseLinearStretchTile( const float *in, float *out, size_t n,
                                 const std::vector<std::pair<float, float>> &controlPoints,
                                 float nodata )
{
    if ( controlPoints.size() < 2 )
    {
        for ( size_t i = 0; i < n; i++ )
        {
            out[i] = ( in[i] == nodata || std::isnan( in[i] ) ) ? nodata : in[i];
        }
        return;
    }

    for ( size_t i = 0; i < n; i++ )
    {
        const float val = in[i];
        if ( val == nodata || std::isnan( val ) )
        {
            out[i] = nodata;
            continue;
        }
        if ( val <= controlPoints.front().first )
        {
            out[i] = controlPoints.front().second;
            continue;
        }
        if ( val >= controlPoints.back().first )
        {
            out[i] = controlPoints.back().second;
            continue;
        }
        for ( size_t p = 0; p < controlPoints.size() - 1; p++ )
        {
            const float x1 = controlPoints[p].first;
            const float y1 = controlPoints[p].second;
            const float x2 = controlPoints[p + 1].first;
            const float y2 = controlPoints[p + 1].second;
            if ( val >= x1 && val <= x2 )
            {
                const float dx = std::max( 1e-6f, x2 - x1 );
                const float ratio = ( val - x1 ) / dx;
                out[i] = y1 + ratio * ( y2 - y1 );
                break;
            }
        }
    }
}

void histogramEqualizeApplyTile( const float *in, float *out, size_t n,
                                 float minVal, float binWidth,
                                 const std::vector<float> &cdf, int bins,
                                 float cdfMin, float denom, float nodata )
{
    for ( size_t i = 0; i < n; i++ )
    {
        if ( in[i] == nodata || std::isnan( in[i] ) )
        {
            out[i] = nodata;
            continue;
        }
        const int bin = equalizeBinOf( in[i], minVal, binWidth, bins );
        if ( denom < 1e-6f )
        {
            out[i] = 128.0f;
        }
        else
        {
            out[i] = ( cdf[static_cast<size_t>( bin )] - cdfMin ) / denom * 255.0f;
        }
    }
}

void bandRatioTile( const float *band1, const float *band2, float *out, size_t count )
{
    for ( size_t i = 0; i < count; ++i )
        out[i] = MathUtils::safeDiv( band1[i], band2[i] );
}

void ihsTransformTile( const float *bip3, const float nodataR, const float nodataG,
                       const float nodataB, float *outI, float *outH, float *outS,
                       size_t n )
{
    for ( size_t i = 0; i < n; ++i )
    {
        const float rv = bip3[i * 3];
        const float gv = bip3[i * 3 + 1];
        const float bv = bip3[i * 3 + 2];
        // Mask invalid / NoData pixels: non-finite or declared NoData
        if ( !std::isfinite( rv ) || !std::isfinite( gv ) || !std::isfinite( bv ) ||
             rv == nodataR || gv == nodataG || bv == nodataB )
        {
            outI[i] = quietNan();
            outH[i] = quietNan();
            outS[i] = quietNan();
            continue;
        }
        ImageEnhancement::rgbToIhs( rv, gv, bv, outI[i], outH[i], outS[i] );
    }
}

// ---------------------------------------------------------------------------
// Streaming stretch driver
// ---------------------------------------------------------------------------

bool streamBandStretch( const GdalDatasetWrapper &src, int bandNum, float nodata,
                        const StretchParams &params, GdalStreamingOutput &dst,
                        int tileDim, QString *errorMessage )
{
    const auto fail = [&]( const char *what ) {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "%1 (band %2)" )
                                .arg( QString::fromUtf8( what ) )
                                .arg( bandNum );
        return false;
    };

    // Pass 1 — streaming statistics: exact replication of
    // MathUtils::computeStatsWithNodata (first pass) in raster order.
    StreamingBandStats stats( nodata );
    if ( params.kind != StretchKind::Piecewise )
    {
        GdalBlockStream stream( src, bandNum, tileDim, tileDim, 0 );
        const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *px ) {
            const size_t n = static_cast<size_t>( tile.width ) * tile.height;
            for ( size_t i = 0; i < n; ++i )
                stats.add( px[i] );
            return true;
        } );
        if ( !ok )
            return fail( "Failed to read band for stretch statistics" );
    }

    const float mean = stats.mean();

    // Stretch bounds / equalize state, resolved per kind before the apply pass.
    float loVal = 0.0f;
    float hiVal = 0.0f;
    bool allNodata = false; // percentClip/equalize write all-nodata for an
                            // all-invalid band (linearStretch does not)
    float eqMin = 0.0f;
    float eqBinWidth = 1.0f;
    std::vector<float> eqCdf;
    float eqCdfMin = 0.0f;
    float eqDenom = 1.0f;
    bool eqConstant = false; // valid data with min == max

    switch ( params.kind )
    {
        case StretchKind::Linear:
        {
            // Dialog convention: an all-invalid band stretches over 0..0.
            loVal = stats.foundValid() ? stats.min() : 0.0f;
            hiVal = stats.foundValid() ? stats.max() : 0.0f;
            break;
        }
        case StretchKind::StdDev:
        {
            // Second statistics pass: Σ(v - mean)² with the float mean, in
            // raster order (computeStatsWithNodata's second pass).
            StreamingCenteredSq centered;
            GdalBlockStream stream( src, bandNum, tileDim, tileDim, 0 );
            const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *px ) {
                const size_t n = static_cast<size_t>( tile.width ) * tile.height;
                for ( size_t i = 0; i < n; ++i )
                {
                    const float v = px[i];
                    if ( std::isfinite( v ) && v != nodata )
                        centered.add( v, mean );
                }
                return true;
            } );
            if ( !ok )
                return fail( "Failed to read band for stddev pass" );
            const float stddev = populationStddev( centered.sumSq(), stats.validCount() );
            loVal = mean - params.stddevK * stddev;
            hiVal = mean + params.stddevK * stddev;
            break;
        }
        case StretchKind::PercentClip:
        {
            if ( stats.foundValid() )
            {
                // Histogram pass + exact in-bin refinement of the two sorted
                // endpoints (percentClipStretch materialized and sorted every
                // valid value; this selects the same two order statistics).
                PercentClipSelector selector;
                selector.beginHistogram( stats.min(), stats.max() );
                GdalBlockStream stream( src, bandNum, tileDim, tileDim, 0 );
                bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *px ) {
                    const size_t n = static_cast<size_t>( tile.width ) * tile.height;
                    for ( size_t i = 0; i < n; ++i )
                    {
                        const float v = px[i];
                        if ( std::isfinite( v ) && v != nodata )
                            selector.addValue( v );
                    }
                    return true;
                } );
                if ( !ok )
                    return fail( "Failed to read band for percent-clip histogram" );
                selector.finalize( stats.validCount(), params.clipPercent );
                if ( selector.needsRefinement() )
                {
                    GdalBlockStream refineStream( src, bandNum, tileDim, tileDim, 0 );
                    ok = refineStream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *px ) {
                        const size_t n = static_cast<size_t>( tile.width ) * tile.height;
                        for ( size_t i = 0; i < n; ++i )
                        {
                            const float v = px[i];
                            if ( std::isfinite( v ) && v != nodata )
                                selector.collectValue( v );
                        }
                        return true;
                    } );
                    if ( !ok )
                        return fail( "Failed to read band for percent-clip refinement" );
                }
                const PercentileEndpoints ep = selector.endpoints();
                loVal = ep.lo;
                hiVal = ep.hi;
            }
            else
            {
                // percentClipStretch: no valid values -> all-nodata output.
                allNodata = true;
            }
            break;
        }
        case StretchKind::HistogramEqualize:
        {
            if ( !stats.foundValid() )
            {
                // histogramEqualize: no valid values -> all-nodata output.
                allNodata = true;
                break;
            }
            if ( stats.min() == stats.max() )
            {
                eqConstant = true;
                break;
            }
            eqMin = stats.min();
            eqBinWidth = ( stats.max() - stats.min() ) / kEqualizeBins;
            std::vector<uint64_t> hist( static_cast<size_t>( kEqualizeBins ), 0 );
            GdalBlockStream stream( src, bandNum, tileDim, tileDim, 0 );
            const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *px ) {
                const size_t n = static_cast<size_t>( tile.width ) * tile.height;
                for ( size_t i = 0; i < n; ++i )
                {
                    const float v = px[i];
                    if ( v == nodata || std::isnan( v ) )
                        continue;
                    ++hist[static_cast<size_t>( equalizeBinOf( v, eqMin, eqBinWidth, kEqualizeBins ) )];
                }
                return true;
            } );
            if ( !ok )
                return fail( "Failed to read band for equalization histogram" );
            // CDF in float, same accumulation order as histogramEqualize.
            uint64_t histValid = 0;
            for ( int i = 0; i < kEqualizeBins; ++i )
                histValid += hist[static_cast<size_t>( i )];
            eqCdf.assign( static_cast<size_t>( kEqualizeBins ), 0.0f );
            eqCdf[0] = static_cast<float>( hist[0] ) / histValid;
            for ( int i = 1; i < kEqualizeBins; ++i )
                eqCdf[static_cast<size_t>( i )] = eqCdf[static_cast<size_t>( i - 1 )]
                                                + static_cast<float>( hist[static_cast<size_t>( i )] ) / histValid;
            for ( int i = 0; i < kEqualizeBins; ++i )
            {
                if ( hist[static_cast<size_t>( i )] > 0 )
                {
                    eqCdfMin = eqCdf[static_cast<size_t>( i )];
                    break;
                }
            }
            eqDenom = 1.0f - eqCdfMin;
            break;
        }
        case StretchKind::Piecewise:
            break; // per-pixel, no statistics needed
    }

    // Apply pass — stream tiles, remap, write straight to the output raster.
    GdalBlockStream stream( src, bandNum, tileDim, tileDim, 0 );
    std::vector<float> out( static_cast<size_t>( tileDim ) * tileDim );
    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *px ) {
        const size_t n = static_cast<size_t>( tile.width ) * tile.height;
        float *outTile = out.data();
        if ( allNodata )
        {
            std::fill( outTile, outTile + n, nodata );
            return dst.writeTile( bandNum, tile, outTile );
        }
        switch ( params.kind )
        {
            case StretchKind::Linear:
            case StretchKind::PercentClip:
            case StretchKind::StdDev:
                linearStretchApplyTile( px, outTile, n, loVal, hiVal, nodata );
                break;
            case StretchKind::HistogramEqualize:
                if ( eqConstant )
                {
                    for ( size_t i = 0; i < n; ++i )
                        outTile[i] = ( px[i] == nodata || std::isnan( px[i] ) ) ? nodata : 128.0f;
                }
                else
                {
                    histogramEqualizeApplyTile( px, outTile, n, eqMin, eqBinWidth, eqCdf,
                                                kEqualizeBins, eqCdfMin, eqDenom, nodata );
                }
                break;
            case StretchKind::Piecewise:
                piecewiseLinearStretchTile( px, outTile, n, params.piecewisePoints, nodata );
                break;
        }
        return dst.writeTile( bandNum, tile, outTile );
    } );
    if ( !ok )
        return fail( "Failed to write stretched band" );
    return true;
}

} // namespace ImageEnhancementStreaming
