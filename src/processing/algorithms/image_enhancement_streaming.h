// image_enhancement_streaming.h — Tile-streaming variants of the
// ImageEnhancement kernels used by the GUI dialogs (#691 streaming goal).
//
// The speckle filter dialog, the image enhancement panel and the contrast
// stretch dialog used to materialize every input and output band as
// full-raster float frames (2×B frames ≈ 4.4 GiB at 12000² × 4 bands) plus a
// full-frame IntegralImage SAT (20 B/px ≈ 2.7 GiB) before filtering. This
// module re-expresses the same kernels as tile-window operations over
// GdalBlockStream / GdalMultibandBlockStream with O(tile) memory, plus a
// two-pass streaming statistics stage for the stretch paths.
//
// Numerical contract: every tile kernel below is a formula-for-formula
// replica of the corresponding full-frame kernel in image_enhancement.cpp
// (window statistics of lee/frost/kuan/gamma-map, the separable convolution
// renormalization, and the stretch remaps). Halo tiles pre-replicate raster
// borders exactly like the full-frame kernels' std::clamp border handling, so
// outputs match the full-frame path up to floating-point summation order
// (SAT prefix sums vs direct window sums — last-ULP differences only).
//
// Threading: streaming is single-threaded and sequential (GDAL reads are not
// concurrent-safe on one dataset), matching the GdalBlockStream contract.
#pragma once

#include "processing/gdal/gdal_block_stream.h" // for Tile geometry

#include <QString>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

class GdalDatasetWrapper;
class GdalStreamingOutput;

namespace ImageEnhancementStreaming
{

/// Tile dimension for the dialog streaming paths (framework default).
constexpr int kTileDim = 256;

// ---------------------------------------------------------------------------
// Halo-tile windowed filtering (SAR speckle + spatial filters)
// ---------------------------------------------------------------------------

/**
 * Kernel over one halo tile: @a haloBuf holds tile.bufferWidth ×
 * tile.bufferHeight floats (tile core at offset (halo, halo), border margin
 * replicate-clamped to the raster edge). The kernel writes tile.width *
 * tile.height filtered floats to @a coreOut (row-major, stride tile.width).
 */
using WindowedTileFn = std::function<void( const GdalBlockStream::Tile &tile,
                                           const float *haloBuf, float *coreOut )>;

/**
 * Stream one band of @a src through halo tiles, apply @a fn per tile and
 * write each tile core to band @a bandNum of @a dst. @a halo must be at
 * least the kernel radius. Returns false when a read or a tile write fails.
 */
bool streamBandWindowed( const GdalDatasetWrapper &src, int bandNum,
                         GdalStreamingOutput &dst, int tileDim, int halo,
                         const WindowedTileFn &fn );

// --- Tile-window speckle kernels: formula replicas of ImageEnhancement::
// --- leeFilter / frostFilter / kuanFilter / gammaMapFilter (the full-frame
// --- versions compute the identical window mean/variance via a summed-area
// --- table; here the window sum is accumulated directly over the halo tile).

void speckleTileLee( const GdalBlockStream::Tile &tile, const float *haloBuf,
                     float *coreOut, int kernelSize, float noiseVariance );
void speckleTileFrost( const GdalBlockStream::Tile &tile, const float *haloBuf,
                       float *coreOut, int kernelSize, float damping );
void speckleTileKuan( const GdalBlockStream::Tile &tile, const float *haloBuf,
                      float *coreOut, int kernelSize, float noiseVariance );
void speckleTileGammaMap( const GdalBlockStream::Tile &tile, const float *haloBuf,
                          float *coreOut, int kernelSize, float noiseVariance );

// --- Tile-window spatial filters: replicas of ImageEnhancement::meanFilter /
// --- gaussianFilter / medianFilter / sobelFilter / laplacianFilter.

void convolveTileMean( const GdalBlockStream::Tile &tile, const float *haloBuf,
                       float *coreOut, int kernelSize );
void convolveTileGaussian( const GdalBlockStream::Tile &tile, const float *haloBuf,
                           float *coreOut, int kernelSize, float sigma );
void convolveTileMedian( const GdalBlockStream::Tile &tile, const float *haloBuf,
                         float *coreOut, int kernelSize );
/// 3×3 Sobel gradient magnitude (kernelSize is fixed by the full-frame kernel).
void convolveTileSobel( const GdalBlockStream::Tile &tile, const float *haloBuf,
                        float *coreOut );
/// 4-neighbour Laplacian (kernelSize is fixed by the full-frame kernel).
void convolveTileLaplacian( const GdalBlockStream::Tile &tile, const float *haloBuf,
                            float *coreOut );

// ---------------------------------------------------------------------------
// Streaming band statistics
// ---------------------------------------------------------------------------

/**
 * Exact streaming replica of MathUtils::computeStatsWithNodata's first pass:
 * min/max/sum/validCount accumulated in raster order (tiles are visited
 * row-major, so the double-precision sum order — and therefore the bits —
 * match the full-frame path). Valid = finite && != nodata.
 */
class StreamingBandStats
{
  public:
    explicit StreamingBandStats( float nodata ) : m_nodata( nodata ) {}

    void add( float v )
    {
        if ( !std::isfinite( v ) || v == m_nodata )
            return;
        if ( !m_found )
        {
            m_min = v;
            m_max = v;
            m_found = true;
        }
        if ( v < m_min )
            m_min = v;
        if ( v > m_max )
            m_max = v;
        m_sum += v;
        ++m_valid;
    }

    bool foundValid() const { return m_found; }
    size_t validCount() const { return m_valid; }
    float min() const { return m_min; }
    float max() const { return m_max; }
    double sum() const { return m_sum; }

    /// float(sum/validCount) — identical to computeStatsWithNodata's mean.
    float mean() const { return m_found ? static_cast<float>( m_sum / m_valid ) : 0.0f; }

  private:
    float m_nodata = 0.0f;
    bool m_found = false;
    float m_min = 0.0f;
    float m_max = 0.0f;
    double m_sum = 0.0;
    size_t m_valid = 0;
};

/**
 * Accumulator for computeStatsWithNodata's second pass: the centered sum of
 * squares Σ(v - mean)² over valid pixels, in raster order (bit-exact).
 */
class StreamingCenteredSq
{
  public:
    void add( float v, float mean )
    {
        if ( !std::isfinite( v ) )
            return;
        const double diff = v - mean;
        m_sumSq += diff * diff;
    }
    double sumSq() const { return m_sumSq; }

  private:
    double m_sumSq = 0.0;
};

/// float(sqrt(sumSq/validCount)) when validCount > 1, else 0 — the
/// computeStatsWithNodata population-stddev formula.
float populationStddev( double centeredSumSq, size_t validCount );

// ---------------------------------------------------------------------------
// Percent-clip endpoint selection (exact percentClipStretch replica)
// ---------------------------------------------------------------------------

struct PercentileEndpoints
{
    float lo = 0.0f;
    float hi = 0.0f;
};

/**
 * Selects percentClipStretch's sorted-array endpoints (valid[lo], valid[hi])
 * without materializing the values: a 64 Ki-bin histogram over [min, max]
 * locates the bins holding the two ranks, then a refinement pass collects
 * only the values inside those bins for an exact in-bin selection. Bin
 * assignment uses exact double-edge comparisons, so the result equals the
 * full-frame std::sort index pair.
 */
class PercentClipSelector
{
  public:
    static constexpr int kBins = 65536;
    /// Safety cap on values collected per endpoint bin; beyond it the bin's
    /// lower edge is used (degenerate near-constant rasters only; the error
    /// is bounded by the bin width, range/65536).
    static constexpr size_t kRefineCap = 1UL << 22;

    void beginHistogram( float minVal, float maxVal );
    void addValue( float v );

    /// Compute the two ranks from the valid-pixel count and clip percentage
    /// (replicating percentClipStretch's size_t truncation and the
    /// hi <= lo degenerate case). Call after the histogram pass.
    void finalize( size_t validCount, float pct );

    bool needsRefinement() const { return m_needsRefinement; }
    /// Refinement pass: keep values falling in the two selected bins.
    void collectValue( float v );
    /// Endpoints after finalize() (and the refinement pass when needed).
    PercentileEndpoints endpoints() const;

  private:
    int binOf( float v ) const;
    float binLowEdge( int bin ) const
    {
        return static_cast<float>( m_minD + static_cast<double>( bin ) * m_step );
    }

    float m_minVal = 0.0f;
    float m_maxVal = 0.0f;
    double m_minD = 0.0;
    double m_step = 0.0;
    std::vector<uint64_t> m_hist;
    bool m_degenerate = true;
    bool m_needsRefinement = false;
    size_t m_loRank = 0;
    size_t m_hiRank = 0;
    int m_binLo = -1;
    int m_binHi = -1;
    size_t m_cumLo = 0;
    size_t m_cumHi = 0;
    std::vector<float> m_valsLo;
    std::vector<float> m_valsHi;
    PercentileEndpoints m_endpoints;
};

// ---------------------------------------------------------------------------
// Stretch tile kernels + streaming driver
// ---------------------------------------------------------------------------

enum class StretchKind
{
    Linear,
    PercentClip,
    StdDev,
    HistogramEqualize,
    Piecewise
};

struct StretchParams
{
    StretchKind kind = StretchKind::Linear;
    float clipPercent = 2.0f;                                  ///< PercentClip
    float stddevK = 2.0f;                                      ///< StdDev
    std::vector<std::pair<float, float>> piecewisePoints;      ///< Piecewise
};

/**
 * Two-pass streaming contrast stretch of one band: streaming statistics
 * (min/max, or mean/stddev, or histogram) followed by a streaming apply pass
 * written tile-by-tile to @a dst. Exact behavioural replica of the
 * ImageEnhancement::linearStretch / percentClipStretch / stddevStretch /
 * histogramEqualize / piecewiseLinearStretch pipeline used by the dialogs.
 *
 * @a nodata is the band's float-cast declared sentinel (NaN when undeclared);
 * sentinel/NaN pixels stay sentinel/NaN on output, matching the dialogs'
 * existing output convention.
 */
bool streamBandStretch( const GdalDatasetWrapper &src, int bandNum, float nodata,
                        const StretchParams &params, GdalStreamingOutput &dst,
                        int tileDim, QString *errorMessage = nullptr );

/// Tile-formula replica of ImageEnhancement::linearStretch.
void linearStretchApplyTile( const float *in, float *out, size_t n,
                             float minVal, float maxVal, float nodata );
/// Tile-formula replica of ImageEnhancement::piecewiseLinearStretch.
void piecewiseLinearStretchTile( const float *in, float *out, size_t n,
                                 const std::vector<std::pair<float, float>> &controlPoints,
                                 float nodata );
/// Apply pass of ImageEnhancement::histogramEqualize (cdf already built).
void histogramEqualizeApplyTile( const float *in, float *out, size_t n,
                                 float minVal, float binWidth,
                                 const std::vector<float> &cdf, int bins,
                                 float cdfMin, float denom, float nodata );
/// Tile-formula replica of ImageEnhancement::bandRatio.
void bandRatioTile( const float *band1, const float *band2, float *out, size_t count );

/**
 * Tile-formula replica of the image enhancement panel's per-pixel IHS
 * decomposition: a NaN in any band or a band value equal to its declared
 * sentinel yields NaN in all three components (the panel's #380 semantics).
 * @a bip3 is a 3-band BIP tile, out plane stride is n.
 */
void ihsTransformTile( const float *bip3, const float nodataR, const float nodataG,
                       const float nodataB, float *outI, float *outH, float *outS,
                       size_t n );

} // namespace ImageEnhancementStreaming
