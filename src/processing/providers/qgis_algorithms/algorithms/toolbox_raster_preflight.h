// Shared toolbox preflight for multi-raster QGIS algorithms.
// Fail closed on grid mismatch (no hidden resample) and resolve the
// numeric domain once per raster for scale-sensitive indices.
#pragma once

#include "data/raster_grid_compat.h"
#include "processing/contracts/scientific_contracts.h"

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

inline sicnu::processing::contracts::NumericDomainContract numericDomainFromLayer(
  const QgsRasterLayer *layer, const float *const *bands, int bandCount, size_t pixelCount )
{
  using sicnu::processing::contracts::defaultUnitDomain;
  using sicnu::processing::contracts::domainFromDeclaredScale;
  using sicnu::processing::contracts::domainFromMaxAbsSample;

  if ( layer )
  {
    GDALAllRegister();
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

} // namespace sicnu::processing::toolbox
