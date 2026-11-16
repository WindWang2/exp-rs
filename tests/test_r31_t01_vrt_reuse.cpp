#include <catch2/catch_test_macros.hpp>

#include "data/data_manager.h"
#include "data/virtual_raster_preflight.h"
#include "data/virtual_raster_recipe.h"
#include "geospatial/convert/raster_convert.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <qgsapplication.h>

#include <gdal_priv.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace sicnu::data;

namespace
{

void ensureQgis()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char name[] = "test_r31_t01_vrt_reuse";
  static char *argv[] = { name, nullptr };
  static auto *app = new QgsApplication( argc, argv, true );
  (void) app;
  QgsApplication::initQgis();
}

void writeSource( const QString &path, int bandCount = 2, double noData = -9999.0 )
{
  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), 9, 9, bandCount,
                                     GDT_Float32, nullptr );
  REQUIRE( dataset != nullptr );
  double transform[6] = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( dataset, transform ) == CE_None );
  REQUIRE( GDALSetProjection( dataset, "EPSG:32648" ) == CE_None );
  std::vector<float> pixels( 81 );
  for ( int bandNumber = 1; bandNumber <= bandCount; ++bandNumber )
  {
    for ( int i = 0; i < 81; ++i )
      pixels[static_cast<size_t>( i )] = 100.0f * bandNumber + static_cast<float>( i );
    GDALRasterBandH band = GDALGetRasterBand( dataset, bandNumber );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 9, 9,
                           pixels.data(), 9, 9, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( band, noData ) == CE_None );
  }
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

QString stagedPath( const QString &vrtPath )
{
  const QFileInfo info( vrtPath );
  const auto entries = info.dir().entryInfoList(
    { info.fileName() + QStringLiteral( ".warp-*.tif" ) }, QDir::Files, QDir::Name );
  REQUIRE( entries.size() == 1 );
  return entries.front().absoluteFilePath();
}

struct ResolveOutcome
{
    bool succeeded = false;
    bool reused = false;
    bool warped = false;
};

ResolveOutcome resolvePreparedRaster( DataManager &manager, const SourceDescriptor &source )
{
  ResolveOutcome outcome;
  sicnu::geo::ConvertProgress progress;
  progress.report = [&outcome]( double, const std::string &message ) {
    outcome.reused = outcome.reused || message.find( "reusing prepared" ) != std::string::npos;
    outcome.warped = outcome.warped || message.find( "warped input" ) != std::string::npos;
  };
  RegisterRequest request;
  request.source = source;
  request.preparationProgress = &progress;
  const RegisterResult result = manager.registerSource( request );
  outcome.succeeded = !result.assetId.isNull();
  return outcome;
}

QByteArray readFileBytes( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  return file.readAll();
}

QString diagnosticText( const Result<AssetId> &result )
{
  QStringList messages;
  for ( const auto &diagnostic : result.diagnostics() )
    messages.append( diagnostic.code + QStringLiteral( ": " ) + diagnostic.message );
  return messages.join( QStringLiteral( "; " ) );
}

} // namespace

TEST_CASE( "R31-T01 WP08: staged warp reuse requires complete content and grid identity",
           "[r31][t01][vrt_reuse]" )
{
  ensureQgis();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString sourcePath = dir.filePath( QStringLiteral( "source.tif" ) );
  writeSource( sourcePath );

  DataManager manager;
  const AssetId source = registerRaster( manager, sourcePath );
  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ source, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;

  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  INFO( diagnosticText( created ).toStdString() );
  REQUIRE( created );
  const auto snapshot = manager.asset( created.value() );
  REQUIRE( snapshot );
  const QString staged = stagedPath( snapshot->source().canonicalSource );
  const QString identity = staged + QStringLiteral( ".identity.json" );
  REQUIRE( QFileInfo::exists( identity ) );

  const auto resolve = [&manager, &snapshot]( void ) {
    return resolvePreparedRaster( manager, snapshot->source() );
  };

  const QDateTime stagedStamp = QFileInfo( staged ).lastModified();
  const QDateTime identityStamp = QFileInfo( identity ).lastModified();
  const ResolveOutcome unchanged = resolve();
  CHECK( unchanged.succeeded );
  CHECK( unchanged.reused );
  CHECK_FALSE( unchanged.warped );
  CHECK( QFileInfo( staged ).lastModified() == stagedStamp );
  CHECK( QFileInfo( identity ).lastModified() == identityStamp );

  {
    GDALDatasetH dataset = GDALOpen( staged.toUtf8().constData(), GA_Update );
    REQUIRE( dataset != nullptr );
    double transform[6] = {};
    REQUIRE( GDALGetGeoTransform( dataset, transform ) == CE_None );
    transform[2] = 0.25;
    REQUIRE( GDALSetGeoTransform( dataset, transform ) == CE_None );
    GDALClose( dataset );
  }
  const ResolveOutcome rotated = resolve();
  CHECK( rotated.succeeded );
  CHECK( rotated.warped );
  CHECK_FALSE( rotated.reused );

  const QString reduced = dir.filePath( QStringLiteral( "reduced.tif" ) );
  {
    GDALDatasetH dataset = GDALOpen( staged.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( dataset != nullptr );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH copy = GDALCreate( driver, reduced.toUtf8().constData(),
                                   GDALGetRasterXSize( dataset ), GDALGetRasterYSize( dataset ),
                                   1, GDT_Float32, nullptr );
    REQUIRE( copy != nullptr );
    double transform[6] = {};
    REQUIRE( GDALGetGeoTransform( dataset, transform ) == CE_None );
    REQUIRE( GDALSetGeoTransform( copy, transform ) == CE_None );
    REQUIRE( GDALSetProjection( copy, GDALGetProjectionRef( dataset ) ) == CE_None );
    const int width = GDALGetRasterXSize( dataset );
    const int height = GDALGetRasterYSize( dataset );
    std::vector<float> pixels( static_cast<size_t>( width ) * height );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( dataset, 1 ), GF_Read, 0, 0, width, height,
                           pixels.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( copy, 1 ), GF_Write, 0, 0, width, height,
                           pixels.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( copy );
    GDALClose( dataset );
    REQUIRE( QFile::remove( staged ) );
    REQUIRE( QFile::copy( reduced, staged ) );
  }
  const ResolveOutcome missingBand = resolve();
  CHECK( missingBand.succeeded );
  CHECK( missingBand.warped );
  CHECK_FALSE( missingBand.reused );

  {
    GDALDatasetH dataset = GDALOpen( staged.toUtf8().constData(), GA_Update );
    REQUIRE( dataset != nullptr );
    float damaged = -777.0f;
    REQUIRE( GDALRasterIO( GDALGetRasterBand( dataset, 1 ), GF_Write, 0, 0, 1, 1,
                           &damaged, 1, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALSetRasterNoDataValue( GDALGetRasterBand( dataset, 2 ), 0.0 ) == CE_None );
    GDALClose( dataset );
  }
  const ResolveOutcome damaged = resolve();
  CHECK( damaged.succeeded );
  CHECK( damaged.warped );
  CHECK_FALSE( damaged.reused );

  GDALDatasetH finalDataset = GDALOpen( staged.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( finalDataset != nullptr );
  CHECK( GDALGetRasterCount( finalDataset ) == 2 );
  double finalTransform[6] = {};
  REQUIRE( GDALGetGeoTransform( finalDataset, finalTransform ) == CE_None );
  CHECK( finalTransform[2] == 0.0 );
  CHECK( finalTransform[4] == 0.0 );
  int hasNoData = 0;
  CHECK( GDALGetRasterNoDataValue( GDALGetRasterBand( finalDataset, 2 ), &hasNoData ) == -9999.0 );
  CHECK( hasNoData == 1 );
  GDALClose( finalDataset );
}

TEST_CASE( "R31-T01 WP08: NaN NoData identity survives staged warp reuse",
           "[r31][t01][vrt_reuse]" )
{
  ensureQgis();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString sourcePath = dir.filePath( QStringLiteral( "nan-source.tif" ) );
  writeSource( sourcePath, 1, std::numeric_limits<double>::quiet_NaN() );

  DataManager manager;
  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ registerRaster( manager, sourcePath ), 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  REQUIRE( preflightVirtualRaster( recipe, manager ).canCreate );
  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  INFO( diagnosticText( created ).toStdString() );
  REQUIRE( created );
  const auto snapshot = manager.asset( created.value() );
  REQUIRE( snapshot );
  const QString staged = stagedPath( snapshot->source().canonicalSource );
  const QString identity = staged + QStringLiteral( ".identity.json" );

  GDALDatasetH dataset = GDALOpen( staged.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( dataset != nullptr );
  int hasNoData = 0;
  const double noData = GDALGetRasterNoDataValue( GDALGetRasterBand( dataset, 1 ), &hasNoData );
  GDALClose( dataset );
  REQUIRE( hasNoData == 1 );
  REQUIRE( std::isnan( noData ) );

  const QByteArray stagedBytes = readFileBytes( staged );
  const QByteArray identityBytes = readFileBytes( identity );
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson( identityBytes, &parseError );
  REQUIRE( parseError.error == QJsonParseError::NoError );
  REQUIRE( document.isObject() );
  const QJsonArray bands = document.object().value( "bands" ).toArray();
  REQUIRE( bands.size() == 1 );
  const QJsonValue encodedNoData = bands[0].toObject().value( "nodata" );
  CHECK( encodedNoData.isString() );
  CHECK( encodedNoData.toString() == QStringLiteral( "nan" ) );

  const QDateTime stagedStamp = QFileInfo( staged ).lastModified();
  const QDateTime identityStamp = QFileInfo( identity ).lastModified();
  const ResolveOutcome unchanged = resolvePreparedRaster( manager, snapshot->source() );
  CHECK( unchanged.succeeded );
  CHECK( unchanged.reused );
  CHECK_FALSE( unchanged.warped );
  CHECK( readFileBytes( staged ) == stagedBytes );
  CHECK( readFileBytes( identity ) == identityBytes );
  CHECK( QFileInfo( staged ).lastModified() == stagedStamp );
  CHECK( QFileInfo( identity ).lastModified() == identityStamp );
}

TEST_CASE( "R31-T01 WP08: tiny many-band input can persist and reuse its full identity",
           "[r31][t01][vrt_reuse]" )
{
  ensureQgis();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString sourcePath = dir.filePath( QStringLiteral( "many-band-source.tif" ) );
  // The complete raster payload is only 324 kB. Selecting one output band
  // must not impose an undocumented 64 KiB limit on the prepared identity.
  constexpr int bandCount = 1000;
  writeSource( sourcePath, bandCount );

  DataManager manager;
  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ registerRaster( manager, sourcePath ), 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  REQUIRE( preflightVirtualRaster( recipe, manager ).canCreate );
  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  INFO( diagnosticText( created ).toStdString() );
  REQUIRE( created );
  const auto snapshot = manager.asset( created.value() );
  REQUIRE( snapshot );
  const QString staged = stagedPath( snapshot->source().canonicalSource );
  const QString identity = staged + QStringLiteral( ".identity.json" );
  const QByteArray stagedBytes = readFileBytes( staged );
  const QByteArray identityBytes = readFileBytes( identity );
  REQUIRE( identityBytes.size() > 64 * 1024 );
  const QJsonObject recorded = QJsonDocument::fromJson( identityBytes ).object();
  CHECK( recorded.value( "band_count" ).toInt() == bandCount );
  REQUIRE( recorded.value( "bands" ).toArray().size() == bandCount );

  const QDateTime stagedStamp = QFileInfo( staged ).lastModified();
  const QDateTime identityStamp = QFileInfo( identity ).lastModified();
  const ResolveOutcome unchanged = resolvePreparedRaster( manager, snapshot->source() );
  CHECK( unchanged.succeeded );
  CHECK( unchanged.reused );
  CHECK_FALSE( unchanged.warped );
  CHECK( readFileBytes( staged ) == stagedBytes );
  CHECK( readFileBytes( identity ) == identityBytes );
  CHECK( QFileInfo( staged ).lastModified() == stagedStamp );
  CHECK( QFileInfo( identity ).lastModified() == identityStamp );
}
