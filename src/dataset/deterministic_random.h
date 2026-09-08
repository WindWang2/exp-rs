// deterministic_random.h — platform-independent deterministic randomness
// (ADR 0136).
//
// std::mt19937 + std::shuffle + std::uniform_int_distribution are NOT
// portable: the standard does not pin their algorithms, so the "same seed"
// can yield different assignments on MSVC vs libstdc++ — unacceptable for a
// reproducibility foundation. This header provides:
//
//   - splitmix64: 64-bit state seeding / mixing (public-domain reference);
//   - Pcg32: a small PCG-style generator (uint64 state, uint32 output);
//   - DeterministicRandom: uniform integers/floats, shuffle, weighted pick —
//     every operation's algorithm is fixed in THIS file, so identical seeds
//     replay identically on any platform and standard library.
//
// Seed discipline: derive per-purpose seeds from one root seed via
// `seedFor(purpose)` so adding a new stochastic step to a pipeline never
// changes the draws of the existing steps (a classic reproducibility trap).
#pragma once

#include <cstdint>
#include <QString>
#include <QVector>

namespace sicnu::dataset
{

/// SplitMix64 — used to expand one 64-bit seed into generator state or to
/// derive per-purpose seeds. Fixed algorithm, never platform-dependent.
class SplitMix64
{
  public:
    explicit SplitMix64( quint64 seed )
      : m_state( seed )
    {
    }

    quint64 next()
    {
        m_state += 0x9E3779B97F4A7C15ull;
        quint64 z = m_state;
        z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
        z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBull;
        return z ^ ( z >> 31 );
    }

  private:
    quint64 m_state;
};

/// PCG-style 32-bit output generator with 64-bit state (LCG step + xorshift
/// output mix). Fixed algorithm; deterministic across platforms.
class Pcg32
{
  public:
    explicit Pcg32( quint64 seed )
    {
        SplitMix64 seeder( seed );
        m_state = seeder.next();
        m_stream = seeder.next() | 1ull; // odd stream constant
    }

    quint32 nextU32()
    {
        m_state = m_state * 6364136223846793005ull + ( m_stream | 1ull );
        const quint64 mixed = m_state ^ ( m_state >> 22 );
        const quint32 word = quint32( ( mixed >> ( 22 + ( m_state >> 61 ) ) ) & 0xFFFFFFFFull );
        // Fallback mix keeps output well-distributed for all advanced bits.
        return word ^ quint32( m_state >> 32 );
    }

    /// Uniform in [0, bound) via modulo rejection — exact, no modulo bias.
    quint32 nextBounded( quint32 bound )
    {
        if ( bound == 0 )
            return 0;
        const quint32 threshold = quint32( 0 - bound ) % bound;
        while ( true )
        {
            const quint32 draw = nextU32();
            if ( draw >= threshold )
                return draw % bound;
        }
    }

    /// Uniform double in [0, 1).
    double nextDouble()
    {
        return double( nextU32() ) / 4294967296.0;
    }

    /// Fisher–Yates shuffle, fixed traversal order.
    template <typename T>
    void shuffle( QVector<T> &items )
    {
        for ( int i = items.size() - 1; i > 0; --i )
        {
            const int j = int( nextBounded( quint32( i + 1 ) ) );
            qSwap( items[i], items[j] );
        }
    }

  private:
    quint64 m_state = 0;
    quint64 m_stream = 0;
};

/// One stochastic context. All split/patch randomness flows through this so
/// every random behavior is seed-policy auditable (goal §30).
class DeterministicRandom
{
  public:
    explicit DeterministicRandom( quint64 seed )
      : m_generator( seed )
    {
    }

    explicit DeterministicRandom( const QString &seedText )
      : m_generator( hashSeed( seedText ) )
    {
    }

    quint32 uniform( quint32 bound ) { return m_generator.nextBounded( bound ); }
    double uniform01() { return m_generator.nextDouble(); }

    template <typename T>
    void shuffle( QVector<T> &items )
    {
        m_generator.shuffle( items );
    }

    /// Deterministic 64-bit hash of a seed string (SplitMix64 finalizer over
    /// FNV-1a) — textual seeds are stable across platforms.
    static quint64 hashSeed( const QString &text );

    /// Derive an independent seed for one named purpose (e.g. "split",
    /// "patch.subsample") from the root seed. Purposes are namespaced with
    /// ':' so adding "split.x" never perturbs "split".
    static quint64 seedFor( quint64 rootSeed, const QString &purpose );

  private:
    Pcg32 m_generator;
};

} // namespace sicnu::dataset
