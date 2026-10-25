// portable_random.h — Qt-free deterministic randomness for science kernels
// (#1685, R22-01).
//
// std::mt19937 itself IS portable: the standard pins its state size, seeding
// and transition function ([rand.eng.mersenne]), so raw engine draws replay
// identically on libstdc++/libc++/MSVC. What the standard does NOT pin are
// the algorithms of std::shuffle, std::uniform_int_distribution and
// std::normal_distribution — "seed = 42" paths built on them produce
// different train/test splits, reservoirs and RANSAC samples on each STL
// (see also src/faultlab/deterministic.h and the hand-rolled Fisher-Yates
// already used by rs_cross_validation.cpp / rs_spatial_cross_validation.cpp).
//
// This header keeps std::mt19937 as the engine and replaces only those
// non-portable operations with algorithms fixed in THIS file:
//
//   - fisherYatesShuffle: descending Fisher-Yates, j = engine() % i
//     (same convention as the existing cross-validation code);
//   - boundedBelow / uniformIndex: single-draw modulo (documented bias
//     < 2^-32 relative for the draw ranges used here);
//   - uniformUnit53 / nextU64: fixed 53-bit/64-bit composition of two draws;
//   - normal01: Box-Muller cos form, exactly two unit53 draws per value.
//
// Golden-sequence anchors live in tests/test_r22_science_random.cpp.
#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace rs::algorithms::portable
{

/// Uniform 64-bit value from two engine draws (first draw = high half).
/// The draws are sequenced through named locals: packing both rng() calls
/// into one expression would leave their evaluation order unspecified
/// ([intro.execution]) — a portability defect of exactly the class this
/// header exists to remove.
inline std::uint64_t nextU64( std::mt19937 &rng )
{
    const std::uint32_t hi = rng();
    const std::uint32_t lo = rng();
    return ( static_cast<std::uint64_t>( hi ) << 32 ) | lo;
}

/// Uniform index in [0, bound). Single draw; bound must be > 0 and, to keep
/// the modulo bias under a relative 2^-32, bound <= 2^32.
inline std::uint32_t boundedBelow( std::mt19937 &rng, std::uint32_t bound )
{
    return rng() % bound;
}

/// Uniform index in [0, bound) for size_t reservoir bounds; two draws via
/// nextU64 (the modulo bias is the same negligible class as boundedBelow).
inline std::size_t uniformIndex( std::mt19937 &rng, std::size_t bound )
{
    if ( bound <= 1 )
        return 0;
    return static_cast<std::size_t>( nextU64( rng ) % bound );
}

/// Uniform double in [0, 1) with full 53-bit mantissa resolution: the first
/// draw supplies the high 32 bits, the second the low 21 bits. Draw order is
/// sequenced through named locals (see nextU64).
inline double uniformUnit53( std::mt19937 &rng )
{
    const std::uint32_t hi = rng();
    const std::uint32_t lo = rng();
    const std::uint64_t v = ( static_cast<std::uint64_t>( hi ) << 21 ) | ( lo >> 11 );
    return static_cast<double>( v ) * 0x1.0p-53;
}

/// Standard normal draw via Box-Muller (cos branch): exactly two
/// uniformUnit53 draws per value, so draw counts are predictable and the
/// sequence is pinned. u1 == 0 is remapped to the smallest step to keep
/// log() finite.
inline double normal01( std::mt19937 &rng )
{
    double u1 = uniformUnit53( rng );
    const double u2 = uniformUnit53( rng );
    if ( u1 == 0.0 )
        u1 = 0x1.0p-53;
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    return std::sqrt( -2.0 * std::log( u1 ) ) * std::cos( kTwoPi * u2 );
}

/// In-place Fisher-Yates shuffle, descending: for i = n-1 … 1,
/// j = engine() % (i + 1); swap(v[i], v[j]). Same fixed convention as the
/// hand-rolled shuffles in rs_cross_validation.cpp — every STL replays the
/// same permutation for the same seed.
template <typename T>
void fisherYatesShuffle( std::mt19937 &rng, std::vector<T> &values )
{
    for ( std::size_t i = values.size(); i > 1; --i )
    {
        const std::size_t j = rng() % i;
        std::swap( values[i - 1], values[j] );
    }
}

/// Same shuffle for iterator ranges (used by containers that are not
/// std::vector, e.g. QVector via begin()/end()).
template <typename RandomIt>
void fisherYatesShuffleRange( std::mt19937 &rng, RandomIt first, RandomIt last )
{
    const auto n = static_cast<std::size_t>( last - first );
    for ( std::size_t i = n; i > 1; --i )
    {
        const std::size_t j = rng() % i;
        std::swap( first[i - 1], first[j] );
    }
}

} // namespace rs::algorithms::portable
