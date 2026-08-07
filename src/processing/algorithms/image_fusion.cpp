// image_fusion.cpp — Phase 11.1
#include "image_fusion.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

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
        float defaultMsWeight = (1.0f - panWeight) / nBands;
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

    const int n = width * height;

    // Compute sum of MS bands per pixel
    QVector<float> msSum( n, 0.0f );
    for ( int b = 0; b < nBands; ++b )
    {
        if ( !msBands[b] )
            return result;
        for ( int i = 0; i < n; ++i )
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
        for ( int i = 0; i < n; ++i )
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

    const int n = width * height;

    // Step 1: Compute mean and covariance matrix
    QVector<double> means( nBands, 0.0 );
    QVector<int> counts( nBands, 0 );
    for ( int b = 0; b < nBands; ++b )
    {
        if ( !msBands[b] )
            return result;
        for ( int i = 0; i < n; ++i )
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

    const int n = width * height;

    // Step 1: Histogram-match pan to intensity (mean of R, G, B)
    QVector<float> intensity( n );
    for ( int i = 0; i < n; ++i )
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

    const int n = width * height;

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

    // Track per-component valid mask (a pixel contributes only if valid across
    // synPan and the current band).
    for ( int k = 1; k < total; ++k )
    {
        const int bandIdx = k - 1;
        // working <- ms[bandIdx]
        QVector<float> work( n );
        for ( int i = 0; i < n; ++i )
        {
            bool valid = ( gs[0][i] != nodata && !std::isnan( gs[0][i] ) &&
                           msBands[bandIdx][i] != nodata && !std::isnan( msBands[bandIdx][i] ) );
            work[i] = valid ? msBands[bandIdx][i] : 0.0f;
        }

        for ( int j = 0; j < k; ++j )
        {
            double dot = 0.0;
            double normSq = 0.0;
            for ( int i = 0; i < n; ++i )
            {
                bool valid = ( gs[0][i] != nodata && !std::isnan( gs[0][i] ) &&
                               msBands[bandIdx][i] != nodata && !std::isnan( msBands[bandIdx][i] ) );
                if ( !valid )
                    continue;
                dot += static_cast<double>( work[i] ) * gs[j][i];
                normSq += static_cast<double>( gs[j][i] ) * gs[j][i];
            }
            double c = ( normSq > 1e-20 ) ? ( dot / normSq ) : 0.0;
            coef[k][j] = c;
            for ( int i = 0; i < n; ++i )
            {
                bool valid = ( gs[0][i] != nodata && !std::isnan( gs[0][i] ) &&
                               msBands[bandIdx][i] != nodata && !std::isnan( msBands[bandIdx][i] ) );
                if ( !valid )
                    continue;
                work[i] = static_cast<float>( work[i] - c * gs[j][i] );
            }
        }

        // gs[k] <- work (with nodata preserved)
        for ( int i = 0; i < n; ++i )
        {
            bool valid = ( gs[0][i] != nodata && !std::isnan( gs[0][i] ) &&
                           msBands[bandIdx][i] != nodata && !std::isnan( msBands[bandIdx][i] ) );
            gs[k][i] = valid ? work[i] : nodata;
        }
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

    const int w = panDataset.width();
    const int h = panDataset.height();
    const int msBands = msDataset.bandCount();
    if ( msBands < 1 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Multispectral raster has no bands" );
        return false;
    }

    const size_t pixelCount = static_cast<size_t>( w ) * static_cast<size_t>( h );
    std::vector<float> panData( pixelCount );
    if ( !panDataset.readBandData( 1, panData.data(), w, h ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Failed to read panchromatic band" );
        return false;
    }

    const int nMsBands = std::min( msBands, 4 );
    QVector<std::vector<float>> msData( nMsBands );
    QVector<const float *> msPtrs( nMsBands );
    for ( int b = 0; b < nMsBands; ++b )
    {
        msData[b].resize( pixelCount );
        if ( !msDataset.readBandData( b + 1, msData[b].data(), w, h ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Failed to read multispectral band %1" ).arg( b + 1 );
            return false;
        }
        msPtrs[b] = msData[b].data();
    }

    const float nodata = -9999.0f;
    QVector<QVector<float>> result;

    if ( params.method == QStringLiteral( "linear" ) )
    {
        result = linearWeighted( msPtrs, nMsBands, panData.data(), w, h, nodata,
                                 params.msWeights, params.panWeight );
    }
    else if ( params.method == QStringLiteral( "brovey" ) )
    {
        result = brovey( msPtrs, nMsBands, panData.data(), w, h, nodata );
    }
    else if ( params.method == QStringLiteral( "ihs" ) )
    {
        if ( params.redIdx < 0 || params.greenIdx < 0 || params.blueIdx < 0 ||
             params.redIdx >= nMsBands || params.greenIdx >= nMsBands || params.blueIdx >= nMsBands )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Invalid RGB band selection for IHS fusion" );
            return false;
        }
        result = ihsFusion( msPtrs[params.redIdx], msPtrs[params.greenIdx], msPtrs[params.blueIdx],
                            panData.data(), w, h, nodata );
    }
    else if ( params.method == QStringLiteral( "pca" ) )
    {
        result = pcaFusion( msPtrs, nMsBands, panData.data(), w, h, nodata );
    }
    else if ( params.method == QStringLiteral( "gram_schmidt" ) )
    {
        result = gramSchmidtFusion( msPtrs, nMsBands, panData.data(), w, h, nodata );
    }
    else
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Unsupported native fusion method" );
        return false;
    }

    if ( result.isEmpty() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Fusion produced no output" );
        return false;
    }

    std::vector<std::vector<float>> outBands;
    outBands.reserve( result.size() );
    for ( int b = 0; b < result.size(); ++b )
    {
        const QVector<float> &band = result[b];
        outBands.emplace_back( band.constBegin(), band.constEnd() );
    }

    QString writeError;
    if ( !writeGdalOutput( outputPath, w, h, outBands,
                           panDataset.geoTransform(), panDataset.projection(), &writeError ) )
    {
        if ( errorMessage )
            *errorMessage = writeError;
        return false;
    }

    return true;
}
