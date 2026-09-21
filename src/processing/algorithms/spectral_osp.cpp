// spectral_osp.cpp — see spectral_osp.h for contracts.

#include "spectral_osp.h"

#include "processing/algorithms/primitives/dense_linalg.h"

#include <cmath>
#include <limits>

namespace SpectralOsp
{
namespace
{

// Below this fraction of the target energy the projected direction cannot be
// distinguished from the round-off of the projection itself.
constexpr double kMinProjectedEnergyFraction = 1e-12;

bool validateSpectra( const float *target, int bands,
                      const std::vector<std::vector<float>> &interference,
                      QString *errorMessage )
{
    if ( !target || bands <= 0 )
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
    if ( interference.empty() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "OSP requires at least one undesired signature (an empty interference "
                "matrix would reduce the detector to a bare dot product)" );
        return false;
    }
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
                    *errorMessage =
                        QStringLiteral( "interference spectrum %1 has non-finite values" ).arg( c );
                return false;
            }
            const double v = static_cast<double>( s[static_cast<size_t>( b )] );
            norm2 += v * v;
        }
        if ( !( norm2 > 0.0 ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "interference spectrum %1 is the zero spectrum" )
                                    .arg( c );
            return false;
        }
    }
    return true;
}

} // namespace

bool buildFilter( const float *target, int bands,
                  const std::vector<std::vector<float>> &interference,
                  Filter *out, QString *errorMessage, double *interferenceCondition )
{
    if ( interferenceCondition )
        *interferenceCondition = -1.0;
    if ( !out )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "invalid buildFilter arguments" );
        return false;
    }
    if ( !validateSpectra( target, bands, interference, errorMessage ) )
        return false;

    double targetNorm2 = 0.0;
    for ( int b = 0; b < bands; ++b )
    {
        const double v = static_cast<double>( target[b] );
        targetNorm2 += v * v;
    }
    if ( !( targetNorm2 > 0.0 ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "target spectrum is the zero spectrum" );
        return false;
    }

    const int k = static_cast<int>( interference.size() );

    // Gram matrix G = UᵀU (k×k) and right-hand side Uᵀd.
    std::vector<double> gram( static_cast<size_t>( k ) * k, 0.0 );
    std::vector<double> rhs( static_cast<size_t>( k ), 0.0 );
    for ( int a = 0; a < k; ++a )
    {
        const std::vector<float> &sa = interference[static_cast<size_t>( a )];
        for ( int b = a; b < k; ++b )
        {
            const std::vector<float> &sb = interference[static_cast<size_t>( b )];
            double dot = 0.0;
            for ( int i = 0; i < bands; ++i )
                dot += static_cast<double>( sa[static_cast<size_t>( i )] ) *
                       static_cast<double>( sb[static_cast<size_t>( i )] );
            gram[static_cast<size_t>( a ) * k + b] = dot;
            gram[static_cast<size_t>( b ) * k + a] = dot;
        }
        double dotT = 0.0;
        for ( int i = 0; i < bands; ++i )
            dotT += static_cast<double>( sa[static_cast<size_t>( i )] ) *
                    static_cast<double>( target[i] );
        rhs[static_cast<size_t>( a )] = dotT;
    }

    std::vector<double> gramInverse;
    if ( !sicnu::primitives::invertDenseMatrix( gram, k, &gramInverse ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "interference signatures are linearly dependent (duplicate, collinear "
                "or zero columns): UᵀU is singular" );
        return false;
    }
    if ( interferenceCondition )
        *interferenceCondition = sicnu::primitives::conditionNumber( gram, k );

    // z = (UᵀU)⁻¹Uᵀd, then w = d − U z = (I − U(UᵀU)⁻¹Uᵀ) d.
    std::vector<double> z( static_cast<size_t>( k ), 0.0 );
    for ( int a = 0; a < k; ++a )
    {
        const size_t rowOffset = static_cast<size_t>( a ) * k;
        double row = 0.0;
        for ( int b = 0; b < k; ++b )
            row += gramInverse[rowOffset + b] * rhs[static_cast<size_t>( b )];
        z[static_cast<size_t>( a )] = row;
    }

    out->weight.assign( static_cast<size_t>( bands ), 0.0 );
    double projectedNorm2 = 0.0;
    for ( int i = 0; i < bands; ++i )
    {
        double wi = static_cast<double>( target[i] );
        for ( int c = 0; c < k; ++c )
            wi -= z[static_cast<size_t>( c )] *
                  static_cast<double>( interference[static_cast<size_t>( c )][static_cast<size_t>( i )] );
        if ( !std::isfinite( wi ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "projected target direction is non-finite" );
            return false;
        }
        out->weight[static_cast<size_t>( i )] = wi;
        projectedNorm2 += wi * wi;
    }

    if ( !( projectedNorm2 > kMinProjectedEnergyFraction * targetNorm2 ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "target spectrum lies (numerically) inside the undesired subspace: the "
                "projection retains less than 1e-12 of the target energy" );
        return false;
    }
    return true;
}

float ospScore( const float *x, const Filter &filter, int bands,
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

} // namespace SpectralOsp
