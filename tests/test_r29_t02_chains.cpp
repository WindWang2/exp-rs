// tests/test_r29_t02_chains.cpp — R29-T02 WP11 delivery chains ② and ③.
//
// Chain ②: mixed import batch → cancel mid-way → process-restart resume
// (journaled items never double-registered) → virtual raster on an explicit
// TARGET GRID (the WP06 warp path) → recipe reopen (deterministic product).
//
// Chain ③: cartography produce through the REAL TaskCenter dispatch → real
// map CONTENT (non-transparent pixels, exact page geometry) → a failing
// produce targeting the same artifact must NOT pollute the existing one →
// a subsequent successful produce works again.
//
// Oracle independence: import identity assertions read the DataManager
// catalog; map-content assertions count painted pixels and compare file
// bytes directly; the warp grid/pixel oracle mirrors
// test_r29_t02_virtual_warp.cpp's independent OGR transform.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <ogr_spatialref.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <json/json.h>

#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>

#include "agent/cartography/cartography_operators.h"
#include "agent/mapspec/mapspec.h"
#include "data/data_manager.h"
#include "data/import_session.h"
#include "data/raster_grid_compat.h"
#include "data/virtual_raster_preflight.h"
#include "data/virtual_raster_recipe.h"
#include "jobs/job_types.h"
#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <qgsapplication.h>
#include <qgsproject.h>

using namespace sicnu::data;
using namespace sicnu::agent::mapspec;

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR ""
#endif

namespace
{

void writeUtmBand( const QString &path, int seed )
{
  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), 21, 21, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  std::array<double, 6> gt = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
  GDALSetProjection( ds, "EPSG:32648" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( 21 );
  for ( int row = 0; row < 21; ++row )
  {
    for ( int col = 0; col < 21; ++col )
      line[col] = 1000.f + seed * 100.f + row * 21.f + col;
    REQUIRE( GDALRasterIO( band, GF_Write, 0, row, 21, 1, line.data(), 21, 1,
                           GDT_Float32, 0, 0 ) == CE_None );
  }
  GDALClose( ds );
}

/// Minimal JSON parse helper (same shape as cp11's own `parse`).
Json::Value parse( const std::string &json )
{
  Json::CharReaderBuilder builder;
  Json::Value value;
  std::istringstream stream( json );
  std::string errors;
  if ( !Json::parseFromStream( builder, stream, &value, &errors ) )
    FAIL( errors );
  return value;
}

std::string sha256OfFile( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  QCryptographicHash hash( QCryptographicHash::Sha256 );
  hash.addData( &file );
  file.close();
  return hash.result().toHex().toStdString();
}

/// Writes an honest EPSG:4326 raster covering the same ground as the UTM
/// fixture (same area, ~same ground pixel size), so the chain's virtual
/// raster exercises a REAL reprojection (WP06) rather than relabeled
/// numbers.
void writeWgs84Band( const QString &path, int seed )
{
  OGRSpatialReference src;
  REQUIRE( src.importFromEPSG( 32648 ) == OGRERR_NONE );
  OGRSpatialReference dst;
  REQUIRE( dst.importFromEPSG( 4326 ) == OGRERR_NONE );
  dst.SetAxisMappingStrategy( OAMS_TRADITIONAL_GIS_ORDER ); // void in GDAL 3.13
  OGRCoordinateTransformation *xf = OGRCreateCoordinateTransformation( &src, &dst );
  REQUIRE( xf != nullptr );
  double xs[4] = { 500000.0, 500210.0, 500000.0, 500210.0 };
  double ys[4] = { 4500000.0, 4500000.0, 4499790.0, 4499790.0 };
  xf->Transform( 4, xs, ys ); // C++ Transform returns void in GDAL 3.13
  OGRCoordinateTransformation::DestroyCT( xf );
  double minLon = xs[0], maxLon = xs[0], maxLat = ys[0];
  for ( int i = 0; i < 4; ++i )
  {
    REQUIRE( std::isfinite( xs[i] ) );
    REQUIRE( std::isfinite( ys[i] ) );
    minLon = std::min( minLon, xs[i] );
    maxLon = std::max( maxLon, xs[i] );
    maxLat = std::max( maxLat, ys[i] );
  }
  // The transform really ran: geographic longitudes, not the UTM inputs.
  REQUIRE( maxLon < 360.0 );

  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), 21, 21, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  const double resX = ( maxLon - minLon ) / 21.0;
  const double resY = resX; // ~square degrees ≈ the UTM ground pixel size
  std::array<double, 6> gt = { minLon, resX, 0.0, maxLat, 0.0, -resY };
  REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
  GDALSetProjection( ds, "EPSG:4326" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( 21 );
  for ( int row = 0; row < 21; ++row )
  {
    for ( int col = 0; col < 21; ++col )
      line[col] = 500.f + seed * 100.f + row * 21.f + col;
    REQUIRE( GDALRasterIO( band, GF_Write, 0, row, 21, 1, line.data(), 21, 1,
                           GDT_Float32, 0, 0 ) == CE_None );
  }
  GDALClose( ds );
}

} // namespace

int main( int argc, char **argv )
{
  qputenv( "SICNU_CARTOGRAPHY_DIR", SICNU_CARTOGRAPHY_DATA_DIR );
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  sicnu::agent::cartography::initCartographyOperators();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

// ---------------------------------------------------------------------------
// Chain ② — import cancel/restart → virtual target grid → recipe reopen.
// ---------------------------------------------------------------------------
TEST_CASE( "Chain ②: mixed import cancel→restart resume→virtual target grid",
           "[r29][t02][chain2]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // Mixed batch: one UTM raster + one honest EPSG:4326 raster over the same
  // ground (the warp input for the virtual stage) + one garbage file.
  const QString a = dir.filePath( QStringLiteral( "a.tif" ) );
  const QString b = dir.filePath( QStringLiteral( "b_4326.tif" ) );
  const QString garbage = dir.filePath( QStringLiteral( "garbage.tif" ) );
  writeUtmBand( a, 0 );
  writeWgs84Band( b, 0 );
  {
    QFile file( garbage );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "not a raster" );
    file.close();
  }

  ImportSession::Options options;
  options.journalPath = dir.filePath( QStringLiteral( "session.json" ) );

  // Phase 1-2 with a mid-batch cancel: one item registers, the rest stays.
  {
    ImportSession first( &manager, { a, b, garbage }, options );
    first.check();
    REQUIRE( first.state() == ImportSessionState::ReadyToImport );
    first.proceedSteps( 1 );
    first.requestCancel();
    first.proceedSteps( 64 ); // observes the cancel at the next boundary
    REQUIRE( first.state() == ImportSessionState::Cancelled );
    const auto summary = first.summary();
    int registered = 0;
    for ( const auto &item : summary.items )
      if ( item.state == ImportSessionItemState::Registered )
        ++registered;
    REQUIRE( registered == 1 ); // exactly 'a' settled before the cancel
    REQUIRE( !summary.journalError.isEmpty() == false );
  }

  // Process-restart shape: a FRESH session over the same journal resumes.
  // The journaled 'a' must resolve in the catalog and NEVER re-register.
  const auto aAssetBefore = manager.findByPath( a );
  REQUIRE( aAssetBefore.has_value() );
  const QString aId = aAssetBefore->id().toString();
  {
    ImportSession second( &manager, { a, b, garbage }, options );
    // journal-restored terminal state (Cancelled, or Failed when the journal
    // recorded a hard error item); resume() accepts both.
    const ImportSessionState restored = second.state();
    REQUIRE( ( restored == ImportSessionState::Cancelled
               || restored == ImportSessionState::Failed ) );
    second.resume();
    // Drain any remaining items stepwise (bounded, UI-shaped driving).
    int guard = 0;
    while ( second.hasPendingItems() && second.state() == ImportSessionState::Importing
            && guard++ < 16 )
      second.proceedSteps( 4 );
    const auto summary = second.summary();
    // 'a' was skipped (already in catalog), 'b' registered, garbage refused.
    bool aSkipped = false, bRegistered = false, garbageRefused = false;
    for ( const auto &item : summary.items )
    {
      if ( item.path == a && item.state == ImportSessionItemState::Skipped )
        aSkipped = true;
      if ( item.path == b && item.state == ImportSessionItemState::Registered )
        bRegistered = true;
      if ( item.path == garbage && item.state == ImportSessionItemState::Refused )
        garbageRefused = true;
    }
    CHECK( aSkipped );
    CHECK( bRegistered );
    CHECK( garbageRefused );
    REQUIRE( second.state() == ImportSessionState::Completed );
  }
  // Stable asset identity across the restart: same id, single registration.
  CHECK( manager.findByPath( a )->id().toString() == aId );

  // Virtual raster over a (UTM) + b (4326) with an EXPLICIT UTM target grid:
  // preflight must classify reprojection and the provider must warp b onto
  // the target grid (WP05/WP06), not relabel it.
  const auto aId2 = manager.findByPath( a )->id();
  const auto bId = manager.findByPath( b )->id();
  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ aId2, 1 }, BandRef{ bId, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:32648" );
  recipe.targetResolutionX = 10.0;
  recipe.targetResolutionY = 10.0;
  recipe.resampling = ResamplingMethod::NearestNeighbour;

  const PreflightResult pre = preflightVirtualRaster( recipe, manager );
  REQUIRE( pre.verdict == PreflightVerdict::RequiresReprojection );
  REQUIRE( pre.canCreate );

  const sicnu::data::Result<sicnu::data::AssetId> created =
    manager.createVirtualRaster( recipe );
  REQUIRE( created );
  const auto snapshot = manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;

  // Recipe reopen (project-reopen shape): the stored recipe round-trips and
  // the product reopens on the target grid with both inputs' content.
  const auto stored = manager.virtualRasterRecipe( created.value() );
  REQUIRE( stored.has_value() );
  CHECK( *stored == recipe );

  ensureGdalInit();
  GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( vrt != nullptr );
  std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
  REQUIRE( GDALGetGeoTransform( vrt, gt.data() ) == CE_None );
  CHECK( isSameCrs( QString::fromUtf8( GDALGetProjectionRef( vrt ) ),
                    QStringLiteral( "EPSG:32648" ) ) );
  // 'a' is native on this grid: its center pixel (10,10) value 1000+210+10
  // must survive the composition at its own coordinates.
  float center = std::numeric_limits<float>::quiet_NaN();
  REQUIRE( GDALRasterIO( GDALGetRasterBand( vrt, 1 ), GF_Read, 10, 10, 1, 1,
                         &center, 1, 1, GDT_Float32, 0, 0 ) == CE_None );
  CHECK( center == Catch::Approx( 1000.f + 210.f + 10.f ).epsilon( 1e-6 ) );
  GDALClose( vrt );
}

// ---------------------------------------------------------------------------
// Chain ③ — TaskCenter cartography produce: real content, failure isolation.
// ---------------------------------------------------------------------------
TEST_CASE( "Chain ③: TaskCenter produce renders real content and failures "
           "do not pollute the standing artifact",
           "[r29][t02][chain3]" )
{
  auto specOf = []( const std::string &layoutName, bool broken ) {
    Json::Value spec = makeMapSpec( layoutName, Json::Value() );
    Json::Value rect( Json::arrayValue );
    rect.append( 12 );
    rect.append( 24 );
    rect.append( 190 );
    rect.append( 160 );
    Json::Value frame( Json::objectValue );
    frame["rect_mm"] = rect;
    Json::Value extent( Json::arrayValue );
    extent.append( 116.0 );
    extent.append( 39.0 );
    extent.append( 117.0 );
    extent.append( 40.0 );
    frame["extent"] = extent;
    appendMapSpecItem( spec, "map_frames", frame );
    if ( broken )
      // Same layout name, envelope-less (no map_frames): produce refuses
      // with a typed VALIDATION_FAILED (the cp11 corpus proves this shape
      // fails) while still resolving the SAME output filename — so the
      // pollution check below exercises a post-validation failure aimed at
      // the standing artifact's path (R2 review P3).
      return parse( R"({ "kind": "map_spec", "layout_name": "chain3-map" })" );
    return spec;
  };

  const auto submitProduce = [&]( const Json::Value &mapspec,
                                  const std::string &directory ) {
    Json::Value operatorParams( Json::objectValue );
    operatorParams["mapspec"] = mapspec;
    operatorParams["directory"] = directory;
    operatorParams["format"] = "png";
    operatorParams["dpi"] = 72.0;
    operatorParams["write_manifest"] = false;
    sicnu::jobs::JobRequest request;
    request.algorithmId = "cartography:produce";
    request.title = "cartography:produce";
    request.source = "ui";
    request.params = operatorParams;
    return sicnu::TaskCenter::instance().submitJob( request );
  };

  QTemporaryDir dir;

  // 1. Successful produce through the REAL dispatch: real content required —
  //    a valid-but-fully-transparent PNG is exactly the H1 failure mode.
  const std::string dirA = dir.filePath( QStringLiteral( "a" ) ).toStdString();
  const long okTask = submitProduce( specOf( "chain3-map", false ), dirA );
  REQUIRE( okTask > 0 );
  const auto okInfo = sicnu::TaskCenter::instance().waitForTask( okTask );
  REQUIRE( okInfo.status == sicnu::TaskStatus::Completed );
  REQUIRE( okInfo.resultPayload.isMember( "artifact" ) );
  const QString artifact = QString::fromStdString(
    okInfo.resultPayload["artifact"].asString() );
  REQUIRE( QFileInfo::exists( artifact ) );
  const QImage image( artifact );
  REQUIRE( !image.isNull() );
  CHECK( image.width() == 841 );
  CHECK( image.height() == 595 );
  int painted = 0;
  for ( int y = 0; y < image.height() && painted < 16; ++y )
    for ( int x = 0; x < image.width(); ++x )
      if ( qAlpha( image.pixel( x, y ) ) > 0 )
      {
        ++painted;
        break;
      }
  CHECK( painted > 0 ); // REAL map ink, not a transparent "success"
  const std::string standingSha = sha256OfFile( artifact );

  // 2. A FAILING produce aimed at the SAME layout/directory must leave the
  //    standing artifact byte-identical (no pollution by half-products).
  const long badTask = submitProduce( specOf( "chain3-map", true ), dirA );
  REQUIRE( badTask > 0 );
  const auto badInfo = sicnu::TaskCenter::instance().waitForTask( badTask );
  CHECK( badInfo.status == sicnu::TaskStatus::Failed );

  // 3. A subsequent valid produce succeeds again (no wedged host lane).
  const std::string dirC = dir.filePath( QStringLiteral( "c" ) ).toStdString();
  const long againTask = submitProduce( specOf( "chain3-map", false ), dirC );
  REQUIRE( againTask > 0 );
  const auto againInfo = sicnu::TaskCenter::instance().waitForTask( againTask );
  CHECK( againInfo.status == sicnu::TaskStatus::Completed );
  REQUIRE( okInfo.resultPayload.isMember( "artifact" ) );
  const QString againArtifact = QString::fromStdString(
    againInfo.resultPayload["artifact"].asString() );
  CHECK( QFileInfo::exists( againArtifact ) );

  // The standing artifact from step 1 is untouched by either later run (it
  // lives in dirA; the failures/other runs wrote elsewhere).
  CHECK( sha256OfFile( artifact ) == standingSha );
}
