// image_fusion.cpp — Phase 11.1
#include "image_fusion.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "data/raster_grid_compat.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include <gdal.h>

#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>

// ---------------------------------------------------------------------------
// Histogram matching (linear: match mean and stddev)
// ---------------------------------------------------------------------------

void ImageFusion::histogramMatch( float *data, int n,
                                  const float *ref, int refN, float nodata )
{
    if ( n <= 0 || refN <= 0 )
        return;

    // Compute stats for source and reference using shared utility
    MathUtils::Stats statsS = MathUtils::computeStatsWithNodata(data, n, nodata);
    MathUtils::Stats statsR = MathUtils::computeStatsWithNodata(ref, refN, nodata);

    if ( statsS.validCount == 0 || statsR.validCount == 0 )
        return;

    double meanS = statsS.mean;
    double meanR = statsR.mean;
    double stdS = statsS.stddev;
    double stdR = statsR.stddev;

    // Linear transform: matched = (data - meanS) * (stdR / stdS) + meanR
    // Guard against near-zero stddev (near-constant data with float jitter)
    double scale = ( stdS > 1e-10 ) ? ( stdR / stdS ) : 1.0;
    for ( int i = 0; i < n; ++i )
    {
        if ( data[i] == nodata || std::isnan( data[i] ) )
            continue;
        data[i] = static_cast<float>( ( data[i] - meanS ) * scale + meanR );
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Linear weighted fusion (simplest method)
// ---------------------------------------------------------------------------

QVector<QVector<float>> ImageFusion::linearWeighted(
    const QVector<const float *> &msBands, int nBands,
    const float *panBand, int width, int height, float nodata,
    const QVector<float> &msWeights, float panWeight )
{
    QVector<QVector<float>> result;
    if ( nBands <= 0 || !panBand || width <= 0 || height <= 0 )
        return result;

    // Build default weights if not provided (equal weight per band)
    QVector<float> weights = msWeights;
    if ( weights.isEmpty() ) {
        weights.resize(nBands);
        float defaultMsWeight = 1.0f - panWeight;
        for (int b = 0; b < nBands; ++b)
            weights[b] = defaultMsWeight;
    }

    SICNU_LOG_INFO( SicnuLogTags::Algorithms,
        QString("Linear weighted fusion: %1 bands, %2x%3, panWeight=%4")
            .arg(nBands).arg(width).arg(height).arg(panWeight) );

    size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
    result.resize(nBands);

    for (int b = 0; b < nBands; ++b) {
        result[b].resize(n);
        const float *ms = msBands[b];
        float *out = result[b].data();
        float msW = std::clamp(weights[b], 0.0f, 1.0f);

        for (size_t i = 0; i < n; ++i) {
            if (ms[i] == nodata || panBand[i] == nodata ||
                std::isnan(ms[i]) || std::isnan(panBand[i])) {
                out[i] = nodata;
            } else {
                out[i] = msW * ms[i] + panWeight * panBand[i];
            }
        }
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::Algorithms, "Linear weighted fusion completed" );
    return result;
}

// ---------------------------------------------------------------------------
// Brovey fusion
// ---------------------------------------------------------------------------

QVector<QVector<float>> ImageFusion::brovey(
    const QVector<const float *> &msBands, int nBands,
    const float *panBand, int width, int height, float nodata )
{
    QVector<QVector<float>> result;
    if ( nBands <= 0 || !panBand || width <= 0 || height <= 0 )
        return result;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Brovey fusion: %1 bands, %2x%3" )
        .arg( nBands ).arg( width ).arg( height ) );

    const size_t n = static_cast<size_t>(width) * height;

    // Compute sum of MS bands per pixel
    QVector<float> msSum( n, 0.0f );
    for ( int b = 0; b < nBands; ++b )
    {
        if ( !msBands[b] )
            return result;
        for ( size_t i = 0; i < n; ++i )
        {
            if ( msBands[b][i] != nodata && !std::isnan( msBands[b][i] ) )
                msSum[i] += msBands[b][i];
        }
    }

    // Apply Brovey: fused[i] = (ms[b][i] / msSum[i]) * pan[i]
    result.resize( nBands );
    for ( int b = 0; b < nBands; ++b )
    {
        result[b].resize( n );
        for ( size_t i = 0; i < n; ++i )
        {
            if ( msBands[b][i] == nodata || std::isnan( msBands[b][i] ) ||
                 panBand[i] == nodata || std::isnan( panBand[i] ) ||
                 msSum[i] < 1e-10f )
            {
                result[b][i] = nodata;
            }
            else
            {
                result[b][i] = ( msBands[b][i] / msSum[i] ) * panBand[i];
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// PCA fusion
// ---------------------------------------------------------------------------

QVector<QVector<float>> ImageFusion::pcaFusion(
    const QVector<const float *> &msBands, int nBands,
    const float *panBand, int width, int height, float nodata )
{
    QVector<QVector<float>> result;
    if ( nBands <= 0 || !panBand || width <= 0 || height <= 0 )
        return result;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "PCA fusion: %1 bands, %2x%3" )
        .arg( nBands ).arg( width ).arg( height ) );

    const size_t n = static_cast<size_t>(width) * height;

    // Step 1: Compute mean and covariance matrix
    QVector<double> means( nBands, 0.0 );
    QVector<int> counts( nBands, 0 );
    for ( int b = 0; b < nBands; ++b )
    {
        if ( !msBands[b] )
            return result;
        for ( size_t i = 0; i < n; ++i )
        {
            if ( msBands[b][i] != nodata && !std::isnan( msBands[b][i] ) )
            {
                means[b] += msBands[b][i];
                counts[b]++;
            }
        }
        if ( counts[b] > 0 )
            means[b] /= counts[b];
    }

    // Covariance matrix (symmetric)
    QVector<double> cov( nBands * nBands, 0.0 );
    int actualValidPixels = 0;
    for ( int i = 0; i < n; ++i )
    {
        bool valid = true;
        for ( int b = 0; b < nBands; ++b )
        {
            if ( msBands[b][i] == nodata || std::isnan( msBands[b][i] ) )
            {
                valid = false;
                break;
            }
        }
        if ( !valid )
            continue;

        actualValidPixels++;
        for ( int b1 = 0; b1 < nBands; ++b1 )
        {
            for ( int b2 = b1; b2 < nBands; ++b2 )
            {
                double d1 = msBands[b1][i] - means[b1];
                double d2 = msBands[b2][i] - means[b2];
                cov[b1 * nBands + b2] += d1 * d2;
            }
        }
    }
    // Normalize by actual valid pixel count (not per-band minimum)
    if ( actualValidPixels <= 1 )
        return result;
    for ( int i = 0; i < nBands * nBands; ++i )
        cov[i] /= ( actualValidPixels - 1 );
    // Fill symmetric
    for ( int b1 = 0; b1 < nBands; ++b1 )
        for ( int b2 = 0; b2 < b1; ++b2 )
            cov[b1 * nBands + b2] = cov[b2 * nBands + b1];

    // Step 2: Jacobi eigen decomposition
    QVector<double> eigVec( nBands * nBands, 0.0 );
    for ( int i = 0; i < nBands; ++i )
        eigVec[i * nBands + i] = 1.0;
    QVector<double> eigVal( cov );

    for ( int iter = 0; iter < 100; ++iter )
    {
        // Find largest off-diagonal element
        int p = 0, q = 1;
        double maxVal = 0;
        for ( int i = 0; i < nBands; ++i )
        {
            for ( int j = i + 1; j < nBands; ++j )
            {
                double v = std::abs( eigVal[i * nBands + j] );
                if ( v > maxVal )
                {
                    maxVal = v;
                    p = i;
                    q = j;
                }
            }
        }
        if ( maxVal < 1e-10 )
            break;

        // Compute rotation angle
        double theta = 0.5 * std::atan2( 2.0 * eigVal[p * nBands + q],
                                          eigVal[p * nBands + p] - eigVal[q * nBands + q] );
        double c = std::cos( theta );
        double s = std::sin( theta );

        // Apply Givens rotation
        for ( int i = 0; i < nBands; ++i )
        {
            double vp = eigVal[i * nBands + p];
            double vq = eigVal[i * nBands + q];
            eigVal[i * nBands + p] = c * vp + s * vq;
            eigVal[i * nBands + q] = -s * vp + c * vq;
        }
        for ( int i = 0; i < nBands; ++i )
        {
            double vp = eigVal[p * nBands + i];
            double vq = eigVal[q * nBands + i];
            eigVal[p * nBands + i] = c * vp + s * vq;
            eigVal[q * nBands + i] = -s * vp + c * vq;
        }

        // Update eigenvectors
        for ( int i = 0; i < nBands; ++i )
        {
            double vp = eigVec[i * nBands + p];
            double vq = eigVec[i * nBands + q];
            eigVec[i * nBands + p] = c * vp + s * vq;
            eigVec[i * nBands + q] = -s * vp + c * vq;
        }
    }

    // Sort eigenvalues in descending order (PC1 = largest variance)
    QVector<int> eigIdx( nBands );
    for ( int i = 0; i < nBands; ++i ) eigIdx[i] = i;
    std::sort( eigIdx.begin(), eigIdx.end(), [&]( int a, int b ) {
        return eigVal[a * nBands + a] > eigVal[b * nBands + b];
    } );

    // Reorder eigenvectors by sorted eigenvalues
    QVector<double> sortedEigVec( nBands * nBands );
    for ( int i = 0; i < nBands; ++i )
        for ( int b = 0; b < nBands; ++b )
            sortedEigVec[b * nBands + i] = eigVec[b * nBands + eigIdx[i]];
    eigVec = sortedEigVec;

    // Step 3: Forward PCA (project data onto eigenvectors)
    QVector<QVector<float>> pc( nBands );
    for ( int b = 0; b < nBands; ++b )
        pc[b].resize( n );

    for ( int i = 0; i < n; ++i )
    {
        // Skip nodata pixels
        bool valid = true;
        for ( int b = 0; b < nBands; ++b )
            if ( msBands[b][i] == nodata || std::isnan( msBands[b][i] ) )
                { valid = false; break; }
        if ( !valid ) {
            for ( int pcIdx = 0; pcIdx < nBands; ++pcIdx )
                pc[pcIdx][i] = nodata;
            continue;
        }
        for ( int pcIdx = 0; pcIdx < nBands; ++pcIdx )
        {
            double val = 0;
            for ( int b = 0; b < nBands; ++b )
                val += ( msBands[b][i] - means[b] ) * eigVec[b * nBands + pcIdx];
            pc[pcIdx][i] = static_cast<float>( val );
        }
    }

    // Step 4: Replace PC1 with histogram-matched panchromatic
    QVector<float> panMatched( n );
    std::memcpy( panMatched.data(), panBand, n * sizeof( float ) );
    histogramMatch( panMatched.data(), n, pc[0].data(), n, nodata );

    // Step 5: Inverse PCA
    result.resize( nBands );
    for ( int b = 0; b < nBands; ++b )
        result[b].resize( n );

    // Use panMatched for PC1, original PCs for the rest
    for ( int i = 0; i < n; ++i )
    {
        // Skip nodata pixels
        if ( panMatched[i] == nodata || std::isnan( panMatched[i] ) ) {
            for ( int b = 0; b < nBands; ++b )
                result[b][i] = nodata;
            continue;
        }
        bool valid = true;
        for ( int pcIdx = 1; pcIdx < nBands; ++pcIdx )
            if ( pc[pcIdx][i] == nodata || std::isnan( pc[pcIdx][i] ) )
                { valid = false; break; }
        if ( !valid ) {
            for ( int b = 0; b < nBands; ++b )
                result[b][i] = nodata;
            continue;
        }
        for ( int b = 0; b < nBands; ++b )
        {
            double val = 0;
            val += panMatched[i] * eigVec[b * nBands + 0]; // PC1 (replaced)
            for ( int pcIdx = 1; pcIdx < nBands; ++pcIdx )
                val += pc[pcIdx][i] * eigVec[b * nBands + pcIdx];
            result[b][i] = static_cast<float>( val + means[b] );
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// IHS fusion
// ---------------------------------------------------------------------------

QVector<QVector<float>> ImageFusion::ihsFusion(
    const float *msR, const float *msG, const float *msB,
    const float *panBand, int width, int height, float nodata )
{
    QVector<QVector<float>> result;
    if ( !msR || !msG || !msB || !panBand || width <= 0 || height <= 0 )
        return result;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "IHS fusion: %1x%2" ).arg( width ).arg( height ) );

    const size_t n = static_cast<size_t>(width) * height;

    // Step 1: Histogram-match pan to intensity (mean of R, G, B)
    QVector<float> intensity( n );
    for ( size_t i = 0; i < n; ++i )
    {
        if ( msR[i] == nodata || msG[i] == nodata || msB[i] == nodata )
            intensity[i] = nodata;
        else
            intensity[i] = ( msR[i] + msG[i] + msB[i] ) / 3.0f;
    }

    QVector<float> panMatched( n );
    std::memcpy( panMatched.data(), panBand, n * sizeof( float ) );
    histogramMatch( panMatched.data(), n, intensity.data(), n, nodata );

    // Step 2: RGB → IHS (using the simplified cylindrical model)
    // I = (R + G + B) / 3
    // S = 1 - min(R,G,B) / I  (when I > 0)
    // H = computed from chromaticity
    QVector<float> H( n ), S( n );
    for ( int i = 0; i < n; ++i )
    {
        if ( msR[i] == nodata || msG[i] == nodata || msB[i] == nodata )
        {
            H[i] = nodata;
            S[i] = nodata;
            continue;
        }

        float r = msR[i], g = msG[i], b = msB[i];
        float I = ( r + g + b ) / 3.0f;

        if ( I < 1e-10f )
        {
            H[i] = 0;
            S[i] = 0;
            continue;
        }

        float m = std::min( { r, g, b } );
        S[i] = 1.0f - m / I;

        // Hue from chromaticity
        float c1 = r - 0.5f * ( g + b );
        float c2 = ( g - b ) * std::sqrt( 3.0f ) / 2.0f;
        H[i] = std::atan2( c2, c1 );
    }

    // Step 3: Replace I with matched pan, IHS → RGB
    result.resize( 3 );
    for ( int b = 0; b < 3; ++b )
        result[b].resize( n );

    for ( int i = 0; i < n; ++i )
    {
        if ( H[i] == nodata || S[i] == nodata ||
             panMatched[i] == nodata || std::isnan( panMatched[i] ) )
        {
            result[0][i] = nodata;
            result[1][i] = nodata;
            result[2][i] = nodata;
            continue;
        }

        float I = panMatched[i];
        float sat = S[i];
        float hue = H[i];

        // IHS → RGB (inverse of the above)
        float M = I * ( 1.0f - sat );
        float c1 = I * sat * std::cos( hue );
        float c2 = I * sat * std::sin( hue );

        float r = I + c1;
        float g = I - 0.5f * c1 - std::sqrt( 3.0f ) / 2.0f * c2;
        float b = I - 0.5f * c1 + std::sqrt( 3.0f ) / 2.0f * c2;

        // Clamp to non-negative
        result[0][i] = std::max( 0.0f, r );
        result[1][i] = std::max( 0.0f, g );
        result[2][i] = std::max( 0.0f, b );
    }

    return result;
}

// ---------------------------------------------------------------------------
// Gram-Schmidt fusion (simulated-pan variant)
// ---------------------------------------------------------------------------

QVector<QVector<float>> ImageFusion::gramSchmidtFusion(
    const QVector<const float *> &msBands, int nBands,
    const float *panBand, int width, int height, float nodata )
{
    QVector<QVector<float>> result;
    if ( nBands <= 0 || !panBand || width <= 0 || height <= 0 )
        return result;

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Gram-Schmidt fusion: %1 bands, %2x%3" )
        .arg( nBands ).arg( width ).arg( height ) );

    const size_t n = static_cast<size_t>(width) * height;

    // Validate band pointers.
    for ( int b = 0; b < nBands; ++b )
        if ( !msBands[b] )
            return result;

    // -----------------------------------------------------------------------
    // Step 1: simulated low-res pan = mean of MS bands (valid pixels only).
    // -----------------------------------------------------------------------
    QVector<float> synPan( n, 0.0f );
    for ( int i = 0; i < n; ++i )
    {
        bool valid = true;
        double sum = 0.0;
        for ( int b = 0; b < nBands; ++b )
        {
            if ( msBands[b][i] == nodata || std::isnan( msBands[b][i] ) )
            { valid = false; break; }
            sum += msBands[b][i];
        }
        synPan[i] = valid ? static_cast<float>( sum / nBands ) : nodata;
    }

    // -----------------------------------------------------------------------
    // Step 2: forward Gram-Schmidt orthogonalization on [synPan, MS_1..MS_n].
    //
    // Treat each band as a vector in pixel-space and orthonormalize with the
    // modified Gram-Schmidt process. GS_0 = synPan; each subsequent GS_k is the
    // residual of MS_k after removing its projection onto all prior GS_j.
    // The coefficient c_{k,j} = <MS_k, GS_j> / <GS_j, GS_j> is stored for the
    // inverse transform.
    // -----------------------------------------------------------------------
    const int total = nBands + 1;
    QVector<QVector<float>> gs( total );
    for ( int k = 0; k < total; ++k )
        gs[k].resize( n );

    // GS_0 <- synPan
    std::memcpy( gs[0].data(), synPan.data(), static_cast<size_t>( n ) * sizeof( float ) );

    // Coefficient matrix: coef[k][j] = <input_k, gs_j> / <gs_j, gs_j>
    QVector<QVector<double>> coef( total, QVector<double>( total, 0.0 ) );

    // Per-k validity mask, reused across the j-loop. A pixel contributes to
    // band k only if synPan (gs[0]) and MS band k are both valid; neither
    // changes during the j-loop, so the mask is computed once per k instead of
    // 2k+1 times inside the inner loops.
    std::vector<char> validMask( n, 0 );

    for ( int k = 1; k < total; ++k )
    {
        const int bandIdx = k - 1;
        // working <- ms[bandIdx]; build the validity mask in the same pass.
        QVector<float> work( n );
        for ( int i = 0; i < n; ++i )
        {
            const bool valid = ( gs[0][i] != nodata && !std::isnan( gs[0][i] ) &&
                                 msBands[bandIdx][i] != nodata && !std::isnan( msBands[bandIdx][i] ) );
            validMask[i] = valid ? 1 : 0;
            work[i] = valid ? msBands[bandIdx][i] : 0.0f;
        }

        for ( int j = 0; j < k; ++j )
        {
            // Coefficient c_{k,j} = Cov(work, GS_j) / Var(GS_j), computed over
            // the mean-centered vectors (standard Gram-Schmidt fusion). A
            // through-origin dot product is dominated by the bands' mean
            // offsets (DN imagery has large means), which leaves nearly all of
            // band k's offset in the residual and degrades spectral fidelity
            // after GS_0 is replaced by the high-res pan.
            double dot = 0.0;
            double normSq = 0.0;
            double sumWork = 0.0, sumG = 0.0;
            int64_t cnt = 0;
            for ( int i = 0; i < n; ++i )
            {
                if ( !validMask[i] )
                    continue;
                dot += static_cast<double>( work[i] ) * gs[j][i];
                normSq += static_cast<double>( gs[j][i] ) * gs[j][i];
                sumWork += static_cast<double>( work[i] );
                sumG += static_cast<double>( gs[j][i] );
                ++cnt;
            }
            double c = 0.0;
            if ( cnt > 1 )
            {
                const double invN = 1.0 / static_cast<double>( cnt );
                const double workMean = sumWork * invN;
                const double gMean = sumG * invN;
                double cDot = 0.0, cNorm = 0.0;
                for ( int i = 0; i < n; ++i )
                {
                    if ( !validMask[i] )
                        continue;
                    const double wc = static_cast<double>( work[i] ) - workMean;
                    const double gc = static_cast<double>( gs[j][i] ) - gMean;
                    cDot += wc * gc;
                    cNorm += gc * gc;
                }
                c = ( cNorm > 1e-20 ) ? ( cDot / cNorm ) : 0.0;
            }
            coef[k][j] = c;
            for ( int i = 0; i < n; ++i )
            {
                if ( !validMask[i] )
                    continue;
                work[i] = static_cast<float>( work[i] - c * gs[j][i] );
            }
        }

        // gs[k] <- work (with nodata preserved)
        for ( int i = 0; i < n; ++i )
            gs[k][i] = validMask[i] ? work[i] : nodata;
    }

    // -----------------------------------------------------------------------
    // Step 3: histogram-match the high-res pan to GS_0 and substitute as GS_0'.
    // -----------------------------------------------------------------------
    QVector<float> panMatched( n );
    std::memcpy( panMatched.data(), panBand, static_cast<size_t>( n ) * sizeof( float ) );
    histogramMatch( panMatched.data(), n, gs[0].data(), n, nodata );

    // -----------------------------------------------------------------------
    // Step 4: inverse Gram-Schmidt.
    //   MS_k = GS_k + sum_{j<k} coef[k][j] * GS_j,  with GS_0 replaced by pan'.
    // -----------------------------------------------------------------------
    result.resize( nBands );
    for ( int b = 0; b < nBands; ++b )
        result[b].resize( n );

    for ( int i = 0; i < n; ++i )
    {
        // A pixel is only reconstructable if synPan and all MS bands were valid
        // (gs components carry nodata otherwise) and pan is valid.
        bool valid = ( panMatched[i] != nodata && !std::isnan( panMatched[i] ) );
        if ( valid )
        {
            for ( int k = 0; k <= nBands; ++k )
            {
                if ( gs[k][i] == nodata || std::isnan( gs[k][i] ) )
                { valid = false; break; }
            }
        }
        if ( !valid )
        {
            for ( int b = 0; b < nBands; ++b )
                result[b][i] = nodata;
            continue;
        }

        // gs0Sub = panMatched
        double gs0Sub = panMatched[i];
        for ( int b = 0; b < nBands; ++b )
        {
            const int k = b + 1;
            // MS_k = GS_k + sum_{j=0..k-1} coef[k][j] * GS'_j ; GS'_0 = panMatched
            double val = gs[k][i] + coef[k][0] * gs0Sub;
            for ( int j = 1; j < k; ++j )
                val += coef[k][j] * gs[j][i];
            result[b][i] = static_cast<float>( val );
        }
    }

    return result;
}

namespace {

// Helper: accumulate statistics using Welford's algorithm for numerical stability
struct StatsAccumulator {
    int64_t count = 0;
    double mean_ = 0.0;
    double m2 = 0.0;

    void add(double val) {
        count++;
        double delta = val - mean_;
        mean_ += delta / count;
        m2 += delta * (val - mean_);
    }

    double mean() const {
        return mean_;
    }

    double stddev() const {
        if (count <= 1) return 0.0;
        double var = m2 / (count - 1);
        return (var > 0.0) ? std::sqrt(var) : 0.0;
    }
};

} // anonymous namespace

bool ImageFusion::processNativeFusion( const QString &panPath, const QString &msPath,
                                       const QString &outputPath,
                                       const NativeFusionParams &params,
                                       QString *errorMessage )
{
    GdalDatasetWrapper panDataset;
    if ( !panDataset.open( panPath ) )
    {
        if ( errorMessage )
            *errorMessage = panDataset.lastError();
        return false;
    }

    GdalDatasetWrapper msDataset;
    if ( !msDataset.open( msPath ) )
    {
        if ( errorMessage )
            *errorMessage = msDataset.lastError();
        return false;
    }

    // Grid preflight checks
    const sicnu::data::GridCompatReport gridReport =
        sicnu::data::compareGrids( sicnu::processing::gridFromDataset( panDataset ),
                                   sicnu::processing::gridFromDataset( msDataset ) );
    for ( const sicnu::data::GridCompatIssue &issue : gridReport.issues )
    {
        if ( issue.verdict == sicnu::data::GridCompatVerdict::PixelSizeMismatch )
        {
            continue;
        }
        if ( issue.blocking )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Pan and multispectral rasters are not co-registered: %1" )
                                    .arg( issue.message );
            return false;
        }
    }

    const int w = panDataset.width();
    const int h = panDataset.height();
    const int msBands = msDataset.bandCount();
    if ( msBands < 1 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Multispectral raster has no bands" );
        return false;
    }

    const int nMsBands = std::min( msBands, 4 );
    int nOutBands = nMsBands;
    if ( params.method == QStringLiteral( "ihs" ) )
    {
        nOutBands = 3;
        if ( params.redIdx < 0 || params.greenIdx < 0 || params.blueIdx < 0 ||
             params.redIdx >= nMsBands || params.greenIdx >= nMsBands || params.blueIdx >= nMsBands )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Invalid RGB band selection for IHS fusion" );
            return false;
        }
    }

    // Create GeoTIFF output dataset for streaming writes
    GdalDatasetWrapper outDataset;
    if ( !outDataset.create( outputPath, w, h, nOutBands, GDT_Float32,
                             panDataset.geoTransform(), panDataset.projection(), errorMessage ) )
    {
        return false;
    }

    bool hasPanNodata = false;
    double panNodataVal = panDataset.bandNoDataValue( 1, &hasPanNodata );
    const float nodata = hasPanNodata ? static_cast<float>( panNodataVal ) : -9999.0f;
    for ( int b = 0; b < nOutBands; ++b )
        outDataset.setBandNoDataValue( b + 1, nodata );

    const int tileW = std::max( 16, params.tileWidth <= 0 ? 512 : params.tileWidth );
    const int tileH = std::max( 16, params.tileHeight <= 0 ? 512 : params.tileHeight );

    const int cols = ( w + tileW - 1 ) / tileW;
    const int rows = ( h + tileH - 1 ) / tileH;
    const size_t maxTilePixels = static_cast<size_t>( tileW ) * tileH;

    // Buffer allocations (reused across all tiles)
    std::vector<float> panBuf( maxTilePixels );
    std::vector<std::vector<float>> msBuf( nMsBands, std::vector<float>( maxTilePixels ) );
    std::vector<std::vector<float>> outBuf( nOutBands, std::vector<float>( maxTilePixels ) );

    // Heterogeneous-resolution support: compareGrids allows PixelSizeMismatch
    // (non-blocking), so the MS raster may have a different pixel size than the
    // pan. The tile loops iterate the pan grid; each MS window is therefore
    // mapped to the MS pixel grid and GDAL resamples on read. When the pixel
    // sizes match, this is an identity mapping (native read).
    const auto panGt = panDataset.geoTransform();
    const auto msGt = msDataset.geoTransform();
    const double panResX = std::abs( panGt[1] );
    const double panResY = std::abs( panGt[5] );
    const double msResX = std::abs( msGt[1] );
    const double msResY = std::abs( msGt[5] );
    const double scaleX = ( msResX > 1e-12 ) ? ( panResX / msResX ) : 1.0;
    const double scaleY = ( msResY > 1e-12 ) ? ( panResY / msResY ) : 1.0;
    const bool resampleMs = ( std::abs( scaleX - 1.0 ) > 1e-9 || std::abs( scaleY - 1.0 ) > 1e-9 );
    if ( resampleMs )
    {
        SICNU_LOG_INFO( SicnuLogTags::Algorithms,
                        QString( "Fusion: pan %.6g x %.6g vs MS %.6g x %.6g — MS tiles "
                                 "will be resampled to the pan grid" )
                            .arg( panResX ).arg( panResY ).arg( msResX ).arg( msResY ) );
    }

    // readMsWindow: read MS @band (1-based) for the pan-grid tile at (xOff,
    // yOff, tw, th) into a tw*th buffer. When resolutions differ, the window
    // is mapped to MS pixels (grids share the origin — compareGrids blocks
    // origin mismatches) and GDAL resamples.
    auto readMsWindow = [&]( int band, int xOff, int yOff, int tw, int th,
                             float *buf ) -> bool {
        if ( !resampleMs )
            return msDataset.readBandWindow( band, xOff, yOff, tw, th, buf );
        const int msXOff = static_cast<int>( std::floor( static_cast<double>( xOff ) * scaleX ) );
        const int msYOff = static_cast<int>( std::floor( static_cast<double>( yOff ) * scaleY ) );
        const int msW = static_cast<int>( std::ceil( static_cast<double>( xOff + tw ) * scaleX ) ) - msXOff;
        const int msH = static_cast<int>( std::ceil( static_cast<double>( yOff + th ) * scaleY ) ) - msYOff;
        return msDataset.readBandWindowScaled( band, msXOff, msYOff, msW, msH,
                                               buf, tw, th, nodata );
    };

    if ( params.method == QStringLiteral( "linear" ) )
    {
        QVector<float> weights = params.msWeights;
        if ( weights.isEmpty() )
        {
            weights.resize( nMsBands );
            float defaultMsWeight = ( 1.0f - params.panWeight ) / nMsBands;
            for ( int b = 0; b < nMsBands; ++b )
                weights[b] = defaultMsWeight;
        }

        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) )
                    return false;
                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !readMsWindow( b + 1, xOff, yOff, tw, th, msBuf[b].data() ) )
                        return false;
                }

                for ( int b = 0; b < nMsBands; ++b )
                {
                    const float msW = std::clamp( weights[b], 0.0f, 1.0f );
                    const float *msData = msBuf[b].data();
                    float *outData = outBuf[b].data();

                    for ( size_t i = 0; i < tileSize; ++i )
                    {
                        if ( msData[i] == nodata || panBuf[i] == nodata ||
                             std::isnan( msData[i] ) || std::isnan( panBuf[i] ) )
                        {
                            outData[i] = nodata;
                        }
                        else
                        {
                            outData[i] = msW * msData[i] + params.panWeight * panBuf[i];
                        }
                    }
                    if ( !outDataset.writeBandWindow( b + 1, xOff, yOff, tw, th, outData ) )
                        return false;
                }
            }
        }
        return true;
    }
    else if ( params.method == QStringLiteral( "brovey" ) )
    {
        std::vector<float> msSum( maxTilePixels );

        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) )
                    return false;
                std::fill_n( msSum.begin(), tileSize, 0.0f );

                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !readMsWindow( b + 1, xOff, yOff, tw, th, msBuf[b].data() ) )
                        return false;
                    const float *msData = msBuf[b].data();
                    for ( size_t i = 0; i < tileSize; ++i )
                    {
                        if ( msData[i] != nodata && !std::isnan( msData[i] ) )
                            msSum[i] += msData[i];
                    }
                }

                for ( int b = 0; b < nMsBands; ++b )
                {
                    const float *msData = msBuf[b].data();
                    float *outData = outBuf[b].data();

                    for ( size_t i = 0; i < tileSize; ++i )
                    {
                        if ( msData[i] == nodata || std::isnan( msData[i] ) ||
                             panBuf[i] == nodata || std::isnan( panBuf[i] ) ||
                             msSum[i] < 1e-10f )
                        {
                            outData[i] = nodata;
                        }
                        else
                        {
                            outData[i] = ( msData[i] / msSum[i] ) * panBuf[i];
                        }
                    }
                    if ( !outDataset.writeBandWindow( b + 1, xOff, yOff, tw, th, outData ) )
                        return false;
                }
            }
        }
        return true;
    }
    else if ( params.method == QStringLiteral( "ihs" ) )
    {
        // PASS 1: Accumulate global statistics for Intensity and Pan
        StatsAccumulator statsI, statsP;
        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) ||
                     !readMsWindow( params.redIdx + 1, xOff, yOff, tw, th, msBuf[0].data() ) ||
                     !readMsWindow( params.greenIdx + 1, xOff, yOff, tw, th, msBuf[1].data() ) ||
                     !readMsWindow( params.blueIdx + 1, xOff, yOff, tw, th, msBuf[2].data() ) )
                    return false;

                const float *msR = msBuf[0].data();
                const float *msG = msBuf[1].data();
                const float *msB = msBuf[2].data();

                for ( size_t i = 0; i < tileSize; ++i )
                {
                    if ( msR[i] != nodata && msG[i] != nodata && msB[i] != nodata &&
                         panBuf[i] != nodata && !std::isnan( msR[i] ) &&
                         !std::isnan( msG[i] ) && !std::isnan( msB[i] ) && !std::isnan( panBuf[i] ) )
                    {
                        float intensity = ( msR[i] + msG[i] + msB[i] ) / 3.0f;
                        statsI.add( intensity );
                        statsP.add( panBuf[i] );
                    }
                }
            }
        }

        double stdI = statsI.stddev();
        double stdP = statsP.stddev();
        double scale = ( stdP > 1e-10 ) ? ( stdI / stdP ) : 1.0;
        double meanI = statsI.mean();
        double meanP = statsP.mean();

        // PASS 2: Stream transform and write
        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) ||
                     !readMsWindow( params.redIdx + 1, xOff, yOff, tw, th, msBuf[0].data() ) ||
                     !readMsWindow( params.greenIdx + 1, xOff, yOff, tw, th, msBuf[1].data() ) ||
                     !readMsWindow( params.blueIdx + 1, xOff, yOff, tw, th, msBuf[2].data() ) )
                    return false;

                const float *msR = msBuf[0].data();
                const float *msG = msBuf[1].data();
                const float *msB = msBuf[2].data();

                for ( size_t i = 0; i < tileSize; ++i )
                {
                    if ( msR[i] == nodata || msG[i] == nodata || msB[i] == nodata ||
                         panBuf[i] == nodata || std::isnan( msR[i] ) ||
                         std::isnan( msG[i] ) || std::isnan( msB[i] ) || std::isnan( panBuf[i] ) )
                    {
                        outBuf[0][i] = nodata;
                        outBuf[1][i] = nodata;
                        outBuf[2][i] = nodata;
                        continue;
                    }

                    float panMatched = static_cast<float>( ( panBuf[i] - meanP ) * scale + meanI );
                    float red = msR[i], green = msG[i], blue = msB[i];
                    float I = ( red + green + blue ) / 3.0f;

                    float m = std::min( { red, green, blue } );
                    float sat = ( I < 1e-10f ) ? 0.0f : ( 1.0f - m / I );
                    float c1 = red - 0.5f * ( green + blue );
                    float c2 = ( green - blue ) * std::sqrt( 3.0f ) / 2.0f;
                    float hue = ( I < 1e-10f ) ? 0.0f : std::atan2( c2, c1 );

                    float newI = panMatched;
                    float newC1 = newI * sat * std::cos( hue );
                    float newC2 = newI * sat * std::sin( hue );

                    float rOut = newI + newC1;
                    float gOut = newI - 0.5f * newC1 - std::sqrt( 3.0f ) / 2.0f * newC2;
                    float bOut = newI - 0.5f * newC1 + std::sqrt( 3.0f ) / 2.0f * newC2;

                    outBuf[0][i] = std::max( 0.0f, rOut );
                    outBuf[1][i] = std::max( 0.0f, gOut );
                    outBuf[2][i] = std::max( 0.0f, bOut );
                }

                for ( int b = 0; b < 3; ++b )
                {
                    if ( !outDataset.writeBandWindow( b + 1, xOff, yOff, tw, th, outBuf[b].data() ) )
                        return false;
                }
            }
        }
        return true;
    }
    else if ( params.method == QStringLiteral( "pca" ) )
    {
        // 2-PASS TILE STREAMING FOR PCA FUSION
        // PASS 1: Accumulate online mean and covariance matrix for MS bands
        std::vector<double> msMean( nMsBands, 0.0 );
        StatsAccumulator statsP;
        int64_t validPixels = 0;

        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) )
                    return false;
                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !readMsWindow( b + 1, xOff, yOff, tw, th, msBuf[b].data() ) )
                        return false;
                }

                for ( size_t i = 0; i < tileSize; ++i )
                {
                    if ( panBuf[i] == nodata || std::isnan( panBuf[i] ) )
                        continue;

                    bool validMs = true;
                    for ( int b = 0; b < nMsBands; ++b )
                    {
                        if ( msBuf[b][i] == nodata || std::isnan( msBuf[b][i] ) )
                        {
                            validMs = false;
                            break;
                        }
                    }
                    if ( !validMs )
                        continue;

                    statsP.add( panBuf[i] );
                    for ( int b = 0; b < nMsBands; ++b )
                        msMean[b] += msBuf[b][i];
                    validPixels++;
                }
            }
        }

        if ( validPixels == 0 )
            return false;

        for ( int b = 0; b < nMsBands; ++b )
            msMean[b] /= static_cast<double>( validPixels );

        // Covariance matrix computation
        std::vector<std::vector<double>> cov( nMsBands, std::vector<double>( nMsBands, 0.0 ) );
        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) )
                    return false;
                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !readMsWindow( b + 1, xOff, yOff, tw, th, msBuf[b].data() ) )
                        return false;
                }

                for ( size_t i = 0; i < tileSize; ++i )
                {
                    if ( panBuf[i] == nodata || std::isnan( panBuf[i] ) )
                        continue;

                    bool validMs = true;
                    for ( int b = 0; b < nMsBands; ++b )
                    {
                        if ( msBuf[b][i] == nodata || std::isnan( msBuf[b][i] ) )
                        {
                            validMs = false;
                            break;
                        }
                    }
                    if ( !validMs )
                        continue;

                    for ( int b1 = 0; b1 < nMsBands; ++b1 )
                    {
                        double d1 = msBuf[b1][i] - msMean[b1];
                        for ( int b2 = 0; b2 < nMsBands; ++b2 )
                        {
                            double d2 = msBuf[b2][i] - msMean[b2];
                            cov[b1][b2] += d1 * d2;
                        }
                    }
                }
            }
        }

        const double covDivisor = static_cast<double>( validPixels > 1 ? validPixels - 1 : 1 );
        for ( int b1 = 0; b1 < nMsBands; ++b1 )
            for ( int b2 = 0; b2 < nMsBands; ++b2 )
                cov[b1][b2] /= covDivisor;

        // Jacobi eigen decomposition for symmetric nMsBands x nMsBands matrix
        std::vector<double> eigVec( nMsBands * nMsBands, 0.0 );
        for ( int i = 0; i < nMsBands; ++i ) eigVec[i * nMsBands + i] = 1.0;
        std::vector<double> eigVal( nMsBands * nMsBands, 0.0 );
        for ( int b1 = 0; b1 < nMsBands; ++b1 )
            for ( int b2 = 0; b2 < nMsBands; ++b2 )
                eigVal[b1 * nMsBands + b2] = cov[b1][b2];

        for ( int iter = 0; iter < 100; ++iter )
        {
            int p = 0, q = 1;
            double maxVal = 0;
            for ( int i = 0; i < nMsBands; ++i )
            {
                for ( int j = i + 1; j < nMsBands; ++j )
                {
                    double v = std::abs( eigVal[i * nMsBands + j] );
                    if ( v > maxVal ) { maxVal = v; p = i; q = j; }
                }
            }
            if ( maxVal < 1e-10 ) break;

            double theta = 0.5 * std::atan2( 2.0 * eigVal[p * nMsBands + q],
                                              eigVal[p * nMsBands + p] - eigVal[q * nMsBands + q] );
            double c = std::cos( theta );
            double s = std::sin( theta );

            for ( int i = 0; i < nMsBands; ++i )
            {
                double vp = eigVal[i * nMsBands + p];
                double vq = eigVal[i * nMsBands + q];
                eigVal[i * nMsBands + p] = c * vp + s * vq;
                eigVal[i * nMsBands + q] = -s * vp + c * vq;
            }
            for ( int i = 0; i < nMsBands; ++i )
            {
                double vp = eigVal[p * nMsBands + i];
                double vq = eigVal[q * nMsBands + i];
                eigVal[p * nMsBands + i] = c * vp + s * vq;
                eigVal[q * nMsBands + i] = -s * vp + c * vq;
            }
            for ( int i = 0; i < nMsBands; ++i )
            {
                double vp = eigVec[i * nMsBands + p];
                double vq = eigVec[i * nMsBands + q];
                eigVec[i * nMsBands + p] = c * vp + s * vq;
                eigVec[i * nMsBands + q] = -s * vp + c * vq;
            }
        }

        std::vector<int> eigIdx( nMsBands );
        for ( int i = 0; i < nMsBands; ++i ) eigIdx[i] = i;
        std::sort( eigIdx.begin(), eigIdx.end(), [&]( int a, int b ) {
            return eigVal[a * nMsBands + a] > eigVal[b * nMsBands + b];
        } );

        std::vector<std::vector<double>> V( nMsBands, std::vector<double>( nMsBands ) );
        for ( int i = 0; i < nMsBands; ++i )
            for ( int b = 0; b < nMsBands; ++b )
                V[b][i] = eigVec[b * nMsBands + eigIdx[i]];

        // PC1 mean is mathematically 0 and PC1 stddev is sqrt(lambda1)
        double stdPC1 = std::sqrt( std::max( 0.0, eigVal[eigIdx[0] * nMsBands + eigIdx[0]] ) );
        double stdP = statsP.stddev();
        double scale = ( stdP > 1e-10 ) ? ( stdPC1 / stdP ) : 1.0;
        double meanPC1 = 0.0;
        double meanP = statsP.mean();

        // PASS 2: Stream write transformed PCA tiles
        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) )
                    return false;
                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !readMsWindow( b + 1, xOff, yOff, tw, th, msBuf[b].data() ) )
                        return false;
                }

                for ( size_t i = 0; i < tileSize; ++i )
                {
                    if ( panBuf[i] == nodata || std::isnan( panBuf[i] ) )
                    {
                        for ( int b = 0; b < nMsBands; ++b )
                            outBuf[b][i] = nodata;
                        continue;
                    }

                    bool validMs = true;
                    for ( int b = 0; b < nMsBands; ++b )
                    {
                        if ( msBuf[b][i] == nodata || std::isnan( msBuf[b][i] ) )
                        {
                            validMs = false;
                            break;
                        }
                    }
                    if ( !validMs )
                    {
                        for ( int b = 0; b < nMsBands; ++b )
                            outBuf[b][i] = nodata;
                        continue;
                    }

                    std::vector<double> pc( nMsBands, 0.0 );
                    for ( int k = 0; k < nMsBands; ++k )
                    {
                        for ( int b = 0; b < nMsBands; ++b )
                            pc[k] += ( msBuf[b][i] - msMean[b] ) * V[b][k];
                    }

                    // Substitute PC1 with histogram-matched Pan
                    pc[0] = ( panBuf[i] - meanP ) * scale + meanPC1;

                    // Inverse PCA transform: MS = V * PC + msMean
                    for ( int b = 0; b < nMsBands; ++b )
                    {
                        double val = msMean[b];
                        for ( int k = 0; k < nMsBands; ++k )
                            val += V[b][k] * pc[k];
                        outBuf[b][i] = static_cast<float>( val );
                    }
                }

                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !outDataset.writeBandWindow( b + 1, xOff, yOff, tw, th, outBuf[b].data() ) )
                        return false;
                }
            }
        }
        return true;
    }
    else if ( params.method == QStringLiteral( "gram_schmidt" ) )
    {
        // 2-PASS TILE STREAMING FOR GRAM-SCHMIDT FUSION
        // PASS 1: Accumulate mean for synthetic Pan, Pan stats, and GS dot products
        StatsAccumulator statsSynPan, statsP;
        std::vector<std::vector<double>> coef( nMsBands + 1, std::vector<double>( nMsBands + 1, 0.0 ) );
        std::vector<double> normSq( nMsBands + 1, 0.0 );

        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) )
                    return false;
                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !readMsWindow( b + 1, xOff, yOff, tw, th, msBuf[b].data() ) )
                        return false;
                }

                for ( size_t i = 0; i < tileSize; ++i )
                {
                    if ( panBuf[i] == nodata || std::isnan( panBuf[i] ) )
                        continue;

                    bool validMs = true;
                    double synVal = 0.0;
                    for ( int b = 0; b < nMsBands; ++b )
                    {
                        if ( msBuf[b][i] == nodata || std::isnan( msBuf[b][i] ) )
                        {
                            validMs = false;
                            break;
                        }
                        synVal += msBuf[b][i];
                    }
                    if ( !validMs )
                        continue;

                    synVal /= nMsBands;
                    statsSynPan.add( synVal );
                    statsP.add( panBuf[i] );

                    // GS_0 = synVal
                    normSq[0] += synVal * synVal;
                    for ( int b = 0; b < nMsBands; ++b )
                    {
                        const int k = b + 1;
                        coef[k][0] += msBuf[b][i] * synVal;
                    }
                }
            }
        }

        if ( normSq[0] > 1e-10 )
        {
            for ( int b = 0; b < nMsBands; ++b )
                coef[b + 1][0] /= normSq[0];
        }

        double stdSyn = statsSynPan.stddev();
        double stdP = statsP.stddev();
        double scale = ( stdP > 1e-10 ) ? ( stdSyn / stdP ) : 1.0;
        double meanSyn = statsSynPan.mean();
        double meanP = statsP.mean();

        // PASS 2: Stream write transformed Gram-Schmidt tiles
        for ( int r = 0; r < rows; ++r )
        {
            const int yOff = r * tileH;
            const int th = std::min( tileH, h - yOff );
            for ( int c = 0; c < cols; ++c )
            {
                const int xOff = c * tileW;
                const int tw = std::min( tileW, w - xOff );
                const size_t tileSize = static_cast<size_t>( tw ) * th;

                if ( !panDataset.readBandWindow( 1, xOff, yOff, tw, th, panBuf.data() ) )
                    return false;
                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !readMsWindow( b + 1, xOff, yOff, tw, th, msBuf[b].data() ) )
                        return false;
                }

                for ( size_t i = 0; i < tileSize; ++i )
                {
                    if ( panBuf[i] == nodata || std::isnan( panBuf[i] ) )
                    {
                        for ( int b = 0; b < nMsBands; ++b )
                            outBuf[b][i] = nodata;
                        continue;
                    }

                    bool validMs = true;
                    double synVal = 0.0;
                    for ( int b = 0; b < nMsBands; ++b )
                    {
                        if ( msBuf[b][i] == nodata || std::isnan( msBuf[b][i] ) )
                        {
                            validMs = false;
                            break;
                        }
                        synVal += msBuf[b][i];
                    }
                    if ( !validMs )
                    {
                        for ( int b = 0; b < nMsBands; ++b )
                            outBuf[b][i] = nodata;
                        continue;
                    }
                    synVal /= nMsBands;

                    float panMatched = static_cast<float>( ( panBuf[i] - meanP ) * scale + meanSyn );
                    double gs0Sub = panMatched;

                    for ( int b = 0; b < nMsBands; ++b )
                    {
                        const int k = b + 1;
                        double val = msBuf[b][i] + coef[k][0] * ( gs0Sub - synVal );
                        outBuf[b][i] = static_cast<float>( val );
                    }
                }

                for ( int b = 0; b < nMsBands; ++b )
                {
                    if ( !outDataset.writeBandWindow( b + 1, xOff, yOff, tw, th, outBuf[b].data() ) )
                        return false;
                }
            }
        }
        return true;
    }

    if ( errorMessage )
        *errorMessage = QStringLiteral( "Unsupported native fusion method" );
    return false;
}

