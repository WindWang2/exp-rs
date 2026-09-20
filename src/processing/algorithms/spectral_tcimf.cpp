// spectral_tcimf.cpp — see spectral_tcimf.h for contracts.

#include "spectral_tcimf.h"

#include "processing/algorithms/primitives/dense_linalg.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace SpectralTcimf
{
namespace
{

// The distortionless denominator is the Schur complement
// tᵀR'⁻¹t − bᵀA⁻¹b ∈ [0, tᵀR'⁻¹t]. Below this fraction of the unconstrained
// value the target is (numerically) inside the interference span and the
// constraint wᵀt = 1 is unsatisfiable.
constexpr double kMinDenominatorFraction = 1e-12;

bool allFinite( const std::vector<double> &v )
{
    for ( const double x : v )
        if ( !std::isfinite( x ) )
            return false;
    return true;
}

/// R' = R + loading*(tr(R)/B)*I, then inverted. Returns false on a singular
/// loaded matrix or non-finite input (same structural contract as CEM).
bool invertLoadedCorrelation( const std::vector<double> &correlation, int bands,
                              double loading, std::vector<double> *inverse,
                              QString *errorMessage )
{
    if ( correlation.size() != static_cast<size_t>( bands ) * bands )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "correlation size does not match band count" );
        return false;
    }
    if ( !allFinite( correlation ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "background correlation has non-finite values" );
        return false;
    }
    if ( !std::isfinite( loading ) || loading < 0.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "loading must be a finite value >= 0" );
        return false;
    }
    double trace = 0.0;
    for ( int i = 0; i < bands; ++i )
        trace += correlation[static_cast<size_t>( i ) * bands + i];
    if ( !( trace > 0.0 ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "background correlation has a non-positive trace" );
        return false;
    }

    std::vector<double> loaded = correlation;
    if ( loading > 0.0 )
    {
        const double load = loading * ( trace / static_cast<double>( bands ) );
        for ( int i = 0; i < bands; ++i )
            loaded[static_cast<size_t>( i ) * bands + i] += load;
    }
    if ( !sicnu::primitives::invertDenseMatrix( loaded, bands, inverse ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "loaded background correlation is singular" );
        return false;
    }
    return true;
}

} // namespace

bool buildFilter( const float *target, int bands,
                  const std::vector<std::vector<float>> &interference,
                  const std::vector<double> &correlation,
                  double loading, Filter *out,
                  QString *errorMessage, double *interferenceCondition )
{
    if ( interferenceCondition )
        *interferenceCondition = -1.0;
    if ( !target || !out || bands <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "invalid buildFilter arguments" );
        return false;
    }
    for ( int b = 0; b < bands; ++b )
    {
        if ( !std::isfinite( static_cast<double>( target[b] ) ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "target spectrum has non-finite values" );
            return false;
        }
    }

    // Structural validation of every interference spectrum before any linear
    // algebra: wrong band count, non-finite values and zero spectra are
    // user-input errors, not rank failures.
    const int k = static_cast<int>( interference.size() );
    for ( int c = 0; c < k; ++c )
    {
        const std::vector<float> &s = interference[static_cast<size_t>( c )];
        if ( static_cast<int>( s.size() ) != bands )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "interference spectrum %1 has %2 values, expected %3" )
                                    .arg( c )
                                    .arg( static_cast<qlonglong>( s.size() ) )
                                    .arg( bands );
            return false;
        }
        double norm2 = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            if ( !std::isfinite( static_cast<double>( s[static_cast<size_t>( b )] ) ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "interference spectrum %1 has non-finite values" ).arg( c );
                return false;
            }
            const double v = static_cast<double>( s[static_cast<size_t>( b )] );
            norm2 += v * v;
        }
        if ( !( norm2 > 0.0 ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "interference spectrum %1 is the zero spectrum" ).arg( c );
            return false;
        }
    }

    std::vector<double> invCorr;
    if ( !invertLoadedCorrelation( correlation, bands, loading, &invCorr, errorMessage ) )
        return false;

    // u = R'⁻¹ t
    std::vector<double> u( static_cast<size_t>( bands ), 0.0 );
    for ( int i = 0; i < bands; ++i )
    {
        const size_t rowOffset = static_cast<size_t>( i ) * bands;
        double row = 0.0;
        for ( int j = 0; j < bands; ++j )
            row += invCorr[rowOffset + j] * static_cast<double>( target[j] );
        u[static_cast<size_t>( i )] = row;
    }

    // Lagrange solution of  min wᵀR'w  s.t.  wᵀt = 1, Sᵀw = 0:
    //   w = R'⁻¹(t − S z) / (tᵀR'⁻¹(t − S z)),   z = (SᵀR'⁻¹S)⁻¹ SᵀR'⁻¹t
    // The numerator applies R'⁻¹ to the COMBINATION (t − S z), not to t and S
    // separately: only then do both constraints hold exactly —
    //   wᵀt = (t−Sz)ᵀR'⁻¹t / denom = 1                 (denom = the same dot)
    //   wᵀs_a = (t−Sz)ᵀR'⁻¹s_a / denom
    //         = (b_a − zᵀ(SᵀR'⁻¹s)_a) / denom = 0      (z = A⁻¹b, A symmetric)
    // With k = 0 this reduces to the CEM filter w = R'⁻¹t/(tᵀR'⁻¹t) with the
    // same arithmetic order, hence bit-identical (asserted in the tests).
    std::vector<double> p( static_cast<size_t>( bands ), 0.0 );
    for ( int i = 0; i < bands; ++i )
        p[static_cast<size_t>( i )] = static_cast<double>( target[i] );
    double denomFull = 0.0; // tᵀR'⁻¹t, the unconstrained (CEM) denominator
    for ( int i = 0; i < bands; ++i )
        denomFull += static_cast<double>( target[i] ) * u[static_cast<size_t>( i )];

    if ( k > 0 )
    {
        // Gram matrix under the R'⁻¹ metric and the right-hand side.
        std::vector<double> gram( static_cast<size_t>( k ) * k, 0.0 );
        std::vector<double> rhs( static_cast<size_t>( k ), 0.0 );
        // su[c] = R'⁻¹ s_c (computed once, reused for both A and b).
        std::vector<std::vector<double>> su( static_cast<size_t>( k ) );
        for ( int c = 0; c < k; ++c )
        {
            su[static_cast<size_t>( c )].assign( static_cast<size_t>( bands ), 0.0 );
            for ( int i = 0; i < bands; ++i )
            {
                const size_t rowOffset = static_cast<size_t>( i ) * bands;
                double row = 0.0;
                for ( int j = 0; j < bands; ++j )
                    row += invCorr[rowOffset + j] *
                           static_cast<double>( interference[static_cast<size_t>( c )][static_cast<size_t>( j )] );
                su[static_cast<size_t>( c )][static_cast<size_t>( i )] = row;
            }
        }
        for ( int a = 0; a < k; ++a )
        {
            const std::vector<float> &sa = interference[static_cast<size_t>( a )];
            for ( int b = 0; b < k; ++b )
            {
                double dot = 0.0;
                for ( int i = 0; i < bands; ++i )
                    dot += static_cast<double>( sa[static_cast<size_t>( i )] ) *
                           su[static_cast<size_t>( b )][static_cast<size_t>( i )];
                gram[static_cast<size_t>( a ) * k + b] = dot;
            }
            double dotT = 0.0;
            for ( int i = 0; i < bands; ++i )
                dotT += static_cast<double>( sa[static_cast<size_t>( i )] ) * u[static_cast<size_t>( i )];
            rhs[static_cast<size_t>( a )] = dotT;
        }

        std::vector<double> gramInverse;
        if ( !sicnu::primitives::invertDenseMatrix( gram, k, &gramInverse ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral(
                    "interference signatures are linearly dependent under the background "
                    "metric (duplicate, collinear or zero columns)" );
            return false;
        }
        if ( interferenceCondition )
            *interferenceCondition = sicnu::primitives::conditionNumber( gram, k );
        // z = A⁻¹ b, then p = t − S z.
        std::vector<double> z( static_cast<size_t>( k ), 0.0 );
        for ( int a = 0; a < k; ++a )
        {
            const size_t rowOffset = static_cast<size_t>( a ) * k;
            double row = 0.0;
            for ( int b = 0; b < k; ++b )
                row += gramInverse[rowOffset + b] * rhs[static_cast<size_t>( b )];
            z[static_cast<size_t>( a )] = row;
        }
        for ( int c = 0; c < k; ++c )
        {
            const std::vector<float> &s = interference[static_cast<size_t>( c )];
            const double zc = z[static_cast<size_t>( c )];
            for ( int i = 0; i < bands; ++i )
                p[static_cast<size_t>( i )] -= zc * static_cast<double>( s[static_cast<size_t>( i )] );
        }
    }

    // v = R'⁻¹ p (for k = 0 this is exactly CEM's R'⁻¹t with the same order).
    std::vector<double> v( static_cast<size_t>( bands ), 0.0 );
    for ( int i = 0; i < bands; ++i )
    {
        const size_t rowOffset = static_cast<size_t>( i ) * bands;
        double row = 0.0;
        for ( int j = 0; j < bands; ++j )
            row += invCorr[rowOffset + j] * p[static_cast<size_t>( j )];
        if ( !std::isfinite( row ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "constrained numerator is non-finite" );
            return false;
        }
        v[static_cast<size_t>( i )] = row;
    }

    // Distortionless normalization: w = v / (tᵀv). The denominator is the
    // Schur complement tᵀR'⁻¹t − bᵀA⁻¹b ∈ [0, tᵀR'⁻¹t]: a (near-)zero value
    // means the target lies inside the interference span, so the constraint
    // wᵀt = 1 is unsatisfiable — refuse instead of emitting a huge filter.
    // The floor is relative to the unconstrained denominator so the refusal
    // is scale-free (and never fires for k = 0, where the two are equal).
    double denom = 0.0;
    for ( int i = 0; i < bands; ++i )
        denom += static_cast<double>( target[i] ) * v[static_cast<size_t>( i )];
    if ( !std::isfinite( denom ) || !( denom > kMinDenominatorFraction * denomFull ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "target spectrum lies inside the interference span (zero distortionless "
                "denominator)" );
        return false;
    }

    out->weight.assign( static_cast<size_t>( bands ), 0.0 );
    for ( int i = 0; i < bands; ++i )
    {
        const double w = v[static_cast<size_t>( i )] / denom;
        if ( !std::isfinite( w ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "filter weight is non-finite" );
            return false;
        }
        out->weight[static_cast<size_t>( i )] = w;
    }
    return true;
}

float tcimfScore( const float *x, const Filter &filter, int bands,
                  std::vector<double> *scratch )
{
    // @a scratch is validated for capacity but unused (same rationale as
    // SpectralCem::cemScore): the streaming driver passes one shared scratch
    // to every detector kernel uniformly.
    if ( !x || !scratch || scratch->size() < static_cast<size_t>( bands ) )
        return std::numeric_limits<float>::quiet_NaN();
    if ( filter.weight.size() != static_cast<size_t>( bands ) )
        return std::numeric_limits<float>::quiet_NaN();

    double score = 0.0;
    for ( int b = 0; b < bands; ++b )
    {
        if ( !std::isfinite( x[b] ) )
            return std::numeric_limits<float>::quiet_NaN();
        score += filter.weight[static_cast<size_t>( b )] * static_cast<double>( x[b] );
    }
    return static_cast<float>( score );
}

} // namespace SpectralTcimf
