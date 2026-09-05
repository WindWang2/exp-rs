// src/processing/algorithms/sar/sar_speckle.cpp
#include "sar_speckle.h"

#include "processing/algorithms/image_enhancement_streaming.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "processing/gdal/gdal_window_read.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// Directions for refined-Lee's 8-window edge detection (unit offsets).
struct DirWindow
{
  int dx;
  int dy;
};

constexpr DirWindow kDirections[8] = {
  { 1, 0 },  { 1, 1 },   { 0, 1 },  { -1, 1 },
  { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
};
} // namespace

QString speckleMethodToString( SpeckleMethod method )
{
  switch ( method )
  {
    case SpeckleMethod::Lee:
      return QStringLiteral( "lee" );
    case SpeckleMethod::EnhancedLee:
      return QStringLiteral( "enhanced_lee" );
    case SpeckleMethod::Frost:
      return QStringLiteral( "frost" );
    case SpeckleMethod::Kuan:
      return QStringLiteral( "kuan" );
    case SpeckleMethod::GammaMap:
      return QStringLiteral( "gamma_map" );
    case SpeckleMethod::RefinedLee:
      return QStringLiteral( "refined_lee" );
    case SpeckleMethod::Multitemporal:
      return QStringLiteral( "multitemporal" );
  }
  return QString();
}

SpeckleMethod speckleMethodFromString( const QString &token, bool *ok )
{
  const QString t = token.trimmed().toLower();
  *ok = true;
  if ( t == QLatin1String( "lee" ) )
    return SpeckleMethod::Lee;
  if ( t == QLatin1String( "enhanced_lee" ) || t == QLatin1String( "enhancedlee" ) )
    return SpeckleMethod::EnhancedLee;
  if ( t == QLatin1String( "frost" ) )
    return SpeckleMethod::Frost;
  if ( t == QLatin1String( "kuan" ) )
    return SpeckleMethod::Kuan;
  if ( t == QLatin1String( "gamma_map" ) || t == QLatin1String( "gammamap" ) )
    return SpeckleMethod::GammaMap;
  if ( t == QLatin1String( "refined_lee" ) || t == QLatin1String( "refinedlee" ) )
    return SpeckleMethod::RefinedLee;
  if ( t == QLatin1String( "multitemporal" ) )
    return SpeckleMethod::Multitemporal;
  *ok = false;
  return SpeckleMethod::Lee;
}

void refinedLeeTile( const GdalBlockStream::Tile &tile, const float *haloBuf, float *coreOut,
                     int kernelSize, int looks )
{
  const int r = kernelSize / 2;
  const int bw = tile.bufferWidth;
  const int ci = tile.halo; // center offset == halo

  for ( int y = 0; y < tile.height; ++y )
  {
    for ( int x = 0; x < tile.width; ++x )
    {
      const float center = haloBuf[( y + ci ) * bw + ( x + ci )];
      if ( !std::isfinite( center ) )
      {
        coreOut[y * tile.width + x] = kNan;
        continue;
      }

      // Lee's refined filter: pick the 8-direction sub-window with the lowest
      // variance (the one NOT crossing an edge), then MMSE-blend the center
      // pixel toward that window's mean.
      float bestMean = center;
      double bestVar = std::numeric_limits<double>::max();
      for ( const DirWindow &dir : kDirections )
      {
        double sum = 0.0;
        double sumSq = 0.0;
        int count = 0;
        for ( int j = -r; j <= r; ++j )
        {
          for ( int i2 = -r; i2 <= r; ++i2 )
          {
            const bool sx = i2 * dir.dx >= 0;
            const bool sy = j * dir.dy >= 0;
            if ( dir.dx != 0 && dir.dy != 0 )
            {
              if ( !sx || !sy )
                continue;
            }
            else if ( dir.dx != 0 )
            {
              if ( !sx )
                continue;
            }
            else
            {
              if ( !sy )
                continue;
            }
            const float v = haloBuf[( y + ci + j ) * bw + ( x + ci + i2 )];
            if ( !std::isfinite( v ) )
              continue;
            sum += v;
            sumSq += static_cast<double>( v ) * v;
            ++count;
          }
        }
        if ( count < 4 )
          continue;
        const double mean = sum / count;
        const double variance = std::max( 0.0, sumSq / count - mean * mean );
        if ( variance < bestVar )
        {
          bestVar = variance;
          bestMean = static_cast<float>( mean );
        }
      }
      if ( bestVar == std::numeric_limits<double>::max() )
      {
        coreOut[y * tile.width + x] = center;
        continue;
      }

      const double cu = 1.0 / std::sqrt( static_cast<double>( std::max( 1, looks ) ) );
      const double meanD = static_cast<double>( bestMean );
      const double ciVar = bestVar / ( meanD * meanD + 1e-12 );
      const double w = std::clamp( 1.0 - cu * cu / ( ciVar + 1e-12 ), 0.0, 1.0 );
      const double estimate = meanD + w * ( static_cast<double>( center ) - meanD );
      coreOut[y * tile.width + x] = static_cast<float>( estimate );
    }
  }
}

/// Tile kernel replica of ImageEnhancement::enhancedLeeFilter's per-pixel
/// formula (Lopes modified-Lee): homogeneous → mean, point target → pixel,
/// heterogeneous → adaptive exponential weighting between the two.
void enhancedLeeTile( const GdalBlockStream::Tile &tile, const float *haloBuf, float *coreOut,
                      int kernelSize, float noiseVariance, float damping )
{
  const int r = kernelSize / 2;
  const int bw = tile.bufferWidth;
  const int ci = tile.halo;
  const float cu = std::sqrt( noiseVariance );
  const float cmax = std::sqrt( 1.0f + 2.0f * noiseVariance );

  for ( int y = 0; y < tile.height; ++y )
  {
    for ( int x = 0; x < tile.width; ++x )
    {
      const float pixel = haloBuf[( y + ci ) * bw + ( x + ci )];
      if ( !std::isfinite( pixel ) )
      {
        coreOut[y * tile.width + x] = kNan;
        continue;
      }
      double sum = 0.0;
      double sumSq = 0.0;
      int count = 0;
      for ( int j = -r; j <= r; ++j )
      {
        for ( int i2 = -r; i2 <= r; ++i2 )
        {
          const float v = haloBuf[( y + ci + j ) * bw + ( x + ci + i2 )];
          if ( !std::isfinite( v ) )
            continue;
          sum += v;
          sumSq += static_cast<double>( v ) * v;
          ++count;
        }
      }
      if ( count == 0 )
      {
        coreOut[y * tile.width + x] = pixel;
        continue;
      }
      const float mean = static_cast<float>( sum / count );
      const float localVar = static_cast<float>(
        std::max( 0.0, sumSq / count - static_cast<double>( mean ) * mean ) );

      if ( localVar <= 0.0f || mean <= 0.0f )
      {
        coreOut[y * tile.width + x] = mean;
        continue;
      }
      const float cl = std::sqrt( localVar ) / mean;
      if ( cl <= cu )
      {
        coreOut[y * tile.width + x] = mean;
      }
      else if ( cl >= cmax )
      {
        coreOut[y * tile.width + x] = pixel;
      }
      else
      {
        const float denom = cmax - cl;
        const float weight =
          ( denom > 1e-6f ) ? std::exp( -damping * ( cl - cu ) / denom ) : 0.0f;
        coreOut[y * tile.width + x] = mean * weight + pixel * ( 1.0f - weight );
      }
    }
  }
}

bool speckleRaster( const GdalDatasetWrapper &src, int band,
                    const SpeckleParams &params, float nodata,
                    const QStringList &companionPaths,
                    GdalStreamingOutput &dst, int tileDim, int outBand,
                    const QString &polarizations, const QString &sensor )
{
  const int kernel = std::clamp( params.kernelSize, 3, 15 );
  const int halo = kernel / 2;

  if ( params.method == SpeckleMethod::Multitemporal )
  {
    if ( companionPaths.isEmpty() )
      return false;
    std::vector<GdalDatasetWrapper> companions( companionPaths.size() );
    for ( int i = 0; i < companionPaths.size(); ++i )
    {
      if ( !companions[i].open( companionPaths.at( i ) ) )
        return false;
      if ( companions[i].width() != src.width() || companions[i].height() != src.height() )
        return false;
    }

    // Lockstep tile streaming: every scene is read once per tile (O(tile)
    // memory, one pass — the multi-temporal gate compares each companion
    // pixel against ITS OWN local statistics, then pools valid observations).
    const int tw = std::min( tileDim, src.width() );
    const int th = std::min( tileDim, src.height() );
    const int totalTiles =
      ( ( src.width() + tw - 1 ) / tw ) * ( ( src.height() + th - 1 ) / th );
    int tileIndex = 0;
    for ( int ty = 0; ty < src.height(); ty += th )
    {
      for ( int tx = 0; tx < src.width(); tx += tw )
      {
        const int w = std::min( tw, src.width() - tx );
        const int h = std::min( th, src.height() - ty );

        std::vector<float> refTile;
        if ( !sicnu::processing::readClampedWindow( src, band, tx, ty, w, h, halo, refTile ) )
          return false;

        const size_t core = static_cast<size_t>( w ) * h;
        std::vector<double> sumV( core, 0.0 );
        std::vector<int> count( core, 0 );
        const int bw = w + 2 * halo;

        auto accumulate = [&]( const std::vector<float> &haloBuf ) {
          for ( int y = 0; y < h; ++y )
          {
            for ( int x = 0; x < w; ++x )
            {
              const size_t idx = static_cast<size_t>( y ) * w + x;
              const float v = haloBuf[( y + halo ) * bw + ( x + halo )];
              if ( !std::isfinite( v ) || v == nodata )
                continue;
              // Local statistics in THIS scene decide the deviation gate.
              double wsum = 0.0;
              double wsumSq = 0.0;
              int wcount = 0;
              for ( int j = -halo; j <= halo; ++j )
              {
                for ( int i2 = -halo; i2 <= halo; ++i2 )
                {
                  const float wv = haloBuf[( y + halo + j ) * bw + ( x + halo + i2 )];
                  if ( !std::isfinite( wv ) || wv == nodata )
                    continue;
                  wsum += wv;
                  wsumSq += static_cast<double>( wv ) * wv;
                  ++wcount;
                }
              }
              if ( wcount == 0 )
                continue;
              const double mean = wsum / wcount;
              const double var = std::max( 0.0, wsumSq / wcount - mean * mean );
              if ( std::fabs( v - mean ) <= params.deviationK * std::sqrt( var ) )
              {
                sumV[idx] += v;
                ++count[idx];
              }
            }
          }
        };

        accumulate( refTile );
        std::vector<float> companionTile;
        for ( size_t c = 0; c < companions.size(); ++c )
        {
          if ( !sicnu::processing::readClampedWindow( companions[c], band, tx, ty, w, h, halo,
                                                      companionTile ) )
            return false;
          accumulate( companionTile );
        }

        std::vector<float> out( core );
        for ( size_t i = 0; i < core; ++i )
        {
          if ( count[i] == 0 )
          {
            const float ref = refTile[( static_cast<int>( i ) / w + halo ) * bw +
                                      ( static_cast<int>( i ) % w + halo )];
            out[i] = std::isfinite( ref ) ? ref : kNan;
            continue;
          }
          out[i] = static_cast<float>( sumV[i] / count[i] );
        }

        // writeTile expects the tile geometry shape of GdalBlockStream::Tile.
        GdalBlockStream::Tile geom;
        geom.xOffset = tx;
        geom.yOffset = ty;
        geom.width = w;
        geom.height = h;
        geom.halo = 0;
        geom.bufferWidth = w;
        geom.bufferHeight = h;
        geom.index = tileIndex++;
        geom.totalTiles = totalTiles;
        geom.rasterWidth = src.width();
        geom.rasterHeight = src.height();
        if ( !dst.writeTile( outBand, geom, out.data() ) )
          return false;
      }
    }
    writeSarOutputMetadata( dst, QString(), QStringLiteral( "linear_power" ), polarizations,
                            sensor, 0.0, 0.0 );
    dst.setMetadataItem( QStringLiteral( "SICNU_SAR_SPECKLE" ), QStringLiteral( "multitemporal" ) );
    return true;
  }

  // Spatial filters: route to the existing streaming kernels (no duplicate
  // formulas) plus the refined-Lee kernel defined above.
  ImageEnhancementStreaming::WindowedTileFn kernelFn;
  switch ( params.method )
  {
    case SpeckleMethod::Lee:
      kernelFn = [ & ]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
        ImageEnhancementStreaming::speckleTileLee( tile, buf, core, kernel,
                                                   static_cast<float>( params.noiseVariance ) );
      };
      break;
    case SpeckleMethod::EnhancedLee:
      kernelFn = [ & ]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
        enhancedLeeTile( tile, buf, core, kernel, static_cast<float>( params.noiseVariance ),
                         static_cast<float>( params.dampingFactor ) );
      };
      break;
    case SpeckleMethod::Frost:
      kernelFn = [ & ]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
        ImageEnhancementStreaming::speckleTileFrost( tile, buf, core, kernel,
                                                     static_cast<float>( params.dampingFactor ) );
      };
      break;
    case SpeckleMethod::Kuan:
      kernelFn = [ & ]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
        ImageEnhancementStreaming::speckleTileKuan( tile, buf, core, kernel,
                                                    static_cast<float>( params.noiseVariance ) );
      };
      break;
    case SpeckleMethod::GammaMap:
      kernelFn = [ & ]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
        ImageEnhancementStreaming::speckleTileGammaMap( tile, buf, core, kernel,
                                                        static_cast<float>( params.noiseVariance ) );
      };
      break;
    case SpeckleMethod::RefinedLee:
      kernelFn = [ & ]( const GdalBlockStream::Tile &tile, const float *buf, float *core ) {
        refinedLeeTile( tile, buf, core, kernel, params.looks );
      };
      break;
    case SpeckleMethod::Multitemporal:
      return false; // handled above
  }

  // The shared kernels don't know declared sentinels: mask them to NaN in a
  // pre-pass copy of the halo buffer (O(tile) memory) so statistics stay
  // physical.
  GdalBlockStream stream( src, band, tileDim, tileDim, halo );
  const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *pixels ) {
    std::vector<float> cleaned( static_cast<size_t>( tile.bufferWidth ) * tile.bufferHeight );
    for ( size_t i = 0; i < cleaned.size(); ++i )
    {
      const float v = pixels[i];
      cleaned[i] = ( !std::isfinite( v ) || v == nodata ) ? kNan : v;
    }
    std::vector<float> out( static_cast<size_t>( tile.width ) * tile.height );
    kernelFn( tile, cleaned.data(), out.data() );
    return dst.writeTile( outBand, tile, out.data() );
  } );
  if ( ok )
  {
    writeSarOutputMetadata( dst, QString(), QStringLiteral( "linear_power" ), polarizations,
                            sensor, 0.0, 0.0 );
    dst.setMetadataItem( QStringLiteral( "SICNU_SAR_SPECKLE" ),
                         speckleMethodToString( params.method ) );
  }
  return ok;
}

} // namespace sicnu::sar
