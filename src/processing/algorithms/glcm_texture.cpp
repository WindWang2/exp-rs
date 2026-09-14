// src/processing/algorithms/glcm_texture.cpp — D15 Package B: GLCM texture.
#include "glcm_texture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace rs::processing
{
namespace
{
  constexpr double kEntropyEpsilon = 1e-12;

  GlcmHaralickMetrics nanMetrics()
  {
    GlcmHaralickMetrics m;
    m.contrast = std::nan( "" );
    m.dissimilarity = std::nan( "" );
    m.homogeneity = std::nan( "" );
    m.energy = std::nan( "" );
    m.entropy = std::nan( "" );
    m.angularSecondMoment = std::nan( "" );
    m.mean = std::nan( "" );
    m.variance = std::nan( "" );
    m.correlation = std::nan( "" );
    return m;
  }

  struct Offset
  {
      int dx, dy;
  };

  Offset offsetFor( GlcmDirection direction, int d )
  {
    switch ( direction )
    {
      case GlcmDirection::Deg0: return { d, 0 };
      case GlcmDirection::Deg45: return { d, -d };
      case GlcmDirection::Deg90: return { 0, d };
      case GlcmDirection::Deg135: return { -d, -d };
      case GlcmDirection::Omnidirectional:
      default: return { d, 0 };
    }
  }

  int quantize( float v, const GlcmConfig &config )
  {
    const double span = static_cast<double>( config.maxVal ) - static_cast<double>( config.minVal );
    if ( !( span > 0.0 ) )
      return 0;
    double t = ( static_cast<double>( v ) - config.minVal ) / span * config.quantLevels;
    if ( std::isnan( t ) )
      return 0;
    // Clamp BEFORE the float->int conversion: t is unbounded for extreme
    // inputs and [conv.fpint] makes an out-of-range cast undefined.
    t = std::clamp( t, 0.0, static_cast<double>( config.quantLevels - 1 ) );
    return static_cast<int>( std::floor( t ) );
  }

  // Normalized co-occurrence for one direction; nullopt when no pair fits
  // the window at this offset (degenerate geometry for the step).
  std::optional<std::vector<double>> directionalGlcm( std::span<const float> windowPixels,
                                                      int winWidth, int winHeight,
                                                      const GlcmConfig &config )
  {
    if ( config.quantLevels < 1 || winWidth <= 0 || winHeight <= 0
         || windowPixels.size() < static_cast<size_t>( winWidth ) * winHeight
         || !( config.maxVal > config.minVal ) || config.stepDistance < 1 )
      return std::nullopt;

    const int g = config.quantLevels;
    const Offset off = offsetFor( config.direction, config.stepDistance );
    std::vector<uint64_t> counts( static_cast<size_t>( g ) * g, 0 );
    uint64_t total = 0;
    for ( int y = 0; y < winHeight; ++y )
    {
      for ( int x = 0; x < winWidth; ++x )
      {
        const int nx = x + off.dx;
        const int ny = y + off.dy;
        if ( nx < 0 || nx >= winWidth || ny < 0 || ny >= winHeight )
          continue;
        const int i = quantize( windowPixels[static_cast<size_t>( y ) * winWidth + x], config );
        const int j = quantize( windowPixels[static_cast<size_t>( ny ) * winWidth + nx], config );
        ++counts[static_cast<size_t>( i ) * g + j];
        ++total;
      }
    }
    if ( total == 0 )
      return std::nullopt;

    std::vector<double> p( static_cast<size_t>( g ) * g, 0.0 );
    const double denom = config.isSymmetric ? 2.0 * static_cast<double>( total )
                                            : static_cast<double>( total );
    for ( int i = 0; i < g; ++i )
      for ( int j = 0; j < g; ++j )
      {
        const double c = static_cast<double>( counts[static_cast<size_t>( i ) * g + j] );
        const double symmetric = config.isSymmetric
                                   ? c + static_cast<double>( counts[static_cast<size_t>( j ) * g + i] )
                                   : c;
        p[static_cast<size_t>( i ) * g + j] = symmetric / denom;
      }
    return p;
  }

  GlcmHaralickMetrics metricsFromP( const std::vector<double> &p, int g )
  {
    double contrast = 0.0, dissimilarity = 0.0, homogeneity = 0.0, angularMoment = 0.0, entropy = 0.0;
    for ( int i = 0; i < g; ++i )
    {
      for ( int j = 0; j < g; ++j )
      {
        const double pij = p[static_cast<size_t>( i ) * g + j];
        if ( pij <= 0.0 )
          continue;
        const double diff = static_cast<double>( i - j );
        contrast += diff * diff * pij;
        dissimilarity += std::abs( diff ) * pij;
        homogeneity += pij / ( 1.0 + diff * diff );
        angularMoment += pij * pij;
        entropy += -pij * std::log( pij + kEntropyEpsilon );
      }
    }

    std::vector<double> marginalI( g, 0.0 ), marginalJ( g, 0.0 );
    for ( int i = 0; i < g; ++i )
      for ( int j = 0; j < g; ++j )
      {
        const double pij = p[static_cast<size_t>( i ) * g + j];
        marginalI[i] += pij;
        marginalJ[j] += pij;
      }
    double meanI = 0.0, meanJ = 0.0;
    for ( int i = 0; i < g; ++i )
    {
      meanI += i * marginalI[i];
      meanJ += i * marginalJ[i];
    }
    double varI = 0.0, varJ = 0.0;
    for ( int i = 0; i < g; ++i )
    {
      varI += ( i - meanI ) * ( i - meanI ) * marginalI[i];
      varJ += ( i - meanJ ) * ( i - meanJ ) * marginalJ[i];
    }
    double correlation = 0.0;
    const double sigma = std::sqrt( varI ) * std::sqrt( varJ );
    if ( sigma > 0.0 )
    {
      for ( int i = 0; i < g; ++i )
        for ( int j = 0; j < g; ++j )
          correlation += ( i - meanI ) * ( j - meanJ ) * p[static_cast<size_t>( i ) * g + j];
      correlation /= sigma;
    }

    GlcmHaralickMetrics m;
    m.contrast = contrast;
    m.dissimilarity = dissimilarity;
    m.homogeneity = homogeneity;
    m.energy = std::sqrt( angularMoment );
    m.entropy = entropy;
    m.angularSecondMoment = angularMoment;
    m.mean = meanI;
    m.variance = varI;
    m.correlation = correlation;
    return m;
  }

  bool windowHasNonFinite( std::span<const float> windowPixels, int winWidth, int winHeight )
  {
    const size_t required = static_cast<size_t>( winWidth ) * winHeight;
    if ( windowPixels.size() < required )
      return true;
    for ( size_t i = 0; i < required; ++i )
      if ( !std::isfinite( windowPixels[i] ) )
        return true;
    return false;
  }

  double metricField( const GlcmHaralickMetrics &m, const std::string &featureName )
  {
    if ( featureName == "contrast" ) return m.contrast;
    if ( featureName == "dissimilarity" ) return m.dissimilarity;
    if ( featureName == "homogeneity" ) return m.homogeneity;
    if ( featureName == "energy" ) return m.energy;
    if ( featureName == "entropy" ) return m.entropy;
    if ( featureName == "asm" ) return m.angularSecondMoment;
    if ( featureName == "mean" ) return m.mean;
    if ( featureName == "variance" ) return m.variance;
    if ( featureName == "correlation" ) return m.correlation;
    return std::nan( "" );
  }
} // namespace

GlcmHaralickMetrics GlcmTextureCalculator::computeForWindow( std::span<const float> windowPixels,
                                                             int winWidth, int winHeight,
                                                             const GlcmConfig &config )
{
  if ( windowHasNonFinite( windowPixels, winWidth, winHeight ) )
    return nanMetrics();

  if ( config.direction == GlcmDirection::Omnidirectional )
  {
    // Mean of the four directional normalized matrices (equal pair
    // weighting, DECISIONS B2); metrics are computed ONCE on the averaged
    // matrix — averaging per-direction metrics would disagree for the
    // nonlinear statistics (ASM, entropy, correlation).
    std::vector<double> average;
    int directionsUsed = 0;
    for ( const GlcmDirection dir : { GlcmDirection::Deg0, GlcmDirection::Deg45,
                                      GlcmDirection::Deg90, GlcmDirection::Deg135 } )
    {
      GlcmConfig dirCfg = config;
      dirCfg.direction = dir;
      const auto p = directionalGlcm( windowPixels, winWidth, winHeight, dirCfg );
      if ( !p )
        continue;
      if ( average.empty() )
        average.assign( p->size(), 0.0 );
      for ( size_t k = 0; k < p->size(); ++k )
        average[k] += ( *p )[k];
      ++directionsUsed;
    }
    if ( directionsUsed == 0 )
      return nanMetrics();
    for ( double &v : average )
      v /= directionsUsed;
    return metricsFromP( average, config.quantLevels );
  }

  const auto p = directionalGlcm( windowPixels, winWidth, winHeight, config );
  if ( !p )
    return nanMetrics();
  return metricsFromP( *p, config.quantLevels );
}

std::vector<float> GlcmTextureCalculator::computeTextureFeatureMap( const float *rasterData,
                                                                    int width, int height,
                                                                    const GlcmConfig &config,
                                                                    const std::string &featureName )
{
  const size_t n = static_cast<size_t>( std::max( 0, width ) ) * std::max( 0, height );
  std::vector<float> map( n, std::nan( "" ) );
  if ( !rasterData || width <= 0 || height <= 0 )
    return map;

  int half = std::max( 0, config.windowSize / 2 ); // odd: 3->1, 5->2
  if ( config.windowSize > 0 && config.windowSize % 2 == 0 )
    half = std::max( 0, ( config.windowSize - 1 ) / 2 ); // even snaps down to odd

  std::vector<float> window;
  for ( int y = 0; y < height; ++y )
  {
    for ( int x = 0; x < width; ++x )
    {
      // True clamp replication: the window is always the full odd square;
      // out-of-range neighbours read their nearest edge pixel.
      const int n = half * 2 + 1;
      window.assign( static_cast<size_t>( n ) * n, 0.0f );
      for ( int wy = 0; wy < n; ++wy )
      {
        const int sy = std::clamp( y - half + wy, 0, height - 1 );
        for ( int wx = 0; wx < n; ++wx )
        {
          const int sx = std::clamp( x - half + wx, 0, width - 1 );
          window[static_cast<size_t>( wy ) * n + wx] = rasterData[static_cast<size_t>( sy ) * width + sx];
        }
      }
      const GlcmHaralickMetrics m = computeForWindow( window, n, n, config );
      map[static_cast<size_t>( y ) * width + x] = static_cast<float>( metricField( m, featureName ) );
    }
  }
  return map;
}

std::vector<double> GlcmTextureCalculator::normalizedCooccurrence( std::span<const float> windowPixels,
                                                                   int winWidth, int winHeight,
                                                                   const GlcmConfig &config,
                                                                   int &outLevels )
{
  outLevels = std::max( 1, config.quantLevels );
  if ( config.direction == GlcmDirection::Omnidirectional )
  {
    std::vector<double> sum;
    int directionsUsed = 0;
    for ( const GlcmDirection dir : { GlcmDirection::Deg0, GlcmDirection::Deg45,
                                      GlcmDirection::Deg90, GlcmDirection::Deg135 } )
    {
      GlcmConfig dirCfg = config;
      dirCfg.direction = dir;
      const auto p = directionalGlcm( windowPixels, winWidth, winHeight, dirCfg );
      if ( !p )
        continue;
      if ( sum.empty() )
        sum.assign( p->size(), 0.0 );
      for ( size_t k = 0; k < p->size(); ++k )
        sum[k] += ( *p )[k];
      ++directionsUsed;
    }
    if ( directionsUsed == 0 )
      return {};
    for ( double &v : sum )
      v /= directionsUsed;
    return sum;
  }
  const auto p = directionalGlcm( windowPixels, winWidth, winHeight, config );
  return p ? *p : std::vector<double> {};
}

} // namespace rs::processing
