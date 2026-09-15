// src/processing/algorithms/spectral_sparse_unmixing.cpp — FISTA sparse unmixing
#include "spectral_sparse_unmixing.h"

#include "spectral_classification.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpectralSparseUnmixing
{

namespace
{
    // Defensive cap: the Gram materialization is n^2 doubles; 2048 atoms keep
    // it at 32 MB of working state. Sparse unmixing dictionaries beyond this
    // are a caller design error, not a runtime scaling problem.
    constexpr int kMaxAtoms = 2048;

    bool pixelFinite( const float *pixel, int bands )
    {
        for ( int b = 0; b < bands; ++b )
            if ( !std::isfinite( pixel[b] ) )
                return false;
        return true;
    }
} // namespace

bool buildDictionary( const float *endmembers, int bands, int nEndmembers,
                      const Config &config, Dictionary *out,
                      QString *errorMessage )
{
    if ( !endmembers || !out || bands <= 0 || nEndmembers <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid sparse unmixing dictionary arguments" );
        return false;
    }
    if ( config.lambda < 0.0 || config.sumToOnePenalty < 0.0
         || !std::isfinite( config.lambda ) || !std::isfinite( config.sumToOnePenalty ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "lambda and sumToOnePenalty must be finite values >= 0" );
        return false;
    }
    if ( config.maxIterations < 1 || config.tolerance <= 0.0 || !std::isfinite( config.tolerance ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "maxIterations >= 1 and a finite tolerance > 0 are required" );
        return false;
    }
    if ( nEndmembers > kMaxAtoms )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "Sparse unmixing dictionary is capped at %1 atoms (got %2)" )
                                .arg( kMaxAtoms )
                                .arg( nEndmembers );
        return false;
    }

    const size_t n = static_cast<size_t>( nEndmembers );

    // Norm guard (relative, mirroring the FCLS refusal).
    double maxNormSq = 0.0;
    std::vector<double> normSq( n, 0.0 );
    for ( int e = 0; e < nEndmembers; ++e )
    {
        double sum = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            const double v = static_cast<double>( endmembers[static_cast<size_t>( e ) * bands + b] );
            if ( !std::isfinite( v ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Endmember %1 contains a non-finite value" ).arg( e );
                return false;
            }
            sum += v * v;
        }
        normSq[e] = sum;
        maxNormSq = std::max( maxNormSq, sum );
    }
    if ( maxNormSq <= 0.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "All endmembers are zero vectors" );
        return false;
    }
    for ( int e = 0; e < nEndmembers; ++e )
    {
        if ( normSq[static_cast<size_t>( e )] <= maxNormSq * 1e-24 )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Endmember %1 is (numerically) a zero vector" ).arg( e );
            return false;
        }
    }

    // Pairwise spectral-angle redundancy guard. An L1 solution over
    // near-duplicate atoms splits mass arbitrarily; refuse rather than emit
    // an uninterpretable result. (Angle threshold <= 0 disables the guard,
    // which is a documented escape hatch for deliberately correlated
    // dictionaries — the configuration that turns this into plain NNLS.)
    if ( config.collinearAngleDegrees > 0.0 )
    {
        const float nodata = SpectralClassification::kNoDataSentinel;
        const double thresholdRadians = config.collinearAngleDegrees * 3.14159265358979323846 / 180.0;
        std::vector<float> spectrumA( static_cast<size_t>( bands ) );
        std::vector<float> spectrumB( static_cast<size_t>( bands ) );
        for ( int a = 0; a < nEndmembers; ++a )
        {
            std::copy( endmembers + static_cast<size_t>( a ) * bands,
                       endmembers + static_cast<size_t>( a + 1 ) * bands,
                       spectrumA.begin() );
            for ( int c = a + 1; c < nEndmembers; ++c )
            {
                std::copy( endmembers + static_cast<size_t>( c ) * bands,
                           endmembers + static_cast<size_t>( c + 1 ) * bands,
                           spectrumB.begin() );
                const double angle = SpectralClassification::spectralAngle(
                    spectrumA.data(), spectrumB.data(), static_cast<size_t>( bands ), nodata );
                if ( std::isfinite( angle ) && angle < thresholdRadians )
                {
                    if ( errorMessage )
                        *errorMessage = QStringLiteral(
                            "Endmembers %1 and %2 are near-collinear (spectral angle %.4g < %.4g deg); "
                            "the sparse solution would not be interpretable" )
                                            .arg( a )
                                            .arg( c )
                                            .arg( angle * 180.0 / 3.14159265358979323846 )
                                            .arg( config.collinearAngleDegrees );
                    return false;
                }
            }
        }
    }

    // Band-major endmember copy: E[b * n + e].
    out->endmembers.assign( static_cast<size_t>( bands ) * n, 0.0 );
    for ( int e = 0; e < nEndmembers; ++e )
    {
        for ( int b = 0; b < bands; ++b )
            out->endmembers[static_cast<size_t>( b ) * n + e] =
                static_cast<double>( endmembers[static_cast<size_t>( e ) * bands + b] );
    }
    out->bands = bands;
    out->nEndmembers = nEndmembers;

    // G = E^T E (+ rho * 11^T when the sum-to-one penalty is active — the
    // same augmentation as the master FCLS solver).
    const double rho = config.sumToOnePenalty;
    out->gram.assign( n * n, 0.0 );
    for ( int a = 0; a < nEndmembers; ++a )
    {
        for ( int c = a; c < nEndmembers; ++c )
        {
            double sum = 0.0;
            for ( int b = 0; b < bands; ++b )
                sum += out->endmembers[static_cast<size_t>( b ) * n + a]
                       * out->endmembers[static_cast<size_t>( b ) * n + c];
            out->gram[static_cast<size_t>( a ) * n + c] = sum;
            out->gram[static_cast<size_t>( c ) * n + a] = sum;
        }
    }
    if ( rho > 0.0 )
    {
        for ( int a = 0; a < nEndmembers; ++a )
            for ( int c = 0; c < nEndmembers; ++c )
                out->gram[static_cast<size_t>( a ) * n + c] += rho;
    }

    out->etUnit.assign( n, 0.0 );
    for ( int e = 0; e < nEndmembers; ++e )
        out->etUnit[static_cast<size_t>( e )] = 1.0; // E^T 1 for the penalty RHS

    // Lipschitz estimate: bounded deterministic power iteration on G.
    // Start vector = 1/sqrt(n); a fixed sweep count keeps the estimate
    // reproducible across runs and platforms (no adaptive early exit that
    // could diverge on rounding).
    std::vector<double> v( n, 1.0 / std::sqrt( static_cast<double>( n ) ) );
    double lambdaMax = 0.0;
    const int kPowerSweeps = 64;
    for ( int sweep = 0; sweep < kPowerSweeps; ++sweep )
    {
        std::vector<double> w( n, 0.0 );
        for ( int i = 0; i < nEndmembers; ++i )
        {
            double sum = 0.0;
            for ( int j = 0; j < nEndmembers; ++j )
                sum += out->gram[static_cast<size_t>( i ) * n + j] * v[static_cast<size_t>( j )];
            w[static_cast<size_t>( i )] = sum;
        }
        double norm = 0.0;
        for ( int i = 0; i < nEndmembers; ++i )
            norm += w[static_cast<size_t>( i )] * w[static_cast<size_t>( i )];
        norm = std::sqrt( norm );
        if ( !( norm > 0.0 ) || !std::isfinite( norm ) )
            break;
        lambdaMax = norm;
        for ( int i = 0; i < nEndmembers; ++i )
            v[static_cast<size_t>( i )] = w[static_cast<size_t>( i )] / norm;
    }
    // The estimate may undershoot lambda_max by up to (1 - (r2/r1)^2K); a
    // 5% inflation keeps the FISTA step safely below the stability bound.
    out->lipschitz = lambdaMax * 1.05;

    return true;
}

bool solveSparsePixel( const float *pixel, const Dictionary &dictionary,
                       const Config &config,
                       std::vector<double> *abundances,
                       int32_t *iterationsUsed, bool *converged,
                       QString *errorMessage )
{
    if ( !pixel || !abundances || !iterationsUsed || !converged )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid sparse solve arguments" );
        return false;
    }
    const int bands = dictionary.bands;
    const int nEndmembers = dictionary.nEndmembers;
    if ( bands <= 0 || nEndmembers <= 0
         || dictionary.gram.size() != static_cast<size_t>( nEndmembers ) * nEndmembers )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Dictionary was not built (call buildDictionary first)" );
        return false;
    }

    if ( !pixelFinite( pixel, bands ) )
    {
        abundances->assign( static_cast<size_t>( nEndmembers ),
                            std::numeric_limits<double>::quiet_NaN() );
        *iterationsUsed = 0;
        *converged = false;
        return true;
    }

    const size_t n = static_cast<size_t>( nEndmembers );
    // q = E^T x (+ rho * 1 when the sum-to-one penalty is active).
    std::vector<double> q( n, 0.0 );
    for ( int e = 0; e < nEndmembers; ++e )
    {
        double sum = 0.0;
        for ( int b = 0; b < bands; ++b )
            sum += dictionary.endmembers[static_cast<size_t>( b ) * n + e]
                   * static_cast<double>( pixel[b] );
        q[e] = sum;
    }
    if ( config.sumToOnePenalty > 0.0 )
    {
        for ( int e = 0; e < nEndmembers; ++e )
            q[e] += config.sumToOnePenalty;
    }

    // FISTA with prox(v) = max(v - lambda * step, 0): the exact prox of
    // lambda * ||.||_1 + i_{>=0} (separable sum of soft threshold and clamp).
    const double step = 1.0 / std::max( dictionary.lipschitz, std::numeric_limits<double>::denorm_min() );
    const double threshold = config.lambda * step;

    std::vector<double> a( n, 0.0 );
    std::vector<double> aPrev( n, 0.0 );
    std::vector<double> y( n, 0.0 );
    std::vector<double> grad( n, 0.0 );

    double momentum = 1.0;
    int used = 0;
    bool reached = false;
    for ( int it = 0; it < config.maxIterations; ++it )
    {
        used = it + 1;
        // grad = G y - q
        for ( int i = 0; i < nEndmembers; ++i )
        {
            double sum = 0.0;
            for ( int j = 0; j < nEndmembers; ++j )
                sum += dictionary.gram[static_cast<size_t>( i ) * n + j] * y[static_cast<size_t>( j )];
            grad[static_cast<size_t>( i )] = sum - q[static_cast<size_t>( i )];
        }
        double delta = 0.0;
        double prevNorm = 0.0;
        for ( int i = 0; i < nEndmembers; ++i )
        {
            const double next = std::max( 0.0,
                                          y[static_cast<size_t>( i )] - step * grad[static_cast<size_t>( i )]
                                              - threshold );
            delta = std::max( delta, std::abs( next - a[static_cast<size_t>( i )] ) );
            prevNorm = std::max( prevNorm, std::abs( a[static_cast<size_t>( i )] ) );
            aPrev[static_cast<size_t>( i )] = a[static_cast<size_t>( i )];
            a[static_cast<size_t>( i )] = next;
        }
        if ( delta <= config.tolerance * std::max( 1.0, prevNorm ) )
        {
            reached = true;
            break;
        }
        const double momentumNext = ( 1.0 + std::sqrt( 1.0 + 4.0 * momentum * momentum ) ) / 2.0;
        const double beta = ( momentum - 1.0 ) / momentumNext;
        for ( int i = 0; i < nEndmembers; ++i )
            y[static_cast<size_t>( i )] = a[static_cast<size_t>( i )]
                                          + beta * ( a[static_cast<size_t>( i )] - aPrev[static_cast<size_t>( i )] );
        momentum = momentumNext;
    }

    *abundances = std::move( a );
    *iterationsUsed = used;
    *converged = reached;
    return true;
}

bool unmixSparse( const float *pixels, size_t count, int bands,
                  const float *endmembers, int nEndmembers,
                  const Config &config, SparseUnmixResult *result,
                  QString *errorMessage )
{
    if ( !pixels || !endmembers || !result || count == 0 || bands <= 0 || nEndmembers <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid sparse unmixing arguments" );
        return false;
    }

    Dictionary dictionary;
    if ( !buildDictionary( endmembers, bands, nEndmembers, config, &dictionary, errorMessage ) )
        return false;

    const size_t n = static_cast<size_t>( nEndmembers );
    result->abundances.assign( count * n, 0.0f );
    result->reconstructionError.assign( count, 0.0f );
    result->abundanceSums.assign( count, 0.0f );
    result->iterations.assign( count, 0 );
    result->converged.assign( count, 0 );

    std::vector<double> abundances;

    for ( size_t p = 0; p < count; ++p )
    {
        const float *pixel = pixels + p * static_cast<size_t>( bands );
        int32_t used = 0;
        bool converged = false;
        if ( !solveSparsePixel( pixel, dictionary, config, &abundances, &used, &converged, errorMessage ) )
            return false;
        if ( !pixelFinite( pixel, bands ) )
        {
            // NaN convention: NaN abundances, NaN error/sum.
            for ( int e = 0; e < nEndmembers; ++e )
                result->abundances[p * n + static_cast<size_t>( e )] =
                    std::numeric_limits<float>::quiet_NaN();
            result->reconstructionError[p] = std::numeric_limits<float>::quiet_NaN();
            result->abundanceSums[p] = std::numeric_limits<float>::quiet_NaN();
            result->iterations[p] = 0;
            result->converged[p] = 0;
            continue;
        }
        double sum = 0.0;
        double squaredError = 0.0;
        for ( int e = 0; e < nEndmembers; ++e )
        {
            const double a = abundances[static_cast<size_t>( e )];
            result->abundances[p * n + static_cast<size_t>( e )] = static_cast<float>( a );
            sum += a;
        }
        // Reconstruction error: ||x - E a|| / sqrt(bands).
        for ( int b = 0; b < bands; ++b )
        {
            double recon = 0.0;
            for ( int e = 0; e < nEndmembers; ++e )
                recon += dictionary.endmembers[static_cast<size_t>( b ) * n + e]
                         * abundances[static_cast<size_t>( e )];
            const double d = recon - static_cast<double>( pixel[b] );
            squaredError += d * d;
        }
        result->reconstructionError[p] =
            static_cast<float>( std::sqrt( squaredError / static_cast<double>( bands ) ) );
        result->abundanceSums[p] = static_cast<float>( sum );
        result->iterations[p] = used;
        result->converged[p] = converged ? 1 : 0;
    }
    return true;
}

} // namespace SpectralSparseUnmixing
