// grader_sha256.cpp — self-contained SHA-256 (FIPS 180-4).
#include "grader_sha256.h"

#include <cstring>

namespace sicnu::grader {

namespace {

inline std::uint32_t rotr( std::uint32_t x, std::uint32_t n )
{
    return ( x >> n ) | ( x << ( 32u - n ) );
}

constexpr std::uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

} // namespace

Sha256::Sha256()
{
    m_state[0] = 0x6a09e667u;
    m_state[1] = 0xbb67ae85u;
    m_state[2] = 0x3c6ef372u;
    m_state[3] = 0xa54ff53au;
    m_state[4] = 0x510e527fu;
    m_state[5] = 0x9b05688cu;
    m_state[6] = 0x1f83d9abu;
    m_state[7] = 0x5be0cd19u;
    std::memset( m_buffer, 0, sizeof( m_buffer ) );
}

void Sha256::transform( const unsigned char *block )
{
    std::uint32_t w[64];
    for ( int i = 0; i < 16; ++i ) {
        w[i] = ( std::uint32_t( block[i * 4] ) << 24 ) | ( std::uint32_t( block[i * 4 + 1] ) << 16 ) |
               ( std::uint32_t( block[i * 4 + 2] ) << 8 ) | std::uint32_t( block[i * 4 + 3] );
    }
    for ( int i = 16; i < 64; ++i ) {
        const std::uint32_t s0 = rotr( w[i - 15], 7 ) ^ rotr( w[i - 15], 18 ) ^ ( w[i - 15] >> 3 );
        const std::uint32_t s1 = rotr( w[i - 2], 17 ) ^ rotr( w[i - 2], 19 ) ^ ( w[i - 2] >> 10 );
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    std::uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

    for ( int i = 0; i < 64; ++i ) {
        const std::uint32_t S1 = rotr( e, 6 ) ^ rotr( e, 11 ) ^ rotr( e, 25 );
        const std::uint32_t ch = ( e & f ) ^ ( ~e & g );
        const std::uint32_t temp1 = h + S1 + ch + kK[i] + w[i];
        const std::uint32_t S0 = rotr( a, 2 ) ^ rotr( a, 13 ) ^ rotr( a, 22 );
        const std::uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
        const std::uint32_t temp2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

void Sha256::update( const unsigned char *data, std::size_t len )
{
    if ( m_finalized )
        return;
    m_bitLen += static_cast<std::uint64_t>( len ) * 8u;
    while ( len > 0 ) {
        const std::size_t take = std::min( len, sizeof( m_buffer ) - m_bufferLen );
        std::memcpy( m_buffer + m_bufferLen, data, take );
        m_bufferLen += take;
        data += take;
        len -= take;
        if ( m_bufferLen == sizeof( m_buffer ) ) {
            transform( m_buffer );
            m_bufferLen = 0;
        }
    }
}

std::string Sha256::hexDigest()
{
    if ( !m_finalized ) {
        // Pad: 0x80, zeros to 56 (mod 64), then the 64-bit big-endian bit
        // length. Staged through a 128-byte scratch so the two-block case
        // (56 <= buffered < 64) never writes past m_buffer.
        const std::uint64_t bitLen = m_bitLen;
        unsigned char tail[128];
        const std::size_t zeros = ( m_bufferLen < 56 ) ? ( 56 - m_bufferLen - 1 ) : ( 120 - m_bufferLen - 1 );
        std::memcpy( tail, m_buffer, m_bufferLen );
        tail[m_bufferLen] = 0x80;
        std::memset( tail + m_bufferLen + 1, 0, zeros );
        for ( int i = 0; i < 8; ++i )
            tail[m_bufferLen + 1 + zeros + i] = static_cast<unsigned char>( ( bitLen >> ( 56 - i * 8 ) ) & 0xFFu );
        const std::size_t tailLen = m_bufferLen + 1 + zeros + 8; // 64 or 128
        transform( tail );
        if ( tailLen > 64 )
            transform( tail + 64 );
        m_bufferLen = 0;
        m_finalized = true;
    }

    static const char *kHex = "0123456789abcdef";
    std::string out;
    out.reserve( 64 );
    for ( int i = 0; i < 8; ++i ) {
        for ( int shift = 28; shift >= 0; shift -= 4 )
            out.push_back( kHex[( m_state[i] >> shift ) & 0xFu] );
    }
    return out;
}

std::string sha256Hex( const std::string &bytes )
{
    Sha256 h;
    h.update( bytes.data(), bytes.size() );
    return h.hexDigest();
}

} // namespace sicnu::grader
