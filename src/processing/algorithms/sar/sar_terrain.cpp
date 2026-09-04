// src/processing/algorithms/sar/sar_terrain.cpp
#include "sar_terrain.h"

#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "processing/gdal/gdal_window_read.h"

#include <gdal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr double kDegToRad = M_PI / 180.0;

using sicnu::processing::readClampedWindow;
} // namespace


SlopeAspect slopeAspectAt( const float *dem, int bufferWidth, int x, int y,
                           double cellSizeMeters, double demUnitScale )
{
  SlopeAspect out;
  const int i = y * bufferWidth + x;
  const double a = dem[i - bufferWidth - 1] * demUnitScale;
  const double b = dem[i - bufferWidth] * demUnitScale;
  const double c = dem[i - bufferWidth + 1] * demUnitScale;
  const double d = dem[i - 1] * demUnitScale;
  const double e = dem[i] * demUnitScale;
  const double f = dem[i + 1] * demUnitScale;
  const double g = dem[i + bufferWidth - 1] * demUnitScale;
  const double h = dem[i + bufferWidth] * demUnitScale;
  const double k = dem[i + bufferWidth + 1] * demUnitScale;

  // A NoData/NaN hole in the 3×3 window makes every Horn derivative garbage
  // (a -9999 sentinel reads as a cliff): the facet is invalid, not steep.
  for ( const double v : { a, b, c, d, e, f, g, h, k } )
  {
    if ( !std::isfinite( v ) )
    {
      out.valid = false;
      return out;
    }
  }

  // Horn's method: dz/dx, dz/dy over the 3×3 window.
  const double dzdx = ( ( c + 2.0 * f + k ) - ( a + 2.0 * d + g ) ) / ( 8.0 * cellSizeMeters );
  const double dzdy = ( ( g + 2.0 * h + k ) - ( a + 2.0 * b + c ) ) / ( 8.0 * cellSizeMeters );

  const double slope = std::atan( std::sqrt( dzdx * dzdx + dzdy * dzdy ) );
  out.slopeDeg = slope / kDegToRad;

  // Aspect: downslope azimuth in degrees from north, clockwise, on a
  // north-up raster (+y north, +x east); downhill direction is (-dzdx, +dzdy).
  if ( slope < 1e-12 )
  {
    out.aspectDeg = -1.0; // flat: aspect undefined
  }
  else
  {
    double aspect = std::atan2( -dzdx, dzdy ) / kDegToRad;
    if ( aspect < 0.0 )
      aspect += 360.0;
    out.aspectDeg = aspect;
  }
  return out;
}

double localIncidenceAngle( double slopeDeg, double aspectDeg, double incidenceDeg,
                            double headingDeg )
{
  // Flat ground: θi = θ0 exactly (aspect undefined).
  if ( slopeDeg < 1e-9 || aspectDeg < 0.0 )
    return incidenceDeg;
  const double alpha = slopeDeg * kDegToRad;
  const double theta0 = incidenceDeg * kDegToRad;
  const double deltaBetaPhi = ( aspectDeg - headingDeg ) * kDegToRad;
  const double cosThetaI =
    std::cos( alpha ) * std::cos( theta0 ) +
    std::sin( alpha ) * std::sin( theta0 ) * std::cos( deltaBetaPhi );
  return std::acos( std::clamp( cosThetaI, -1.0, 1.0 ) ) / kDegToRad;
}

bool isLayoverOrShadow( double incidenceLocalDeg, double cosThetaMax )
{
  // θi ≥ θmax (beyond ~85°) → shadow; facets facing away fold into the same
  // cos θi ≤ cos θmax bucket because cos(θi>90°) < 0 < cos θmax.
  const double cosThetaI = std::cos( incidenceLocalDeg * kDegToRad );
  return cosThetaI <= cosThetaMax;
}

bool terrainFlattenRaster( const GdalDatasetWrapper &sigma0Ds, int band,
                           const GdalDatasetWrapper &demDs,
                           const TerrainCorrectionOptions &options, float nodata,
                           GdalStreamingOutput &dst, int tileDim,
                           const QString &polarizations, const QString &sensor )
{
  if ( !sigma0Ds.isValid() || !demDs.isValid() )
    return false;
  if ( sigma0Ds.width() != demDs.width() || sigma0Ds.height() != demDs.height() )
    return false;

  // Same-size is not same-grid: a shifted/scaled DEM silently misregisters
  // every pixel. Compare geotransforms (small tolerance for serialization
  // round-trips) and projections before streaming.
  {
    const std::array<double, 6> dataGt = sigma0Ds.geoTransform();
    const std::array<double, 6> demGtLocal = demDs.geoTransform();
    for ( int i = 0; i < 6; ++i )
    {
      if ( std::fabs( dataGt[i] - demGtLocal[i] ) > 1e-9 *
             std::max( 1.0, std::fabs( dataGt[i] ) ) )
        return false;
    }
    const QString dataProj = sigma0Ds.projection();
    const QString demProj = demDs.projection();
    if ( !dataProj.isEmpty() && !demProj.isEmpty() && dataProj != demProj )
      return false;
  }

  // DEM cell size in meters from the geotransform (north-up rasters; rotated
  // grids are rejected upstream by the grid-compatibility layer). Degree
  // cells (geographic DEMs) are rejected: the Horn denominators would be
  // ~10^4x too small and every facet would read as a vertical cliff.
  const std::array<double, 6> demGt = demDs.geoTransform();
  const double cellX = std::fabs( demGt[1] );
  const double cellY = std::fabs( demGt[5] );
  if ( cellX <= 0.0 || cellY <= 0.0 )
    return false;
  if ( demDs.projection().contains( QLatin1String( "deg" ), Qt::CaseInsensitive ) )
    return false; // geographic DEM units are angles, not meters
  const double cellMeters = 0.5 * ( cellX + cellY );

  // Map the DEM's declared sentinel to NaN so Horn statistics cannot see it.
  bool demHasNodata = false;
  const double demNodata = demDs.bandNoDataValue( 1, &demHasNodata );
  const float demSentinel =
    demHasNodata && std::isfinite( demNodata ) ? static_cast<float>( demNodata )
                                               : std::numeric_limits<float>::quiet_NaN();

  const int halo = 1; // Horn needs the 8-neighborhood
  const int gammaBand = 1;
  const int maskBand = options.applyShadowMask ? 2 : 0;
  const int incidenceBand = options.writeIncidenceBand ? ( maskBand > 0 ? 3 : 2 ) : 0;

  GdalBlockStream dataStream( sigma0Ds, band, tileDim, tileDim, halo );

  const bool ok = dataStream.forEach(
    [&]( const GdalBlockStream::Tile &tile, const float *pixels ) {
      std::vector<float> demTile;
      if ( !readClampedWindow( demDs, 1, tile.xOffset, tile.yOffset, tile.width, tile.height,
                               halo, demTile ) )
        return false;
      for ( float &v : demTile )
      {
        if ( !std::isfinite( v ) || v == demSentinel )
          v = kNan;
      }

      std::vector<float> gamma( static_cast<size_t>( tile.width ) * tile.height );
      std::vector<float> incidence( static_cast<size_t>( tile.width ) * tile.height );
      std::vector<uint8_t> validity( static_cast<size_t>( tile.width ) * tile.height, 1 );

      const double cosTheta0 = std::cos( options.incidenceDeg * kDegToRad );
      for ( int y = 0; y < tile.height; ++y )
      {
        for ( int x = 0; x < tile.width; ++x )
        {
          const size_t idx = static_cast<size_t>( y ) * tile.width + x;
          const float v = pixels[( y + halo ) * tile.bufferWidth + ( x + halo )];
          if ( !std::isfinite( v ) || v == nodata )
          {
            gamma[idx] = kNan;
            incidence[idx] = kNan;
            validity[idx] = 255;
            continue;
          }
          const SlopeAspect sa = slopeAspectAt( demTile.data(), tile.bufferWidth, x + halo,
                                                y + halo, cellMeters, options.demUnitScale );
          if ( !sa.valid )
          {
            // DEM hole: no meaningful incidence or flattening.
            gamma[idx] = kNan;
            incidence[idx] = kNan;
            validity[idx] = 0;
            continue;
          }
          incidence[idx] = static_cast<float>(
            localIncidenceAngle( sa.slopeDeg, sa.aspectDeg, options.incidenceDeg,
                                 options.headingDeg ) );
          if ( sa.aspectDeg < 0.0 )
          {
            // Flat facet: θi == θ0, so the flattening ratio is 1.
            gamma[idx] = v;
            validity[idx] = 1;
            continue;
          }
          const double thetaI =
            localIncidenceAngle( sa.slopeDeg, sa.aspectDeg, options.incidenceDeg,
                                 options.headingDeg );
          if ( options.applyShadowMask && isLayoverOrShadow( thetaI, options.flattenCosThetaMax ) )
          {
            gamma[idx] = kNan;
            validity[idx] = 0;
            continue;
          }
          if ( options.applyFlattening )
          {
            const double cosThetaI = std::cos( thetaI * kDegToRad );
            if ( cosThetaI <= 0.0 )
            {
              gamma[idx] = kNan;
              validity[idx] = 0;
              continue;
            }
            gamma[idx] = static_cast<float>( v * cosTheta0 / cosThetaI );
          }
          else
          {
            gamma[idx] = v;
          }
        }
      }

      if ( !dst.writeTile( gammaBand, tile, gamma.data() ) )
        return false;
      if ( maskBand > 0 && !dst.writeTileRaw( maskBand, tile, validity.data(), GDT_Byte ) )
        return false;
      if ( incidenceBand > 0 && !dst.writeTile( incidenceBand, tile, incidence.data() ) )
        return false;
      return true;
    } );

  if ( ok )
  {
    writeSarOutputMetadata( dst, QStringLiteral( "gamma0" ),
                            QStringLiteral( "linear_power" ), polarizations, sensor,
                            options.incidenceDeg, options.headingDeg );
    dst.setMetadataItem( QStringLiteral( "SICNU_SAR_TERRAIN_CORRECTED" ),
                         options.applyFlattening ? QStringLiteral( "gamma0_rtc" )
                                                 : QStringLiteral( "mask_only" ) );
  }
  return ok;
}

} // namespace sicnu::sar
