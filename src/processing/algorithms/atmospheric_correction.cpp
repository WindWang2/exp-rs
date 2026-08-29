// src/processing/algorithms/atmospheric_correction.cpp — DOS atmospheric correction
#include "atmospheric_correction.h"
#include "math_utils.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "core/sicnu_logging.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFile>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace AtmosphericCorrection
{

static float findMin(const float *data, size_t count)
{
    float minVal = std::numeric_limits<float>::max();
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        if (!std::isnan(data[i])) {
            if (data[i] < minVal) {
                minVal = data[i];
                found = true;
            }
        }
    }
    return found ? minVal : 0.0f;
}

static bool convertAndFindMin(const float *dn, std::vector<float> &radiance,
                              size_t count, float gain, float bias, float &minRadiance)
{
    if (!dnToRadiance(dn, radiance.data(), count, gain, bias))
        return false;
    minRadiance = findMin(radiance.data(), count);
    return true;
}

bool dnToRadiance(const float *dn, float *radiance, size_t count, float gain, float bias)
{
    return MathUtils::linearScale(dn, radiance, count, gain, bias);
}

bool dos1(const float *dn, float *surface, size_t count, float gain, float bias)
{
    if (!dn || !surface || count == 0) return false;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "DOS1 atmospheric correction: %1 pixels, gain=%2, bias=%3" )
        .arg( count ).arg( gain ).arg( bias ) );

    std::vector<float> radiance(count);
    float minRadiance;
    if (!convertAndFindMin(dn, radiance, count, gain, bias, minRadiance))
        return false;

    for (size_t i = 0; i < count; i++) {
        surface[i] = std::isnan(radiance[i]) ? std::numeric_limits<float>::quiet_NaN() : (radiance[i] - minRadiance);
    }
    return true;
}

namespace {

/// Incremental percentile statistics over a streamed band (QUAC, #634).
///
/// Same two-pass pattern as DarkObjectStats: accumulateRange() over the
/// whole scene, prepareBins(), accumulateBins(), then percentile(q) walks
/// the histogram to the requested rank. Memory is O(bins); 65536 bins give
/// sub-permille rank resolution for QUAC's 1%/99% stretch on DN-scale data.
/// Non-finite values are invalid.
struct StreamingPercentiles
{
    explicit StreamingPercentiles( int bins = 65536 )
      : m_nbins( std::max( 16, std::min( bins, 1 << 20 ) ) )
    {
    }

    void accumulateRange( const float *data, size_t count )
    {
        if ( !data )
            return;
        for ( size_t i = 0; i < count; ++i ) {
            const float v = data[i];
            if ( !std::isfinite( v ) )
                continue;
            m_minVal = std::min( m_minVal, static_cast<double>( v ) );
            m_maxVal = std::max( m_maxVal, static_cast<double>( v ) );
            ++m_valid;
        }
    }

    bool prepareBins()
    {
        if ( m_valid == 0 )
            return false;
        if ( m_maxVal <= m_minVal ) {
            m_singleLevel = true;
            return false;
        }
        m_binWidth = ( m_maxVal - m_minVal ) / ( m_nbins - 1 );
        m_counts.assign( static_cast<size_t>( m_nbins ), 0 );
        return true;
    }

    void accumulateBins( const float *data, size_t count )
    {
        if ( !data || m_counts.empty() )
            return;
        for ( size_t i = 0; i < count; ++i ) {
            const float v = data[i];
            if ( !std::isfinite( v ) )
                continue;
            size_t b = static_cast<size_t>( ( v - m_minVal ) / m_binWidth );
            if ( b >= static_cast<size_t>( m_nbins ) )
                b = static_cast<size_t>( m_nbins ) - 1;
            ++m_counts[b];
        }
    }

    /// Value at rank q in [0,1] of the valid distribution (bin centre).
    float percentile( double q ) const
    {
        if ( m_valid == 0 )
            return 0.0f;
        if ( m_singleLevel || m_counts.empty() )
            return static_cast<float>( m_minVal );
        const double rank = q * static_cast<double>( m_valid - 1 );
        size_t cumulative = 0;
        for ( int b = 0; b < m_nbins; ++b ) {
            cumulative += m_counts[static_cast<size_t>( b )];
            if ( static_cast<double>( cumulative ) > rank )
                return static_cast<float>( m_minVal + ( b + 0.5 ) * m_binWidth );
        }
        return static_cast<float>( m_maxVal );
    }

    bool hasValid() const { return m_valid > 0; }

private:
    int m_nbins;
    double m_minVal = std::numeric_limits<double>::infinity();
    double m_maxVal = -std::numeric_limits<double>::infinity();
    double m_binWidth = 0.0;
    size_t m_valid = 0;
    bool m_singleLevel = false;
    std::vector<uint64_t> m_counts;
};

/// Incremental dark-object statistics (Chavez 1996).
///
/// Streams tile-by-tile: accumulateRange() over the whole scene, then
/// accumulateBins() (the binning needs the frozen min/max range), then ask for
/// darkLevel(). Memory is O(bins) regardless of scene size, so out-of-core
/// rasters can be corrected without loading the full band.
/// Non-finite values (NaN and ±inf) are treated as invalid radiance.
struct DarkObjectStats
{
    explicit DarkObjectStats( int bins = 1024 )
      : m_nbins( std::max( 16, std::min( bins, 1 << 20 ) ) )
    {
    }

    /// Pass 1: fold a buffer into the min/max/valid range.
    void accumulateRange( const float *data, size_t count )
    {
        if ( !data )
            return;
        for ( size_t i = 0; i < count; ++i ) {
            const float v = data[i];
            if ( !std::isfinite( v ) )
                continue;
            m_minVal = std::min( m_minVal, v );
            m_maxVal = std::max( m_maxVal, v );
            ++m_valid;
        }
    }

    /// Freeze the range and allocate bin counts. Returns false when there is
    /// no bin-able range (all-invalid scene or single-level scene).
    bool prepareBins()
    {
        if ( m_valid == 0 )
            return false;
        if ( m_maxVal == m_minVal ) {
            m_singleLevel = true;
            return false;
        }
        m_binWidth = ( m_maxVal - m_minVal ) / ( m_nbins - 1 );
        m_counts.assign( static_cast<size_t>( m_nbins ), 0 );
        return true;
    }

    /// Pass 2: fold a buffer into the bin counts (range must be frozen).
    void accumulateBins( const float *data, size_t count )
    {
        if ( !data || m_counts.empty() )
            return;
        for ( size_t i = 0; i < count; ++i ) {
            const float v = data[i];
            if ( !std::isfinite( v ) )
                continue;
            size_t b = static_cast<size_t>( ( v - m_minVal ) / m_binWidth );
            if ( b >= static_cast<size_t>( m_nbins ) )
                b = static_cast<size_t>( m_nbins ) - 1; // maxVal lands in the last bin
            ++m_counts[b];
        }
    }

    /// Dark-object level: the lowest bin holding at least 0.01% of the valid
    /// scene (min 2 pixels); sparser bins are sensor noise. Clamped to the
    /// scene maximum so the level can never overshoot the brightest pixel.
    /// Falls back to the global minimum for tiny scenes; 0.0 for empty input.
    float darkLevel() const
    {
        if ( m_valid == 0 )
            return 0.0f;
        if ( m_singleLevel )
            return m_minVal;
        const size_t threshold = std::max<size_t>( 2, m_valid / 10000 );
        for ( int b = 0; b < m_nbins; ++b ) {
            if ( m_counts[static_cast<size_t>( b )] >= threshold )
                return std::min( m_minVal + m_binWidth * ( static_cast<float>( b ) + 0.5f ), m_maxVal );
        }
        return m_minVal; // Tiny scene: fall back to the global minimum.
    }

    size_t validCount() const { return m_valid; }

  private:
    int m_nbins;
    float m_minVal = std::numeric_limits<float>::max();
    float m_maxVal = -std::numeric_limits<float>::max();
    float m_binWidth = 1.0f;
    size_t m_valid = 0;
    bool m_singleLevel = false;
    std::vector<size_t> m_counts;
};

} // namespace

float findDarkObjectByHistogram(const float *radiance, size_t count, int bins)
{
    if (!radiance || count == 0)
        return 0.0f;

    DarkObjectStats stats(bins);
    stats.accumulateRange(radiance, count);
    if (!stats.prepareBins())
        return stats.darkLevel(); // empty / single-level scene
    stats.accumulateBins(radiance, count);
    return stats.darkLevel();
}

bool dos1Histogram(const float *dn, float *surface, size_t count, float gain, float bias)
{
    if (!dn || !surface || count == 0) return false;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "DOS1 (histogram) atmospheric correction: %1 pixels, gain=%2, bias=%3" )
        .arg( count ).arg( gain ).arg( bias ) );

    std::vector<float> radiance(count);
    if (!dnToRadiance(dn, radiance.data(), count, gain, bias))
        return false;

    const float darkLevel = findDarkObjectByHistogram(radiance.data(), count);
    for (size_t i = 0; i < count; ++i)
        surface[i] = radiance[i] - darkLevel;
    return true;
}

bool dos2(const float *dn, float *surface, size_t count, float gain, float bias, float transmittance)
{
    if (!dn || !surface || count == 0) return false;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "DOS2 atmospheric correction: %1 pixels, transmittance=%2" )
        .arg( count ).arg( transmittance ) );
    if (std::isnan(transmittance) || transmittance <= 0.0f || transmittance > 1.0f) return false;

    std::vector<float> radiance(count);
    float pathRadiance;
    if (!convertAndFindMin(dn, radiance, count, gain, bias, pathRadiance))
        return false;

    for (size_t i = 0; i < count; i++) {
        surface[i] = std::isnan(radiance[i]) ? std::numeric_limits<float>::quiet_NaN() : ((radiance[i] - pathRadiance) / transmittance);
    }
    return true;
}

float estimateTransmittance(float airmass)
{
    if (std::isnan(airmass) || airmass <= 0.0f) return 0.0f;
    // Aerosol optical depth at ~550 nm for a clear atmosphere.
    // tau=0.1 is the standard DOS1 assumption (Chavez, 1996,
    // "Image-based atmospheric corrections — revisited and improved",
    // Photogrammetric Engineering & Remote Sensing 62(9):1025-1036).
    constexpr float tau = 0.1f;
    return std::exp(-tau * airmass);
}

// ---------------------------------------------------------------------------
// QUAC (Quick Atmospheric Correction) - Bernstein et al., 2008
// ---------------------------------------------------------------------------



bool quac(const float *const *dnBands, float *const *outBands,
          int bandCount, size_t pixels, QString *errorMessage)
{
    if (!dnBands || !outBands || bandCount < 2 || pixels == 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QUAC requires >= 2 bands and > 0 pixels");
        return false;
    }

    SICNU_LOG_INFO(SicnuLogTags::Algorithms,
                   QString("QUAC atmospheric correction: %1 bands, %2 pixels").arg(bandCount).arg(pixels));

    // Per-band 1st percentile (dark, path-radiance proxy) and 99th (bright).
    std::vector<float> dark(bandCount), bright(bandCount);
    for (int b = 0; b < bandCount; ++b) {
        if (!dnBands[b] || !outBands[b]) {
            if (errorMessage)
                *errorMessage = QStringLiteral("QUAC: null band buffer at index %1").arg(b);
            return false;
        }
        std::vector<float> valid;
        valid.reserve(pixels);
        for (size_t i = 0; i < pixels; ++i) {
            if (std::isfinite(dnBands[b][i]))
                valid.push_back(dnBands[b][i]);
        }
        if (valid.empty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("QUAC: band %1 has no valid pixels").arg(b + 1);
            return false;
        }
        // Single-copy optimization: partition once and derive both percentiles from the same scratch buffer.
        // Avoids the ~2.4 GB transient (2 full-band copies per band on large scenes).
        std::vector<float> scratch = valid;
        auto validEnd = std::partition(scratch.begin(), scratch.end(),
                                       [](float v) { return !std::isnan(v); });
        const size_t nValid = static_cast<size_t>(std::distance(scratch.begin(), validEnd));
        // Use nth_element twice on the same partitioned range (single allocation/copy).
        const size_t rankDark = static_cast<size_t>(1.0f / 100.0f * (nValid - 1));
        const size_t rankBright = static_cast<size_t>(99.0f / 100.0f * (nValid - 1));
        if (nValid == 0) {
            dark[b] = 0.0f;
            bright[b] = 0.0f;
        } else if (nValid == 1) {
            dark[b] = scratch[0];
            bright[b] = scratch[0];
        } else {
            std::nth_element(scratch.begin(), scratch.begin() + rankDark, validEnd);
            dark[b] = scratch[rankDark];
            std::nth_element(scratch.begin(), scratch.begin() + rankBright, validEnd);
            bright[b] = scratch[rankBright];
        }
    }

    // Scene-average bright reference (QUAC assumes ~average surface reflectance ~0.5).
    const float meanBright = std::accumulate(bright.begin(), bright.end(), 0.0f) / bandCount;
    const float meanDark = std::accumulate(dark.begin(), dark.end(), 0.0f) / bandCount;
    const float refRange = meanBright - meanDark;
    if (refRange <= 0.0f || meanBright <= 0.0f) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QUAC: degenerate image (zero dynamic range or all-dark scene)");
        return false;
    }

    for (int b = 0; b < bandCount; ++b) {
        const float range = bright[b] - dark[b];
        if (range <= 0.0f) {
            // Flat band: output zeros.
            std::fill(outBands[b], outBands[b] + pixels, 0.0f);
            continue;
        }
        // Scale so the bright percentile maps to ~0.5 reflectance, then stretch
        // by the scene-average ratio. gain = 0.5 * refRange / (range * meanBright)
        const float gain = 0.5f * refRange / (range * meanBright);
        const float offset = -dark[b] * gain;
        const float *src = dnBands[b];
        float *dst = outBands[b];
        for (size_t i = 0; i < pixels; ++i) {
            float v = std::isnan(src[i]) ? std::numeric_limits<float>::quiet_NaN()
                                         : gain * src[i] + offset;
            // Clamp only the negative side (#632): hard-clipping the upper
            // bound destroyed bright targets (clouds, bright soils with true
            // reflectance > 1 after the gain stretch), biasing downstream
            // ratios. Values above 1 are kept - QUAC is an approximation
            // (percentile stretch, not Bernstein endmember means); the
            // deviation is documented in the operator schema.
            if (!std::isnan(v))
                v = std::max(0.0f, v);
            dst[i] = v;
        }
    }
    return true;
}

bool processFileMultiBand(const QString &sourcePath, const QString &outputPath,
                          int method, QString *errorMessage,
                          const std::function<void(double, const QString &)> &progress)
{
    if (method != Method::Quac) {
        if (errorMessage)
            *errorMessage = QStringLiteral("processFileMultiBand: unsupported method %1").arg(method);
        return false;
    }

    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();
    const int bandCount = srcDataset.bandCount();
    if (bandCount < 2) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QUAC requires a multi-band raster (>= 2 bands)");
        return false;
    }

    // Streaming QUAC (#634): the old path materialized TWO full copies of
    // every band (2 x bandCount x W x H floats - ~200 GB at 50k x 50k x 10
    // bands). The transform is per-band independent, so each band streams
    // with O(tile) memory: two stats passes (range, then bins), then a
    // transform-and-write pass. The in-memory quac() kernel keeps its
    // contract for direct callers/tests.
    GdalDatasetWrapper outDataset;
    QString createError;
    if (!outDataset.create(outputPath, width, height, bandCount, GDT_Float32,
                           srcDataset.geoTransform(), srcDataset.projection(), &createError)) {
        if (errorMessage)
            *errorMessage = createError;
        return false;
    }
    bool hasFirstNoData = false;
    const double firstNodata = srcDataset.bandNoDataValue(1, &hasFirstNoData);
    if (hasFirstNoData)
        outDataset.setBandNoDataValue(1, firstNodata);

    constexpr int kTile = 256;
    std::vector<float> dark(bandCount), bright(bandCount);

    // Passes 1+2 per band: streaming 1%/99% percentiles.
    for (int b = 0; b < bandCount; ++b) {
        bool hasNoData = false;
        const double nodataVal = srcDataset.bandNoDataValue(b + 1, &hasNoData);
        const float nodataF = static_cast<float>(nodataVal);
        const bool maskNodata = hasNoData && std::isfinite(nodataVal);
        std::vector<float> tile;
        auto normalize = [&](size_t n) {
            if (!maskNodata)
                return;
            for (size_t i = 0; i < n; ++i)
                if (tile[i] == nodataF)
                    tile[i] = std::numeric_limits<float>::quiet_NaN();
        };

        StreamingPercentiles stats;
        const auto statsPass = [&](bool binning) -> bool {
            return GdalBlockStream(srcDataset, b + 1, kTile, kTile).forEach(
                [&](const GdalBlockStream::Tile &t, const float *pixels) {
                    const size_t n = static_cast<size_t>(t.width) * t.height;
                    tile.assign(pixels, pixels + n);
                    normalize(n);
                    if (binning)
                        stats.accumulateBins(tile.data(), n);
                    else
                        stats.accumulateRange(tile.data(), n);
                    return true;
                });
        };
        if (!statsPass(false) || !stats.prepareBins() || !statsPass(true) || !stats.hasValid()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("QUAC: band %1 has no valid pixels").arg(b + 1);
            QFile::remove(outputPath);
            return false;
        }
        dark[b] = stats.percentile(0.01);
        bright[b] = stats.percentile(0.99);
        if (progress)
            progress(0.1 + 0.4 * (b + 1) / bandCount, QStringLiteral("QUAC stats band %1").arg(b + 1));
    }

    // Scene-average references (same math as the in-memory kernel).
    const float meanBright = std::accumulate(bright.begin(), bright.end(), 0.0f) / bandCount;
    const float meanDark = std::accumulate(dark.begin(), dark.end(), 0.0f) / bandCount;
    const float refRange = meanBright - meanDark;
    if (refRange <= 0.0f || meanBright <= 0.0f) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QUAC: degenerate image (zero dynamic range or all-dark scene)");
        QFile::remove(outputPath);
        return false;
    }

    // Pass 3 per band: transform and write tile-by-tile.
    for (int b = 0; b < bandCount; ++b) {
        const float range = bright[b] - dark[b];
        const float gain = (range > 0.0f) ? 0.5f * refRange / (range * meanBright) : 0.0f;
        const float offset = -dark[b] * gain;
        const bool flatBand = range <= 0.0f;

        bool hasNoData = false;
        const double nodataVal = srcDataset.bandNoDataValue(b + 1, &hasNoData);
        const float nodataF = static_cast<float>(nodataVal);
        const bool maskNodata = hasNoData && std::isfinite(nodataVal);

        std::vector<float> out;
        bool ok = GdalBlockStream(srcDataset, b + 1, kTile, kTile).forEach(
            [&](const GdalBlockStream::Tile &t, const float *pixels) {
                const size_t n = static_cast<size_t>(t.width) * t.height;
                out.resize(n);
                for (size_t i = 0; i < n; ++i) {
                    const float v = pixels[i];
                    const bool invalid = !std::isfinite(v) || (maskNodata && v == nodataF);
                    if (invalid)
                        out[i] = std::numeric_limits<float>::quiet_NaN();
                    else if (flatBand)
                        out[i] = 0.0f;
                    else
                        out[i] = std::max(0.0f, gain * v + offset);  // clamp negatives only (#632)
                }
                return outDataset.writeBandWindow(b + 1, t.xOffset, t.yOffset,
                                                  t.width, t.height, out.data());
            });
        if (!ok) {
            if (errorMessage)
                *errorMessage = QStringLiteral("QUAC: failed to stream band %1").arg(b + 1);
            QFile::remove(outputPath);
            return false;
        }
        if (progress)
            progress(0.5 + 0.45 * (b + 1) / bandCount, QStringLiteral("QUAC write band %1").arg(b + 1));
    }

    if (progress)
        progress(1.0, QStringLiteral("QUAC complete"));
    return true;
}

bool processFile(const QString &sourcePath, const QString &outputPath,
                 int bandNum, int method, float gain, float bias,
                 float airmass, QString *errorMessage)
{
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();

    // Out-of-core path: create the output up front and stream the band
    // tile-by-tile, so memory stays O(tile) instead of O(width*height).
    // DOS1/DOS2 need full-scene dark-object statistics first, which costs two
    // extra streaming passes over the band (range, then bin counts).
    GdalDatasetWrapper outDataset;
    if (!outDataset.create(outputPath, width, height, 1, GDT_Float32,
                           srcDataset.geoTransform(), srcDataset.projection(), errorMessage))
        return false;

    bool hasSrcNoData = false;
    const double bandNoData = srcDataset.bandNoDataValue(bandNum, &hasSrcNoData);
    if (hasSrcNoData) {
        outDataset.setBandNoDataValue(1, bandNoData);
    }

    constexpr int kTile = 256; // nominal stream tile size (edge-clamped)
    const bool needsDarkLevel = (method == Method::Dos1 || method == Method::Dos2);
    const float transmittance = (method == Method::Dos2) ? estimateTransmittance(airmass) : 1.0f;

    // Pass 1 (+2): full-scene dark-object statistics for DOS1/DOS2.
    float darkLevel = 0.0f;
    if (needsDarkLevel) {
        DarkObjectStats stats;
        std::vector<float> radiance;
        QString statError;
        const auto statsPass = [&](bool binning) -> bool {
            return GdalBlockStream(srcDataset, bandNum, kTile, kTile).forEach(
                [&](const GdalBlockStream::Tile &tile, const float *pixels) {
                    const size_t n = static_cast<size_t>(tile.width) * tile.height;
                    radiance.resize(n);
                    for (size_t i = 0; i < n; ++i) {
                        float v = pixels[i];
                        if (!std::isfinite(v) || (hasSrcNoData && (!std::isnan(bandNoData) ? std::abs(v - bandNoData) < 1e-4f : std::isnan(v))))
                            radiance[i] = std::numeric_limits<float>::quiet_NaN();
                        else
                            radiance[i] = gain * v + bias;
                    }
                    if (binning)
                        stats.accumulateBins(radiance.data(), n);
                    else
                        stats.accumulateRange(radiance.data(), n);
                    return true;
                });
        };
        if (!statsPass(false)) {
            if (errorMessage)
                *errorMessage = statError;
            return false;
        }
        if (stats.prepareBins()) {
            if (!statsPass(true)) {
                if (errorMessage)
                    *errorMessage = statError;
                return false;
            }
        }
        darkLevel = stats.darkLevel();
    }

    // Pass 3: transform and write each tile.
    std::vector<float> out;
    QString tileError;
    const bool ok = GdalBlockStream(srcDataset, bandNum, kTile, kTile).forEach(
        [&](const GdalBlockStream::Tile &tile, const float *pixels) {
            const size_t n = static_cast<size_t>(tile.width) * tile.height;
            out.resize(n);
            for (size_t i = 0; i < n; ++i) {
                float v = pixels[i];
                if (!std::isfinite(v) || (hasSrcNoData && (!std::isnan(bandNoData) ? std::abs(v - bandNoData) < 1e-4f : std::isnan(v)))) {
                    out[i] = hasSrcNoData ? static_cast<float>(bandNoData) : std::numeric_limits<float>::quiet_NaN();
                } else {
                    switch (method) {
                    case Method::DnToRadiance:
                        out[i] = gain * v + bias;
                        break;
                    case Method::Dos1:
                        out[i] = gain * v + bias - darkLevel;
                        break;
                    case Method::Dos2:
                        out[i] = (transmittance > 0.0f) ? ((gain * v + bias - darkLevel) / transmittance)
                                                        : std::numeric_limits<float>::quiet_NaN();
                        break;
                    default:
                        out[i] = std::numeric_limits<float>::quiet_NaN();
                        break;
                    }
                }
            }
            return outDataset.writeBandWindow(1, tile.xOffset, tile.yOffset,
                                              tile.width, tile.height, out.data());
        });
    if (!ok) {
        if (errorMessage)
            *errorMessage = tileError.isEmpty()
                                ? QStringLiteral("Failed to stream band %1").arg(bandNum)
                                : tileError;
        // A truncated output must not be left behind as a seemingly valid
        // raster (GUI/CLI direct paths bypass the OutputCommitter) (#617).
        QFile::remove(outputPath);
        return false;
    }

    return true;
}

bool processFileDos(const QString &sourcePath, const QString &outputPath,
                    int bandNum, int method,
                    const RadiometricCalibration::BandCoefficients &coeffs,
                    RadiometricCalibration::SensorType sensor,
                    double sunElevationDeg,
                    float airmass, QString *errorMessage)
{
    using RadiometricCalibration::toToaReflectance;
    using RadiometricCalibration::SensorType;

    if (method != Method::Dos1 && method != Method::Dos2) {
        if (errorMessage)
            *errorMessage = QStringLiteral("processFileDos only supports Dos1/Dos2");
        return false;
    }
    if (sensor == SensorType::Landsat && !std::isfinite(sunElevationDeg)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Landsat DOS requires a finite sun elevation");
        return false;
    }

    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }
    const int width = srcDataset.width();
    const int height = srcDataset.height();

    GdalDatasetWrapper outDataset;
    if (!outDataset.create(outputPath, width, height, 1, GDT_Float32,
                           srcDataset.geoTransform(), srcDataset.projection(), errorMessage))
        return false;

    // Surface-reflectance output: NaN is the NoData convention.
    outDataset.setBandNoDataValue(1, std::numeric_limits<double>::quiet_NaN());

    bool hasSrcNoData = false;
    const double bandNoData = srcDataset.bandNoDataValue(bandNum, &hasSrcNoData);

    constexpr int kTile = 256;
    const float transmittance = (method == Method::Dos2) ? estimateTransmittance(airmass) : 1.0f;

    // Passes 1+2: dark-object statistics in TOA-reflectance space (the same
    // streaming histogram estimator the radiance path uses).
    float darkLevel = 0.0f;
    {
        DarkObjectStats stats;
        std::vector<float> rhoToa;
        const auto statsPass = [&](bool binning) -> bool {
            return GdalBlockStream(srcDataset, bandNum, kTile, kTile).forEach(
                [&](const GdalBlockStream::Tile &tile, const float *pixels) {
                    const size_t n = static_cast<size_t>(tile.width) * tile.height;
                    rhoToa.resize(n);
                    if (!toToaReflectance(pixels, rhoToa.data(), n, coeffs, sensor, sunElevationDeg))
                        return false;
                    if (binning)
                        stats.accumulateBins(rhoToa.data(), n);
                    else
                        stats.accumulateRange(rhoToa.data(), n);
                    return true;
                });
        };
        if (!statsPass(false)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Failed to compute TOA reflectance for band %1 "
                                               "(missing REFLECTANCE_MULT/ADD or QUANTIFICATION_VALUE?)")
                                    .arg(bandNum);
            QFile::remove(outputPath);
            return false;
        }
        if (stats.prepareBins()) {
            if (!statsPass(true)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Failed to accumulate dark-object histogram for band %1").arg(bandNum);
                QFile::remove(outputPath);
                return false;
            }
        }
        darkLevel = stats.darkLevel();
    }

    // Pass 3: Chavez DOS in reflectance space.
    std::vector<float> out;
    QString tileError;
    const bool ok = GdalBlockStream(srcDataset, bandNum, kTile, kTile).forEach(
        [&](const GdalBlockStream::Tile &tile, const float *pixels) {
            const size_t n = static_cast<size_t>(tile.width) * tile.height;
            out.resize(n);
            for (size_t i = 0; i < n; ++i) {
                const bool invalid =
                    !std::isfinite(pixels[i])
                    || (hasSrcNoData
                        && (!std::isnan(bandNoData) ? std::abs(pixels[i] - bandNoData) < 1e-4f : std::isnan(pixels[i])));
                if (invalid) {
                    out[i] = std::numeric_limits<float>::quiet_NaN();
                    continue;
                }
                float rho = std::numeric_limits<float>::quiet_NaN();
                if (!toToaReflectance(pixels + i, &rho, 1, coeffs, sensor, sunElevationDeg)) {
                    rho = std::numeric_limits<float>::quiet_NaN();
                }
                out[i] = dosReflectance(rho, darkLevel, transmittance);
            }
            return outDataset.writeBandWindow(1, tile.xOffset, tile.yOffset,
                                              tile.width, tile.height, out.data());
        });
    if (!ok) {
        if (errorMessage)
            *errorMessage = tileError.isEmpty()
                                ? QStringLiteral("Failed to stream band %1").arg(bandNum)
                                : tileError;
        QFile::remove(outputPath);
        return false;
    }
    return true;
}

} // namespace AtmosphericCorrection