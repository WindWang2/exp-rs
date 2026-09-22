// src/repair_planner/repair_sha256.cpp
#include "repair_sha256.h"

#include <cstring>

namespace sicnu::repair {
namespace {

struct Sha256Context
{
    std::uint32_t state[8];
    std::uint64_t bitLen;
    std::uint8_t buffer[64];
    std::size_t bufferLen;
};

constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline std::uint32_t rotr( std::uint32_t x, std::uint32_t n )
{
    return ( x >> n ) | ( x << ( 32 - n ) );
}

void sha256Transform( Sha256Context &ctx, const std::uint8_t block[64] )
{
    std::uint32_t w[64];
    for ( int i = 0; i < 16; ++i )
    {
        w[i] = ( std::uint32_t( block[i * 4] ) << 24 ) |
               ( std::uint32_t( block[i * 4 + 1] ) << 16 ) |
               ( std::uint32_t( block[i * 4 + 2] ) << 8 ) |
               std::uint32_t( block[i * 4 + 3] );
    }
    for ( int i = 16; i < 64; ++i )
    {
        const std::uint32_t s0 = rotr( w[i - 15], 7 ) ^ rotr( w[i - 15], 18 ) ^ ( w[i - 15] >> 3 );
        const std::uint32_t s1 = rotr( w[i - 2], 17 ) ^ rotr( w[i - 2], 19 ) ^ ( w[i - 2] >> 10 );
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
    std::uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];

    for ( int i = 0; i < 64; ++i )
    {
        const std::uint32_t s1 = rotr( e, 6 ) ^ rotr( e, 11 ) ^ rotr( e, 25 );
        const std::uint32_t ch = ( e & f ) ^ ( ~e & g );
        const std::uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        const std::uint32_t s0 = rotr( a, 2 ) ^ rotr( a, 13 ) ^ rotr( a, 22 );
        const std::uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
        const std::uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

void sha256Update( Sha256Context &ctx, const std::uint8_t *data, std::size_t len )
{
    ctx.bitLen += std::uint64_t( len ) * 8u;
    while ( len > 0 )
    {
        const std::size_t take = std::min( len, std::size_t{64} - ctx.bufferLen );
        std::memcpy( ctx.buffer + ctx.bufferLen, data, take );
        ctx.bufferLen += take;
        data += take;
        len -= take;
        if ( ctx.bufferLen == 64 )
        {
            sha256Transform( ctx, ctx.buffer );
            ctx.bufferLen = 0;
        }
    }
}

void sha256Final( Sha256Context &ctx, std::uint8_t digest[32] )
{
    // 0x80 padding + zero pad + 64-bit big-endian length. The saved bitLen is
    // the message length; update() may keep counting padding bits, which the
    // final length bytes below deliberately ignore.
    const std::uint64_t bitLen = ctx.bitLen;
    const std::uint8_t pad = 0x80;
    sha256Update( ctx, &pad, 1 );
    const std::uint8_t zero = 0x00;
    while ( ctx.bufferLen != 56 )
        sha256Update( ctx, &zero, 1 );
    std::uint8_t lengthBytes[8];
    for ( int i = 0; i < 8; ++i )
        lengthBytes[i] = std::uint8_t( bitLen >> ( 56 - i * 8 ) );
    std::memcpy( ctx.buffer + 56, lengthBytes, 8 );
    sha256Transform( ctx, ctx.buffer );
    ctx.bufferLen = 0;

    for ( int i = 0; i < 8; ++i )
    {
        digest[i * 4] = std::uint8_t( ctx.state[i] >> 24 );
        digest[i * 4 + 1] = std::uint8_t( ctx.state[i] >> 16 );
        digest[i * 4 + 2] = std::uint8_t( ctx.state[i] >> 8 );
        digest[i * 4 + 3] = std::uint8_t( ctx.state[i] );
    }
}

} // namespace

std::string sha256Hex( const std::string &data )
{
    Sha256Context ctx;
    ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;
    ctx.bitLen = 0;
    ctx.bufferLen = 0;

    sha256Update( ctx, reinterpret_cast<const std::uint8_t *>( data.data() ), data.size() );
    std::uint8_t digest[32];
    sha256Final( ctx, digest );

    static const char *kHex = "0123456789abcdef";
    std::string out;
    out.reserve( 64 );
    for ( const std::uint8_t byte : digest )
    {
        out.push_back( kHex[byte >> 4] );
        out.push_back( kHex[byte & 0x0f] );
    }
    return out;
}

} // namespace sicnu::repair
