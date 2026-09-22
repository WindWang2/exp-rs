// deterministic.h — portable deterministic randomness for the fault lab
// (mirrors the discipline of src/dataset/deterministic_random.h, ADR 0136,
// without the Qt types so the fault-lab core stays Qt-free).
//
// std::mt19937 / std::shuffle / std::uniform_int_distribution are NOT
// portable: the standard does not pin their algorithms, so identical seeds
// can produce different sequences across standard libraries. Every random
// step in fault labs (synthetic replacement values, temporal permutations)
// flows through the fixed-algorithm primitives below.
#pragma once

#include <cstdint>
#include <string>

namespace sicnu::faultlab::deterministic
{

/// SplitMix64 — expands one 64-bit seed into generator state or derives
/// per-purpose seeds. Fixed algorithm, never platform-dependent.
inline std::uint64_t splitmix64( std::uint64_t &state )
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
    z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBull;
    return z ^ ( z >> 31 );
}

/// Derives an independent 64-bit seed for one named purpose from a root
/// seed. Purposes are namespaced with ':' so adding a new stochastic step
/// never perturbs the draws of the existing ones.
std::uint64_t seedFor( std::uint64_t rootSeed, const std::string &purpose );

/// PCG-style 32-bit output generator with 64-bit state (LCG step + output
/// mix). Fixed algorithm; identical seeds replay identically everywhere.
class Pcg32
{
  public:
    explicit Pcg32( std::uint64_t seed );

    std::uint32_t nextU32();

    /// Uniform in [0, bound) via modulo rejection — exact, no modulo bias.
    std::uint32_t nextBounded( std::uint32_t bound );

    /// Uniform double in [0, 1).
    double nextDouble();

  private:
    std::uint64_t mState = 0;
    std::uint64_t mStream = 0;
};

} // namespace sicnu::faultlab::deterministic
