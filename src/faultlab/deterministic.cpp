// deterministic.cpp — portable deterministic randomness (see header).
#include "deterministic.h"

#include <cstring>

namespace sicnu::faultlab::deterministic
{

std::uint64_t seedFor( std::uint64_t rootSeed, const std::string &purpose )
{
    // FNV-1a over the purpose text, then a SplitMix64 finalizer: the same
    // textual seed maps to the same derived seed on every platform.
    std::uint64_t hash = 14695981039346656037ull;
    for ( const char c : purpose )
    {
        hash ^= static_cast<std::uint64_t>( static_cast<unsigned char>( c ) );
        hash *= 1099511628211ull;
    }
    hash ^= rootSeed + 0x9E3779B97F4A7C15ull;
    hash *= 0xBF58476D1CE4E5B9ull;
    hash ^= hash >> 27;
    return hash;
}

Pcg32::Pcg32( std::uint64_t seed )
{
    std::uint64_t state = seed;
    mState = splitmix64( state );
    mStream = splitmix64( state ) | 1ull; // odd stream constant
}

std::uint32_t Pcg32::nextU32()
{
    mState = mState * 6364136223846793005ull + mStream;
    const std::uint64_t mixed = mState ^ ( mState >> 22 );
    const std::uint32_t word = static_cast<std::uint32_t>(
        ( mixed >> ( 22 + ( mState >> 61 ) ) ) & 0xFFFFFFFFull );
    return word ^ static_cast<std::uint32_t>( mState >> 32 );
}

std::uint32_t Pcg32::nextBounded( std::uint32_t bound )
{
    if ( bound == 0 )
    {
        return 0;
    }
    const std::uint32_t threshold = static_cast<std::uint32_t>( 0 - bound ) % bound;
    while ( true )
    {
        const std::uint32_t draw = nextU32();
        if ( draw >= threshold )
        {
            return draw % bound;
        }
    }
}

double Pcg32::nextDouble()
{
    return static_cast<double>( nextU32() ) / 4294967296.0;
}

} // namespace sicnu::faultlab::deterministic
