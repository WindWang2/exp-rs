// src/processing/algorithms/spectral_unmixing.cpp — linear spectral unmixing
#include "spectral_unmixing.h"
#include "processing/algorithms/primitives/dense_linalg.h"
#include "processing/algorithms/endmember_extraction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace SpectralUnmixing
{

namespace
{

// The Gram-matrix inversion lives in the shared primitive
// primitives/dense_linalg.h (single owner; this file used to carry its own
// Gauss-Jordan copy with the same 1e-12 pivot threshold).
} // namespace

bool unmix( const float *pixels, size_t count, int bands,
            const float *endmembers, int nEndmembers,
            UnmixResult *result, QString *errorMessage )
{
    if ( !pixels || !endmembers || !result || count == 0 || bands <= 0
         || nEndmembers < 1 || nEndmembers > bands )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid unmixing arguments" );
        return false;
    }

    result->abundances.assign( count * static_cast<size_t>( nEndmembers ), 0.0f );
    result->reconstructionError.assign( count, 0.0f );

    // Gram matrix G = E^T E (nEndmembers x nEndmembers), reused per pixel.
    std::vector<double> gram( static_cast<size_t>( nEndmembers ) * nEndmembers, 0.0 );
    for ( int e = 0; e < nEndmembers; ++e )
    {
        for ( int f = 0; f < nEndmembers; ++f )
        {
            double sum = 0.0;
            for ( int b = 0; b < bands; ++b )
                sum += static_cast<double>( endmembers[static_cast<size_t>( e ) * bands + b] )
                       * endmembers[static_cast<size_t>( f ) * bands + b];
            gram[static_cast<size_t>( e ) * nEndmembers + f] = sum;
        }
    }

    double gramTrace = 0.0;
    for ( int e = 0; e < nEndmembers; ++e )
        gramTrace += gram[static_cast<size_t>( e ) * nEndmembers + e];
    const double kRidge = std::max( 1e-9, ( gramTrace / nEndmembers ) * 1e-7 );

    // Precompute (G + ridge I)^-1 once: the per-pixel hot loop then solves the
    // normal equations with a single matrix-vector product (O(E^2) per pixel
    // instead of a full Gaussian elimination, O(E^3) per pixel).
    std::vector<double> invGram = gram;
    for ( int e = 0; e < nEndmembers; ++e )
        invGram[static_cast<size_t>( e ) * nEndmembers + e] += kRidge;
    const bool invertible = sicnu::primitives::invertDenseMatrixInPlace( invGram, nEndmembers );

    std::vector<double> rhs( nEndmembers, 0.0 );
    std::vector<double> abundance( nEndmembers, 0.0 );
    std::vector<double> est( bands, 0.0 );

    for ( size_t p = 0; p < count; ++p )
    {
        const float *x = pixels + p * static_cast<size_t>( bands );

        bool hasNonFinite = false;
        for ( int b = 0; b < bands; ++b )
        {
            if ( !std::isfinite( x[b] ) ) { hasNonFinite = true; break; }
        }
        if ( hasNonFinite )
        {
            for ( int e = 0; e < nEndmembers; ++e )
                result->abundances[p * static_cast<size_t>( nEndmembers ) + e] =
                    std::numeric_limits<float>::quiet_NaN();
            result->reconstructionError[p] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }

        // Right-hand side: E^T x.
        for ( int e = 0; e < nEndmembers; ++e )
        {
            const float *emRow = &endmembers[static_cast<size_t>( e ) * bands];
            double sum = 0.0;
            for ( int b = 0; b < bands; ++b )
                sum += static_cast<double>( emRow[b] ) * x[b];
            rhs[static_cast<size_t>( e )] = sum;
        }

        // Solve (G + ridge I) a = rhs via a = (G + ridge I)^-1 rhs.
        if ( !invertible )
        {
            // Singular even with the ridge: leave abundances at zero and the
            // reconstruction error as the pixel norm.
            double normSq = 0.0;
            for ( int b = 0; b < bands; ++b )
                normSq += static_cast<double>( x[b] ) * x[b];
            result->reconstructionError[p] =
                static_cast<float>( std::sqrt( normSq / bands ) );
            continue;
        }
        for ( int e = 0; e < nEndmembers; ++e )
        {
            double sum = 0.0;
            for ( int f = 0; f < nEndmembers; ++f )
                sum += invGram[static_cast<size_t>( e ) * nEndmembers + f]
                       * rhs[static_cast<size_t>( f )];
            abundance[static_cast<size_t>( e )] = sum;
        }

        // Clip to [0, 1] and renormalize to unit sum (approximate sum-to-one).
        double sum = 0.0;
        for ( int e = 0; e < nEndmembers; ++e )
        {
            abundance[static_cast<size_t>( e )] =
                std::clamp( abundance[static_cast<size_t>( e )], 0.0, 1.0 );
            sum += abundance[static_cast<size_t>( e )];
        }
        if ( sum > 1e-12 )
        {
            for ( int e = 0; e < nEndmembers; ++e )
                abundance[static_cast<size_t>( e )] /= sum;
        }

        for ( int e = 0; e < nEndmembers; ++e )
            result->abundances[p * static_cast<size_t>( nEndmembers ) + e] =
                static_cast<float>( abundance[static_cast<size_t>( e )] );

        // Reconstruction error: RMSE of x - E a over the bands.
        std::fill( est.begin(), est.end(), 0.0 );
        for ( int e = 0; e < nEndmembers; ++e )
        {
            const double ae = abundance[static_cast<size_t>( e )];
            if ( ae == 0.0 )
                continue;
            const float *emRow = &endmembers[static_cast<size_t>( e ) * bands];
            for ( int b = 0; b < bands; ++b )
                est[static_cast<size_t>( b )] += ae * static_cast<double>( emRow[b] );
        }

        double errorSq = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            const double diff = static_cast<double>( x[b] ) - est[static_cast<size_t>( b )];
            errorSq += diff * diff;
        }
        result->reconstructionError[p] =
            static_cast<float>( std::sqrt( errorSq / bands ) );
    }
    return true;
}

namespace
{

/// Cholesky factorization (lower triangular, in place) of a symmetric PD
/// matrix. Returns false when a pivot is non-positive — i.e. the matrix is
/// not positive definite at the working tolerance.
bool choleskyInPlace( std::vector<double> &a, int n, double minPivot )
{
    for ( int i = 0; i < n; ++i )
    {
        for ( int j = 0; j <= i; ++j )
        {
            double sum = a[static_cast<size_t>( i ) * n + j];
            for ( int k = 0; k < j; ++k )
                sum -= a[static_cast<size_t>( i ) * n + k]
                       * a[static_cast<size_t>( j ) * n + k];
            if ( i == j )
            {
                if ( sum <= minPivot )
                    return false;
                a[static_cast<size_t>( i ) * n + i] = std::sqrt( sum );
            }
            else
            {
                a[static_cast<size_t>( i ) * n + j] =
                    sum / a[static_cast<size_t>( j ) * n + j];
            }
        }
        for ( int j = i + 1; j < n; ++j )
            a[static_cast<size_t>( i ) * n + j] = 0.0;
    }
    return true;
}

/// Solve L Lᵀ z = b in place for the factored @p l (from choleskyInPlace).
void choleskySolve( const std::vector<double> &l, int n, const std::vector<double> &b,
                    std::vector<double> *z )
{
    z->assign( static_cast<size_t>( n ), 0.0 );
    for ( int i = 0; i < n; ++i )
    {
        double sum = b[static_cast<size_t>( i )];
        for ( int k = 0; k < i; ++k )
            sum -= l[static_cast<size_t>( i ) * n + k] * ( *z )[static_cast<size_t>( k )];
        ( *z )[static_cast<size_t>( i )] = sum / l[static_cast<size_t>( i ) * n + i];
    }
    for ( int i = n - 1; i >= 0; --i )
    {
        double sum = ( *z )[static_cast<size_t>( i )];
        for ( int k = i + 1; k < n; ++k )
            sum -= l[static_cast<size_t>( k ) * n + i] * ( *z )[static_cast<size_t>( k )];
        ( *z )[static_cast<size_t>( i )] = sum / l[static_cast<size_t>( i ) * n + i];
    }
}

constexpr int kMaxNnlsSweeps = 4096;

/// Lawson–Hanson active-set NNLS on the normal equations: minimize
/// ½ zᵀ G z − uᵀ z subject to z ≥ 0, with @p gram positive definite.
/// Deterministic pivot order (most-negative gradient first, lowest index on
/// ties). Finite by the NNLS theorem; the sweep cap is a defensive bound.
bool nnlsNormalEquations( const std::vector<double> &gram, int n,
                          const std::vector<double> &u, std::vector<double> *z )
{
    z->assign( static_cast<size_t>( n ), 0.0 );
    std::vector<bool> passive( static_cast<size_t>( n ), false );
    int passiveCount = 0;

    std::vector<double> gradient( static_cast<size_t>( n ), 0.0 );
    std::vector<double> l( static_cast<size_t>( n ) * n, 0.0 );
    std::vector<double> candidate( static_cast<size_t>( n ), 0.0 );
    // Scratch reused across active-set iterations: this solver runs once per
    // pixel, so per-iteration (re)allocation would dominate the profile.
    std::vector<double> gPp( static_cast<size_t>( n ) * n, 0.0 );
    std::vector<double> uP( static_cast<size_t>( n ), 0.0 );
    std::vector<double> zP( static_cast<size_t>( n ), 0.0 );
    std::vector<int> members;
    members.reserve( static_cast<size_t>( n ) );

    // Dual-feasibility pivot tolerance, scaled to the problem: rounding in
    // G·z is ~eps * |G| * |z|, so an absolute threshold (e.g. 1e-12) sits
    // BELOW the noise floor of large-penalty systems and pivots forever on
    // a zero-gradient variable. 1e-11 * |u|max is far above the noise yet
    // corresponds to a negligible abundance error (~1e-11 / |G|diag).
    double maxAbsU = 1.0;
    for ( int i = 0; i < n; ++i )
        maxAbsU = std::max( maxAbsU, std::abs( u[static_cast<size_t>( i )] ) );
    const double pivotTol = 1e-11 * maxAbsU;

    for ( int sweep = 0; sweep < kMaxNnlsSweeps; ++sweep )
    {
        // Gradient of the objective at the current point.
        for ( int i = 0; i < n; ++i )
        {
            double sum = u[static_cast<size_t>( i )];
            for ( int j = 0; j < n; ++j )
                sum -= gram[static_cast<size_t>( i ) * n + j] * ( *z )[static_cast<size_t>( j )];
            gradient[static_cast<size_t>( i )] = sum;
        }
        int pivot = -1;
        double best = pivotTol;
        for ( int i = 0; i < n; ++i )
        {
            if ( passive[static_cast<size_t>( i )] )
                continue;
            if ( gradient[static_cast<size_t>( i )] > best )
            {
                best = gradient[static_cast<size_t>( i )];
                pivot = i;
            }
        }
        if ( pivot < 0 )
            return true; // KKT satisfied
        passive[static_cast<size_t>( pivot )] = true;
        ++passiveCount;

        bool innerProgressed = true;
        while ( innerProgressed )
        {
            // Solve the unconstrained problem on the passive set.
            gPp.assign( static_cast<size_t>( passiveCount * passiveCount ), 0.0 );
            uP.assign( static_cast<size_t>( passiveCount ), 0.0 );
            members.clear();
            for ( int i = 0; i < n; ++i )
                if ( passive[static_cast<size_t>( i )] )
                    members.push_back( i );
            for ( int r = 0; r < passiveCount; ++r )
            {
                uP[static_cast<size_t>( r )] = u[static_cast<size_t>( members[static_cast<size_t>( r )] )];
                for ( int c = 0; c < passiveCount; ++c )
                    gPp[static_cast<size_t>( r ) * passiveCount + c] =
                        gram[static_cast<size_t>( members[static_cast<size_t>( r )] ) * n
                             + members[static_cast<size_t>( c )]];
            }
            l = gPp;
            if ( !choleskyInPlace( l, passiveCount, 0.0 ) )
                return false; // principal submatrix lost PD
            choleskySolve( l, passiveCount, uP, &zP );
            for ( int r = 0; r < passiveCount; ++r )
                candidate[static_cast<size_t>( members[static_cast<size_t>( r )] )] =
                    zP[static_cast<size_t>( r )];

            bool allPositive = true;
            for ( int r = 0; r < passiveCount; ++r )
                if ( zP[static_cast<size_t>( r )] <= 0.0 )
                    allPositive = false;
            if ( allPositive )
            {
                std::fill( z->begin(), z->end(), 0.0 );
                for ( int r = 0; r < passiveCount; ++r )
                    ( *z )[static_cast<size_t>( members[static_cast<size_t>( r )] )] =
                        zP[static_cast<size_t>( r )];
                break;
            }

            // Step toward the candidate until a variable hits zero, then
            // move it back to the active set.
            double alpha = std::numeric_limits<double>::max();
            for ( int r = 0; r < passiveCount; ++r )
            {
                const int m = members[static_cast<size_t>( r )];
                if ( zP[static_cast<size_t>( r )] <= 0.0 )
                {
                    if ( ( *z )[static_cast<size_t>( m )] <= 0.0 )
                    {
                        // Degenerate: an already-zero passive member wants to
                        // go negative — deactivate it with a zero step.
                        alpha = 0.0;
                        break;
                    }
                    const double denom =
                        ( *z )[static_cast<size_t>( m )] - zP[static_cast<size_t>( r )];
                    if ( denom > 1e-300 )
                        alpha = std::min( alpha,
                                          ( *z )[static_cast<size_t>( m )] / denom );
                }
            }
            if ( alpha == std::numeric_limits<double>::max() )
                return false; // no feasible step — defensive
            for ( int i = 0; i < n; ++i )
            {
                if ( !passive[static_cast<size_t>( i )] )
                    continue;
                ( *z )[static_cast<size_t>( i )] +=
                    alpha * ( candidate[static_cast<size_t>( i )]
                              - ( *z )[static_cast<size_t>( i )] );
                if ( ( *z )[static_cast<size_t>( i )] <= 1e-14 )
                {
                    ( *z )[static_cast<size_t>( i )] = 0.0;
                    passive[static_cast<size_t>( i )] = false;
                    --passiveCount;
                }
            }
            if ( passiveCount == 0 )
                innerProgressed = false;
        }
    }
    return false; // sweep cap exhausted — defensive, should be unreachable
}

} // namespace

bool unmixFcls( const float *pixels, size_t count, int bands,
                const float *endmembers, int nEndmembers,
                UnmixResult *result, QString *errorMessage )
{
    if ( !pixels || !endmembers || !result || count == 0 || bands <= 0
         || nEndmembers < 1 || nEndmembers > bands )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid FCLS unmixing arguments" );
        return false;
    }

    // Fail-closed guards: zero-norm endmembers and collinear (rank-deficient)
    // endmember sets refuse up front — a fully constrained solve over a
    // degenerate simplex is not defined.
    double maxNormSq = 0.0;
    std::vector<double> normSq( static_cast<size_t>( nEndmembers ), 0.0 );
    for ( int e = 0; e < nEndmembers; ++e )
    {
        double sum = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            const double v = static_cast<double>( endmembers[static_cast<size_t>( e ) * bands + b] );
            sum += v * v;
        }
        normSq[static_cast<size_t>( e )] = sum;
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
                *errorMessage = QStringLiteral( "Endmember %1 is (numerically) a zero "
                                                "vector" )
                                    .arg( e );
            return false;
        }
    }

    std::vector<double> gram( static_cast<size_t>( nEndmembers ) * nEndmembers, 0.0 );
    double gramTrace = 0.0;
    for ( int e = 0; e < nEndmembers; ++e )
    {
        for ( int f = e; f < nEndmembers; ++f )
        {
            double sum = 0.0;
            for ( int b = 0; b < bands; ++b )
                sum += static_cast<double>( endmembers[static_cast<size_t>( e ) * bands + b] )
                       * endmembers[static_cast<size_t>( f ) * bands + b];
            gram[static_cast<size_t>( e ) * nEndmembers + f] = sum;
            gram[static_cast<size_t>( f ) * nEndmembers + e] = sum;
        }
        gramTrace += gram[static_cast<size_t>( e ) * nEndmembers + e];
    }
    const double gramMeanDiag = gramTrace / nEndmembers;
    {
        // PD probe of the raw Gram matrix: a rank-deficient endmember set has
        // a singular Gram matrix (collinear endmembers refuse here).
        std::vector<double> probe = gram;
        if ( !choleskyInPlace( probe, nEndmembers, gramMeanDiag * 1e-12 ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Endmember matrix is rank-deficient "
                                                "(collinear endmembers; Gram pivot at or "
                                                "below 1e-12x mean diagonal); FCLS "
                                                "requires a non-degenerate simplex" );
            return false;
        }
    }

    // Penalty augmentation for sum-to-one: G~ = G + rho*11^T, u~ = u + rho.
    const double rho = gramMeanDiag * 1e6;
    std::vector<double> augmentedGram( static_cast<size_t>( nEndmembers ) * nEndmembers );
    for ( int e = 0; e < nEndmembers; ++e )
        for ( int f = 0; f < nEndmembers; ++f )
            augmentedGram[static_cast<size_t>( e ) * nEndmembers + f] =
                gram[static_cast<size_t>( e ) * nEndmembers + f] + rho;
    // The augmented matrix stays PD (G PD, 11^T PSD, rho > 0).

    result->abundances.assign( count * static_cast<size_t>( nEndmembers ), 0.0f );
    result->reconstructionError.assign( count, 0.0f );

    std::vector<double> u( nEndmembers, 0.0 );
    std::vector<double> augmentedU( nEndmembers, 0.0 );
    std::vector<double> abundance( nEndmembers, 0.0 );
    std::vector<double> est( bands, 0.0 );

    for ( size_t p = 0; p < count; ++p )
    {
        const float *x = pixels + p * static_cast<size_t>( bands );

        bool hasNonFinite = false;
        for ( int b = 0; b < bands; ++b )
            if ( !std::isfinite( x[b] ) ) { hasNonFinite = true; break; }
        if ( hasNonFinite )
        {
            for ( int e = 0; e < nEndmembers; ++e )
                result->abundances[p * static_cast<size_t>( nEndmembers ) + e] =
                    std::numeric_limits<float>::quiet_NaN();
            result->reconstructionError[p] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }

        for ( int e = 0; e < nEndmembers; ++e )
        {
            const float *emRow = &endmembers[static_cast<size_t>( e ) * bands];
            double sum = 0.0;
            for ( int b = 0; b < bands; ++b )
                sum += static_cast<double>( emRow[b] ) * x[b];
            u[static_cast<size_t>( e )] = sum;
            augmentedU[static_cast<size_t>( e )] = sum + rho;
        }

        if ( !nnlsNormalEquations( augmentedGram, nEndmembers, augmentedU, &abundance ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "FCLS inner solver failed to converge" );
            return false;
        }

        for ( int e = 0; e < nEndmembers; ++e )
            result->abundances[p * static_cast<size_t>( nEndmembers ) + e] =
                static_cast<float>( abundance[static_cast<size_t>( e )] );

        std::fill( est.begin(), est.end(), 0.0 );
        for ( int e = 0; e < nEndmembers; ++e )
        {
            const double ae = abundance[static_cast<size_t>( e )];
            if ( ae == 0.0 )
                continue;
            const float *emRow = &endmembers[static_cast<size_t>( e ) * bands];
            for ( int b = 0; b < bands; ++b )
                est[static_cast<size_t>( b )] += ae * static_cast<double>( emRow[b] );
        }
        double errorSq = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            const double diff = static_cast<double>( x[b] ) - est[static_cast<size_t>( b )];
            errorSq += diff * diff;
        }
        result->reconstructionError[p] =
            static_cast<float>( std::sqrt( errorSq / bands ) );
    }
    return true;
}

} // namespace SpectralUnmixing

// ─── D13 · typed unmixing seam (Day 13) ───────────────────────────────────
namespace exp_spectral
{
  bool SpectralUnmixing::extractEndmembers( const float *pixels, size_t pixelCount, int bandCount,
                                            int endmemberCount, EndmemberExtractionMethod method,
                                            std::vector<float> *outEndmembers, unsigned int seed )
  {
    Q_UNUSED( seed ); // both extractors below are deterministic; seed reserved at the seam
    if ( outEndmembers == nullptr )
      return false;
    outEndmembers->clear();
    if ( pixels == nullptr || pixelCount == 0 || bandCount <= 0 ||
         endmemberCount <= 0 || static_cast<size_t>( endmemberCount ) > pixelCount )
      return false;

    if ( method == EndmemberExtractionMethod::PixelPurityIndex )
    {
      // Delegate to the proven seeded PPI kernel (legacy namespace).
      EndmemberExtraction::EndmemberResult ppi;
      if ( !EndmemberExtraction::pixelPurityIndex( pixels, pixelCount, bandCount,
                                                   endmemberCount, 256, &ppi ) )
        return false;
      outEndmembers->resize( ppi.endmembers.size() );
      std::copy( ppi.endmembers.begin(), ppi.endmembers.end(), outEndmembers->begin() );
      return true;
    }

    // Vertex Component Analysis (sequential simplex growing, Nascimento–Dias
    // spirit): each step takes the pixel farthest from the AFFINE HULL of the
    // endmembers found so far (first pick: farthest from the mean; later
    // picks: farthest from the hull through the first pick), and returns the
    // ORIGINAL pixel spectra at those indices. Measuring against the affine
    // hull — not the mean-relative linear span — is what lets a noiseless
    // p-endmember mixture cube (which spans exactly p−1 affine dimensions)
    // still expose its p-th vertex instead of collapsing to zero residual.
    const size_t B = static_cast<size_t>( bandCount );
    std::vector<double> mean( B, 0.0 );
    for ( size_t p = 0; p < pixelCount; ++p )
      for ( size_t b = 0; b < B; ++b )
        mean[b] += pixels[p * B + b];
    for ( size_t b = 0; b < B; ++b )
      mean[b] /= static_cast<double>( pixelCount );

    // Orthonormal basis Q of the hull directions already "used up"
    // (differences relative to the first chosen pixel).
    std::vector<std::vector<double>> basis;
    basis.reserve( static_cast<size_t>( endmemberCount ) );
    std::vector<size_t> chosen;
    chosen.reserve( static_cast<size_t>( endmemberCount ) );

    for ( int k = 0; k < endmemberCount; ++k )
    {
      double bestNorm = -1.0;
      size_t bestPixel = 0;
      std::vector<double> bestDir( B, 0.0 );
      bool haveCandidate = false;

      // Reference point: the mean for the first pick, the first chosen
      // endmember afterwards (hull origin).
      const bool useMean = ( k == 0 );
      const float *referencePixel = useMean ? nullptr : pixels + chosen[0] * B;

      for ( size_t p = 0; p < pixelCount; ++p )
      {
        if ( std::find( chosen.begin(), chosen.end(), p ) != chosen.end() )
          continue;
        // Project (x - reference) onto the orthogonal complement of span(Q).
        std::vector<double> dir( B );
        for ( size_t b = 0; b < B; ++b )
        {
          const double refValue = useMean ? mean[b] : static_cast<double>( referencePixel[b] );
          dir[b] = pixels[p * B + b] - refValue;
        }
        for ( const std::vector<double> &q : basis )
        {
          double dot = 0.0;
          for ( size_t b = 0; b < B; ++b )
            dot += dir[b] * q[b];
          for ( size_t b = 0; b < B; ++b )
            dir[b] -= dot * q[b];
        }
        double norm2 = 0.0;
        for ( size_t b = 0; b < B; ++b )
          norm2 += dir[b] * dir[b];
        if ( norm2 > bestNorm )
        {
          bestNorm = norm2;
          bestPixel = p;
          bestDir = std::move( dir );
          haveCandidate = true;
        }
      }
      if ( !haveCandidate || !( bestNorm > 1e-12 ) )
        return false; // every remaining pixel lies on the endmember hull:
                      // the cube carries fewer vertices than requested

      const size_t chosenPixel = bestPixel;
      chosen.push_back( chosenPixel );
      if ( k == 0 )
        continue; // no hull direction exists yet; the second pick anchors it

      // Gram-Schmidt the new hull direction (relative to the anchor) into the
      // orthonormal basis.
      std::vector<double> q = bestDir;
      for ( const std::vector<double> &existing : basis )
      {
        double dot = 0.0;
        for ( size_t b = 0; b < B; ++b )
          dot += q[b] * existing[b];
        for ( size_t b = 0; b < B; ++b )
          q[b] -= dot * existing[b];
      }
      double norm = 0.0;
      for ( size_t b = 0; b < B; ++b )
        norm += q[b] * q[b];
      norm = std::sqrt( norm );
      if ( !( norm > 1e-12 ) )
        return false; // collapsed hull direction: degenerate cube
      for ( size_t b = 0; b < B; ++b )
        q[b] /= norm;
      basis.push_back( std::move( q ) );
    }

    // Emit the original spectra at the selected pure-pixel indices.
    outEndmembers->resize( static_cast<size_t>( endmemberCount ) * B );
    for ( int k = 0; k < endmemberCount; ++k )
    {
      const size_t src = chosen[static_cast<size_t>( k )] * B;
      std::copy( pixels + src, pixels + src + B, outEndmembers->begin() + static_cast<size_t>( k ) * B );
    }
    return true;
  }

  bool SpectralUnmixing::unmixFcls( const float *pixels, size_t pixelCount, int bandCount,
                                    const float *endmembers, int endmemberCount,
                                    UnmixingResult *result, QString *errorMessage )
  {
    if ( result == nullptr )
    {
      if ( errorMessage )
        *errorMessage = QStringLiteral( "result sink must not be null" );
      return false;
    }

    // Delegate to the proven penalty-augmented Lawson–Hanson kernel.
    ::SpectralUnmixing::UnmixResult legacy;
    if ( !::SpectralUnmixing::unmixFcls( pixels, pixelCount, bandCount, endmembers,
                                         endmemberCount, &legacy, errorMessage ) )
      return false;

    result->abundances = std::move( legacy.abundances );
    result->reconstructionError = std::move( legacy.reconstructionError );

    // QA metric: mean |sum(f) - 1| — measured, not assumed.
    double violation = 0.0;
    if ( pixelCount > 0 )
    {
      for ( size_t p = 0; p < pixelCount; ++p )
      {
        double sum = 0.0;
        for ( int e = 0; e < endmemberCount; ++e )
          sum += result->abundances[p * static_cast<size_t>( endmemberCount ) + static_cast<size_t>( e )];
        violation += std::abs( sum - 1.0 );
      }
      violation /= static_cast<double>( pixelCount );
    }
    result->meanSumConstraintViolation = violation;
    return true;
  }
} // namespace exp_spectral
