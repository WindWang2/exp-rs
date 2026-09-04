// src/processing/algorithms/sar/sar_ratio.cpp
#include "sar_ratio.h"

#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "processing/gdal/gdal_window_read.h"

#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

using sicnu::processing::readClampedWindow;
} // namespace

QString ratioOutputToString( RatioOutput output )
{
  switch ( output )
  {
    case RatioOutput::Ratio:
      return QStringLiteral( "ratio" );
    case RatioOutput::LogRatio:
      return QStringLiteral( "log_ratio" );
    case RatioOutput::LogDifference:
      return QStringLiteral( "log_difference" );
  }
  return QString();
}

/// Pure per-pixel ratio math (unit-testable).
double ratioValue( double a, double b, const RatioParams &params )
{
  if ( params.inputIsDb )
  {
    a = dbToLinear( a );
    b = dbToLinear( b );
  }
  if ( params.output == RatioOutput::Ratio )
  {
    if ( b == 0.0 )
      return std::numeric_limits<double>::quiet_NaN();
    return a / b;
  }
  // Log-domain outputs need strictly positive powers.
  if ( !( a > 0.0 ) || !( b > 0.0 ) )
    return std::numeric_limits<double>::quiet_NaN();
  const double dbA = linearToDb( a );
  const double dbB = linearToDb( b );
  if ( params.output == RatioOutput::LogRatio )
    return dbA - dbB;
  return std::fabs( dbA - dbB );
}

bool ratioRaster( const GdalDatasetWrapper &a, int bandA, const GdalDatasetWrapper &b,
                  int bandB, const RatioParams &params, float nodataA, float nodataB,
                  GdalStreamingOutput &dst, int tileDim,
                  const QString &polarizations, const QString &sensor )
{
  if ( !a.isValid() || !b.isValid() )
    return false;
  if ( a.width() != b.width() || a.height() != b.height() )
    return false;

  // Lockstep tile iteration (the change-streaming pattern): both scenes are
  // read once per tile via clamped windows (halo 0), O(tile) memory.
  const int tw = std::min( tileDim, a.width() );
  const int th = std::min( tileDim, a.height() );
  std::vector<float> tileA;
  std::vector<float> tileB;
  for ( int ty = 0; ty < a.height(); ty += th )
  {
    const int h = std::min( th, a.height() - ty );
    for ( int tx = 0; tx < a.width(); tx += tw )
    {
      const int w = std::min( tw, a.width() - tx );
      if ( !sicnu::processing::readClampedWindow( a, bandA, tx, ty, w, h, 0, tileA ) )
        return false;
      if ( !sicnu::processing::readClampedWindow( b, bandB, tx, ty, w, h, 0, tileB ) )
        return false;

      GdalBlockStream::Tile geom;
      geom.xOffset = tx;
      geom.yOffset = ty;
      geom.width = w;
      geom.height = h;
      geom.halo = 0;
      geom.bufferWidth = w;
      geom.bufferHeight = h;
      geom.rasterWidth = a.width();
      geom.rasterHeight = a.height();

      std::vector<float> out( static_cast<size_t>( w ) * h );
      for ( int y = 0; y < h; ++y )
      {
        for ( int x = 0; x < w; ++x )
        {
          const size_t idx = static_cast<size_t>( y ) * w + x;
          const float va = tileA[idx];
          const float vb = tileB[idx];
          if ( !std::isfinite( va ) || !std::isfinite( vb ) || va == nodataA || vb == nodataB )
          {
            out[idx] = kNan;
            continue;
          }
          const double r = ratioValue( va, vb, params );
          out[idx] = std::isfinite( r ) ? static_cast<float>( r ) : kNan;
        }
      }
      if ( !dst.writeTile( 1, geom, out.data() ) )
        return false;
    }
  }
  writeSarOutputMetadata( dst, QString(), QStringLiteral( "linear_power" ), polarizations,
                          sensor, 0.0, 0.0 );
  dst.setMetadataItem( QStringLiteral( "SICNU_SAR_RATIO_OUTPUT" ),
                       ratioOutputToString( params.output ) );
  return true;
}

} // namespace sicnu::sar
