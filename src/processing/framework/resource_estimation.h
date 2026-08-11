// src/processing/framework/resource_estimation.h
#pragma once

#include <json/json.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>

namespace sicnu::processing {

/// Overflow-safe product of two unsigned 64-bit values; nullopt on overflow.
inline std::optional<std::uint64_t> checkedMul( std::uint64_t a, std::uint64_t b )
{
  if ( a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a )
    return std::nullopt;
  return a * b;
}

/// Overflow-safe product of several factors; nullopt on overflow.
inline std::optional<std::uint64_t> checkedMulN( std::initializer_list<std::uint64_t> factors )
{
  std::uint64_t acc = 1;
  for ( const std::uint64_t f : factors )
  {
    const auto r = checkedMul( acc, f );
    if ( !r ) return std::nullopt;
    acc = *r;
  }
  return acc;
}

/// Bytes per sample for a GDALDataType value (GDT_Byte=1 ... GDT_CFloat64=11);
/// 0 for GDT_Unknown.
inline std::uint64_t gdalBytesPerSample( int gdalDataType )
{
  switch ( gdalDataType )
  {
    case 1: return 1;   // GDT_Byte
    case 2:             // GDT_UInt16
    case 3: return 2;   // GDT_Int16
    case 4:             // GDT_UInt32
    case 5:             // GDT_Int32
    case 8:             // GDT_CInt16
    case 10: return 4;  // GDT_CFloat32
    case 6: return 4;   // GDT_Float32
    case 7:             // GDT_Float64
    case 9:             // GDT_CInt32
    case 11: return 8;  // GDT_CFloat64
    default: return 0;
  }
}

/**
 * Builds an execution-resource estimate JSON object for a streaming operator:
 *   estimatedRamBytes = tileWidth*tileHeight*bands*bytesPerSample*
 *                       simultaneousBuffers + matrixBytes + fixedOverhead
 * All arithmetic is overflow-safe: on overflow the RAM field is omitted
 * (0 = unknown) rather than silently wrapping to a bogus small number.
 *
 * Result keys: "tileWidth", "tileHeight", "estimatedRamBytes",
 * "temporaryDiskBytes", "basis" ("dynamic"). When an input raster's band count
 * is unknown (0), the estimate still covers a single band so it never
 * understates the per-tile working set.
 */
inline Json::Value makeStreamingEstimate( std::uint64_t tileWidth, std::uint64_t tileHeight,
                                          std::uint64_t bands, std::uint64_t bytesPerSample,
                                          std::uint64_t simultaneousBuffers,
                                          std::uint64_t matrixBytes, std::uint64_t fixedOverhead,
                                          std::uint64_t temporaryDiskBytes = 0 )
{
  Json::Value est( Json::objectValue );
  est["tileWidth"] = Json::Value::UInt64( tileWidth );
  est["tileHeight"] = Json::Value::UInt64( tileHeight );

  const std::uint64_t safeBands = bands > 0 ? bands : 1;
  const std::uint64_t safeBytes = bytesPerSample > 0 ? bytesPerSample : 4;

  std::optional<std::uint64_t> ram =
    checkedMulN( { tileWidth, tileHeight, safeBands, safeBytes, simultaneousBuffers } );
  if ( ram )
  {
    const std::uint64_t matrixAndFixed = matrixBytes + fixedOverhead;
    const bool matrixOverflow = matrixAndFixed < matrixBytes; // wrap check
    if ( !matrixOverflow )
    {
      const std::uint64_t total = *ram + matrixAndFixed;
      if ( total >= *ram ) // no wrap
        est["estimatedRamBytes"] = Json::Value::UInt64( total );
    }
  }

  if ( temporaryDiskBytes > 0 )
    est["temporaryDiskBytes"] = Json::Value::UInt64( temporaryDiskBytes );
  est["basis"] = "dynamic";
  return est;
}

} // namespace sicnu::processing
