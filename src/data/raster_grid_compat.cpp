// src/data/raster_grid_compat.cpp — shared raster-grid compatibility service
#include "raster_grid_compat.h"

#include <ogr_spatialref.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <cmath>

namespace sicnu::data
{

namespace
{

bool isSameCrs( const QString &wktA, const QString &wktB )
{
  if ( wktA.trimmed() == wktB.trimmed() )
    return true;
  OGRSpatialReference srsA, srsB;
  if ( srsA.importFromWkt( wktA.toUtf8().constData() ) == OGRERR_NONE &&
       srsB.importFromWkt( wktB.toUtf8().constData() ) == OGRERR_NONE )
  {
    return srsA.IsSame( &srsB ) != 0;
  }
  return false;
}

/// Relative tolerance for grid (pixel size / extent) comparison, mirroring
/// virtual_raster_preflight's kGridTolerance.
constexpr double kGridTolerance = 1e-9;

/// Relative-tolerance equality for a scalar grid quantity (pixel size).
bool sameScalar( double a, double b )
{
  const double scale = std::max( { std::abs( a ), std::abs( b ), 1.0 } );
  return std::abs( a - b ) <= kGridTolerance * scale;
}

/// True when @a value is an integer within @a tolerance (in @a value's units).
bool nearInteger( double value, double tolerance )
{
  return std::abs( value - std::round( value ) ) <= tolerance;
}

/// Extent equality with a relative tolerance scaled like sameScalar.
bool sameExtent( const SpatialExtent &a, const SpatialExtent &b )
{
  const double scale = std::max( { std::abs( a.minimumX ), std::abs( a.maximumX ),
                                   std::abs( b.minimumX ), std::abs( b.maximumX ),
                                   std::abs( a.minimumY ), std::abs( a.maximumY ),
                                   std::abs( b.minimumY ), std::abs( b.maximumY ),
                                   1.0 } );
  const double tolerance = kGridTolerance * scale;
  return std::abs( a.minimumX - b.minimumX ) <= tolerance &&
         std::abs( a.maximumX - b.maximumX ) <= tolerance &&
         std::abs( a.minimumY - b.minimumY ) <= tolerance &&
         std::abs( a.maximumY - b.maximumY ) <= tolerance;
}

bool extentsOverlap( const SpatialExtent &a, const SpatialExtent &b )
{
  return a.maximumX > b.minimumX && b.maximumX > a.minimumX &&
         a.maximumY > b.minimumY && b.maximumY > a.minimumY;
}

QString formatExtent( const SpatialExtent &e )
{
  return QStringLiteral( "[%1, %2] x [%3, %4]" )
    .arg( e.minimumX, 0, 'g', 8 )
    .arg( e.maximumX, 0, 'g', 8 )
    .arg( e.minimumY, 0, 'g', 8 )
    .arg( e.maximumY, 0, 'g', 8 );
}

} // namespace

double RasterGrid::pixelSizeX() const
{
  return std::abs( geoTransform[1] );
}

double RasterGrid::pixelSizeY() const
{
  return std::abs( geoTransform[5] );
}

std::optional<SpatialExtent> RasterGrid::extent() const
{
  if ( !hasGeoTransform || width <= 0 || height <= 0 )
    return std::nullopt;

  // Corners of the affine transform; rotation-aware.
  const double x0 = geoTransform[0];
  const double y0 = geoTransform[3];
  const double x1 = x0 + width * geoTransform[1] + height * geoTransform[2];
  const double y1 = y0 + width * geoTransform[4] + height * geoTransform[5];

  SpatialExtent e;
  e.minimumX = std::min( x0, x1 );
  e.maximumX = std::max( x0, x1 );
  e.minimumY = std::min( y0, y1 );
  e.maximumY = std::max( y0, y1 );
  e.valid = e.maximumX > e.minimumX && e.maximumY > e.minimumY;
  return e;
}

bool GridCompatReport::hasBlockingIssue() const
{
  for ( const GridCompatIssue &issue : issues )
  {
    if ( issue.blocking )
      return true;
  }
  return false;
}

std::optional<GridCompatIssue> GridCompatReport::primaryBlocking() const
{
  for ( const GridCompatIssue &issue : issues )
  {
    if ( issue.blocking )
      return issue;
  }
  return std::nullopt;
}

GridCompatReport compareGrids( const RasterGrid &a, const RasterGrid &b )
{
  GridCompatReport report;

  // --- CRS: exactly one side unreferenced is not comparable; differing CRSs
  // need a reprojection before any pixel operation.
  const bool aHasCrs = !a.crsWkt.trimmed().isEmpty();
  const bool bHasCrs = !b.crsWkt.trimmed().isEmpty();
  if ( aHasCrs != bHasCrs )
  {
    report.issues.append( {
      GridCompatVerdict::MissingCrs,
      QStringLiteral( "grid.missing_crs" ),
      aHasCrs
        ? QStringLiteral( "The second raster has no CRS; assign a CRS before "
                          "comparing its grid with the first raster." )
        : QStringLiteral( "The first raster has no CRS; assign a CRS before "
                          "comparing its grid with the second raster." ),
      true } );
    return report;
  }
  if ( aHasCrs && bHasCrs )
  {
    bool crsMismatch = false;
    OGRSpatialReferenceH srsA = OSRNewSpatialReference( a.crsWkt.toUtf8().constData() );
    OGRSpatialReferenceH srsB = OSRNewSpatialReference( b.crsWkt.toUtf8().constData() );
    if ( srsA && srsB )
    {
      crsMismatch = ( OSRIsSame( srsA, srsB ) == 0 );
    }
    else
    {
      crsMismatch = ( a.crsWkt.trimmed() != b.crsWkt.trimmed() );
    }
    if ( srsA ) OSRDestroySpatialReference( srsA );
    if ( srsB ) OSRDestroySpatialReference( srsB );

    if ( crsMismatch )
    {
      report.issues.append( {
        GridCompatVerdict::CrsMismatch,
        QStringLiteral( "grid.crs_mismatch" ),
        QStringLiteral( "The rasters use different coordinate reference systems; "
                        "reproject the second raster to the first's CRS before "
                        "comparing pixels." ),
        true } );
      return report;
    }
  }

  // Both unreferenced: not spatially comparable. The caller falls back to
  // dimension checks (e.g. plain synthetic rasters without georeferencing).
  if ( !a.hasGeoTransform || !b.hasGeoTransform )
    return report;

  // --- Pixel grid: orientation, rotation, or differing pixel sizes.
  const bool axisOrientationMismatch = ( a.geoTransform[1] * b.geoTransform[1] <= 0.0 ) ||
                                       ( a.geoTransform[5] * b.geoTransform[5] <= 0.0 );
  if ( axisOrientationMismatch )
  {
    report.issues.append( {
      GridCompatVerdict::AxisOrientationMismatch,
      QStringLiteral( "grid.axis_orientation_mismatch" ),
      QStringLiteral( "The rasters have opposite axis orientation (north-up vs south-up or mirrored); "
                      "rectify the second raster before comparing pixels." ),
      true } );
    return report;
  }

  const bool rotated = a.geoTransform[2] != 0.0 || a.geoTransform[4] != 0.0 ||
                       b.geoTransform[2] != 0.0 || b.geoTransform[4] != 0.0;
  if ( rotated )
  {
    report.issues.append( {
      GridCompatVerdict::RotationMismatch,
      QStringLiteral( "grid.rotation_mismatch" ),
      QStringLiteral( "The rasters carry rotation terms; rectify/orthorectify "
                      "before comparing pixels." ),
      true } );
    return report;
  }

  if ( !sameScalar( a.pixelSizeX(), b.pixelSizeX() ) ||
       !sameScalar( a.pixelSizeY(), b.pixelSizeY() ) )
  {
    report.issues.append( {
      GridCompatVerdict::PixelSizeMismatch,
      QStringLiteral( "grid.pixel_size_mismatch" ),
      QStringLiteral( "The rasters use different pixel grids (%1 x %2 vs %3 x "
                      "%4); resample the second raster to the first's grid "
                      "before comparing pixels." )
          .arg( a.pixelSizeX(), 0, 'g', 6 )
          .arg( a.pixelSizeY(), 0, 'g', 6 )
          .arg( b.pixelSizeX(), 0, 'g', 6 )
          .arg( b.pixelSizeY(), 0, 'g', 6 ),
      true } );
    return report;
  }

  // --- Origin alignment: on the shared lattice the origins must land on the
  // same pixel corners; a sub-pixel offset shifts every pixel's footprint.
  const double px = a.pixelSizeX();
  const double py = a.pixelSizeY();
  const double dxPx = ( b.geoTransform[0] - a.geoTransform[0] ) / px;
  const double dyPx = ( b.geoTransform[3] - a.geoTransform[3] ) / py;
  if ( !nearInteger( dxPx, 1e-6 ) || !nearInteger( dyPx, 1e-6 ) )
  {
    report.issues.append( {
      GridCompatVerdict::OriginMisalignment,
      QStringLiteral( "grid.origin_misalignment" ),
      QStringLiteral( "The rasters' pixel origins are offset by a sub-pixel "
                      "amount (%1, %2 pixels); align both rasters to a common "
                      "grid before comparing pixels." )
        .arg( dxPx, 0, 'g', 4 )
        .arg( dyPx, 0, 'g', 4 ),
      true } );
    return report;
  }

  // --- Extent: whole-pixel shifts and size differences land here. Pixel
  // correspondence requires identical footprints.
  const std::optional<SpatialExtent> ea = a.extent();
  const std::optional<SpatialExtent> eb = b.extent();
  if ( ea && eb && !sameExtent( *ea, *eb ) )
  {
    const bool overlap = extentsOverlap( *ea, *eb );
    report.issues.append( {
      GridCompatVerdict::ExtentMismatch,
      QStringLiteral( "grid.extent_mismatch" ),
      overlap
        ? QStringLiteral( "The raster extents differ (%1 vs %2); clip both "
                          "rasters to a common extent before comparing pixels." )
            .arg( formatExtent( *ea ), formatExtent( *eb ) )
        : QStringLiteral( "The raster extents do not overlap (%1 vs %2); clip "
                          "both rasters to a common extent before comparing "
                          "pixels." )
            .arg( formatExtent( *ea ), formatExtent( *eb ) ),
      true } );
    return report;
  }

  // --- NoData: warning-grade; pixel-level operations still proceed but the
  // declared invalid values differ between the two rasters.
  const int sharedBands = std::min( a.bandNoData.size(), b.bandNoData.size() );
  for ( int i = 0; i < sharedBands; ++i )
  {
    if ( a.bandNoData[i] && b.bandNoData[i] && *a.bandNoData[i] != *b.bandNoData[i] )
    {
      report.issues.append( {
        GridCompatVerdict::NoDataMismatch,
        QStringLiteral( "grid.nodata_mismatch" ),
        QStringLiteral( "The rasters declare different NoData values for band "
                        "%1 (%2 vs %3); confirm this is intended before "
                        "combining pixels." )
          .arg( i + 1 )
          .arg( *a.bandNoData[i] )
          .arg( *b.bandNoData[i] ),
        false } );
      break;
    }
  }

  return report;
}

GridCompatReport compareStructures( const RasterStructure &a,
                                    const RasterStructure &b )
{
  RasterGrid gridA;
  gridA.crsWkt = a.crsWkt;
  gridA.hasGeoTransform = a.hasGeoTransform;
  gridA.geoTransform = a.geoTransform;
  gridA.width = a.width;
  gridA.height = a.height;
  for ( const RasterBandStructure &band : a.bands )
    gridA.bandNoData.append( band.noDataValue );

  RasterGrid gridB;
  gridB.crsWkt = b.crsWkt;
  gridB.hasGeoTransform = b.hasGeoTransform;
  gridB.geoTransform = b.geoTransform;
  gridB.width = b.width;
  gridB.height = b.height;
  for ( const RasterBandStructure &band : b.bands )
    gridB.bandNoData.append( band.noDataValue );

  return compareGrids( gridA, gridB );
}

} // namespace sicnu::data
