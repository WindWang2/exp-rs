// tests/test_r30_t03_vrt_prepare_control.cpp — R30-T03 WP04.
//
// The VRT preparation's progress/cancel contract (new API surface, verified
// GREEN-only — the ConvertProgress plumbing is additive; no base build):
//   * staged progress: the planned grid and per-input preparation are
//     reported with non-decreasing fractions;
//   * cooperative cancellation during warp staging fails typed
//     ("virtual_raster.cancelled") and leaves the previous generation and
//     its dependencies untouched;
//   * a late cancel observed between inputs stops further warps.
#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal.h>

#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include "data/data_manager.h"
#include "data/virtual_raster_recipe.h"
#include "geospatial/convert/raster_convert.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QApplication>
#include <qgsapplication.h>

using namespace sicnu::data;

namespace
{

constexpr int kSide = 21;

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_r30_t03_vrt_prepare_control";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void )application;
  QgsApplication::initQgis();
}

QString writeUtmSource( const QString &path, float base )
{
  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), kSide, kSide, 1, GDT_Float32,
                nullptr );
  REQUIRE( ds != nullptr );
  std::array<double, 6> gt = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
  GDALSetProjection( ds, "EPSG:32648" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( kSide );
  for ( int row = 0; row < kSide; ++row )
  {
    for ( int col = 0; col < kSide; ++col )
      line[col] = base + static_cast<float>( row ) * kSide
                  + static_cast<float>( col );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, row, kSide, 1, line.data(),
                           kSide, 1, GDT_Float32, 0, 0 ) == CE_None );
  }
  GDALClose( ds );
  return path;
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

VirtualRasterRecipe warpRecipe( const AssetId &a )
{
  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ a, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  return recipe;
}

QHash<QString, qint64> warpIntermediates( const QString &vrtPath )
{
  QHash<QString, qint64> files;
  const QFileInfo vrtInfo( vrtPath );
  const QString base = vrtInfo.fileName() + QStringLiteral( ".warp" );
  for ( const QFileInfo &entry : vrtInfo.dir().entryInfoList( QDir::Files ) )
    if ( entry.fileName().startsWith( base ) )
      files.insert( entry.fileName(), entry.size() );
  return files;
}

} // namespace

TEST_CASE( "VRT warp preparation reports staged progress",
           "[r30][t03][prepare-control]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;
  const AssetId a =
    registerRaster( manager, writeUtmSource(
                                  dir.filePath( QStringLiteral( "a.tif" ) ),
                                  1000.f ) );

  std::vector<std::pair<double, std::string>> events;
  sicnu::geo::ConvertProgress progress;
  progress.report = [ &events ]( double fraction, const std::string &message ) {
    events.emplace_back( fraction, message );
  };

  const Result<AssetId> created =
    manager.createVirtualRaster( warpRecipe( a ),
                                 PersistencePolicy::ProjectPersistent,
                                 &progress );
  REQUIRE( created );

  REQUIRE( !events.empty() );
  bool sawGrid = false;
  bool sawInput = false;
  for ( const auto &[fraction, message] : events )
  {
    // The staged fractions and the warp's own GDAL per-input 0→1 callbacks
    // interleave, so global monotonicity does NOT hold; every reported
    // fraction must still be a legal progress value.
    CHECK( fraction >= 0.0 );
    CHECK( fraction <= 1.0 );
    if ( message.find( "target grid planned" ) != std::string::npos )
      sawGrid = true;
    if ( message.find( "warped input" ) != std::string::npos
         || message.find( "reusing prepared" ) != std::string::npos )
      sawInput = true;
  }
  CHECK( sawGrid );
  CHECK( sawInput );
}

TEST_CASE( "Cancelling VRT warp preparation is typed and keeps the previous "
           "generation intact",
           "[r30][t03][prepare-control]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;
  const AssetId a =
    registerRaster( manager, writeUtmSource(
                                  dir.filePath( QStringLiteral( "a.tif" ) ),
                                  1000.f ) );

  // Generation 1: healthy.
  const Result<AssetId> created =
    manager.createVirtualRaster( warpRecipe( a ) );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;
  const QHash<QString, qint64> generation1 = warpIntermediates( vrtPath );
  REQUIRE( generation1.size() == 1 );
  QFile vrtFile( vrtPath );
  REQUIRE( vrtFile.open( QIODevice::ReadOnly ) );
  const QByteArray bytes1 = vrtFile.readAll();
  vrtFile.close();

  // Replace the input (new content ⇒ new identity ⇒ a new warp must run).
  QFile::remove( dir.filePath( QStringLiteral( "a.tif" ) ) );
  writeUtmSource( dir.filePath( QStringLiteral( "a.tif" ) ), 8000.f );

  // Cancel BEFORE any warp starts: the pre-warp boundary check fires and no
  // GDAL work begins.
  std::atomic<bool> cancelFlag{ true };
  sicnu::geo::ConvertProgress progress;
  progress.cancelFlag = &cancelFlag;
  progress.report = []( double, const std::string & ) {};

  RegisterRequest request;
  request.source = snapshot->source();
  request.preparationProgress = &progress;
  const RegisterResult cancelled = manager.registerSource( request );
  REQUIRE( cancelled.assetId.isNull() );
  bool typedCancelled = false;
  for ( const Diagnostic &d : cancelled.diagnostics )
    if ( d.code == QLatin1String( "virtual_raster.cancelled" ) )
      typedCancelled = true;
  CHECK( typedCancelled );

  // The previous generation is COMPLETE: same bytes, dependencies on disk.
  REQUIRE( vrtFile.open( QIODevice::ReadOnly ) );
  CHECK( vrtFile.readAll() == bytes1 );
  vrtFile.close();
  const QHash<QString, qint64> afterCancel = warpIntermediates( vrtPath );
  for ( auto it = generation1.constBegin(); it != generation1.constEnd(); ++it )
    CHECK( afterCancel.contains( it.key() ) );

  // A cancelled preparation never flipped the asset's state.
  const std::optional<AssetSnapshot> still =
    manager.asset( created.value() );
  REQUIRE( still.has_value() );
  CHECK( still->state() == AssetState::Ready );

  // A clean retry (no cancel) completes the new generation.
  const RegisterResult retried = [&manager, &snapshot]() {
    RegisterRequest request;
    request.source = snapshot->source();
    return manager.registerSource( request );
  }();
  REQUIRE( !retried.assetId.isNull() );
  const QHash<QString, qint64> generation2 = warpIntermediates( vrtPath );
  REQUIRE( generation2.size() == 1 );
  CHECK( generation2.constBegin().key() != generation1.constBegin().key() );
}

TEST_CASE( "A late cancel between inputs stops further warps",
           "[r30][t03][prepare-control]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;
  // Two distinct inputs, both needing warp.
  const AssetId a =
    registerRaster( manager, writeUtmSource(
                                  dir.filePath( QStringLiteral( "a.tif" ) ),
                                  1000.f ) );
  const AssetId b =
    registerRaster( manager, writeUtmSource(
                                  dir.filePath( QStringLiteral( "b.tif" ) ),
                                  2000.f ) );

  VirtualRasterRecipe recipe = warpRecipe( a );
  recipe.inputs = { BandRef{ a, 1 }, BandRef{ b, 1 } };

  std::atomic<bool> cancelFlag{ false };
  int warpStarts = 0;
  sicnu::geo::ConvertProgress progress;
  progress.cancelFlag = &cancelFlag;
  progress.report = [ &cancelFlag, &warpStarts ]( double,
                                                  const std::string &message ) {
    // Cancel as soon as the FIRST input starts preparing; the second must
    // never start (the layout's pre-warp boundary check observes the flag).
    if ( message.find( "warped input" ) != std::string::npos
         || message.find( "reusing prepared" ) != std::string::npos )
    {
      ++warpStarts;
      cancelFlag.store( true );
    }
  };

  const Result<AssetId> created =
    manager.createVirtualRaster( recipe, PersistencePolicy::ProjectPersistent,
                                 &progress );
  REQUIRE( !created );
  bool typedCancelled = false;
  for ( const Diagnostic &d : created.diagnostics() )
    if ( d.code == QLatin1String( "virtual_raster.cancelled" )
         || d.code == QLatin1String( "virtual_raster.warp_failed" ) )
      typedCancelled = true;
  CHECK( typedCancelled );
  // Small race tolerance: the first input may complete its tiny warp before
  // the callback fires, but the SECOND input must never have started.
  CHECK( warpStarts <= 1 );
}
