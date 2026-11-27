// Shared toolbox preflight for multi-raster QGIS algorithms.
// Fail closed on grid mismatch (no hidden resample) and resolve the
// numeric domain once per raster for scale-sensitive indices.
#pragma once

#include "data/raster_grid_compat.h"
#include "processing/contracts/scientific_contracts.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <processing/qgsprocessingalgorithm.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrasterlayer.h>
#include <qgsrectangle.h>
#include <qgsexception.h>

#include <QObject>
#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sicnu::processing::toolbox
{

inline sicnu::data::RasterGrid gridFromRasterLayer( const QgsRasterLayer *layer )
{
  sicnu::data::RasterGrid grid;
  if ( !layer )
    return grid;
  grid.width = layer->width();
  grid.height = layer->height();
  grid.crsWkt = layer->crs().toWkt();
  const QgsRectangle extent = layer->extent();
  const double px = layer->rasterUnitsPerPixelX();
  const double py = layer->rasterUnitsPerPixelY();
  grid.hasGeoTransform = std::isfinite( px ) && std::isfinite( py ) && px > 0.0 && py > 0.0;
  if ( grid.hasGeoTransform )
  {
    grid.geoTransform = { extent.xMinimum(), px, 0.0, extent.yMaximum(), 0.0, -py };
  }
  return grid;
}

/// Require identical width/height and a compatible geotransform (pixel size +
/// origin) in addition to CRS. Throws QgsProcessingException naming the
/// mismatch — never silently resample onto the reference grid.
inline void requireCompatibleRasterGrid( const QgsRasterLayer *reference,
                                         const QgsRasterLayer *other,
                                         const QString &otherLabel )
{
  if ( !reference || !other )
    return;
  if ( reference->width() != other->width() || reference->height() != other->height() )
  {
    throw QgsProcessingException(
      QObject::tr( "Grid mismatch (%1): raster dimensions differ (%2x%3 vs %4x%5); "
                   "resample to a common grid before combining pixels." )
        .arg( otherLabel )
        .arg( reference->width() )
        .arg( reference->height() )
        .arg( other->width() )
        .arg( other->height() ) );
  }
  const sicnu::data::GridCompatReport report =
    sicnu::data::compareGrids( gridFromRasterLayer( reference ), gridFromRasterLayer( other ) );
  if ( const auto blocking = report.primaryBlocking() )
  {
    throw QgsProcessingException(
      QObject::tr( "Grid mismatch (%1): %2" ).arg( otherLabel, blocking->message ) );
  }
}

namespace detail
{
/// Stored-space SICNU_NUMERIC_OFFSET (#1740) of an open dataset: the
/// band-level stamp wins (a mixed stack stamps only the bands carrying the
/// affine contract), the dataset-level stamp is the fallback — the same
/// precedence rs_spectral_index_operator's readDeclaredOffset uses. Offset-0
/// products leave the key unstamped, so 0.0 means "no affine term".
inline double declaredNumericOffset( GDALDatasetH ds )
{
  double offset = 0.0;
  bool found = false;
  if ( GDALRasterBandH band = GDALGetRasterBand( ds, 1 ) )
  {
    if ( const char *raw = GDALGetMetadataItem( band, "SICNU_NUMERIC_OFFSET", nullptr ) )
    {
      bool ok = false;
      const double v = QString::fromUtf8( raw ).toDouble( &ok );
      if ( ok && std::isfinite( v ) )
      {
        offset = v;
        found = true;
      }
    }
  }
  if ( !found )
  {
    if ( const char *raw = GDALGetMetadataItem( ds, "SICNU_NUMERIC_OFFSET", nullptr ) )
    {
      bool ok = false;
      const double v = QString::fromUtf8( raw ).toDouble( &ok );
      if ( ok && std::isfinite( v ) )
        offset = v;
    }
  }
  return offset;
}
} // namespace detail

/// Resolve the numeric domain of a band set once per raster (#801). When
/// @p declaredOffset is non-null it additionally reports the stored-space
/// SICNU_NUMERIC_OFFSET that belongs with the declared scale (#1740):
/// physical = ( stored - offset ) / scale.
inline sicnu::processing::contracts::NumericDomainContract numericDomainFromLayer(
  const QgsRasterLayer *layer, const float *const *bands, int bandCount, size_t pixelCount,
  double *declaredOffset = nullptr )
{
  using sicnu::processing::contracts::defaultUnitDomain;
  using sicnu::processing::contracts::domainFromDeclaredScale;
  using sicnu::processing::contracts::domainFromMaxAbsSample;

  if ( declaredOffset )
    *declaredOffset = 0.0;

  if ( layer )
  {
    // ensureGdalInit(), not a bare GDALAllRegister(): this probe can run on a
    // JobEngine thread, and the helper is the one place that also caps OpenCV
    // nested parallelism (#692, #1646).
    ensureGdalInit();
    GDALDatasetH ds = GDALOpen( layer->source().toUtf8().constData(), GA_ReadOnly );
    if ( ds )
    {
      double declared = 0.0;
      bool hasDeclared = false;
      if ( const char *raw = GDALGetMetadataItem( ds, "SICNU_NUMERIC_SCALE", nullptr ) )
      {
        bool ok = false;
        const double v = QString::fromUtf8( raw ).toDouble( &ok );
        if ( ok && std::isfinite( v ) && v > 0.0 )
        {
          declared = v;
          hasDeclared = true;
        }
      }
      // The affine term travels with the scale: capture it here (band-level
      // stamp first, dataset-level second) so scale-sensitive indices can
      // subtract it before dividing.
      if ( declaredOffset )
        *declaredOffset = detail::declaredNumericOffset( ds );
      GDALClose( ds );
      if ( hasDeclared )
        return domainFromDeclaredScale( declared );
    }
  }

  double observedMaxAbs = 0.0;
  for ( int b = 0; b < bandCount; ++b )
  {
    const float *samples = bands[b];
    if ( !samples )
      continue;
    for ( size_t i = 0; i < pixelCount; ++i )
    {
      const float v = samples[i];
      if ( std::isfinite( v ) && v > 0.0f )
        observedMaxAbs = std::max( observedMaxAbs, static_cast<double>( v ) );
    }
  }
  if ( observedMaxAbs > 0.0 )
    return domainFromMaxAbsSample( observedMaxAbs );
  return defaultUnitDomain();
}

inline void applyNumericDomain( std::vector<float> &buf,
                                const sicnu::processing::contracts::NumericDomainContract &domain )
{
  if ( !domain.isDnScale() )
    return;
  const float inv = static_cast<float>( 1.0 / domain.divisor );
  for ( float &v : buf )
  {
    if ( std::isfinite( v ) )
      v *= inv;
  }
}

/// Stored-space SICNU_NUMERIC_OFFSET of one toolbox band input (#1740):
/// band-level stamp first, dataset-level second; 0.0 when the product
/// carries no affine term. Used by the ratio indices, which have no shared
/// domain to resolve — each participating band is its own layer.
inline double numericOffsetFromLayer( const QgsRasterLayer *layer )
{
  if ( !layer )
    return 0.0;
  // Same ensureGdalInit() probe contract as numericDomainFromLayer above.
  ensureGdalInit();
  GDALDatasetH ds = GDALOpen( layer->source().toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
    return 0.0;
  const double offset = detail::declaredNumericOffset( ds );
  GDALClose( ds );
  return offset;
}

/// Subtract the stored-space offset before any scale division (#1740):
/// physical = ( stored - offset ) / scale. A ratio index cancels the scale
/// in (a-b)/(a+b) but NOT the offset, so every toolbox index path applies
/// this to each participating band first. Sentinel-2 L2A PB >= 04.00 stamps
/// offset -1000 (DN = rho*10000 - 1000), Landsat 8/9 C2 L2 stamps
/// -ADD/MULT; without the subtraction NDVI on such products is silently
/// wrong (rho_nir 0.4 / rho_red 0.1 stores as DN 3000/0 on S2 — the
/// offset-blind ratio is 1.0, the true NDVI 0.6).
inline void applyNumericOffset( std::vector<float> &buf, double offset )
{
  if ( offset == 0.0 )
    return;
  const float off = static_cast<float>( offset );
  for ( float &v : buf )
  {
    if ( std::isfinite( v ) )
      v -= off;
  }
}

} // namespace sicnu::processing::toolbox
