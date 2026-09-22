/***************************************************************************
  lab/sha256.cpp — see sha256.h
***************************************************************************/

#include "sha256.h"

#include <array>
#include <cstring>

namespace sicnu::lab
{
namespace
{

constexpr std::array<uint32_t, 64> kRoundConstants = {
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
  0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t rotateRight( uint32_t value, unsigned bits )
{
  return ( value >> bits ) | ( value << ( 32u - bits ) );
}

void transform( uint32_t state[8], const uint8_t block[64] )
{
  std::array<uint32_t, 64> w{};
  for ( unsigned i = 0; i < 16; ++i )
  {
    w[i] = ( uint32_t( block[i * 4] ) << 24 ) | ( uint32_t( block[i * 4 + 1] ) << 16 ) |
           ( uint32_t( block[i * 4 + 2] ) << 8 ) | uint32_t( block[i * 4 + 3] );
  }
  for ( unsigned i = 16; i < 64; ++i )
  {
    const uint32_t s0 = rotateRight( w[i - 15], 7 ) ^ rotateRight( w[i - 15], 18 ) ^ ( w[i - 15] >> 3 );
    const uint32_t s1 = rotateRight( w[i - 2], 17 ) ^ rotateRight( w[i - 2], 19 ) ^ ( w[i - 2] >> 10 );
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

  for ( unsigned i = 0; i < 64; ++i )
  {
    const uint32_t s1 = rotateRight( e, 6 ) ^ rotateRight( e, 11 ) ^ rotateRight( e, 25 );
    const uint32_t ch = ( e & f ) ^ ( ~e & g );
    const uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
    const uint32_t s0 = rotateRight( a, 2 ) ^ rotateRight( a, 13 ) ^ rotateRight( a, 22 );
    const uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
    const uint32_t t2 = s0 + maj;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }

  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

std::string toHex( const uint8_t digest[32] )
{
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve( 64 );
  for ( unsigned i = 0; i < 32; ++i )
  {
    out.push_back( kDigits[digest[i] >> 4] );
    out.push_back( kDigits[digest[i] & 0x0fu] );
  }
  return out;
}

} // namespace

std::string sha256Hex( std::string_view bytes )
{
  uint32_t state[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  };

  const auto *data = reinterpret_cast<const uint8_t *>( bytes.data() );
  const std::size_t total = bytes.size();
  std::size_t offset = 0;

  while ( offset + 64 <= total )
  {
    transform( state, data + offset );
    offset += 64;
  }

  // Tail: remaining bytes + 0x80 + zeros + 64-bit big-endian bit length.
  uint8_t tail[128] = {};
  const std::size_t remainder = total - offset;
  std::memcpy( tail, data + offset, remainder );
  tail[remainder] = 0x80;
  const std::size_t tailBlocks = ( remainder + 9 <= 64 ) ? 1 : 2;
  const uint64_t bitLength = static_cast<uint64_t>( total ) * 8ull;
  for ( unsigned i = 0; i < 8; ++i )
    tail[tailBlocks * 64 - 1 - i] = static_cast<uint8_t>( bitLength >> ( 8u * i ) );
  transform( state, tail );
  if ( tailBlocks == 2 )
    transform( state, tail + 64 );

  uint8_t digest[32];
  for ( unsigned i = 0; i < 8; ++i )
  {
    digest[i * 4] = static_cast<uint8_t>( state[i] >> 24 );
    digest[i * 4 + 1] = static_cast<uint8_t>( state[i] >> 16 );
    digest[i * 4 + 2] = static_cast<uint8_t>( state[i] >> 8 );
    digest[i * 4 + 3] = static_cast<uint8_t>( state[i] );
  }
  return toHex( digest );
}

} // namespace sicnu::lab
