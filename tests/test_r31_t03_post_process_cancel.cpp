// test_r31_t03_post_process_cancel.cpp — R31-T03 WP16 behaviour tests.
//
// RED oracle (before the fix, RsPostProcessTask did NOT forward mFb.isCanceled
// to RsPostProcess::clump): a cancel requested at the deterministic clump-stage
// barrier was ignored until the whole scan finished. The task still returned
// "Cancelled", but only AFTER advance() had reported 80% progress. With the fix
// clump aborts internally at its first poll and progress stays at 10%.
//
// Real GDAL GeoTIFFs are used: a normal run reopens the output and checks
// component ids / NoData / CRS / grid; cancelled runs check the typed terminal
// state, no half-product registration and no damage to an existing output.
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <gdal.h>

#include "app/classification/rs_post_process_task.h"

#include <vector>

namespace
{

/// 8x8 Int32 label raster: 5-region (cols 0-2), 0-column (col 3), 7-region
/// (cols 4-7). With 4-connectivity clump assigns ids 1 / 2 / 3 in raster seed
/// order. NoData = 0 is declared but never used as a label value.
QString makeLabelRaster( const QString &path )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 8, 8, 1,
                                GDT_Int32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 100.0, 2.0, 0.0, 500.0, 0.0, -2.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection( ds,
                     "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                     "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  GDALSetRasterNoDataValue( band, 0.0 );
  std::vector<int> line( 8 );
  for ( int c = 0; c < 8; ++c )
    line[c] = c < 3 ? 5 : ( c == 3 ? 9 : 7 );
  for ( int row = 0; row < 8; ++row )
    GDALRasterIO( band, GF_Write, 0, row, 8, 1, line.data(), 8, 1,
                  GDT_Int32, 0, 0 );
  GDALClose( ds );
  return path;
}

RsPostProcessConfig clumpConfig( const QString &input, const QString &output )
{
  RsPostProcessConfig cfg;
  cfg.inputPath = input;
  cfg.outputRasterPath = output;
  cfg.runSieve = false;
  cfg.runMajority = false;
  cfg.runClump = true;
  cfg.runRecode = false;
  cfg.runPolygonize = false;
  cfg.connectedness = 4;
  // Int32 output: PREDICTOR=2 is not needed.
  cfg.creationOptions = { QStringLiteral( "TILED=YES" ),
                          QStringLiteral( "COMPRESS=DEFLATE" ) };
  return cfg;
}

struct OutputPixels
{
  bool opened = false;
  int width = 0;
  int height = 0;
  double geoTransform[6] = { 0, 0, 0, 0, 0, 0 };
  bool hasGeoTransform = false;
  bool hasNoData = false;
  double noData = 0.0;
  QString projection;
  std::vector<int> pixels;
};

OutputPixels readOutput( const QString &path )
{
  OutputPixels out;
  GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
    return out;
  out.opened = true;
  out.width = GDALGetRasterXSize( ds );
  out.height = GDALGetRasterYSize( ds );
  out.hasGeoTransform = GDALGetGeoTransform( ds, out.geoTransform ) == CE_None;
  const char *wkt = GDALGetProjectionRef( ds );
  out.projection = wkt ? QString::fromUtf8( wkt ) : QString();
  int hasNoData = 0;
  out.noData = GDALGetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), &hasNoData );
  out.hasNoData = hasNoData != 0;
  out.pixels.resize( static_cast<size_t>( out.width * out.height ) );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  const CPLErr rc = GDALRasterIO( band, GF_Read, 0, 0, out.width, out.height,
                                  out.pixels.data(), out.width, out.height,
                                  GDT_Int32, 0, 0 );
  if ( rc != CE_None )
    out.pixels.clear();
  GDALClose( ds );
  return out;
}

bool writeBytes( const QString &path, const QByteArray &bytes )
{
  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  return file.write( bytes ) == bytes.size();
}

QByteArray readBytes( const QString &path )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return {};
  return file.readAll();
}

} // namespace

TEST_CASE( "R31-T03 WP16: clump cancellation terminates INSIDE the stage",
           "[r31t03][wp16][cancel]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString input = dir.filePath( QStringLiteral( "labels.tif" ) );
  const QString output = dir.filePath( QStringLiteral( "clumped.tif" ) );
  makeLabelRaster( input );

  bool barrierEntered = false;
  RsPostProcessTask *self = nullptr;
  RsPostProcessConfig cfg = clumpConfig( input, output );
  // Deterministic barrier: the hook runs exactly at clump stage start, on the
  // same thread. No sleeps and no "make the raster big enough" races. The task
  // owns a copy of the config, so the hook refers to `self` set just after
  // construction, before run() is called.
  cfg.clumpStageEnteredForTest = [&self, &barrierEntered]() {
    barrierEntered = true;
    if ( self )
      self->cancel();
  };

  RsPostProcessTask cancelTask( cfg );
  self = &cancelTask;

  const bool ok = cancelTask.run();
  REQUIRE( barrierEntered );
  CHECK_FALSE( ok );
  const RsPostProcessTask::Result &result = cancelTask.result();
  CHECK_FALSE( result.ok );
  CHECK( result.errorMessage == QStringLiteral( "Cancelled" ) );
  CHECK( result.durationMs == 0 );
  // The RED discriminator: clump aborts before advance() (10% load progress),
  // instead of completing the whole scan and reporting 80%.
  CHECK( cancelTask.progress() < 20.0 );
  // No half-product registration and no success card.
  CHECK_FALSE( QFile::exists( output ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".tmp~" ) ) );
}

TEST_CASE( "R31-T03 WP16: normal clump run keeps real pixel / CRS / grid values",
           "[r31t03][wp16][numeric]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString input = dir.filePath( QStringLiteral( "labels.tif" ) );
  const QString output = dir.filePath( QStringLiteral( "clumped.tif" ) );
  makeLabelRaster( input );

  RsPostProcessTask task( clumpConfig( input, output ) );
  REQUIRE( task.run() );
  REQUIRE( task.result().ok );
  CHECK( task.result().errorMessage.isEmpty() );
  CHECK( task.progress() == 100.0 );
  REQUIRE( QFile::exists( output ) );

  const OutputPixels out = readOutput( output );
  REQUIRE( out.opened );
  REQUIRE( out.width == 8 );
  REQUIRE( out.height == 8 );
  REQUIRE( out.pixels.size() == 64 );
  CHECK( out.pixels[0] == 1 );  // 5-region
  CHECK( out.pixels[3] == 2 );  // 9-column
  CHECK( out.pixels[4] == 3 );  // 7-region
  CHECK( out.pixels[63] == 3 );
  CHECK( out.projection.contains( QStringLiteral( "WGS 84" ) ) );
  REQUIRE( out.hasGeoTransform );
  CHECK( out.geoTransform[0] == 100.0 );
  CHECK( out.geoTransform[1] == 2.0 );
  CHECK( out.geoTransform[5] == -2.0 );
  CHECK( out.hasNoData );
}

TEST_CASE( "R31-T03 WP16: cancellation at the start edge and existing output safety",
           "[r31t03][wp16][cancel][negative]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString input = dir.filePath( QStringLiteral( "labels.tif" ) );
  const QString output = dir.filePath( QStringLiteral( "published.tif" ) );
  makeLabelRaster( input );

  // An already published raster must remain byte-identical when a later
  // clump run is cancelled inside the stage.
  const QByteArray sentinel( "R31-T03-PREVIOUS-PUBLICATION" );
  REQUIRE( writeBytes( output, sentinel ) );

  RsPostProcessConfig cfg = clumpConfig( input, output );
  bool barrierEntered = false;
  RsPostProcessTask *self = nullptr;
  cfg.clumpStageEnteredForTest = [&self, &barrierEntered]() {
    barrierEntered = true;
    if ( self )
      self->cancel();
  };
  RsPostProcessTask task( cfg );
  self = &task;
  CHECK_FALSE( task.run() );
  REQUIRE( barrierEntered );
  CHECK( task.result().errorMessage == QStringLiteral( "Cancelled" ) );
  CHECK( readBytes( output ) == sentinel );

  // Start edge: cancel BEFORE run() — the load stage must refuse immediately.
  RsPostProcessTask early( clumpConfig( input, dir.filePath( QStringLiteral( "early.tif" ) ) ) );
  early.cancel();
  CHECK_FALSE( early.run() );
  CHECK( early.result().errorMessage == QStringLiteral( "Cancelled" ) );
  CHECK( early.progress() < 20.0 );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "early.tif" ) ) ) );
}

TEST_CASE( "R31-T03 WP16: other stages keep their numeric results",
           "[r31t03][wp16][numeric]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString input = dir.filePath( QStringLiteral( "labels.tif" ) );
  const QString output = dir.filePath( QStringLiteral( "recoded.tif" ) );
  makeLabelRaster( input );

  RsPostProcessConfig cfg;
  cfg.inputPath = input;
  cfg.outputRasterPath = output;
  cfg.runSieve = false;
  cfg.runMajority = false;
  cfg.runClump = false;
  cfg.runRecode = true;
  cfg.runPolygonize = false;
  cfg.recodeMap = { { 5, 50 }, { 9, 90 }, { 7, 70 } };
  cfg.creationOptions = { QStringLiteral( "TILED=YES" ),
                          QStringLiteral( "COMPRESS=DEFLATE" ) };

  RsPostProcessTask task( cfg );
  REQUIRE( task.run() );
  REQUIRE( task.result().ok );
  const OutputPixels out = readOutput( output );
  REQUIRE( out.pixels.size() == 64 );
  CHECK( out.pixels[0] == 50 );
  CHECK( out.pixels[3] == 90 );
  CHECK( out.pixels[4] == 70 );
}
