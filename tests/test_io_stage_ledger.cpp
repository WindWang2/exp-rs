/***************************************************************************
  tests/test_io_stage_ledger.cpp — dataset staging ledger, attach-existing,
  orphan sweep. Crash scenarios are simulated honestly: a journal without a
  staged file, a truncated staged file, a spoofed (driver-swapped) staged
  file. Publication goes through the real atomic_fs group publish.
 ***************************************************************************/

#include "geospatial/io/stage_ledger.h"
#include "geospatial/common.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

using namespace sicnu::geo;
using namespace sicnu::geo::io;

namespace
{

void ensureGdal()
{
  static std::once_flag once;
  std::call_once( once, [] { GDALAllRegister(); } );
}

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_stage_ledger" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

/// Creates a REAL staged-shaped GTiff ("<stem>.<n>.<n>.tmp.tif") with 8x8
/// pixels so attach validation opens actual data, not a stub.
std::string makeStagedRaster( const std::string &dir, const std::string &stagedName )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / stagedName ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 8, 8, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
  unsigned char pixels[64];
  for ( int i = 0; i < 64; ++i )
    pixels[i] = static_cast<unsigned char>( i % 200 );
  REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 8, 8, pixels, 8, 8, GDT_Byte, 0, 0 ) == CE_None );
  GDALClose( dataset );
  return path;
}

StageRecord journal( const std::string &dir, const std::string &finalName, const std::string &stagedPath )
{
  StageRecord record;
  record.runId = "run-" + finalName;
  record.producer = "test-harness";
  record.finalPath = ( fs::path( dir ) / finalName ).string();
  record.stagedPath = stagedPath;
  record.driver = "GTiff";
  record.width = 8;
  record.height = 8;
  record.bandCount = 1;
  return record;
}

std::string issueCodes( const AttachCheck &check )
{
  std::string codes;
  for ( const AttachIssue &issue : check.issues )
    codes += issue.code + ",";
  return codes;
}

} // namespace

TEST_CASE( "stage ledger roundtrips a journaled transaction", "[io][ledger]" )
{
  const std::string dir = scratch( "roundtrip" );
  const std::string staged = makeStagedRaster( dir, "out.7.42.tmp.tif" );
  const StageRecord record = journal( dir, "out.tif", staged );
  recordStaged( record );

  // Ledger file lives next to the TARGET, state forced to "staged".
  REQUIRE( atomic_fs::fileExists( stageLedgerPath( record.finalPath ) ) );
  const StageRecord readBack = readStageLedger( record.finalPath );
  CHECK( readBack.runId == record.runId );
  CHECK( readBack.state == "staged" );
  CHECK( readBack.stagedPath == staged );
  CHECK( readBack.width == 8 );
  CHECK( !readBack.updatedAtUtc.empty() );

  removeStageLedger( record.finalPath );
  CHECK( !atomic_fs::fileExists( stageLedgerPath( record.finalPath ) ) );
  CHECK_THROWS_AS( readStageLedger( record.finalPath ), GeoError );
}

TEST_CASE( "recordStaged refuses empty identity fields", "[io][ledger][negative]" )
{
  const std::string dir = scratch( "refuse" );
  StageRecord record = journal( dir, "out.tif", dir + "/out.1.2.tmp.tif" );
  record.runId.clear();
  REQUIRE_THROWS_AS( recordStaged( record ), GeoError );
  record.runId = "run";
  record.stagedPath.clear();
  REQUIRE_THROWS_AS( recordStaged( record ), GeoError );
}

TEST_CASE( "attachExisting validates and finalizeAttached publishes + verifies", "[io][ledger][attach]" )
{
  const std::string dir = scratch( "attach" );
  const std::string staged = makeStagedRaster( dir, "out.3.14.tmp.tif" );
  const StageRecord record = journal( dir, "out.tif", staged );
  recordStaged( record );

  const AttachCheck check = attachExisting( record.finalPath );
  INFO( check.toJson().toStyledString() );
  REQUIRE( check.attachable );
  CHECK( check.issues.empty() );

  FinalizeManifestFields fields;
  fields.producer = "test-harness";
  fields.driver = "GTiff";
  fields.width = 8;
  fields.height = 8;
  fields.bandCount = 1;
  fields.dtype = "Byte";
  finalizeAttached( record.finalPath, &fields );

  // Published: main file + manifest sidecar (sidecar-first group publish),
  // staging consumed, ledger says "finalized", verifier green.
  CHECK( atomic_fs::fileExists( record.finalPath ) );
  CHECK( atomic_fs::fileExists( record.finalPath + ".sicnu-manifest.json" ) );
  CHECK( !atomic_fs::fileExists( staged ) );
  CHECK( readStageLedger( record.finalPath ).state == "finalized" );

  const ManifestVerifyReport report = verifyDataset( record.finalPath );
  INFO( report.toJson().toStyledString() );
  CHECK( report.verified );

  // A finalized transaction cannot be finalized again nor discarded.
  CHECK_FALSE( attachExisting( record.finalPath ).attachable );
  REQUIRE_THROWS_AS( discardAttached( record.finalPath ), GeoError );
}

TEST_CASE( "attachExisting fails closed on every crash shape", "[io][ledger][negative]" )
{
  SECTION( "no journal at all" )
  {
    const std::string dir = scratch( "nojournal" );
    const AttachCheck check = attachExisting( ( fs::path( dir ) / "out.tif" ).string() );
    CHECK( !check.attachable );
    CHECK( issueCodes( check ).find( "ledger_missing" ) != std::string::npos );
  }

  SECTION( "journaled but staged file vanished (real crash + manual cleanup)" )
  {
    const std::string dir = scratch( "vanished" );
    const StageRecord record = journal( dir, "out.tif", dir + "/out.1.2.tmp.tif" );
    recordStaged( record ); // staged file never created
    const AttachCheck check = attachExisting( record.finalPath );
    CHECK( !check.attachable );
    CHECK( issueCodes( check ).find( "staged_missing" ) != std::string::npos );
  }

  SECTION( "truncated staged file cannot open" )
  {
    const std::string dir = scratch( "truncated" );
    const std::string staged = makeStagedRaster( dir, "out.5.6.tmp.tif" );
    { std::ofstream out( staged, std::ios::binary | std::ios::trunc ); out << "II*"; } // TIFF magic, no body
    const StageRecord record = journal( dir, "out.tif", staged );
    recordStaged( record );
    const AttachCheck check = attachExisting( record.finalPath );
    CHECK( !check.attachable );
    CHECK( issueCodes( check ).find( "staged_unopenable" ) != std::string::npos );
  }

  SECTION( "spoofed staged file (driver differs from declaration)" )
  {
    const std::string dir = scratch( "spoofed" );
    // Journal claims GTiff; the staged file is a plain byte file GDAL cannot
    // open as GTiff → unopenable. For a driver MISMATCH we journal a GeoJSON
    // file as GPKG: GeoJSON opens (not as GPKG) → driver_mismatch.
    const std::string staged = dir + "/out.9.9.tmp.gpkg";
    {
      std::ofstream out( staged );
      out << R"json({"type":"FeatureCollection","features":[]})json";
    }
    StageRecord record = journal( dir, "out.tif", staged );
    record.driver = "GPKG";
    record.width = 0;
    record.height = 0;
    record.bandCount = 0;
    recordStaged( record );
    const AttachCheck check = attachExisting( record.finalPath );
    INFO( check.toJson().toStyledString() );
    CHECK( !check.attachable );
    // GeoJSON is a core OGR driver: the file OPENS, but as GeoJSON — the
    // declared-vs-opened driver comparison is what must catch the spoof.
    CHECK( issueCodes( check ).find( "driver_mismatch" ) != std::string::npos );
  }

  SECTION( "shape drift between journal and staged dataset" )
  {
    const std::string dir = scratch( "shapedrift" );
    const std::string staged = makeStagedRaster( dir, "out.2.2.tmp.tif" );
    StageRecord record = journal( dir, "out.tif", staged );
    record.width = 64; // lie
    recordStaged( record );
    const AttachCheck check = attachExisting( record.finalPath );
    CHECK( !check.attachable );
    CHECK( issueCodes( check ).find( "shape_mismatch" ) != std::string::npos );
  }

  SECTION( "finalizeAttached refuses when attach fails" )
  {
    const std::string dir = scratch( "refuse-finalize" );
    const StageRecord record = journal( dir, "out.tif", dir + "/gone.1.2.tmp.tif" );
    recordStaged( record );
    REQUIRE_THROWS_AS( finalizeAttached( record.finalPath, nullptr ), GeoError );
    CHECK( readStageLedger( record.finalPath ).state == "staged" );
  }
}

TEST_CASE( "discardAttached removes staging and journal", "[io][ledger][discard]" )
{
  const std::string dir = scratch( "discard" );
  const std::string staged = makeStagedRaster( dir, "out.8.8.tmp.tif" );
  const StageRecord record = journal( dir, "out.tif", staged );
  recordStaged( record );

  discardAttached( record.finalPath );
  CHECK( !atomic_fs::fileExists( staged ) );
  CHECK( !atomic_fs::fileExists( stageLedgerPath( record.finalPath ) ) );
  CHECK( !fs::exists( fs::u8path( record.finalPath ) ) ); // target never appeared

  // Discarding twice is quiet.
  discardAttached( record.finalPath );
}

TEST_CASE( "sweepOrphans finds, reports and (opt-in) removes staging leftovers", "[io][ledger][sweep]" )
{
  SECTION( "dry run reports without deleting; a live transaction is reported, not condemned" )
  {
    const std::string dir = scratch( "sweep-dry" );
    const std::string staged = makeStagedRaster( dir, "out.11.22.tmp.tif" );
    const StageRecord record = journal( dir, "out.tif", staged );
    recordStaged( record );
    // A stale ledger: journal whose staged file is gone.
    const StageRecord stale = journal( dir, "older.tif", dir + "/older.3.4.tmp.tif" );
    recordStaged( stale );

    const std::vector<StrayStaging> strays = sweepOrphans( dir, /*remove=*/false );
    INFO( "count=" << strays.size() );
    // staged main + LIVE ledger (staged file alive) + stale ledger.
    CHECK( strays.size() == 3 );
    bool sawStagedFile = false;
    bool sawLive = false;
    bool sawStaleLedger = false;
    for ( const StrayStaging &stray : strays )
    {
      sawStagedFile |= stray.kind == "staged_file";
      sawLive |= stray.kind == "live_staged";
      sawStaleLedger |= stray.kind == "stale_ledger";
      CHECK( !stray.removed );
      CHECK( stray.display.find( dir ) != std::string::npos ); // local paths display as themselves
    }
    CHECK( sawStagedFile );
    CHECK( sawLive );
    CHECK( sawStaleLedger );
    // Nothing deleted.
    CHECK( atomic_fs::fileExists( staged ) );
    CHECK( atomic_fs::fileExists( stageLedgerPath( record.finalPath ) ) );
  }

  SECTION( "remove=true consumes the leftovers but never a live transaction" )
  {
    const std::string dir = scratch( "sweep-remove" );
    const std::string staged = makeStagedRaster( dir, "out.11.22.tmp.tif" );
    const StageRecord record = journal( dir, "out.tif", staged );
    recordStaged( record );

    const std::vector<StrayStaging> strays = sweepOrphans( dir, /*remove=*/true );
    // staged main + its ledger (live: the staged file still exists) — an
    // ATTACHABLE transaction is never swept, review F6.
    REQUIRE( strays.size() == 2 );
    for ( const StrayStaging &stray : strays )
      CHECK_FALSE( stray.removed ); // claimed by a live ledger on both counts
    CHECK( atomic_fs::fileExists( staged ) );
    CHECK( atomic_fs::fileExists( stageLedgerPath( record.finalPath ) ) );
    CHECK( readStageLedger( record.finalPath ).state == "staged" );
    // ... and the transaction is still attachable after the sweep.
    CHECK( attachExisting( record.finalPath ).attachable );

    // Once the staged file disappears (externally), the ledger becomes
    // genuinely stale and remove=true consumes it.
    atomic_fs::removeFileQuiet( staged );
    const std::vector<StrayStaging> second = sweepOrphans( dir, /*remove=*/true );
    REQUIRE( second.size() == 1 );
    CHECK( second.front().kind == "stale_ledger" );
    CHECK( second.front().removed );
    CHECK( !atomic_fs::fileExists( stageLedgerPath( record.finalPath ) ) );
  }

  SECTION( "healthy published dataset + manifest is never swept" )
  {
    const std::string dir = scratch( "sweep-healthy" );
    const std::string staged = makeStagedRaster( dir, "good.1.1.tmp.tif" );
    const StageRecord record = journal( dir, "good.tif", staged );
    recordStaged( record );
    FinalizeManifestFields fields;
    fields.producer = "test-harness";
    fields.driver = "GTiff";
    fields.width = 8;
    fields.height = 8;
    fields.bandCount = 1;
    finalizeAttached( record.finalPath, &fields );

    CHECK( sweepOrphans( dir, /*remove=*/true ).empty() );
    CHECK( atomic_fs::fileExists( record.finalPath ) );
    CHECK( atomic_fs::fileExists( record.finalPath + ".sicnu-manifest.json" ) );
  }
}

TEST_CASE( "recordStaged refuses a staged path outside the target directory", "[io][ledger][negative]" )
{
  const std::string dir = scratch( "foreign-dir" );
  const std::string otherDir = scratch( "foreign-other" );
  const std::string staged = makeStagedRaster( otherDir, "out.1.2.tmp.tif" );
  StageRecord record = journal( dir, "out.tif", staged ); // staged lives in another directory
  try
  {
    recordStaged( record );
    FAIL( "a foreign-directory staged path must be refused" );
  }
  catch ( const GeoError &error )
  {
    CHECK( error.code() == ErrorCode::InvalidArgument );
  }
  // The same transaction with the staged file next to the target is legal.
  record.stagedPath = dir + "/out.1.2.tmp.tif";
  recordStaged( record );
  CHECK( atomic_fs::fileExists( stageLedgerPath( record.finalPath ) ) );
}

TEST_CASE( "staging ledger tolerates Unicode target names", "[io][ledger][unicode]" )
{
  const std::string dir = scratch( "都市_é" );
  const std::string staged = makeStagedRaster( dir, "结果.1.2.tmp.tif" );
  const StageRecord record = journal( dir, "结果.tif", staged );
  recordStaged( record );
  const AttachCheck check = attachExisting( record.finalPath );
  INFO( check.toJson().toStyledString() );
  CHECK( check.attachable );
  discardAttached( record.finalPath );
  CHECK( !atomic_fs::fileExists( staged ) );
}
