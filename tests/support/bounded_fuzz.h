// bounded_fuzz.h — deterministic, bounded input generation for contract
// property tests (Verification 7.0, task D).
//
// Ground rules (no external fuzz engine, CI-safe, memory-safe):
//   * FIXED seed sequences — every run of a property test explores the same
//     inputs; a failure is reproducible by seed + iteration index.
//   * Every generator takes an explicit byte/element CAP. Tests never build
//     inputs larger than the documented parser bounds + slack, so a bug can
//     neither hang a run nor exhaust memory.
//   * Generators are total functions of (state, cap) — no global state.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::testing
{

/// xorshift64* — tiny, deterministic, good-enough distribution for contract
/// fuzzing (not cryptographic; never used for security-relevant values).
class BoundedRandom
{
  public:
    explicit BoundedRandom( uint64_t seed )
        : m_state( seed ? seed : 0x9E3779B97F4A7C15ull )
    {
    }

    uint64_t next()
    {
        uint64_t x = m_state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        m_state = x;
        return x * 0x2545F4914F6CDD1Dull;
    }

    /// Uniform in [0, bound); bound 0 returns 0.
    uint32_t below( uint32_t bound )
    {
        if ( bound == 0 )
            return 0;
        return static_cast<uint32_t>( next() % bound );
    }

    /// True with probability ~p (0.0–1.0).
    bool chance( double p ) { return ( next() & 0xFFFFFFFFull ) < static_cast<uint64_t>( p * 4294967295.0 ); }

    /// Picks one element of @p items. PRECONDITION: items is non-empty
    /// (callers must not pass an empty vector — a reference cannot be empty).
    template <typename T>
    const T &pick( const std::vector<T> &items )
    {
        return items[below( static_cast<uint32_t>( items.size() ) )];
    }

    /// Random string of length in [minLen, min(maxLen, minLen + spread)]
    /// drawn from @p alphabet. Caps are hard: result never exceeds maxLen.
    std::string string( size_t minLen, size_t maxLen, const std::vector<char> &alphabet )
    {
        if ( alphabet.empty() || minLen > maxLen )
            return std::string();
        const size_t len = minLen + below( static_cast<uint32_t>( maxLen - minLen + 1 ) );
        std::string out;
        out.reserve( len );
        for ( size_t i = 0; i < len; ++i )
            out.push_back( alphabet[below( static_cast<uint32_t>( alphabet.size() ) )] );
        return out;
    }

    /// Mutation helper: byte-level noise over a seed input, bounded count.
    std::string mutate( const std::string &seed, uint32_t maxMutations )
    {
        std::string out = seed;
        const uint32_t mutations = 1 + below( maxMutations );
        for ( uint32_t i = 0; i < mutations && !out.empty(); ++i )
        {
            const size_t pos = below( static_cast<uint32_t>( out.size() ) );
            switch ( below( 3 ) )
            {
            case 0:
                out[pos] = static_cast<char>( next() & 0xFF );
                break;
            case 1:
                out.insert( out.begin() + static_cast<long>( pos ),
                            static_cast<char>( next() & 0xFF ) );
                break;
            case 2:
                out.erase( out.begin() + static_cast<long>( pos ) );
                break;
            }
        }
        return out;
    }

  private:
    uint64_t m_state;
};

} // namespace sicnu::testing
