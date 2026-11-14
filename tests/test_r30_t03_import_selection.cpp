// tests/test_r30_t03_import_selection.cpp — R30-T03 WP07.
//
// Explicit container-member selection through the EXISTING identity chain:
//   * the container's preflight reports the member spellings verbatim
//     (subdataset_names) and keeps its NeedsUserChoice default;
//   * a selected member spelling prefights as itself (Importable, proven
//     sd1: identity, canonicalized container path) — never NeedsUserChoice;
//   * registering the member produces a real raster asset; alternate
//     spellings (a symlinked container) collapse onto the same asset;
//   * an ImportSession batch [container, member] refuses the container (the
//     un-guessed choice), registers the member, and a journal-driven resume
//     keeps the member's identity stable.
//
// RED discipline: only base-present APIs are used; new report facts are read
// through toJson() so this file also compiles against the unpatched base
// (where the member spelling was refused path-missing / source-unsupported).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gdal.h>

#include <array>
#include <vector>

#include "data/data_manager.h"
#include "data/import_preflight.h"
#include "data/import_session.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QApplication>
#include <qgsapplication.h>

using namespace sicnu::data;

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_r30_t03_import_selection";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void )application;
  QgsApplication::initQgis();
}

/// A real two-variable NetCDF container (the driver reports one subdataset
/// per variable, plus WRITE_LONLAT's lat/lon grids). Skips the test when the
/// local GDAL build cannot write netCDF.
bool writeNetcdfContainer( const QString &path )
{
  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "netCDF" );
  if ( driver == nullptr )
    return false;
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), 4, 4, 2, GDT_Float32,
                nullptr );
  if ( ds == nullptr )
    return false;
  std::array<double, 6> gt = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
  GDALSetGeoTransform( ds, gt.data() );
  GDALSetProjection( ds, "EPSG:32648" );
  std::vector<float> band1Line( 4 );
  std::vector<float> band2Line( 4 );
  GDALRasterBandH band1 = GDALGetRasterBand( ds, 1 );
  GDALRasterBandH band2 = GDALGetRasterBand( ds, 2 );
  for ( int row = 0; row < 4; ++row )
  {
    for ( int col = 0; col < 4; ++col )
    {
      band1Line[col] = 100.f * row + col;
      band2Line[col] = 1000.f * row + col;
    }
    REQUIRE( GDALRasterIO( band1, GF_Write, 0, row, 4, 1, band1Line.data(),
                           4, 1, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( band2, GF_Write, 0, row, 4, 1, band2Line.data(),
                           4, 1, GDT_Float32, 0, 0 ) == CE_None );
  }
  GDALClose( ds );
  return true;
}

} // namespace

TEST_CASE( "Container preflight reports member spellings and keeps the "
           "un-guessed default",
           "[r30][t03][import-selection]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString container = dir.filePath( QStringLiteral( "cube.nc" ) );
  if ( !writeNetcdfContainer( container ) )
  {
    WARN( "netCDF driver unavailable — container fixture skipped" );
    return;
  }

  DataManager manager;
  const ImportPreflightReport report =
    ImportPreflight::inspect( container, &manager );

  CHECK( report.verdict == ImportPreflightVerdict::NeedsUserChoice );
  CHECK( report.subdatasetCount > 0 );

  // The member spellings are reported VERBATIM through the machine-readable
  // surface (read via toJson so the assertion is base-compilable too).
  const QJsonObject raster =
    report.toJson().value( QStringLiteral( "raster" ) ).toObject();
  const QJsonArray names =
    raster.value( QStringLiteral( "subdataset_names" ) ).toArray();
  REQUIRE( names.size() >= 2 );
  bool sawBand1 = false;
  for ( const auto &value : names )
  {
    const QString spelling = value.toString();
    CAPTURE( spelling.toUtf8().constData() );
    CHECK( spelling.startsWith( QStringLiteral( "NETCDF:\"" ) ) );
    if ( spelling.endsWith( QStringLiteral( ":Band1" ) ) )
      sawBand1 = true;
  }
  CHECK( sawBand1 );
}

TEST_CASE( "A selected member prefights and imports through the same chain",
           "[r30][t03][import-selection]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString container = dir.filePath( QStringLiteral( "cube.nc" ) );
  if ( !writeNetcdfContainer( container ) )
  {
    WARN( "netCDF driver unavailable — container fixture skipped" );
    return;
  }
  const QString member =
    QStringLiteral( "NETCDF:\"%1\":Band1" ).arg( container );

  DataManager manager;

  // Preflight of the member: Importable, proven qualified identity,
  // canonical spelling, raster facts of the member itself.
  const ImportPreflightReport report = ImportPreflight::inspect( member, &manager );
  CHECK( report.verdict == ImportPreflightVerdict::Importable );
  CHECK( report.resourceKind == ImportPreflightResourceKind::Raster );
  CHECK( report.width == 4 );
  CHECK( report.height == 4 );
  CHECK( report.identityToken.startsWith( QStringLiteral( "sd1:v1:" ) ) );
  CHECK( report.canonicalPath == member ); // container path already absolute
  CHECK( !report.alreadyRegistered );

  // Register through the production pipeline (no provider-key guessing —
  // the same shape ImportSession::proceed uses).
  RegisterRequest request;
  request.source.canonicalSource = member;
  const RegisterResult registered = manager.registerSource( request );
  REQUIRE( !registered.assetId.isNull() );

  const std::optional<AssetSnapshot> snapshot =
    manager.asset( registered.assetId );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->state() == AssetState::Ready );
  CHECK( snapshot->kind() == AssetKind::Raster );
  const auto *structure =
    std::get_if<RasterStructure>( &snapshot->structure() );
  REQUIRE( structure != nullptr );
  CHECK( structure->bandCount == 1 );
  CHECK( structure->width == 4 );

  // The member's own bytes are readable through the registered spelling.
  ensureGdalInit();
  GDALDatasetH ds =
    GDALOpen( snapshot->source().canonicalSource.toUtf8().constData(),
              GA_ReadOnly );
  REQUIRE( ds != nullptr );
  float value = 0.f;
  REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Read, 2, 3, 1, 1,
                         &value, 1, 1, GDT_Float32, 0, 0 ) == CE_None );
  CHECK( value == Catch::Approx( 100.f * 3 + 2 ) );
  GDALClose( ds );

  // An alternate spelling of the SAME member (a symlinked container
  // canonicalizes onto the same identity) dedups onto the same asset.
  const QString link = dir.filePath( QStringLiteral( "link.nc" ) );
  REQUIRE( QFile::link( container, link ) );
  const QString alias =
    QStringLiteral( "NETCDF:\"%1\":Band1" ).arg( link );
  const ImportPreflightReport aliasReport =
    ImportPreflight::inspect( alias, &manager );
  CHECK( aliasReport.alreadyRegistered );
  CHECK( aliasReport.existingAssetId == registered.assetId.toString() );

  const RegisterResult aliasRegistered = [&manager, &alias]() {
    RegisterRequest request;
    request.source.canonicalSource = alias;
    return manager.registerSource( request );
  }();
  REQUIRE( !aliasRegistered.assetId.isNull() );
  CHECK( aliasRegistered.assetId == registered.assetId );
  CHECK( aliasRegistered.reusedExisting );

  // A missing container is typed, not guessed.
  const ImportPreflightReport missing = ImportPreflight::inspect(
    QStringLiteral( "NETCDF:\"%1/absent.nc\":Band1" ).arg( dir.path() ),
    &manager );
  CHECK( missing.verdict == ImportPreflightVerdict::Refused );
}

TEST_CASE( "An import session carries an explicit member selection through "
           "cancel and journal resume",
           "[r30][t03][import-selection]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString container = dir.filePath( QStringLiteral( "cube.nc" ) );
  if ( !writeNetcdfContainer( container ) )
  {
    WARN( "netCDF driver unavailable — container fixture skipped" );
    return;
  }
  const QString member1 =
    QStringLiteral( "NETCDF:\"%1\":Band1" ).arg( container );
  const QString member2 =
    QStringLiteral( "NETCDF:\"%1\":Band2" ).arg( container );

  DataManager manager;
  ImportSession::Options options;
  options.journalPath = dir.filePath( QStringLiteral( "session.json" ) );
  ImportSession session( &manager, { container, member1, member2 }, options );

  // 检查: the container is refused (NeedsUserChoice — never guessed); the
  // two explicit member selections are admissible.
  session.check();
  REQUIRE( session.state() == ImportSessionState::ReadyToImport );
  const ImportSessionSummary summary = session.summary();
  REQUIRE( summary.refusedCount() == 1 );
  REQUIRE( summary.pendingCount() == 2 );

  // 继续导入 (stepwise) → cancel mid-batch: exactly the first member
  // registers; the journal keeps it durably and the session lands Cancelled.
  REQUIRE( session.proceedSteps( 1 ) == 1 );
  session.requestCancel();
  session.proceedSteps( 1 ); // observes the cancel at the boundary
  REQUIRE( session.state() == ImportSessionState::Cancelled );
  const ImportSessionSummary after = session.summary();
  CHECK( after.journalDurable );
  CHECK( after.registeredCount() == 1 );
  REQUIRE( manager.findByPath( member1 ) != std::nullopt );
  CHECK( manager.findByPath( member2 ) == std::nullopt );

  // A FRESH session over the same journal resumes: already-registered
  // members are skipped with the SAME identity (no double registration) and
  // the remaining member completes the batch.
  ImportSession resumed( &manager, { container, member1, member2 }, options );
  resumed.resume();
  REQUIRE( resumed.state() == ImportSessionState::Completed );
  const ImportSessionSummary resumedSummary = resumed.summary();
  CHECK( resumedSummary.registeredCount() == 2 );

  for ( const ImportSessionItem &item : resumedSummary.items )
  {
    if ( item.state == ImportSessionItemState::Registered
         || item.state == ImportSessionItemState::Skipped )
    {
      const std::optional<AssetSnapshot> asset =
        manager.findByPath( item.path );
      INFO( "journal item path not registered under its own identity: "
            << item.path.toUtf8().constData() );
      CHECK( asset.has_value() );
    }
  }

  // Both members live in the catalog as distinct assets of one container.
  REQUIRE( manager.findByPath( member1 ) != std::nullopt );
  REQUIRE( manager.findByPath( member2 ) != std::nullopt );
  CHECK( manager.findByPath( member1 )->id()
         != manager.findByPath( member2 )->id() );
}
