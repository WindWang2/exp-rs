#include "output_verifier.h"

#include "core/qgsdatasourceresolver.h"
#include "data/asset_types.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QFileInfo>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <gdal.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include <cmath>
#include <limits>

namespace sicnu::agent
{

namespace
{

void addIssue( OutputVerification &report, const QString &message )
{
  report.issues.append( message );
  report.ok = false;
}

void addWarning( OutputVerification &report, const QString &message )
{
  report.warnings.append( message );
}

bool isValidGeoTransform( const double gt[6] )
{
  if ( std::isnan( gt[0] ) || std::isnan( gt[1] ) || std::isnan( gt[2] )
       || std::isnan( gt[3] ) || std::isnan( gt[4] ) || std::isnan( gt[5] ) )
  {
    return false;
  }
  // A sensible raster has non-zero pixel size in at least one dimension.
  return std::abs( gt[1] ) > 0.0 || std::abs( gt[5] ) > 0.0;
}

bool allSampledNoData( GDALRasterBandH band, int width, int height )
{
  if ( !band )
    return false;

  int hasNoData = 0;
  const double noDataValue = GDALGetRasterNoDataValue( band, &hasNoData );
  if ( !hasNoData )
    return false; // no declared NoData means we cannot flag the dataset

  constexpr int kMaxSamples = 64;
  const int bufXSize = std::min( kMaxSamples, width );
  const int bufYSize = std::min( kMaxSamples, height );

  // Read a small, regularly down-sampled window.  GDAL handles the decimation.
  std::vector<double> buffer( static_cast<std::size_t>( bufXSize ) * bufYSize );
  if ( GDALRasterIO( band, GF_Read, 0, 0, width, height,
                     buffer.data(), bufXSize, bufYSize, GDT_Float64, 0, 0 ) != CE_None )
  {
    return false;
  }

  for ( double v : buffer )
  {
    if ( std::isnan( noDataValue ) )
    {
      if ( !std::isnan( v ) )
        return false;
    }
    else
    {
      if ( v != noDataValue )
        return false;
    }
  }
  return true;
}

QString wktFromOsr( OGRSpatialReferenceH srs )
{
  if ( !srs )
    return QString();
  char *wkt = nullptr;
  if ( OSRExportToWkt( srs, &wkt ) != OGRERR_NONE )
    return QString();
  QString result = QString::fromUtf8( wkt );
  CPLFree( wkt );
  return result;
}

} // namespace

OutputVerification OutputVerifier::verify( const QString &path, const QString &kindHint ) const
{
  const QString hint = kindHint.isEmpty() ? kindHintFromPath( path ) : kindHint.toLower();

  if ( hint == QStringLiteral( "vector" ) )
    return verifyVector( path );

  // Default to raster (the majority of RS operators) and fall back to vector
  // if the path does not open as a raster.
  OutputVerification raster = verifyRaster( path );
  if ( raster.ok || hint == QStringLiteral( "raster" ) )
    return raster;

  OutputVerification vector = verifyVector( path );
  if ( vector.ok )
    return vector;

  // Neither opened: prefer the raster report but mention both attempts.
  raster.issues.append( QStringLiteral( "Vector probe: %1" ).arg( vector.issues.value( 0, QStringLiteral( "unknown" ) ) ) );
  return raster;
}

OutputVerification OutputVerifier::verifyRaster( const QString &path )
{
  OutputVerification report;
  report.kind = QStringLiteral( "raster" );
  report.ok = false;

  if ( path.isEmpty() )
  {
    addIssue( report, QStringLiteral( "Path is empty" ) );
    return report;
  }

  if ( QgsDataSourceResolver::requiresLocalExistenceCheck( path ) && !QFile::exists( path ) )
  {
    addIssue( report, QStringLiteral( "File does not exist: %1" ).arg( path ) );
    return report;
  }

  ensureGdalInit();
  CPLErrorReset();

  GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(),
                                GDAL_OF_RASTER | GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR,
                                nullptr, nullptr, nullptr );
  if ( !ds )
  {
    const char *msg = CPLGetLastErrorMsg();
    addIssue( report, QStringLiteral( "Cannot open raster: %1" ).arg( msg && msg[0] ? QString::fromUtf8( msg ) : path ) );
    CPLErrorReset();
    return report;
  }

  const int width = GDALGetRasterXSize( ds );
  const int height = GDALGetRasterYSize( ds );
  const int bandCount = GDALGetRasterCount( ds );

  report.summary["width"] = width;
  report.summary["height"] = height;
  report.summary["bandCount"] = bandCount;

  if ( width <= 0 || height <= 0 )
    addIssue( report, QStringLiteral( "Invalid raster dimensions: %1x%2" ).arg( width ).arg( height ) );

  if ( bandCount <= 0 )
    addIssue( report, QStringLiteral( "Raster has no bands" ) );

  const char *proj = GDALGetProjectionRef( ds );
  const QString crs = proj ? QString::fromUtf8( proj ) : QString();
  report.summary["crs"] = crs.toStdString();
  if ( crs.isEmpty() )
    addIssue( report, QStringLiteral( "Raster CRS is missing" ) );

  double gt[6] = { 0, 0, 0, 0, 0, 0 };
  if ( GDALGetGeoTransform( ds, gt ) != CE_None || !isValidGeoTransform( gt ) )
    addIssue( report, QStringLiteral( "Raster geotransform is invalid" ) );
  else
  {
    Json::Value transform( Json::arrayValue );
    for ( int i = 0; i < 6; ++i )
      transform.append( gt[i] );
    report.summary["geoTransform"] = transform;
  }

  if ( bandCount >= 1 )
  {
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    if ( band && allSampledNoData( band, width, height ) )
      addIssue( report, QStringLiteral( "Band 1 sampled pixels are all NoData" ) );
  }

  GDALClose( ds );
  CPLErrorReset();

  if ( report.issues.isEmpty() )
    report.ok = true;

  return report;
}

OutputVerification OutputVerifier::verifyVector( const QString &path )
{
  OutputVerification report;
  report.kind = QStringLiteral( "vector" );
  report.ok = false;

  if ( path.isEmpty() )
  {
    addIssue( report, QStringLiteral( "Path is empty" ) );
    return report;
  }

  if ( QgsDataSourceResolver::requiresLocalExistenceCheck( path ) && !QFile::exists( path ) )
  {
    addIssue( report, QStringLiteral( "File does not exist: %1" ).arg( path ) );
    return report;
  }

  ensureGdalInit();
  CPLErrorReset();

  GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(),
                                GDAL_OF_VECTOR | GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR,
                                nullptr, nullptr, nullptr );
  if ( !ds )
  {
    const char *msg = CPLGetLastErrorMsg();
    addIssue( report, QStringLiteral( "Cannot open vector: %1" ).arg( msg && msg[0] ? QString::fromUtf8( msg ) : path ) );
    CPLErrorReset();
    return report;
  }

  const int layerCount = GDALDatasetGetLayerCount( ds );
  report.summary["layerCount"] = layerCount;
  if ( layerCount <= 0 )
  {
    addIssue( report, QStringLiteral( "Vector dataset has no layers" ) );
    GDALClose( ds );
    CPLErrorReset();
    return report;
  }

  OGRLayerH layer = GDALDatasetGetLayer( ds, 0 );
  if ( !layer )
  {
    addIssue( report, QStringLiteral( "Cannot access first vector layer" ) );
    GDALClose( ds );
    CPLErrorReset();
    return report;
  }

  const GIntBig featureCount = OGR_L_GetFeatureCount( layer, 1 ); // force count, small datasets only
  report.summary["featureCount"] = static_cast<Json::Int64>( featureCount );
  if ( featureCount == 0 )
    addWarning( report, QStringLiteral( "First layer contains no features" ) );

  const OGRwkbGeometryType geomType = OGR_L_GetGeomType( layer );
  report.summary["geometryType"] = OGRGeometryTypeToName( geomType );

  OGRSpatialReferenceH srs = OGR_L_GetSpatialRef( layer );
  const QString crs = wktFromOsr( srs );
  report.summary["crs"] = crs.toStdString();
  if ( crs.isEmpty() )
    addIssue( report, QStringLiteral( "Vector layer CRS is missing" ) );

  OGREnvelope extent;
  const OGRErr extentErr = OGR_L_GetExtent( layer, &extent, 0 );
  if ( featureCount > 0 )
  {
    if ( extentErr != OGRERR_NONE
         || std::isnan( extent.MinX ) || std::isnan( extent.MinY )
         || std::isnan( extent.MaxX ) || std::isnan( extent.MaxY ) )
    {
      addIssue( report, QStringLiteral( "Vector layer extent is invalid" ) );
    }
    else if ( extent.MinX >= extent.MaxX || extent.MinY >= extent.MaxY )
    {
      addIssue( report, QStringLiteral( "Vector layer extent is empty" ) );
    }
    else
    {
      Json::Value ext( Json::arrayValue );
      ext.append( extent.MinX );
      ext.append( extent.MinY );
      ext.append( extent.MaxX );
      ext.append( extent.MaxY );
      report.summary["extent"] = ext;
    }
  }

  GDALClose( ds );
  CPLErrorReset();

  if ( report.issues.isEmpty() )
    report.ok = true;

  return report;
}

QString OutputVerifier::kindHintFromPath( const QString &path )
{
  const QString suffix = QFileInfo( path ).suffix().toLower();
  if ( suffix == QStringLiteral( "shp" ) || suffix == QStringLiteral( "geojson" )
       || suffix == QStringLiteral( "gpkg" ) || suffix == QStringLiteral( "kml" )
       || suffix == QStringLiteral( "csv" ) || suffix == QStringLiteral( "tsv" )
       || suffix == QStringLiteral( "json" ) || suffix == QStringLiteral( "xml" ) )
  {
    return QStringLiteral( "vector" );
  }
  return QStringLiteral( "raster" );
}

} // namespace sicnu::agent
