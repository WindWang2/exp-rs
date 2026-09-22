// util/sha256.cpp — self-contained SHA-256 (FIPS 180-4), streaming, no
// external dependencies. Known-answer vectors are asserted in test_faultlab.
#include "sha256.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace sicnu::faultlab
{

namespace
{

constexpr std::uint32_t kInitState[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                          0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                          0x1f83d9abu, 0x5be0cd19u };

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

inline std::uint32_t rotr( std::uint32_t value, int bits )
{
    return ( value >> bits ) | ( value << ( 32 - bits ) );
}

class Sha256Core
{
  public:
    Sha256Core()
    {
        reset();
    }

    void reset()
    {
        std::memcpy( mState, kInitState, sizeof( mState ) );
        mBufferUsed = 0;
        mTotalBytes = 0;
    }

    void update( const void *data, std::size_t length )
    {
        const auto *bytes = static_cast<const unsigned char *>( data );
        mTotalBytes += length;
        while ( length > 0 )
        {
            const std::size_t take = ( 64 - mBufferUsed < length ) ? ( 64 - mBufferUsed ) : length;
            std::memcpy( mBuffer + mBufferUsed, bytes, take );
            mBufferUsed += take;
            bytes += take;
            length -= take;
            if ( mBufferUsed == 64 )
            {
                processBlock( mBuffer );
                mBufferUsed = 0;
            }
        }
    }

    void finalize( unsigned char out[32] )
    {
        const std::uint64_t bitLength = mTotalBytes * 8;
        const unsigned char pad = 0x80;
        update( &pad, 1 );
        const unsigned char zero = 0x00;
        while ( mBufferUsed != 56 )
        {
            update( &zero, 1 );
        }
        unsigned char lengthBytes[8];
        for ( int i = 0; i < 8; ++i )
        {
            lengthBytes[i] = static_cast<unsigned char>( ( bitLength >> ( 56 - 8 * i ) ) & 0xFFu );
        }
        // Length bytes are appended raw (update() would count them into the
        // message length, which the padding already fixed).
        std::memcpy( mBuffer + mBufferUsed, lengthBytes, 8 );
        processBlock( mBuffer );
        mBufferUsed = 0;
        for ( int i = 0; i < 8; ++i )
        {
            out[4 * i + 0] = static_cast<unsigned char>( ( mState[i] >> 24 ) & 0xFFu );
            out[4 * i + 1] = static_cast<unsigned char>( ( mState[i] >> 16 ) & 0xFFu );
            out[4 * i + 2] = static_cast<unsigned char>( ( mState[i] >> 8 ) & 0xFFu );
            out[4 * i + 3] = static_cast<unsigned char>( mState[i] & 0xFFu );
        }
        reset();
    }

  private:
    void processBlock( const unsigned char *block )
    {
        std::uint32_t w[64];
        for ( int i = 0; i < 16; ++i )
        {
            w[i] = ( static_cast<std::uint32_t>( block[4 * i + 0] ) << 24 ) |
                   ( static_cast<std::uint32_t>( block[4 * i + 1] ) << 16 ) |
                   ( static_cast<std::uint32_t>( block[4 * i + 2] ) << 8 ) |
                   static_cast<std::uint32_t>( block[4 * i + 3] );
        }
        for ( int i = 16; i < 64; ++i )
        {
            const std::uint32_t s0 = rotr( w[i - 15], 7 ) ^ rotr( w[i - 15], 18 ) ^ ( w[i - 15] >> 3 );
            const std::uint32_t s1 = rotr( w[i - 2], 17 ) ^ rotr( w[i - 2], 19 ) ^ ( w[i - 2] >> 10 );
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = mState[0], b = mState[1], c = mState[2], d = mState[3];
        std::uint32_t e = mState[4], f = mState[5], g = mState[6], h = mState[7];

        for ( int i = 0; i < 64; ++i )
        {
            const std::uint32_t S1 = rotr( e, 6 ) ^ rotr( e, 11 ) ^ rotr( e, 25 );
            const std::uint32_t ch = ( e & f ) ^ ( ( ~e ) & g );
            const std::uint32_t temp1 = h + S1 + ch + kRoundConstants[i] + w[i];
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

        mState[0] += a;
        mState[1] += b;
        mState[2] += c;
        mState[3] += d;
        mState[4] += e;
        mState[5] += f;
        mState[6] += g;
        mState[7] += h;
    }

    std::uint32_t mState[8];
    unsigned char mBuffer[64];
    std::size_t mBufferUsed = 0;
    std::uint64_t mTotalBytes = 0;
};

std::string toHexLower( const unsigned char *bytes, std::size_t length )
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve( length * 2 );
    for ( std::size_t i = 0; i < length; ++i )
    {
        out.push_back( digits[ ( bytes[i] >> 4 ) & 0x0F ] );
        out.push_back( digits[ bytes[i] & 0x0F ] );
    }
    return out;
}

} // namespace

std::string sha256Hex( const void *data, std::size_t length )
{
    Sha256Core hasher;
    hasher.update( data, length );
    unsigned char digest[32];
    hasher.finalize( digest );
    return toHexLower( digest, 32 );
}

std::string sha256Hex( const std::string &text )
{
    return sha256Hex( text.data(), text.size() );
}

} // namespace sicnu::faultlab
