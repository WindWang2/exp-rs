/***************************************************************************
  geospatial/util/sha256.cpp — FIPS 180-4 SHA-256 (self-contained).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/util/sha256.h"

#include <cstring>

namespace sicnu::geo
{

namespace
{

constexpr std::uint32_t kRoundConstants[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline std::uint32_t rotr( std::uint32_t value, unsigned bits )
{
  return ( value >> bits ) | ( value << ( 32 - bits ) );
}

} // namespace

Sha256::Sha256()
{
  mState[0] = 0x6a09e667;
  mState[1] = 0xbb67ae85;
  mState[2] = 0x3c6ef372;
  mState[3] = 0xa54ff53a;
  mState[4] = 0x510e527f;
  mState[5] = 0x9b05688c;
  mState[6] = 0x1f83d9ab;
  mState[7] = 0x5be0cd19;
  mTotalBytes = 0;
  mBufferUsed = 0;
}

void Sha256::processBlock( const unsigned char *block )
{
  std::uint32_t w[64];
  for ( int i = 0; i < 16; ++i )
  {
    w[i] = ( static_cast<std::uint32_t>( block[i * 4] ) << 24 ) |
           ( static_cast<std::uint32_t>( block[i * 4 + 1] ) << 16 ) |
           ( static_cast<std::uint32_t>( block[i * 4 + 2] ) << 8 ) |
           static_cast<std::uint32_t>( block[i * 4 + 3] );
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
    const std::uint32_t s1 = rotr( e, 6 ) ^ rotr( e, 11 ) ^ rotr( e, 25 );
    const std::uint32_t ch = ( e & f ) ^ ( ~e & g );
    const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
    const std::uint32_t s0 = rotr( a, 2 ) ^ rotr( a, 13 ) ^ rotr( a, 22 );
    const std::uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
    const std::uint32_t temp2 = s0 + maj;
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

void Sha256::update( const void *data, std::size_t length )
{
  const unsigned char *bytes = static_cast<const unsigned char *>( data );
  mTotalBytes += length;
  while ( length > 0 )
  {
    const std::size_t take = std::min( length, static_cast<std::size_t>( 64 ) - mBufferUsed );
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

std::vector<unsigned char> Sha256::finalize()
{
  const std::uint64_t bitLength = mTotalBytes * 8;
  const unsigned char oneByte = 0x80;
  update( &oneByte, 1 );
  const unsigned char zeroByte = 0x00;
  while ( mBufferUsed != 56 )
    update( &zeroByte, 1 );
  unsigned char lengthBytes[8];
  for ( int i = 0; i < 8; ++i )
    lengthBytes[i] = static_cast<unsigned char>( bitLength >> ( 56 - i * 8 ) );
  update( lengthBytes, 8 );

  std::vector<unsigned char> digest( 32 );
  for ( int i = 0; i < 8; ++i )
  {
    digest[i * 4] = static_cast<unsigned char>( mState[i] >> 24 );
    digest[i * 4 + 1] = static_cast<unsigned char>( mState[i] >> 16 );
    digest[i * 4 + 2] = static_cast<unsigned char>( mState[i] >> 8 );
    digest[i * 4 + 3] = static_cast<unsigned char>( mState[i] );
  }
  // Reset for reuse.
  *this = Sha256();
  return digest;
}

std::vector<unsigned char> sha256Bytes( const void *data, std::size_t length )
{
  Sha256 hash;
  hash.update( data, length );
  return hash.finalize();
}

std::string toHex( const std::vector<unsigned char> &bytes )
{
  static const char *digits = "0123456789abcdef";
  std::string out;
  out.reserve( bytes.size() * 2 );
  for ( const unsigned char b : bytes )
  {
    out += digits[b >> 4];
    out += digits[b & 0x0F];
  }
  return out;
}

std::string sha256Hex( const std::string &text )
{
  return toHex( sha256Bytes( text.data(), text.size() ) );
}

} // namespace sicnu::geo
