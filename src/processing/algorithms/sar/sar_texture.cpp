// src/processing/algorithms/sar/sar_texture.cpp
#include "sar_texture.h"

#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

struct GlcmStats
{
  double mean = 0.0;
  double stddev = 0.0;
  double correlation = 0.0;
  bool defined = false;
};

GlcmStats momentsOf( std::vector<double> &p, int levels )
{
  GlcmStats s;
  double sum = 0.0;
  for ( int i = 0; i < levels; ++i )
    for ( int j = 0; j < levels; ++j )
      sum += p[static_cast<size_t>( i ) * levels + j];
  if ( sum <= 0.0 )
    return s;
  for ( size_t i = 0; i < p.size(); ++i )
    p[i] /= sum;

  double mu = 0.0;
  for ( int i = 0; i < levels; ++i )
    for ( int j = 0; j < levels; ++j )
      mu += i * p[static_cast<size_t>( i ) * levels + j];
  double muY = 0.0;
  for ( int i = 0; i < levels; ++i )
    for ( int j = 0; j < levels; ++j )
      muY += j * p[static_cast<size_t>( i ) * levels + j];
  double var = 0.0;
  double varY = 0.0;
  for ( int i = 0; i < levels; ++i )
    for ( int j = 0; j < levels; ++j )
    {
      var += ( i - mu ) * ( i - mu ) * p[static_cast<size_t>( i ) * levels + j];
      varY += ( j - muY ) * ( j - muY ) * p[static_cast<size_t>( i ) * levels + j];
    }
  double cov = 0.0;
  for ( int i = 0; i < levels; ++i )
    for ( int j = 0; j < levels; ++j )
      cov += ( i - mu ) * ( j - muY ) * p[static_cast<size_t>( i ) * levels + j];

  s.mean = mu;
  s.stddev = std::sqrt( var );
  s.correlation = ( var > 0.0 && varY > 0.0 ) ? cov / std::sqrt( var * varY ) : 1.0;
  s.defined = true;
  return s;
}
} // namespace

GlcmMeasure glcmMeasureFromString( const QString &token, bool *ok )
{
  const QString t = token.trimmed().toLower();
  *ok = true;
  if ( t == QLatin1String( "contrast" ) )
    return GlcmMeasure::Contrast;
  if ( t == QLatin1String( "dissimilarity" ) )
    return GlcmMeasure::Dissimilarity;
  if ( t == QLatin1String( "homogeneity" ) )
    return GlcmMeasure::Homogeneity;
  if ( t == QLatin1String( "energy" ) )
    return GlcmMeasure::Energy;
  if ( t == QLatin1String( "asm" ) )
    return GlcmMeasure::Energy;
  if ( t == QLatin1String( "entropy" ) )
    return GlcmMeasure::Entropy;
  if ( t == QLatin1String( "mean" ) )
    return GlcmMeasure::Mean;
  if ( t == QLatin1String( "stddev" ) || t == QLatin1String( "standard_deviation" ) )
    return GlcmMeasure::StdDev;
  if ( t == QLatin1String( "correlation" ) )
    return GlcmMeasure::Correlation;
  *ok = false;
  return GlcmMeasure::Contrast;
}

QString glcmMeasureToString( GlcmMeasure measure )
{
  switch ( measure )
  {
    case GlcmMeasure::Contrast:
      return QStringLiteral( "contrast" );
    case GlcmMeasure::Dissimilarity:
      return QStringLiteral( "dissimilarity" );
    case GlcmMeasure::Homogeneity:
      return QStringLiteral( "homogeneity" );
    case GlcmMeasure::Energy:
      return QStringLiteral( "energy" );
    case GlcmMeasure::Entropy:
      return QStringLiteral( "entropy" );
    case GlcmMeasure::Mean:
      return QStringLiteral( "mean" );
    case GlcmMeasure::StdDev:
      return QStringLiteral( "stddev" );
    case GlcmMeasure::Correlation:
      return QStringLiteral( "correlation" );
  }
  return QString();
}

void glcmMeasuresForWindow( const float *window, int windowSize, int quantLevels,
                            int dx, int dy, const std::vector<GlcmMeasure> &measureList,
                            float *values )
{
  const int n = windowSize * windowSize;

  // Window extent → equal-width quantization.
  float lo = window[0];
  float hi = window[0];
  for ( int i = 1; i < n; ++i )
  {
    lo = std::min( lo, window[i] );
    hi = std::max( hi, window[i] );
  }
  std::vector<int> level( n, 0 );
  const double span = hi > lo ? static_cast<double>( hi - lo ) : 0.0;
  if ( span <= 0.0 )
  {
    // Degenerate window: every level identical. Contrast/dissimilarity = 0,
    // homogeneity = 1, energy = 1, entropy = 0, correlation = 1 (flat),
    // mean = stddev = quantized mid-level semantics are undefined → use 0.
    for ( size_t m = 0; m < measureList.size(); ++m )
    {
      switch ( measureList[m] )
      {
        case GlcmMeasure::Homogeneity:
        case GlcmMeasure::Energy:
        case GlcmMeasure::Correlation:
          values[m] = 1.0f;
          break;
        default:
          values[m] = 0.0f;
          break;
      }
    }
    return;
  }
  for ( int i = 0; i < n; ++i )
  {
    const double norm = ( static_cast<double>( window[i] ) - lo ) / span;
    int q = static_cast<int>( norm * ( quantLevels - 1 ) + 0.5 );
    level[i] = std::clamp( q, 0, quantLevels - 1 );
  }

  // Symmetric co-occurrence counts for the offset (dx, dy).
  std::vector<double> p( static_cast<size_t>( quantLevels ) * quantLevels, 0.0 );
  long pairs = 0;
  for ( int y = 0; y < windowSize; ++y )
  {
    for ( int x = 0; x < windowSize; ++x )
    {
      const int nx = x + dx;
      const int ny = y + dy;
      if ( nx < 0 || nx >= windowSize || ny < 0 || ny >= windowSize )
        continue;
      const int a = level[y * windowSize + x];
      const int b = level[ny * windowSize + nx];
      p[static_cast<size_t>( a ) * quantLevels + b] += 1.0;
      p[static_cast<size_t>( b ) * quantLevels + a] += 1.0;
      pairs += 2;
    }
  }
  if ( pairs == 0 )
  {
    for ( size_t m = 0; m < measureList.size(); ++m )
      values[m] = kNan;
    return;
  }
  for ( size_t i = 0; i < p.size(); ++i )
    p[i] /= pairs;

  const GlcmStats moments = momentsOf( p, quantLevels ); // also normalizes p in place

  double contrast = 0.0;
  double dissimilarity = 0.0;
  double homogeneity = 0.0;
  double energy = 0.0;
  double entropy = 0.0;
  for ( int i = 0; i < quantLevels; ++i )
  {
    for ( int j = 0; j < quantLevels; ++j )
    {
      const double pij = p[static_cast<size_t>( i ) * quantLevels + j];
      if ( pij <= 0.0 )
        continue;
      const double d = std::abs( i - j );
      contrast += d * d * pij;
      dissimilarity += d * pij;
      homogeneity += pij / ( 1.0 + d * d );
      energy += pij * pij;
      entropy -= pij * std::log( pij );
    }
  }

  for ( size_t m = 0; m < measureList.size(); ++m )
  {
    switch ( measureList[m] )
    {
      case GlcmMeasure::Contrast:
        values[m] = static_cast<float>( contrast );
        break;
      case GlcmMeasure::Dissimilarity:
        values[m] = static_cast<float>( dissimilarity );
        break;
      case GlcmMeasure::Homogeneity:
        values[m] = static_cast<float>( homogeneity );
        break;
      case GlcmMeasure::Energy:
        values[m] = static_cast<float>( energy );
        break;
      case GlcmMeasure::Entropy:
        values[m] = static_cast<float>( entropy );
        break;
      case GlcmMeasure::Mean:
        values[m] = static_cast<float>( moments.defined ? moments.mean : 0.0 );
        break;
      case GlcmMeasure::StdDev:
        values[m] = static_cast<float>( moments.defined ? moments.stddev : 0.0 );
        break;
      case GlcmMeasure::Correlation:
        values[m] = static_cast<float>( moments.defined ? moments.correlation : 1.0 );
        break;
    }
  }
}

bool textureRaster( const GdalDatasetWrapper &src, int band,
                    const TextureParams &params, float nodata,
                    GdalStreamingOutput &dst, int tileDim,
                    const QString &polarizations, const QString &sensor )
{
  const int window = std::clamp( params.windowSize, 3, 15 );
  const int levels = std::clamp( params.quantLevels, 2, 64 );
  const int halo = window / 2;
  const int displacement = std::max( 1, params.displacement );

  int dx = 0;
  int dy = 0;
  switch ( std::clamp( params.directionDeg, 0, 135 ) )
  {
    case 45:
      dx = displacement;
      dy = -displacement;
      break;
    case 90:
      dx = 0;
      dy = displacement;
      break;
    case 135:
      dx = displacement;
      dy = displacement;
      break;
    default:
      dx = displacement;
      dy = 0;
      break;
  }

  std::vector<GlcmMeasure> measureList;
  if ( params.measures.isEmpty() )
  {
    measureList = { GlcmMeasure::Contrast,    GlcmMeasure::Dissimilarity,
                    GlcmMeasure::Homogeneity, GlcmMeasure::Energy,
                    GlcmMeasure::Entropy,     GlcmMeasure::Mean,
                    GlcmMeasure::StdDev,      GlcmMeasure::Correlation };
  }
  else
  {
    for ( const QString &token : params.measures )
    {
      bool ok = false;
      const GlcmMeasure m = glcmMeasureFromString( token, &ok );
      if ( !ok )
        return false;
      measureList.push_back( m );
    }
  }

  std::vector<float> window_( static_cast<size_t>( window ) * window );
  std::vector<float> values( measureList.size() );

  GdalBlockStream stream( src, band, tileDim, tileDim, halo );
  const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *pixels ) {
    const int bw = tile.bufferWidth;
    const int ci = tile.halo;
    std::vector<std::vector<float>> outs( measureList.size() );
    for ( auto &o : outs )
      o.assign( static_cast<size_t>( tile.width ) * tile.height, kNan );

    for ( int y = 0; y < tile.height; ++y )
    {
      for ( int x = 0; x < tile.width; ++x )
      {
        const float v = pixels[( y + ci ) * bw + ( x + ci )];
        if ( !std::isfinite( v ) || v == nodata )
          continue;
        bool allValid = true;
        for ( int j = -halo; j <= halo && allValid; ++j )
        {
          for ( int i2 = -halo; i2 <= halo; ++i2 )
          {
            const float wv = pixels[( y + ci + j ) * bw + ( x + ci + i2 )];
            window_[( j + halo ) * window + ( i2 + halo )] = wv;
            if ( !std::isfinite( wv ) || wv == nodata )
            {
              allValid = false;
              break;
            }
          }
        }
        if ( !allValid )
          continue; // border-adjacent NoData → NaN measures (documented)
        glcmMeasuresForWindow( window_.data(), window, levels, dx, dy, measureList, values.data() );
        for ( size_t m = 0; m < measureList.size(); ++m )
          outs[m][static_cast<size_t>( y ) * tile.width + x] = values[m];
      }
    }
    for ( size_t m = 0; m < measureList.size(); ++m )
    {
      if ( !dst.writeTile( static_cast<int>( m ) + 1, tile, outs[m].data() ) )
        return false;
    }
    return true;
  } );
  if ( ok )
  {
    writeSarOutputMetadata( dst, QString(), QStringLiteral( "linear_power" ), polarizations,
                            sensor, 0.0, 0.0 );
    dst.setMetadataItem( QStringLiteral( "SICNU_SAR_TEXTURE_MEASURES" ), measureList.size() == 1
                                                                            ? glcmMeasureToString( measureList.front() )
                                                                            : QStringLiteral( "%1 measures" )
                                                                                .arg( measureList.size() ) );
  }
  return ok;
}

} // namespace sicnu::sar
