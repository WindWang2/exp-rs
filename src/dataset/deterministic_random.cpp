// deterministic_random.cpp — seed hashing/derivation.
#include "deterministic_random.h"

namespace sicnu::dataset
{

quint64 DeterministicRandom::hashSeed( const QString &text )
{
    // FNV-1a over UTF-8 bytes, finished with the SplitMix64 finalizer so
    // nearby texts spread across the 64-bit space.
    const QByteArray bytes = text.toUtf8();
    quint64 hash = 0xcbf29ce484222325ull;
    for ( unsigned char byte : bytes )
    {
        hash ^= byte;
        hash *= 0x100000001b3ull;
    }
    SplitMix64 finalizer( hash );
    return finalizer.next();
}

quint64 DeterministicRandom::seedFor( quint64 rootSeed, const QString &purpose )
{
    // Hash the purpose text, then mix with the root seed through one more
    // SplitMix64 round — distinct purposes diverge, equal purposes agree.
    SplitMix64 mixer( hashSeed( purpose ) ^ rootSeed );
    return mixer.next();
}

} // namespace sicnu::dataset
