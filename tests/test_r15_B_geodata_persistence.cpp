/***************************************************************************
  tests/test_r15_B_geodata_persistence.cpp
  R15-B — spatial data chain, dataset/experiment stores and recoverable
  persistence: RED->GREEN oracles for the defects reproduced in this track
  (.planning/r15-b/ORACLES.md) plus candidate regression oracles for
  residual review threads of #1481 / #1505 that are already fixed on master.
 ***************************************************************************/

#include "geospatial/common.h"
#include "geospatial/cog/cog_validator.h"
#include "geospatial/fabric/catalog_service.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/remote_identity_resolver.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"
#include "support/http_range_server.h"
#include "support/http_stac_server.h"

#include "dataset/dataset_store.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/reproduction_bundle.h"
#include "experiment/reproduction_bundle_import.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace sicnu::geo;

namespace
{

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_r15b" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

void writeText( const std::string &path, const std::string &text )
{
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  out << text;
}

std::string readText( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void writeTinyRaster( const std::string &path, double value )
{
  RasterWriter writer = RasterWriter::create( path, 4, 4, { RasterBandSpec{} }, { "GTiff", {}, true } );
  std::vector<double> values( 16, value );
  writer.writeWindow( 1, { 0, 0, 4, 4 }, values.data() );
  writer.finalize();
}

void writeTinyShapefile( const std::string &path, const std::string &name )
{
  VectorWriter writer = VectorWriter::create( path, "pts", "Point", { { "name", "String", 16 } },
                                              Crs::fromAuthid( "EPSG:4326" ),
                                              { "ESRI Shapefile", {}, false, "" } );
  Json::Value attrs( Json::objectValue );
  attrs["name"] = name;
  writer.writeFeature( attrs, "POINT (1 2)" );
  writer.finalize();
}

/// Writes the uncommitted group-publish journal exactly as
/// atomic_fs::publishStagedGroup persists it before touching any target
/// (version 1: main + members{staged,target,backup,had_target}).
void writeUncommittedJournal( const std::string &mainTarget,
                              const std::vector<std::pair<std::string, bool>> &targets )
{
  Json::Value root;
  root["version"] = 1;
  root["main"] = mainTarget;
  Json::Value members( Json::arrayValue );
  for ( const auto &t : targets )
  {
    Json::Value m;
    m["staged"] = t.first + ".staged-gone";
    m["target"] = t.first;
    m["backup"] = t.first + ".bak";
    m["had_target"] = t.second;
    members.append( m );
  }
  root["members"] = members;
  Json::StreamWriterBuilder b;
  b["indentation"] = "";
  writeText( mainTarget + ".publish-journal", Json::writeString( b, root ) );
}

} // namespace

// ---------------------------------------------------------------------------
// B13 — crash-consistent group publish is visible to readers
// ---------------------------------------------------------------------------

TEST_CASE( "B13: RasterReader refuses a group left mid-publish by a crash",
           "[r15b][b13][atomic][raster]" )
{
  const std::string dir = scratch( "b13_raster" );
  const std::string target = ( fs::path( dir ) / "scene.tif" ).string();
  const std::string aux = target + ".aux.xml";
  writeTinyRaster( target, 1.0 );
  writeText( aux, "<PAMDataset><Metadata><MDI key=\"GEN\">old</MDI></Metadata></PAMDataset>" );

  // Simulated kill -9 between the sidecar rename and the main rename:
  // journal durable, sidecar backed up and replaced, main still old.
  writeUncommittedJournal( target, { { aux, true }, { target, true } } );
  fs::copy_file( aux, aux + ".bak" );
  writeText( aux, "<PAMDataset><Metadata><MDI key=\"GEN\">new</MDI></Metadata></PAMDataset>" );
  REQUIRE( atomic_fs::hasPendingJournal( target ) );

  // Oracle: a mixed old-main/new-sidecar group is never served as a dataset.
  bool refused = false;
  try
  {
    RasterReader reader = RasterReader::open( target );
  }
  catch ( const GeoError &error )
  {
    refused = true;
    CHECK( error.code() == ErrorCode::IoError );
    CHECK( std::string( error.what() ).find( "interrupted mid-publish" ) != std::string::npos );
  }
  CHECK( refused );
  // Refusal has no side effects: the evidence for recovery stays intact.
  CHECK( atomic_fs::hasPendingJournal( target ) );
  CHECK( fs::exists( aux + ".bak" ) );

  // Explicit recovery restores the complete OLD group, then reads succeed.
  CHECK( atomic_fs::recoverDatasetGroup( target ) );
  CHECK_FALSE( atomic_fs::hasPendingJournal( target ) );
  CHECK( readText( aux ).find( ">old<" ) != std::string::npos );
  RasterReader reader = RasterReader::open( target );
  CHECK( reader.metadata().width == 4 );
}

TEST_CASE( "B13: a committed marker means the new group is complete and readable",
           "[r15b][b13][atomic][raster]" )
{
  const std::string dir = scratch( "b13_committed" );
  const std::string target = ( fs::path( dir ) / "scene.tif" ).string();
  writeTinyRaster( target, 2.0 );
  // Crash after the commit rename but before backup sweep.
  writeUncommittedJournal( target, { { target, true } } );
  fs::rename( target + ".publish-journal", target + ".publish-journal.committed" );
  fs::copy_file( target, target + ".bak" );
  RasterReader reader = RasterReader::open( target );
  CHECK( reader.metadata().width == 4 );
}

TEST_CASE( "B13: VectorReader refuses a shapefile whose .dbf is from another generation",
           "[r15b][b13][atomic][vector]" )
{
  const std::string dir = scratch( "b13_vector" );
  const std::string target = ( fs::path( dir ) / "pts.shp" ).string();
  const std::string dbf = ( fs::path( dir ) / "pts.dbf" ).string();
  writeTinyShapefile( target, "old" );
  const std::string otherDir = scratch( "b13_vector_new" );
  const std::string newer = ( fs::path( otherDir ) / "pts.shp" ).string();
  writeTinyShapefile( newer, "new" );

  writeUncommittedJournal( target, { { dbf, true }, { target, true } } );
  fs::copy_file( dbf, dbf + ".bak" );
  fs::copy_file( ( fs::path( otherDir ) / "pts.dbf" ), dbf, fs::copy_options::overwrite_existing );

  CHECK_THROWS_AS( VectorReader::open( target ), GeoError );

  REQUIRE( atomic_fs::recoverDatasetGroup( target ) );
  VectorReader reader = VectorReader::open( target );
  CHECK( reader.exactFeatureCount() == 1 );
}

#ifndef _WIN32
TEST_CASE( "B13: kill -9 at random points of group publish never leaves a mixed generation",
           "[r15b][b13][atomic][kill9]" )
{
  const std::string dir = scratch( "b13_kill9" );
  const std::string target = ( fs::path( dir ) / "g.tif" ).string();
  const std::string aux = target + ".aux.xml";
  const std::string tfw = target + ".tfw";
  writeText( target, "gen-0" );
  writeText( aux, "gen-0" );
  writeText( tfw, "gen-0" );

  std::mt19937 rng( 1505u ); // fixed seed: reproducible kill schedule
  std::uniform_int_distribution<int> delayUs( 50, 4000 );
  int pendingObserved = 0;
  for ( int round = 0; round < 40; ++round )
  {
    const pid_t child = ::fork();
    REQUIRE( child >= 0 );
    if ( child == 0 )
    {
      // Child: publish generation after generation until killed.
      for ( int gen = 1;; ++gen )
      {
        try
        {
          const std::string staged = atomic_fs::stagedPathFor( target );
          const std::string text = "gen-" + std::to_string( round ) + "-" + std::to_string( gen );
          writeText( staged, text );
          writeText( staged + ".aux.xml", text );
          writeText( staged + ".tfw", text );
          atomic_fs::publishStagedGroup( staged, target );
        }
        catch ( ... )
        {
          ::_exit( 3 );
        }
      }
    }
    std::this_thread::sleep_for( std::chrono::microseconds( delayUs( rng ) ) );
    ::kill( child, SIGKILL );
    int status = 0;
    ::waitpid( child, &status, 0 );
    REQUIRE( WIFSIGNALED( status ) ); // the child never failed on its own

    if ( atomic_fs::hasPendingJournal( target ) )
      ++pendingObserved;
    if ( atomic_fs::hasUncommittedPublishJournal( target ) )
    {
      // Never served mixed: the reader gate refuses BEFORE GDAL dispatch
      // (these members are text, so only the gate's message proves it).
      std::string why;
      try { RasterReader::open( target ); } catch ( const GeoError &e ) { why = e.what(); }
      CHECK( why.find( "interrupted mid-publish" ) != std::string::npos );
    }
    atomic_fs::recoverDatasetGroup( target );
    REQUIRE_FALSE( atomic_fs::hasPendingJournal( target ) );
    const std::string m = readText( target );
    INFO( "round " << round << " main=" << m << " aux=" << readText( aux ) << " tfw=" << readText( tfw ) );
    REQUIRE( !m.empty() );
    CHECK( readText( aux ) == m );
    CHECK( readText( tfw ) == m );
  }
  // Leftover staging from killed writers is reclaimable garbage, never a
  // published member; record how often the kill landed inside a publish.
  WARN( "kill landed inside an uncommitted/committed publish window " << pendingObserved << "/40 times" );
}
#endif

// ---------------------------------------------------------------------------
// B05 — catalog pagination never loops or duplicates
// ---------------------------------------------------------------------------

namespace
{
Json::Value stacFeature( const std::string &id )
{
  Json::Value f( Json::objectValue );
  f["type"] = "Feature";
  f["stac_version"] = "1.0.0";
  f["id"] = id;
  f["properties"]["datetime"] = "2026-08-01T10:00:00Z";
  f["geometry"]["type"] = "Point";
  f["geometry"]["coordinates"].append( 10.5 );
  f["geometry"]["coordinates"].append( 40.5 );
  f["bbox"].append( 10.0 );
  f["bbox"].append( 40.0 );
  f["bbox"].append( 11.0 );
  f["bbox"].append( 41.0 );
  f["assets"]["image"]["href"] = "https://data.example.com/" + id + ".tif";
  f["assets"]["image"]["type"] = "image/tiff";
  f["assets"]["image"]["roles"].append( "data" );
  return f;
}

std::string stacPage( const std::vector<std::string> &ids, const std::string &nextHref )
{
  Json::Value root( Json::objectValue );
  root["type"] = "FeatureCollection";
  root["features"] = Json::Value( Json::arrayValue );
  for ( const auto &id : ids )
    root["features"].append( stacFeature( id ) );
  root["links"] = Json::Value( Json::arrayValue );
  if ( !nextHref.empty() )
  {
    Json::Value link;
    link["rel"] = "next";
    link["href"] = nextHref;
    link["method"] = "GET";
    root["links"].append( link );
  }
  Json::StreamWriterBuilder b;
  b["indentation"] = "";
  return Json::writeString( b, root );
}
} // namespace

TEST_CASE( "B05: CatalogService::searchAll detects a repeating rel=next instead of duplicating",
           "[r15b][b05][catalog][pagination]" )
{
  testsupport::HttpStacServer server;
  REQUIRE( server.port() > 0 );
  testsupport::StacRoute first;
  first.body = stacPage( { "a" }, server.url() + "/search?page=2" );
  server.setRoute( "/search", first );
  testsupport::StacRoute loop;
  loop.body = stacPage( { "b" }, server.url() + "/search?page=2" ); // next points at itself
  server.setRoute( "/search?page=2", loop );

  CatalogServiceOptions opts;
  opts.stac.blockPrivateNetworks = false; // loopback fixture
  const CatalogService service = openCatalogService( server.url(), opts );
  CatalogQuery query;
  query.maxItems = 50;

  bool loopRefused = false;
  std::size_t returned = 0;
  std::set<std::string> ids;
  try
  {
    const auto result = service.searchAll( query );
    returned = result.records.size();
    for ( const auto &r : result.records )
      ids.insert( r.id );
  }
  catch ( const GeoError &error )
  {
    loopRefused = true;
    CHECK( error.code() == ErrorCode::InvalidMetadata );
  }
  INFO( "returned=" << returned << " unique=" << ids.size() << " requests=" << server.requests().size() );
  CHECK( loopRefused );
  CHECK( ids.size() == returned ); // never a duplicated record
  CHECK( server.requests().size() <= 3 );
}

TEST_CASE( "B05: distinct pages still paginate to completion with exact counts",
           "[r15b][b05][catalog][pagination]" )
{
  testsupport::HttpStacServer server;
  testsupport::StacRoute p1;
  p1.body = stacPage( { "a", "b" }, server.url() + "/search?page=2" );
  server.setRoute( "/search", p1 );
  testsupport::StacRoute p2;
  p2.body = stacPage( { "c", "d" }, server.url() + "/search?page=3" );
  server.setRoute( "/search?page=2", p2 );
  testsupport::StacRoute p3;
  p3.body = stacPage( { "e" }, "" );
  server.setRoute( "/search?page=3", p3 );

  CatalogServiceOptions opts;
  opts.stac.blockPrivateNetworks = false;
  const CatalogService service = openCatalogService( server.url(), opts );
  CatalogQuery query;
  query.maxItems = 10;
  const auto all = service.searchAll( query );
  REQUIRE( all.records.size() == 5 );
  CHECK_FALSE( all.truncatedByCap );
  query.maxItems = 3;
  const auto capped = service.searchAll( query );
  CHECK( capped.records.size() == 3 );
  CHECK( capped.truncatedByCap );
}

// ---------------------------------------------------------------------------
// B04/B15 — remote identity resolver fails closed (empty token), never throws
// ---------------------------------------------------------------------------

TEST_CASE( "B04: remote identity resolver answers a refused origin with an empty token",
           "[r15b][b04][remote][identity]" )
{
  // The resolver contract (remote_identity_resolver.h): refusal/offline/
  // inconclusive => EMPTY token (uncacheable). Its callers (processing
  // fingerprint collection) do not catch, so a thrown SSRF refusal would
  // escape the submission path instead of degrading to "uncacheable".
  testsupport::HttpRangeServer server( std::vector<unsigned char>( 512, 7 ) );
  server.setEtag( "\"r15b-strong\"" );
  auto resolver = makeRemoteInputIdentityResolver();
  std::string token = "unset";
  REQUIRE_NOTHROW( token = resolver( server.url() + "/r15b.tif" ) );
  CHECK( token.empty() ); // loopback is private: refused => no identity
  // No outbound request reached the private origin.
  CHECK( server.requestCount() == 0 );
}

// ---------------------------------------------------------------------------
// B12 — reproduction bundle import never relocates unverified/escaping data
// ---------------------------------------------------------------------------

namespace
{
using namespace sicnu::dataset;
using namespace sicnu::experiment;

QString exportBundle( QTemporaryDir &dir, DatasetStore &datasets, ExperimentStore &experiments )
{
  REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
  REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
  const DatasetId datasetId = DatasetId::generate();
  REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() );
  DatasetManifest manifest;
  manifest.setDatasetId( datasetId.toString() );
  manifest.setVersionId( DatasetVersionId::generate().toString() );
  const auto draft = datasets.createDraftVersion( manifest );
  REQUIRE( draft.has_value() );
  const DatasetVersionId versionId =
    DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} );
  REQUIRE( datasets.stageVersion( versionId ).has_value() );
  const auto committed = datasets.commitVersion( versionId );
  REQUIRE( committed.has_value() );

  Experiment experiment;
  experiment.setExperimentId( QStringLiteral( "exp-src" ) );
  experiment.setName( QStringLiteral( "source" ) );
  REQUIRE( experiments.upsertExperiment( experiment ).has_value() );
  ExperimentRun run;
  run.setRunId( QStringLiteral( "run-src" ) );
  run.setExperimentId( QStringLiteral( "exp-src" ) );
  run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
  run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
  run.setDatasetVersionId( committed->versionId() );
  run.setDatasetFingerprint( committed->fingerprint() );
  run.setSeed( 42 );
  run.setStatus( RunStatus::Created );
  REQUIRE( experiments.upsertRun( run ).has_value() );

  ReproductionBundleExporter exporter( experiments, datasets );
  ReproductionBundleOptions exportOptions;
  exportOptions.outputDir = dir.filePath( QStringLiteral( "bundle" ) );
  exportOptions.currentSoftwareRevision = QStringLiteral( "r15b" );
  const auto exportReport = exporter.exportRun( QStringLiteral( "run-src" ), exportOptions );
  REQUIRE( exportReport.ok );
  return exportReport.bundlePath;
}
} // namespace

TEST_CASE( "B12: bundle import never relocates symlinked or checksum-uncovered data/ payloads",
           "[r15b][b12][repro][import][security]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  DatasetStore datasets;
  ExperimentStore experiments;
  const QString bundle = exportBundle( dir, datasets, experiments );

  // Secret outside the bundle that a hostile bundle tries to exfiltrate.
  const QString secret = dir.filePath( QStringLiteral( "outside_secret.txt" ) );
  {
    QFile f( secret );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "TOP-SECRET" );
  }
  const QDir bundleDir( bundle );
  REQUIRE( bundleDir.mkpath( QStringLiteral( "data" ) ) );
  REQUIRE( QFile::link( secret, bundleDir.filePath( QStringLiteral( "data/escape.txt" ) ) ) );
  {
    QFile f( bundleDir.filePath( QStringLiteral( "data/unlisted.bin" ) ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "not covered by checksums.txt" );
  }

  // A payload the integrity gate DOES verify (listed + hashed) still relocates.
  {
    const QByteArray legit( "verified payload" );
    QFile f( bundleDir.filePath( QStringLiteral( "data/legit.bin" ) ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( legit );
    f.close();
    QFile sums( bundleDir.filePath( QStringLiteral( "checksums.txt" ) ) );
    REQUIRE( sums.open( QIODevice::WriteOnly | QIODevice::Append ) );
    sums.write( QCryptographicHash::hash( legit, QCryptographicHash::Sha256 ).toHex() + "  data/legit.bin\n" );
  }

  ExperimentStore target;
  REQUIRE( target.open( dir.filePath( QStringLiteral( "target.db" ) ) ) );
  Experiment home;
  home.setExperimentId( QStringLiteral( "exp-dst" ) );
  home.setName( QStringLiteral( "destination" ) );
  REQUIRE( target.upsertExperiment( home ).has_value() );

  ReproductionBundleImporter importer( target );
  ReproductionBundleImportOptions options;
  options.bundleDir = bundle;
  options.targetExperimentId = QStringLiteral( "exp-dst" );
  options.relocateDataDir = dir.filePath( QStringLiteral( "relocated" ) );
  const auto report = importer.importRun( options );
  INFO( report.warnings.join( QStringLiteral( " | " ) ).toStdString() );

  const QDir relocated( options.relocateDataDir );
  CHECK_FALSE( relocated.exists( QStringLiteral( "escape.txt" ) ) );   // symlink escape refused
  CHECK_FALSE( relocated.exists( QStringLiteral( "unlisted.bin" ) ) ); // unverified bytes refused
  CHECK( report.ok );
  CHECK( relocated.exists( QStringLiteral( "legit.bin" ) ) );
  CHECK( report.relocatedFiles == 1 );
  bool warned = false;
  for ( const QString &w : report.warnings )
    warned = warned || w.contains( QStringLiteral( "escape.txt" ) );
  CHECK( warned );
}

// ---------------------------------------------------------------------------
// B16 — candidate regression oracles for residual #1481 review threads
// (classified FIXED_ON_MASTER; these pin the behavior)
// ---------------------------------------------------------------------------

TEST_CASE( "B16: #1481 SSRF host-classification threads stay closed on master",
           "[r15b][b16][remote][ssrf]" )
{
  // T1: malformed compressed IPv6 with too many tail groups — must not be
  // treated as a public host and must not corrupt memory (ASan lane).
  CHECK_NOTHROW( offline::isPrivateHost( "http://[1:2:3:4:5:6:7::8:9]/" ) );
  // T3: alternative numeric loopback spellings.
  CHECK( offline::isPrivateHost( "http://2130706433/x" ) );
  CHECK( offline::isPrivateHost( "http://127.1/x" ) );
  CHECK( offline::isPrivateHost( "http://0x7f.0.0.1/x" ) );
  // T4: scoped link-local IPv6 with a zone identifier.
  CHECK( offline::isPrivateHost( "http://[fe80::1%25eth0]/x" ) );
  // T13: IPv6 unspecified address.
  CHECK( offline::isPrivateHost( "http://[::]:8080/x" ) );
  // T16: percent-encoded localhost authority.
  CHECK( offline::isPrivateHost( "http://local%68ost:8080/x" ) );
  // #1505 T4: multicast literals.
  CHECK( offline::isPrivateHost( "http://224.0.0.1/x" ) );
  CHECK( offline::isPrivateHost( "http://[ff02::1]/x" ) );
  // Public hosts stay public (no over-blocking).
  CHECK_FALSE( offline::isPrivateHost( "http://8.8.8.8/x" ) );
}

TEST_CASE( "B16: #1481 T17 direct COG validation is gated before GDAL dispatch when offline",
           "[r15b][b16][cog][offline]" )
{
  const bool wasOffline = offline::enabled();
  offline::setEnabled( true );
  const CogValidationReport report = validateCog( "/vsicurl/http://example.invalid/x.tif" );
  offline::setEnabled( wasOffline );
  bool gated = false;
  for ( const auto &check : report.checks )
    gated = gated || ( check["check"].asString() == "offline_gate" && check["verdict"].asString() == "fail" );
  CHECK( gated );
  CHECK_FALSE( report.isCog );
}
