// primitives/morphology.cpp — see morphology.h for the contract.
//
// The 3×3 eight-connectivity passes are formula-identical ports of the
// change-detection cleanup's erodePass/dilatePass (change_detection.cpp @
// 93a7fb0bbd); those now delegate here and tests/test_change_detection.cpp
// keeps pinning the behaviour through the threshold-cleanup path.

#include "morphology.h"

#include <cstring>

namespace sicnu::rs::primitives
{
namespace
{

constexpr uint8_t kForeground = 1;
constexpr uint8_t kBackground = 0;
constexpr uint8_t kNoData = 255;

void erodePass4( const uint8_t *src, uint8_t *dst, int width, int height )
{
  for ( int y = 0; y < height; ++y )
  {
    for ( int x = 0; x < width; ++x )
    {
      const size_t i = static_cast<size_t>( y ) * width + x;
      const uint8_t v = src[i];
      if ( v != kForeground )
      {
        dst[i] = v;
        continue;
      }
      bool all = true;
      // Border acts as foreground: out-of-raster neighbours keep the pixel.
      if ( x > 0 && src[i - 1] != kForeground )
        all = false;
      if ( all && x + 1 < width && src[i + 1] != kForeground )
        all = false;
      if ( all && y > 0 && src[i - static_cast<size_t>( width )] != kForeground )
        all = false;
      if ( all && y + 1 < height && src[i + static_cast<size_t>( width )] != kForeground )
        all = false;
      dst[i] = all ? kForeground : kBackground;
    }
  }
}

void erodePass8( const uint8_t *src, uint8_t *dst, int width, int height )
{
  for ( int y = 0; y < height; ++y )
  {
    for ( int x = 0; x < width; ++x )
    {
      const uint8_t v = src[static_cast<size_t>( y ) * width + x];
      if ( v != kForeground )
      {
        dst[static_cast<size_t>( y ) * width + x] = v;
        continue;
      }
      bool all = true;
      for ( int dy = -1; dy <= 1 && all; ++dy )
      {
        for ( int dx = -1; dx <= 1; ++dx )
        {
          if ( dx == 0 && dy == 0 )
            continue;
          const int nx = x + dx;
          const int ny = y + dy;
          if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
            continue; // border: keep the pixel
          if ( src[static_cast<size_t>( ny ) * width + nx] != kForeground )
          {
            all = false;
            break;
          }
        }
      }
      dst[static_cast<size_t>( y ) * width + x] = all ? kForeground : kBackground;
    }
  }
}

void dilatePass4( const uint8_t *src, uint8_t *dst, int width, int height )
{
  for ( int y = 0; y < height; ++y )
  {
    for ( int x = 0; x < width; ++x )
    {
      const size_t i = static_cast<size_t>( y ) * width + x;
      const uint8_t v = src[i];
      if ( v == kForeground || v == kNoData )
      {
        dst[i] = v;
        continue;
      }
      bool any = false;
      if ( x > 0 && src[i - 1] == kForeground )
        any = true;
      if ( !any && x + 1 < width && src[i + 1] == kForeground )
        any = true;
      if ( !any && y > 0 && src[i - static_cast<size_t>( width )] == kForeground )
        any = true;
      if ( !any && y + 1 < height && src[i + static_cast<size_t>( width )] == kForeground )
        any = true;
      dst[i] = any ? kForeground : kBackground;
    }
  }
}

void dilatePass8( const uint8_t *src, uint8_t *dst, int width, int height )
{
  for ( int y = 0; y < height; ++y )
  {
    for ( int x = 0; x < width; ++x )
    {
      const uint8_t v = src[static_cast<size_t>( y ) * width + x];
      if ( v == kForeground )
      {
        dst[static_cast<size_t>( y ) * width + x] = kForeground;
        continue;
      }
      if ( v == kNoData )
      {
        dst[static_cast<size_t>( y ) * width + x] = kNoData;
        continue;
      }
      bool any = false;
      for ( int dy = -1; dy <= 1 && !any; ++dy )
      {
        for ( int dx = -1; dx <= 1; ++dx )
        {
          if ( dx == 0 && dy == 0 )
            continue;
          const int nx = x + dx;
          const int ny = y + dy;
          if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
            continue;
          if ( src[static_cast<size_t>( ny ) * width + nx] == kForeground )
          {
            any = true;
            break;
          }
        }
      }
      dst[static_cast<size_t>( y ) * width + x] = any ? kForeground : kBackground;
    }
  }
}

} // namespace

void erode( const uint8_t *src, uint8_t *dst, int width, int height, Connectivity conn )
{
  if ( !src || !dst || width <= 0 || height <= 0 || src == dst )
    return;
  if ( conn == Connectivity::Four )
    erodePass4( src, dst, width, height );
  else
    erodePass8( src, dst, width, height );
}

void dilate( const uint8_t *src, uint8_t *dst, int width, int height, Connectivity conn )
{
  if ( !src || !dst || width <= 0 || height <= 0 || src == dst )
    return;
  if ( conn == Connectivity::Four )
    dilatePass4( src, dst, width, height );
  else
    dilatePass8( src, dst, width, height );
}

void open( uint8_t *mask, uint8_t *scratch, int width, int height, Connectivity conn )
{
  if ( !mask || !scratch || width <= 0 || height <= 0 || mask == scratch )
    return;
  erode( mask, scratch, width, height, conn );
  dilate( scratch, mask, width, height, conn );
}

void close( uint8_t *mask, uint8_t *scratch, int width, int height, Connectivity conn )
{
  if ( !mask || !scratch || width <= 0 || height <= 0 || mask == scratch )
    return;
  dilate( mask, scratch, width, height, conn );
  erode( scratch, mask, width, height, conn );
}

bool erodeN( uint8_t *mask, uint8_t *scratch, int width, int height, int iterations, Connectivity conn )
{
  if ( iterations < 0 )
    return false;
  for ( int i = 0; i < iterations; ++i )
  {
    erode( mask, scratch, width, height, conn );
    std::memcpy( mask, scratch, static_cast<size_t>( width ) * height );
  }
  return true;
}

bool dilateN( uint8_t *mask, uint8_t *scratch, int width, int height, int iterations, Connectivity conn )
{
  if ( iterations < 0 )
    return false;
  for ( int i = 0; i < iterations; ++i )
  {
    dilate( mask, scratch, width, height, conn );
    std::memcpy( mask, scratch, static_cast<size_t>( width ) * height );
  }
  return true;
}

} // namespace sicnu::rs::primitives
