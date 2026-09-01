// tests/test_output_verifier.cpp
//
// OutputVerifier contract: fast, sample-based health checks for committed
// raster and vector outputs.  No full scans; local existence checks respect
// QgsDataSourceResolver so VSI paths are not rejected.
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include "agent/output_verifier.h"

using namespace sicnu::agent;

namespace
{

void writeSmallGeoTiff( const QString &path, bool allNoData = false )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 16, 16, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );

  OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
  OSRImportFromEPSG( srs, 4326 );
  char *wkt = nullptr;
  OSRExportToWkt( srs, &wkt );
  GDALSetProjection( ds, wkt );
  CPLFree( wkt );
  OSRDestroySpatialReference( srs );

  double gt[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 }; // GDAL C API takes double*
  GDALSetGeoTransform( ds, gt );

  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  GDALSetRasterNoDataValue( band, -9999.0 );

  std::vector<float> tile( 16 * 16, allNoData ? -9999.0f : 1.0f );
  CPLErr err = GDALRasterIO( band, GF_Write, 0, 0, 16, 16,
                             tile.data(), 16, 16, GDT_Float32, 0, 0 );
  REQUIRE( err == CE_None );
  GDALClose( ds );
}

void writeSmallGeoJson( const QString &path, int featureCount )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GeoJSON" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
  REQUIRE( ds != nullptr );

  OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
  OSRImportFromEPSG( srs, 4326 );
  OGRLayerH layer = GDALDatasetCreateLayer( ds, "test", srs, wkbPoint, nullptr );
  REQUIRE( layer != nullptr );
  OSRDestroySpatialReference( srs );

  for ( int i = 0; i < featureCount; ++i )
  {
    OGRFeatureH feature = OGR_F_Create( OGR_L_GetLayerDefn( layer ) );
    OGRGeometryH point = OGR_G_CreateGeometry( wkbPoint );
    OGR_G_SetPoint_2D( point, 0, 10.0 + i, 20.0 + i );
    OGR_F_SetGeometryDirectly( feature, point );
    REQUIRE( OGR_L_CreateFeature( layer, feature ) == OGRERR_NONE );
    OGR_F_Destroy( feature );
  }

  GDALClose( ds );
}

} // namespace

TEST_CASE( "OutputVerifier accepts a healthy small raster", "[agent][output_verifier][raster]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.path() + QStringLiteral( "/ok.tif" );
  writeSmallGeoTiff( path );

  const OutputVerification report = OutputVerifier().verify( path );
  REQUIRE( report.ok );
  REQUIRE( report.kind == QStringLiteral( "raster" ) );
  REQUIRE( report.summary["width"].asInt() == 16 );
  REQUIRE( report.summary["height"].asInt() == 16 );
  REQUIRE( report.summary["bandCount"].asInt() == 1 );
  REQUIRE( !report.summary["crs"].asString().empty() );
  REQUIRE( report.issues.isEmpty() );
}

TEST_CASE( "OutputVerifier rejects a raster whose sampled pixels are all NoData", "[agent][output_verifier][raster]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.path() + QStringLiteral( "/nodata.tif" );
  writeSmallGeoTiff( path, /*allNoData=*/true );

  const OutputVerification report = OutputVerifier().verify( path );
  REQUIRE_FALSE( report.ok );
  REQUIRE( report.issues.contains( QStringLiteral( "Band 1 sampled pixels are all NoData" ) ) );
}

TEST_CASE( "OutputVerifier rejects a missing local file", "[agent][output_verifier][raster]" )
{
  const OutputVerification report = OutputVerifier().verify( QStringLiteral( "/tmp/output_verifier_nonexistent.tif" ) );
  REQUIRE_FALSE( report.ok );
  REQUIRE( report.issues.size() >= 1 );
}

TEST_CASE( "OutputVerifier accepts a healthy small vector", "[agent][output_verifier][vector]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.path() + QStringLiteral( "/ok.geojson" );
  writeSmallGeoJson( path, 3 );

  const OutputVerification report = OutputVerifier().verify( path );
  REQUIRE( report.ok );
  REQUIRE( report.kind == QStringLiteral( "vector" ) );
  REQUIRE( report.summary["layerCount"].asInt() == 1 );
  REQUIRE( report.summary["featureCount"].asInt64() == 3 );
  REQUIRE( !report.summary["crs"].asString().empty() );
  REQUIRE( report.issues.isEmpty() );
}

TEST_CASE( "OutputVerifier flags an empty vector layer", "[agent][output_verifier][vector]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.path() + QStringLiteral( "/empty.geojson" );
  writeSmallGeoJson( path, 0 );

  const OutputVerification report = OutputVerifier().verify( path );
  REQUIRE( report.ok );
  REQUIRE( report.warnings.contains( QStringLiteral( "First layer contains no features" ) ) );
}

TEST_CASE( "OutputVerifier does not reject a VSI path because of local existence", "[agent][output_verifier][resolver]" )
{
  // /vsimem/ is a GDAL virtual path; QFile::exists must not be applied.
  // The dataset does not exist, so the open check fails, but the issue must
  // not be "File does not exist".
  const OutputVerification report = OutputVerifier().verify( QStringLiteral( "/vsimem/output_verifier_not_there.tif" ) );
  REQUIRE_FALSE( report.ok );
  for ( const QString &issue : report.issues )
  {
    REQUIRE_FALSE( issue.contains( QStringLiteral( "File does not exist" ) ) );
  }
}
