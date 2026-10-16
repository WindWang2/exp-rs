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
#include "geospatial/remote/http_fetch.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"
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

#include <cpl_error.h>
#include <cpl_vsi.h>

#include <dlfcn.h>
#include <netdb.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cmath>
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

// ---------------------------------------------------------------------------
// B07 — a mid-scan SQLite error is a typed failure, never a short success
// ---------------------------------------------------------------------------

namespace
{
/// Corrupts the b-tree page holding @p marker (page-type byte => invalid),
/// so a scan that reaches that row fails with SQLITE_CORRUPT mid-way.
bool corruptPageContaining( const QString &dbPath, const QByteArray &marker, int pageSize = 4096 )
{
  QFile file( dbPath );
  if ( !file.open( QIODevice::ReadWrite ) )
    return false;
  const QByteArray bytes = file.readAll();
  const qsizetype at = bytes.indexOf( marker );
  if ( at < 0 )
    return false;
  const qint64 pageStart = ( at / pageSize ) * pageSize;
  if ( pageStart == 0 )
    return false; // never touch the database header page
  file.seek( pageStart );
  const char garbage = static_cast<char>( 0x7F ); // not a valid b-tree page type
  return file.write( &garbage, 1 ) == 1;
}
} // namespace

TEST_CASE( "B07: ExperimentStore list scans report a mid-scan SQLITE_CORRUPT as failure",
           "[r15b][b07][sqlite][experiment]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString dbPath = dir.filePath( QStringLiteral( "exp.db" ) );
  {
    ExperimentStore store;
    REQUIRE( store.open( dbPath ) );
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-1" ) );
    experiment.setName( QStringLiteral( "scan" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
    const QString padding( 1800, QLatin1Char( 'x' ) );
    for ( int i = 0; i < 60; ++i )
    {
      ExperimentRun run;
      run.setRunId( QStringLiteral( "run-%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ) );
      run.setExperimentId( QStringLiteral( "exp-1" ) );
      run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
      run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
      QJsonObject parameters;
      parameters.insert( QStringLiteral( "marker" ), QStringLiteral( "R15B-MARK-%1-END" ).arg( i ) );
      parameters.insert( QStringLiteral( "pad" ), padding );
      run.setParameters( parameters );
      run.setSeed( static_cast<quint64>( i ) );
      run.setStatus( RunStatus::Created );
      REQUIRE( store.upsertRun( run ).has_value() );
    }
    store.close(); // last connection: WAL checkpointed into the main file
  }
  REQUIRE( corruptPageContaining( dbPath, QByteArrayLiteral( "R15B-MARK-40-END" ) ) );

  ExperimentStore reopened;
  REQUIRE( reopened.open( dbPath ) );
  const auto listed = reopened.listRuns( QString(), QString(), QString(), 0, 100 );
  if ( listed.has_value() )
  {
    // The only acceptable success is a COMPLETE answer.
    INFO( "listRuns succeeded with " << listed.value().second.size() << " of total "
                                     << listed.value().first );
    CHECK( listed.value().second.size() == 60 );
  }
  else
  {
    SUCCEED( "mid-scan corruption surfaced as a typed failure" );
  }
}

// ---------------------------------------------------------------------------
// B02 — full read == tiled stitch == block stitch (integer types, DEFLATE,
// rotated grid, edge padding with declared NoData)
// ---------------------------------------------------------------------------

TEST_CASE( "B02: full read equals tile-walk and block stitches for integer compressed rotated rasters",
           "[r15b][b02][raster][tiles]" )
{
  struct Case { const char *dtype; double lo; double hi; double nodata; };
  const Case cases[] = {
    { "Byte", 0.0, 255.0, 255.0 },
    { "Int16", -32768.0, 32767.0, -32768.0 },
    { "UInt16", 0.0, 65535.0, 0.0 },
    { "Int32", -2147483648.0, 2147483647.0, -2147483648.0 },
    { "UInt32", 0.0, 4294967295.0, 4294967295.0 },
  };
  const int width = 37, height = 23; // deliberately not block multiples
  for ( const Case &c : cases )
  {
    INFO( "dtype=" << c.dtype );
    const std::string dir = scratch( std::string( "b02_" ) + c.dtype );
    const std::string target = ( fs::path( dir ) / "r.tif" ).string();
    std::vector<double> band1( static_cast<std::size_t>( width ) * height );
    std::vector<double> band2( band1.size() );
    for ( int y = 0; y < height; ++y )
      for ( int x = 0; x < width; ++x )
      {
        const std::size_t i = static_cast<std::size_t>( y ) * width + x;
        // Hit both extremes plus a spread in between.
        band1[i] = ( i % 11 == 0 ) ? c.lo : ( i % 13 == 0 ) ? c.hi : c.lo + std::fmod( i * 7919.0, c.hi - c.lo );
        band2[i] = c.hi - std::fmod( i * 104729.0, c.hi - c.lo );
      }
    {
      RasterBandSpec spec;
      spec.dtype = c.dtype;
      spec.hasNoData = true;
      spec.noDataValue = c.nodata;
      RasterWriter writer = RasterWriter::create(
        target, width, height, { spec, spec },
        { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16", "COMPRESS=DEFLATE" }, true } );
      writer.setGeotransform( { 500000.0, 10.0, 2.5, 4200000.0, 1.5, -10.0 } ); // rotated/sheared
      writer.setCrs( Crs::fromAuthid( "EPSG:32650" ) );
      writer.writeWindow( 1, { 0, 0, width, height }, band1.data() );
      writer.writeWindow( 2, { 0, 0, width, height }, band2.data() );
      writer.finalize();
    }
    RasterReader reader = RasterReader::open( target );
    const std::vector<double> full = reader.readFull( { 1, 2 }, 64ULL * 1024 * 1024 );
    REQUIRE( full.size() == band1.size() * 2 );
    CHECK( std::equal( band1.begin(), band1.end(), full.begin() ) );
    CHECK( std::equal( band2.begin(), band2.end(), full.begin() + static_cast<std::ptrdiff_t>( band1.size() ) ) );

    // Tile walk with a tile size unrelated to the native block.
    std::vector<double> stitched( full.size(), -1.0 );
    const TilePlan plan = planTileWalk( reader.metadata(), { 0, 0, width, height }, 7, 5 );
    reader.iterateTiles( plan, { 1, 2 }, [&]( const TileSlice &slice, const std::vector<double> &values ) {
      const TileSlice &w = slice;
      const std::size_t plane = static_cast<std::size_t>( w.width ) * w.height;
      REQUIRE( values.size() == plane * 2 );
      for ( int b = 0; b < 2; ++b )
        for ( int y = 0; y < w.height; ++y )
          for ( int x = 0; x < w.width; ++x )
            stitched[b * band1.size() + static_cast<std::size_t>( w.yOff + y ) * width + ( w.xOff + x )] =
              values[b * plane + static_cast<std::size_t>( y ) * w.width + x];
    } );
    CHECK( stitched == full );

    // Native blocks: edge blocks are padded with the declared NoData.
    const auto [bw, bh] = reader.blockSize( 1 );
    REQUIRE( bw == 16 );
    REQUIRE( bh == 16 );
    for ( int by = 0; by * bh < height; ++by )
      for ( int bx = 0; bx * bw < width; ++bx )
      {
        const std::vector<double> block = reader.readBlock( 1, bx, by );
        REQUIRE( block.size() == static_cast<std::size_t>( bw ) * bh );
        for ( int y = 0; y < bh; ++y )
          for ( int x = 0; x < bw; ++x )
          {
            const int gx = bx * bw + x, gy = by * bh + y;
            const double got = block[static_cast<std::size_t>( y ) * bw + x];
            if ( gx < width && gy < height )
              REQUIRE( got == band1[static_cast<std::size_t>( gy ) * width + gx] );
            else
              REQUIRE( got == c.nodata );
          }
      }
  }
}

TEST_CASE( "B16: #1485 octal and trailing-dot loopback spellings are private",
           "[r15b][b16][remote][ssrf]" )
{
  CHECK( offline::isPrivateHost( "http://0177.0.0.1/x" ) );
  CHECK( offline::isPrivateHost( "http://localhost./x" ) );
}

// ---------------------------------------------------------------------------
// B14 — bulk measurement (hidden: run explicitly with "[b14]"; numbers are
// recorded in .planning/r15-b/PERFORMANCE.md, nothing is asserted on speed)
// ---------------------------------------------------------------------------

namespace
{
long rssKb()
{
  std::ifstream status( "/proc/self/status" );
  std::string line;
  while ( std::getline( status, line ) )
    if ( line.rfind( "VmRSS:", 0 ) == 0 )
      return std::stol( line.substr( 6 ) );
  return -1;
}
double msSince( std::chrono::steady_clock::time_point t0 )
{
  return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - t0 ).count();
}
int envInt( const char *name, int fallback )
{
  const char *v = std::getenv( name );
  return v && *v ? std::atoi( v ) : fallback;
}
} // namespace

TEST_CASE( "B14: bulk catalog paging and experiment-store throughput (measurement)",
           "[.][r15b][b14][perf]" )
{
  const int catalogN = envInt( "R15B_PERF_CATALOG", 100000 );
  const int runsN = envInt( "R15B_PERF_RUNS", 20000 );

  // 1. In-memory catalog: 100k records, page walk at 500/page.
  {
    const long rss0 = rssKb();
    auto t0 = std::chrono::steady_clock::now();
    std::vector<AssetRecord> records;
    records.reserve( static_cast<std::size_t>( catalogN ) );
    for ( int i = 0; i < catalogN; ++i )
    {
      AssetRecord r;
      r.id = "item-" + std::to_string( i );
      r.collection = ( i % 2 ) ? "s2" : "l8";
      r.path = "/data/" + r.id + ".tif";
      r.mediaType = "image/tiff";
      records.push_back( std::move( r ) );
    }
    const CatalogService service = catalogServiceOverRecords( std::move( records ) );
    const double buildMs = msSince( t0 );
    const long rssBuilt = rssKb();
    CatalogQuery query;
    query.limit = 500;
    query.maxItems = 500;
    CatalogContinuation cont;
    std::size_t seen = 0;
    int pages = 0;
    long rssPeak = rssBuilt;
    std::set<std::string> ids;
    t0 = std::chrono::steady_clock::now();
    while ( true )
    {
      CatalogPage page = service.searchPage( query, cont );
      seen += page.records.size();
      for ( const auto &r : page.records )
        ids.insert( r.id );
      ++pages;
      rssPeak = std::max( rssPeak, rssKb() );
      if ( !page.next.hasMore )
        break;
      cont = page.next;
    }
    const double walkMs = msSince( t0 );
    CHECK( seen == static_cast<std::size_t>( catalogN ) );
    CHECK( ids.size() == static_cast<std::size_t>( catalogN ) ); // no skip, no dup
    WARN( "PERF catalog N=" << catalogN << " build_ms=" << buildMs << " pages=" << pages
                            << " walk_ms=" << walkMs << " avg_page_ms=" << walkMs / pages
                            << " rss_kb start=" << rss0 << " built=" << rssBuilt << " walk_peak=" << rssPeak );
  }

  // 2. ExperimentStore: batch writes then page reads.
  {
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::experiment::ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "perf.db" ) ) ) );
    sicnu::experiment::Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-perf" ) );
    experiment.setName( QStringLiteral( "perf" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
    auto t0 = std::chrono::steady_clock::now();
    for ( int i = 0; i < runsN; ++i )
    {
      sicnu::experiment::ExperimentRun run;
      run.setRunId( QStringLiteral( "run-%1" ).arg( i, 7, 10, QLatin1Char( '0' ) ) );
      run.setExperimentId( QStringLiteral( "exp-perf" ) );
      run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
      run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
      run.setSeed( static_cast<quint64>( i ) );
      run.setStatus( sicnu::experiment::RunStatus::Created );
      REQUIRE( store.upsertRun( run ).has_value() );
    }
    const double writeMs = msSince( t0 );
    const long rssWritten = rssKb();
    long rssPeak = rssWritten;
    qint64 read = 0;
    int pages = 0;
    double firstPageMs = 0, lastPageMs = 0;
    t0 = std::chrono::steady_clock::now();
    for ( qint64 offset = 0;; offset += 500 )
    {
      const auto p0 = std::chrono::steady_clock::now();
      const auto page = store.listRuns( QStringLiteral( "exp-perf" ), QString(), QString(), offset, 500 );
      const double pageMs = msSince( p0 );
      REQUIRE( page.has_value() );
      if ( pages == 0 )
        firstPageMs = pageMs;
      lastPageMs = pageMs;
      read += page.value().second.size();
      ++pages;
      rssPeak = std::max( rssPeak, rssKb() );
      if ( page.value().second.size() < 500 )
        break;
    }
    const double readMs = msSince( t0 );
    CHECK( read == runsN );
    WARN( "PERF experiment_store runs=" << runsN << " write_ms=" << writeMs << " writes_per_s="
                                         << ( runsN / ( writeMs / 1000.0 ) ) << " pages=" << pages
                                         << " read_ms=" << readMs << " first_page_ms=" << firstPageMs
                                         << " last_page_ms=" << lastPageMs << " rss_kb written=" << rssWritten
                                         << " read_peak=" << rssPeak );
  }
}

// ---------------------------------------------------------------------------
// #1505 T6 (item 8): a policy refusal of an origin fetch must not degrade to
// the raw /vsicurl/ fallback (which applies no private-network guard and
// follows redirects unguarded). Deterministic shape of the same defect:
// the private-network policy is switched on for an already-open handle, so
// the next uncached read is refused by fetchRangeOnce (PermissionDenied).
// Before the fix serveOrFetch treated that like a transport failure and
// read the bytes from the private host through the fallback anyway.
// ---------------------------------------------------------------------------
TEST_CASE( "1505-T6: a private-network refusal is final, never served by the raw fallback",
           "[r15b][post1505][remote][ssrf]" )
{
  using sicnu::geo::RangeCacheConfig;
  using sicnu::geo::RemoteRangeCache;
  RemoteRangeCache::clearEntries();
  RangeCacheConfig open;
  open.blockSize = 64 * 1024;
  open.maxSingleFetchBytes = 64 * 1024;
  RemoteRangeCache::install( open );
  testsupport::HttpRangeServer server( std::vector<unsigned char>( 1024 * 1024, 0x5A ) );
  const std::string path = RemoteRangeCache::cachedPath( server.url() );
  VSILFILE *fp = VSIFOpenL( path.c_str(), "rb" );
  REQUIRE( fp != nullptr );
  unsigned char head[16] = {};
  REQUIRE( VSIFReadL( head, 1, sizeof head, fp ) == sizeof head );

  RangeCacheConfig guarded = open;
  guarded.blockPrivateNetworks = true;
  RemoteRangeCache::install( guarded );
  const auto fallback0 = RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64();
  const int requests0 = server.requestCount();
  std::vector<unsigned char> buf( 4096, 0 );
  REQUIRE( VSIFSeekL( fp, 768 * 1024, SEEK_SET ) == 0 );
  const size_t got = VSIFReadL( buf.data(), 1, buf.size(), fp );
  CHECK( got == 0 );
  CHECK( RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64() == fallback0 );
  CHECK( server.requestCount() == requests0 );
  VSIFCloseL( fp );
  RemoteRangeCache::clearEntries();
  RemoteRangeCache::install( RangeCacheConfig{} );
}

// ---------------------------------------------------------------------------
// #1505 T3 / #1485 (item 8): DNS rebinding. The guard resolved the hostname,
// validated the answer and freed it; libcurl then resolved the SAME name
// again and connected wherever the second answer pointed. Deterministic
// rebinding resolver: getaddrinfo is interposed for one test-only name —
// the first lookup answers a public TEST-NET address (192.0.2.1, never
// routable), every later lookup answers 127.0.0.1 where the fixture listens.
// A pinned transport never reaches the fixture.
// ---------------------------------------------------------------------------
namespace
{
std::atomic<bool> g_rebindActive{ false };
std::atomic<int> g_rebindCalls{ 0 };
std::atomic<int> g_rebindPublicAnswers{ 1 }; // lookups answered public before rebinding
constexpr const char *kRebindHost = "r15b-rebind.test";
} // namespace

extern "C" int getaddrinfo( const char *node, const char *service, const struct addrinfo *hints,
                            struct addrinfo **res )
{
  using Fn = int ( * )( const char *, const char *, const struct addrinfo *, struct addrinfo ** );
  static Fn real = reinterpret_cast<Fn>( dlsym( RTLD_NEXT, "getaddrinfo" ) );
  if ( node && g_rebindActive.load() && std::strcmp( node, kRebindHost ) == 0 )
  {
    const int n = g_rebindCalls.fetch_add( 1 );
    struct addrinfo numeric;
    std::memset( &numeric, 0, sizeof numeric );
    numeric.ai_family = AF_INET;
    numeric.ai_socktype = hints ? hints->ai_socktype : SOCK_STREAM;
    numeric.ai_flags = AI_NUMERICHOST;
    return real( n < g_rebindPublicAnswers.load() ? "192.0.2.1" : "127.0.0.1", service, &numeric, res );
  }
  return real( node, service, hints, res );
}

TEST_CASE( "1505-T3: guarded fetch connects only to the address set the guard validated (DNS rebinding)",
           "[r15b][post1505][remote][ssrf][dns]" )
{
  testsupport::HttpRangeServer server( std::vector<unsigned char>( 4096, 0x33 ) );
  const std::string url = "http://" + std::string( kRebindHost ) + ":" + std::to_string( server.port() ) +
                          "/fixture.tif";
  HttpFetchOptions options;
  options.timeoutSeconds = 3;
  options.connectTimeoutSeconds = 1;
  options.maxRetries = 0;
  options.redirectGuard = []( const std::string &hopUrl ) -> std::string {
    return offline::isPrivateHost( hopUrl ) ? "private network blocked" : "";
  };
  g_rebindCalls = 0;
  g_rebindPublicAnswers = 1;
  g_rebindActive = true;
  bool threw = false;
  try
  {
    httpFetch( url, options );
  }
  catch ( const GeoError & )
  {
    threw = true;
  }
  g_rebindActive = false;
  INFO( "lookups=" << g_rebindCalls.load() << " fixture requests=" << server.requestCount() );
  CHECK( g_rebindCalls.load() >= 1 );
  CHECK( server.requestCount() == 0 ); // never reached the rebound private address
  CHECK( threw );
}

// ---------------------------------------------------------------------------
// R3-10: a TRANSPORT failure (not a policy refusal) of a guarded ranged
// fetch degraded to the raw /vsicurl/ fallback, which re-resolves the name
// with no private-network check and no pinning. A rebinding name makes the
// guarded fetch fail (public TEST-NET answer, unroutable) and then points
// the fallback's own lookup at 127.0.0.1. Fixed contract: under
// blockPrivateNetworks the http fallback uses the same guarded, pinned
// transport, so the fixture is never contacted.
// ---------------------------------------------------------------------------
TEST_CASE( "R3-10: a guarded fetch's transport failure never degrades to the unguarded fallback",
           "[r15b][post1505][remote][ssrf][dns]" )
{
  using sicnu::geo::RangeCacheConfig;
  using sicnu::geo::RemoteRangeCache;
  RemoteRangeCache::clearEntries();
  testsupport::HttpRangeServer server( std::vector<unsigned char>( 1024 * 1024, 0x6B ) );
  server.setEtag( "\"r15b-r310\"" );
  const std::string url = "http://" + std::string( kRebindHost ) + ":" + std::to_string( server.port() ) +
                          "/fixture.tif";
  RangeCacheConfig open;
  open.blockSize = 64 * 1024;
  open.maxSingleFetchBytes = 64 * 1024;
  open.timeoutSeconds = 3;
  open.connectTimeoutSeconds = 1;
  open.maxRetries = 0;
  RemoteRangeCache::install( open );
  // Open phase: every lookup answers loopback, so the probe proves the
  // identity (policy off) and a first read caches block 0.
  g_rebindCalls = 0;
  g_rebindPublicAnswers = 0;
  g_rebindActive = true;
  VSILFILE *fp = VSIFOpenL( RemoteRangeCache::cachedPath( url ).c_str(), "rb" );
  REQUIRE( fp != nullptr );
  unsigned char head[16] = {};
  REQUIRE( VSIFReadL( head, 1, sizeof head, fp ) == sizeof head );

  RangeCacheConfig guarded = open;
  guarded.blockPrivateNetworks = true;
  RemoteRangeCache::install( guarded );
  // Guarded phase: the guarded fetch's own lookups answer public, so it
  // passes every check and then fails at connect (TEST-NET is unroutable):
  // fetchRangeOnce's host check, the hop guard's host check and the pinning
  // resolution = 3 lookups (libcurl itself does not resolve a pinned name).
  // Every later lookup answers loopback — the rebinding. The error check
  // below proves the guarded fetch failed by TRANSPORT, not by a policy
  // refusal (a miscounted calibration would make the case vacuous).
  g_rebindCalls = 0;
  g_rebindPublicAnswers = 3;
  CPLErrorReset();
  const int requests0 = server.requestCount();
  std::vector<unsigned char> buf( 4096, 0 );
  REQUIRE( VSIFSeekL( fp, 768 * 1024, SEEK_SET ) == 0 );
  const size_t got = VSIFReadL( buf.data(), 1, buf.size(), fp );
  g_rebindActive = false;
  INFO( "lookups=" << g_rebindCalls.load() << " got=" << got
                   << " fixture requests=" << server.requestCount() - requests0 );
  CHECK( g_rebindCalls.load() >= 1 );
  CHECK( server.requestCount() == requests0 ); // the rebound loopback origin is never contacted
  CHECK( got == 0 );
  CHECK( std::string( CPLGetLastErrorMsg() ).find( "origin fetch refused by the private-network policy" ) ==
         std::string::npos );
  VSIFCloseL( fp );
  RemoteRangeCache::clearEntries();
  RemoteRangeCache::install( RangeCacheConfig{} );
}

// ---------------------------------------------------------------------------
// R4-01 (found by the T11 prefetch oracle): blockPrivateNetworks guarded only
// the Open host check and the ranged fetch. The identity probe, the
// revalidation and Stat used validatorOptions(config), which dropped the
// flag — so VSIStatL on a private URL probed it, and a rebinding public
// name was probed with an unguarded, unpinned request.
// ---------------------------------------------------------------------------
TEST_CASE( "R4-01: Stat under the private-network policy never probes a private origin",
           "[r15b][post1505][remote][ssrf]" )
{
  using sicnu::geo::RangeCacheConfig;
  using sicnu::geo::RemoteRangeCache;
  RemoteRangeCache::clearEntries();
  testsupport::HttpRangeServer server( std::vector<unsigned char>( 8192, 0x5C ) );
  server.setEtag( "\"r15b-r401-stat\"" );
  RangeCacheConfig guarded;
  guarded.blockPrivateNetworks = true;
  RemoteRangeCache::install( guarded );
  VSIStatBufL st;
  const int rc = VSIStatL( RemoteRangeCache::cachedPath( server.url() ).c_str(), &st );
  CHECK( rc != 0 );
  CHECK( server.requestCount() == 0 );
  RemoteRangeCache::clearEntries();
  RemoteRangeCache::install( RangeCacheConfig{} );
}

TEST_CASE( "R4-01: the open-time identity probe is guarded and pinned (DNS rebinding)",
           "[r15b][post1505][remote][ssrf][dns]" )
{
  using sicnu::geo::RangeCacheConfig;
  using sicnu::geo::RemoteRangeCache;
  RemoteRangeCache::clearEntries();
  testsupport::HttpRangeServer server( std::vector<unsigned char>( 8192, 0x5D ) );
  server.setEtag( "\"r15b-r401-open\"" );
  const std::string url = "http://" + std::string( kRebindHost ) + ":" + std::to_string( server.port() ) +
                          "/fixture.tif";
  RangeCacheConfig guarded;
  guarded.blockPrivateNetworks = true;
  guarded.timeoutSeconds = 3;
  guarded.connectTimeoutSeconds = 1;
  guarded.maxRetries = 0;
  RemoteRangeCache::install( guarded );
  // Open's host check sees the public answer; every later lookup (the
  // probe's own resolution) answers loopback — the rebinding.
  g_rebindCalls = 0;
  g_rebindPublicAnswers = 1;
  g_rebindActive = true;
  VSILFILE *fp = VSIFOpenL( RemoteRangeCache::cachedPath( url ).c_str(), "rb" );
  g_rebindActive = false;
  INFO( "lookups=" << g_rebindCalls.load() << " fixture requests=" << server.requestCount() );
  CHECK( g_rebindCalls.load() >= 2 ); // the probe did resolve the name again
  CHECK( fp == nullptr );
  CHECK( server.requestCount() == 0 );
  if ( fp )
    VSIFCloseL( fp );
  RemoteRangeCache::clearEntries();
  RemoteRangeCache::install( RangeCacheConfig{} );
}
