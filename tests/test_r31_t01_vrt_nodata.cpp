#include <catch2/catch_test_macros.hpp>

#include "data/data_manager.h"
#include "data/virtual_raster_preflight.h"
#include "data/virtual_raster_recipe.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QApplication>
#include <QTemporaryDir>
#include <qgsapplication.h>

#include <gdal_priv.h>

#include <vector>

using namespace sicnu::data;

namespace
{

void ensureQgis()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char name[] = "test_r31_t01_vrt_nodata";
  static char *argv[] = { name, nullptr };
  static auto *app = new QgsApplication( argc, argv, true );
  (void) app;
  QgsApplication::initQgis();
}

void writeSource( const QString &path )
{
  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), 9, 9, 1,
                                     GDT_Float32, nullptr );
  REQUIRE( dataset != nullptr );
  double transform[6] = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( dataset, transform ) == CE_None );
  REQUIRE( GDALSetProjection( dataset, "EPSG:32648" ) == CE_None );
  std::vector<float> pixels( 81, 100.0f );
  for ( int y = 0; y < 9; ++y )
    for ( int x = 0; x < 9; ++x )
      if ( x == 0 || y == 0 || x == 8 || y == 8 )
        pixels[static_cast<size_t>( y ) * 9 + x] = -9999.0f;
  pixels[3 * 9 + 3] = 0.0f;       // valid zero
  pixels[4 * 9 + 4] = -32768.0f;  // valid value equal to the output fill
  REQUIRE( GDALRasterIO( GDALGetRasterBand( dataset, 1 ), GF_Write, 0, 0, 9, 9,
                         pixels.data(), 9, 9, GDT_Float32, 0, 0 ) == CE_None );
  REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( dataset, 1 ), -9999.0 ) == CE_None );
  GDALClose( dataset );
}

AssetId registerRaster( DataManager &manager, const QString &path )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest request;
  request.source = source;
  const RegisterResult result = manager.registerSource( request );
  REQUIRE( !result.assetId.isNull() );
  return result.assetId;
}

void checkOutput( const QString &path )
{
  GDALDatasetH dataset = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( dataset != nullptr );
  const int width = GDALGetRasterXSize( dataset );
  const int height = GDALGetRasterYSize( dataset );
  std::vector<float> pixels( static_cast<size_t>( width ) * height, 12345.0f );
  REQUIRE( GDALRasterIO( GDALGetRasterBand( dataset, 1 ), GF_Read, 0, 0, width, height,
                         pixels.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
  bool sawValidZero = false;
  bool sawFill = false;
  bool sawOrdinary = false;
  for ( const float value : pixels )
  {
    sawValidZero = sawValidZero || value == 0.0f;
    sawFill = sawFill || value == -32768.0f;
    sawOrdinary = sawOrdinary || value == 100.0f;
    CHECK( value != -9999.0f );
  }
  CHECK( sawValidZero );
  CHECK( sawFill );
  CHECK( sawOrdinary );
  int hasNoData = 0;
  CHECK( GDALGetRasterNoDataValue( GDALGetRasterBand( dataset, 1 ), &hasNoData ) == -32768.0 );
  CHECK( hasNoData == 1 );
  GDALClose( dataset );
}

bool hasAliasWarning( const PreflightResult &result )
{
  for ( const Diagnostic &diagnostic : result.diagnostics )
    if ( diagnostic.code == QLatin1String( "preflight.fill_value_alias" ) )
      return true;
  return false;
}

} // namespace

TEST_CASE( "R31-T01 WP07: source NoData and output fill stay distinct on direct and warped VRTs",
           "[r31][t01][vrt_nodata]" )
{
  ensureQgis();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.filePath( QStringLiteral( "source.tif" ) );
  writeSource( path );
  DataManager manager;
  const AssetId source = registerRaster( manager, path );

  VirtualRasterRecipe direct;
  direct.inputs = { BandRef{ source, 1 } };
  direct.noDataPolicy = NoDataPolicy::FillValue;
  direct.noDataFillValue = -32768.0;
  const PreflightResult directPreflight = preflightVirtualRaster( direct, manager );
  CHECK( directPreflight.canCreate );
  CHECK( hasAliasWarning( directPreflight ) );
  const Result<AssetId> directAsset = manager.createVirtualRaster( direct );
  REQUIRE( directAsset );
  checkOutput( manager.asset( directAsset.value() )->source().canonicalSource );

  VirtualRasterRecipe warped = direct;
  warped.targetCrs = QStringLiteral( "EPSG:4326" );
  warped.targetResolutionX = 0.0001;
  warped.targetResolutionY = 0.0001;
  warped.resampling = ResamplingMethod::NearestNeighbour;
  const PreflightResult warpedPreflight = preflightVirtualRaster( warped, manager );
  CHECK( warpedPreflight.canCreate );
  CHECK( hasAliasWarning( warpedPreflight ) );
  const Result<AssetId> warpedAsset = manager.createVirtualRaster( warped );
  REQUIRE( warpedAsset );
  checkOutput( manager.asset( warpedAsset.value() )->source().canonicalSource );
}
