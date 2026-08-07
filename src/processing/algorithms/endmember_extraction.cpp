// src/processing/algorithms/endmember_extraction.cpp — PPI endmember extraction
#include "endmember_extraction.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace EndmemberExtraction
{

bool pixelPurityIndex( const float *pixels, size_t count, int bands,
                       int nEndmembers, int projections,
                       EndmemberResult *result, QString *errorMessage )
{
    if ( !pixels || !result || count == 0 || bands <= 0
         || nEndmembers < 1 || static_cast<size_t>( nEndmembers ) > count
         || projections < 16 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid PPI arguments" );
        return false;
    }

    // Mean-center the data (PPI operates on centered extremes).
    std::vector<double> mean( bands, 0.0 );
    for ( size_t p = 0; p < count; ++p )
        for ( int b = 0; b < bands; ++b )
            mean[static_cast<size_t>( b )] += pixels[p * static_cast<size_t>( bands ) + b];
    for ( int b = 0; b < bands; ++b )
        mean[static_cast<size_t>( b )] /= static_cast<double>( count );

    // Reproducible randomness: fixed seed.
    std::mt19937 rng( 42 );
    std::normal_distribution<double> normal( 0.0, 1.0 );

    std::vector<int> extremeCounts( count, 0 );
    for ( int proj = 0; proj < projections; ++proj )
    {
        // Random unit vector.
        std::vector<double> direction( bands );
        double norm = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            direction[static_cast<size_t>( b )] = normal( rng );
            norm += direction[static_cast<size_t>( b )] * direction[static_cast<size_t>( b )];
        }
        norm = std::sqrt( norm );
        if ( norm < 1e-12 )
            continue;
        for ( double &v : direction )
            v /= norm;

        // Track the min and max projection extremes.
        size_t minP = 0, maxP = 0;
        double minV = 0.0, maxV = 0.0;
        for ( size_t p = 0; p < count; ++p )
        {
            double v = 0.0;
            for ( int b = 0; b < bands; ++b )
                v += direction[static_cast<size_t>( b )]
                     * ( pixels[p * static_cast<size_t>( bands ) + b] - mean[static_cast<size_t>( b )] );
            if ( p == 0 || v < minV )
            {
                minV = v;
                minP = p;
            }
            if ( p == 0 || v > maxV )
            {
                maxV = v;
                maxP = p;
            }
        }
        ++extremeCounts[minP];
        ++extremeCounts[maxP];
    }

    // Rank pixels by PPI count, tie-broken by index for determinism.
    std::vector<size_t> order( count );
    for ( size_t i = 0; i < count; ++i )
        order[i] = i;
    std::sort( order.begin(), order.end(), [&]( size_t a, size_t b ) {
        if ( extremeCounts[a] != extremeCounts[b] )
            return extremeCounts[a] > extremeCounts[b];
        return a < b;
    } );

    result->endmembers.resize( static_cast<size_t>( nEndmembers ) * bands );
    result->endmemberIndices.resize( nEndmembers );
    result->ppiCounts = std::move( extremeCounts );
    for ( int e = 0; e < nEndmembers; ++e )
    {
        const size_t pixel = order[static_cast<size_t>( e )];
        result->endmemberIndices[static_cast<size_t>( e )] = static_cast<int>( pixel );
        for ( int b = 0; b < bands; ++b )
            result->endmembers[static_cast<size_t>( e ) * bands + b] =
                pixels[pixel * static_cast<size_t>( bands ) + b];
    }
    return true;
}

} // namespace EndmemberExtraction
