// test_r22_data_security_consistency.cpp — R22-02 (r22-data) regression
// suite for the data-access security & consistency batch:
//   * #1679 (P0): isPrivateHost authority parsing terminates the authority
//     at the first '/?#' and strips userinfo with the LAST '@' — the exact
//     bound the transport connects by. Bypass shapes (?@, #@, multi-@,
//     userinfo) must classify by the host libcurl would dial.
//   * resolveForPinning (#1679 defense-in-depth): IP literals are judged by
//     the pinning path itself under the process-wide policy.
//   * #1701 (P2): publish journals — commit-marker rename failure fails
//     loudly with journal+backups kept; stale .bak files never become a
//     rollback source; a failed journal write is an error, not a shrug.
//   * #1703-1 (P3): validateWindow extent math is 64-bit — the int sum
//     overflowed to negative and PASSED validation at the far edge.
//   * #1680 (P2): ArtifactObjectPool::put converges when two pool instances
//     race the same digest (Qt rename refuses an existing destination).
//   * #1702 (P2): SamplePromoter reports a failed source-asset link instead
//     of promoting with silently incomplete provenance.
// Every case was written against the pre-fix master behavior first (RED
// evidence in .planning/r22-data/RED_GREEN.md), then held green twice.

#include <catch2/catch_test_macros.hpp>

#include "geospatial/remote/offline_gate.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/raster/raster_reader.h"
#include "data/artifact_object_pool.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/label_schema.h"
#include "dataset/sample_promotion.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

#include <sqlite3.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using sicnu::geo::offline::isPrivateHost;
using sicnu::geo::offline::HostPinStatus;
using sicnu::geo::offline::resolveForPinning;
namespace atomic_fs = sicnu::geo::atomic_fs;

namespace
{

std::string readFile( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  return std::string( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
}

void writeFileBytes( const std::string &path, const std::string &bytes )
{
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  out << bytes;
}

std::string scratchDir( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_r22_data" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

/// RAII guard for the process-wide private-network policy flag.
struct PolicyGuard
{
  const bool was;
  explicit PolicyGuard( bool on ) : was( sicnu::geo::offline::blockPrivateNetworks() )
  {
    sicnu::geo::offline::setBlockPrivateNetworks( on );
  }
  ~PolicyGuard() { sicnu::geo::offline::setBlockPrivateNetworks( was ); }
};

} // namespace

// ---------------------------------------------------------------------------
// #1679 — authority parsing must agree with the connector (libcurl / RFC 3986)
// ---------------------------------------------------------------------------

TEST_CASE( "isPrivateHost terminates the authority at ?/# before userinfo (#1679)",
           "[r22][data][ssrf][issue1679]" )
{
  // The bypass shape: '?' ends the authority, so the '@' belongs to the
  // QUERY. libcurl connects to 127.0.0.1; the gate must judge 127.0.0.1.
  CHECK( isPrivateHost( "http://127.0.0.1?@example.com/" ) );
  CHECK( isPrivateHost( "http://127.0.0.1/?@example.com/" ) );
  CHECK( isPrivateHost( "http://127.0.0.1:8080?@example.com/" ) );
  CHECK( isPrivateHost( "http://169.254.169.254?@example.com/latest" ) );

  // '#' ends the authority the same way (fragment-carried '@').
  CHECK( isPrivateHost( "http://169.254.169.254#@example.com/latest" ) );
  CHECK( isPrivateHost( "http://10.0.0.5#@public.example.com/x" ) );
  CHECK( isPrivateHost( "http://[::1]#@example.com/x" ) );
  CHECK( isPrivateHost( "http://[::ffff:127.0.0.1]?@example.com/x" ) );

  // Multiple '@': RFC 3986/whatwg take the LAST one as the userinfo split.
  CHECK( isPrivateHost( "http://a@b@127.0.0.1/" ) );
  CHECK( isPrivateHost( "http://user:pw@10.255.0.1/" ) );
  CHECK( isPrivateHost( "http://user@169.254.169.254/latest" ) );

  // Real userinfo to a public host stays public (no over-blocking).
  CHECK_FALSE( isPrivateHost( "http://user:pass@8.8.8.8/x" ) );
  CHECK_FALSE( isPrivateHost( "http://8.8.8.8?@example.com/" ) );
  CHECK_FALSE( isPrivateHost( "http://8.8.8.8#@example.com/" ) );

  // The VSI wrapper spellings route through the same parser.
  CHECK( isPrivateHost( "/vsicurl/http://127.0.0.1?@example.com/a.tif" ) );
  CHECK( isPrivateHost( "/vsirangecache/http://169.254.169.254#@example.com/a.tif" ) );
}

TEST_CASE( "resolveForPinning judges IP literals under the process policy (#1679 depth)",
           "[r22][data][ssrf][issue1679]" )
{
  std::vector<std::string> entries;
  std::string error;

  // Policy OFF (library default): a literal has nothing to pin; loopback
  // fixture servers ride this path with a caller guard of their own.
  {
    PolicyGuard off( false );
    CHECK( resolveForPinning( "127.0.0.1", 80, entries, error ) == HostPinStatus::NoResolutionNeeded );
    CHECK( resolveForPinning( "8.8.8.8", 53, entries, error ) == HostPinStatus::NoResolutionNeeded );
    CHECK( resolveForPinning( "::1", 80, entries, error ) == HostPinStatus::NoResolutionNeeded );
  }

  // Policy ON: the pinning path refuses private literals itself instead of
  // trusting the (once divergent) upstream gate parser.
  {
    PolicyGuard on( true );
    CHECK( resolveForPinning( "127.0.0.1", 80, entries, error ) == HostPinStatus::PrivateAddress );
    CHECK( resolveForPinning( "169.254.169.254", 80, entries, error ) == HostPinStatus::PrivateAddress );
    CHECK( resolveForPinning( "10.1.2.3", 80, entries, error ) == HostPinStatus::PrivateAddress );
    CHECK( resolveForPinning( "::1", 80, entries, error ) == HostPinStatus::PrivateAddress );
    CHECK( resolveForPinning( "::ffff:169.254.169.254", 80, entries, error ) ==
           HostPinStatus::PrivateAddress );
    // Public literals still need no resolution.
    CHECK( resolveForPinning( "8.8.8.8", 53, entries, error ) == HostPinStatus::NoResolutionNeeded );
    CHECK( resolveForPinning( "2001:4860:4860::8888", 53, entries, error ) ==
           HostPinStatus::NoResolutionNeeded );
  }
}

// ---------------------------------------------------------------------------
// #1701 — publish journal crash-consistency
// ---------------------------------------------------------------------------

TEST_CASE( "commit-marker rename failure keeps journal and backups, then recovers (#1701)",
           "[r22][data][atomic][issue1701]" )
{
  const std::string dir = scratchDir( "commit_marker_fail" );
  const std::string target = dir + "/target.shp";
  const std::string staged = dir + "/staged-main.shp";
  const std::string stagedDbf = dir + "/staged-main.dbf";
  const std::string targetDbf = dir + "/target.dbf";
  const std::string journal = target + ".publish-journal";
  const std::string marker = target + ".publish-journal.committed";
  const std::string bak = target + ".bak";

  writeFileBytes( target, "OLD-MAIN" );
  writeFileBytes( staged, "NEW-MAIN" );
  writeFileBytes( stagedDbf, "NEW-DBF" );
  // No target.dbf yet: it is a NEWLY-ADDED member (had_target=false) — the
  // shape whose freshly published bytes a bad recovery would DELETE.

  // A non-empty DIRECTORY at the marker path makes the marker rename fail
  // (POSIX rename(file, dir) = EISDIR) while every other publish step is
  // healthy — the real I/O shape of #1701 without any test-only hooks.
  fs::create_directories( marker );
  writeFileBytes( marker + "/occupant", "x" );

  bool refused = false;
  try
  {
    atomic_fs::publishStagedGroup( staged, target );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    refused = true;
    CHECK( error.code() == sicnu::geo::ErrorCode::IoError );
  }
  // On pre-fix master the failed marker rename was ignored and the .bak set
  // deleted anyway — publish "succeeded" with a half-destroyed recovery
  // state. The fix fails loudly.
  REQUIRE( refused );

  // Recovery material is intact: uncommitted journal + backup of the old
  // main; the new sidecar is published but restorable (it is not referenced
  // by any backup, so recovery removes it — had_target=false).
  CHECK( atomic_fs::fileExists( journal ) );
  CHECK( atomic_fs::fileExists( bak ) );
  CHECK( readFile( bak ) == "OLD-MAIN" );

  CHECK( atomic_fs::recoverDatasetGroup( target ) );
  CHECK( readFile( target ) == "OLD-MAIN" ); // rolled back, not deleted
  CHECK_FALSE( fs::exists( targetDbf ) );    // the new member rolled back too
  CHECK_FALSE( atomic_fs::fileExists( journal ) );
  CHECK_FALSE( atomic_fs::fileExists( bak ) );
}

TEST_CASE( "a stale .bak never becomes the rollback source (#1701)",
           "[r22][data][atomic][issue1701]" )
{
  const std::string dir = scratchDir( "stale_bak" );
  const std::string target = dir + "/target.shp";
  const std::string staged = dir + "/staged-main.shp";
  const std::string stagedDbf = dir + "/staged-main.dbf";
  const std::string targetDbf = dir + "/target.dbf";

  writeFileBytes( target, "CURRENT" );
  // Residue of an ancient interrupted run (its journal was destroyed —
  // recoverFromJournalFile deletes unparseable journals WITHOUT touching
  // .baks). If a later publish crashed before its own backupInPlace(), a
  // recovery would "restore" these bytes over the good target.
  writeFileBytes( target + ".bak", "ANCIENT" );

  writeFileBytes( staged, "NEW-MAIN" );
  writeFileBytes( stagedDbf, "NEW-DBF" );
  // A directory occupying the SIDECAR target makes publishStagedFile fail
  // mid-group — the failure path (cleanup/recover) runs with the journal
  // present and must not consult the stale .bak.
  fs::create_directories( targetDbf );

  bool failed = false;
  try
  {
    atomic_fs::publishStagedGroup( staged, target );
  }
  catch ( const sicnu::geo::GeoError & )
  {
    failed = true;
  }
  REQUIRE( failed );

  // The main target was never replaced (sidecars publish first, main LAST)
  // and the stale ANCIENT bytes must NOT have been rolled onto it.
  CHECK( readFile( target ) == "CURRENT" );
  // cleanup() completed the C++ rollback (journal removed); a later
  // recoverDatasetGroup is the belt-and-braces pass — it finds no journal
  // and must simply keep the good state.
  CHECK_FALSE( atomic_fs::fileExists( target + ".publish-journal" ) );
  CHECK( atomic_fs::recoverDatasetGroup( target ) == false ); // nothing left to recover
  CHECK( readFile( target ) == "CURRENT" ); // recovery keeps the good state
  CHECK_FALSE( atomic_fs::fileExists( target + ".bak" ) );
}

TEST_CASE( "a journal path that cannot be written fails the publish up front (#1701)",
           "[r22][data][atomic][issue1701]" )
{
  const std::string dir = scratchDir( "journal_unwritable" );
  const std::string target = dir + "/target.shp";
  const std::string staged = dir + "/staged-main.shp";
  writeFileBytes( target, "OLD" );
  writeFileBytes( staged, "NEW" );

  // A directory at the journal path makes the ofstream fail to open.
  fs::create_directories( target + ".publish-journal" );

  bool refused = false;
  try
  {
    atomic_fs::publishStagedGroup( staged, target );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    refused = true;
    CHECK( error.code() == sicnu::geo::ErrorCode::IoError );
  }
  REQUIRE( refused );
  CHECK( readFile( target ) == "OLD" ); // nothing was modified
}

// ---------------------------------------------------------------------------
// #1703-1 — validateWindow extent math must not overflow int
// ---------------------------------------------------------------------------

TEST_CASE( "validateWindow rejects far-edge overflow windows with 64-bit math (#1703)",
           "[r22][data][raster][issue1703]" )
{
  sicnu::geo::RasterMetadata metadata;
  metadata.width = 1024;
  metadata.height = 1024;
  metadata.bandCount = 1;

  // 1 + INT_MAX wraps negative in int math and PASSED validation pre-fix.
  sicnu::geo::RasterWindow evil;
  evil.xOff = 1;
  evil.yOff = 0;
  evil.width = std::numeric_limits<int>::max();
  evil.height = 1;
  std::string error;
  CHECK_FALSE( sicnu::geo::RasterReader::validateWindow( metadata, evil, &error ) );
  CHECK( error == "window exceeds raster extent" );

  sicnu::geo::RasterWindow evilY;
  evilY.xOff = 0;
  evilY.yOff = 2;
  evilY.width = 1;
  evilY.height = std::numeric_limits<int>::max();
  CHECK_FALSE( sicnu::geo::RasterReader::validateWindow( metadata, evilY, &error ) );

  // Ordinary in-bounds and cleanly out-of-bounds windows keep their verdicts.
  sicnu::geo::RasterWindow ok;
  ok.xOff = 1020;
  ok.yOff = 0;
  ok.width = 4;
  ok.height = 1024;
  CHECK( sicnu::geo::RasterReader::validateWindow( metadata, ok, &error ) );
  sicnu::geo::RasterWindow out;
  out.xOff = 1021;
  out.yOff = 0;
  out.width = 4;
  out.height = 1;
  CHECK_FALSE( sicnu::geo::RasterReader::validateWindow( metadata, out, &error ) );
}

// ---------------------------------------------------------------------------
// #1680 — concurrent same-digest puts converge
// ---------------------------------------------------------------------------

TEST_CASE( "two pool instances racing one digest both converge (#1680)",
           "[r22][data][pool][issue1680]" )
{
  const std::string dir = scratchDir( "pool_race" );
  const QString payload = QString::fromStdString( dir ) + QStringLiteral( "/payload.bin" );
  {
    std::string bytes( 4096, '\0' );
    for ( std::size_t i = 0; i < bytes.size(); ++i )
      bytes[i] = static_cast<char>( ( i * 31 ) % 251 );
    writeFileBytes( payload.toStdString(), bytes );
  }

  // Two independently-opened pools over one root (the same-digest race the
  // issue describes); each round is a fresh content so the race window is
  // hit from the cold path every time.
  for ( int round = 0; round < 8; ++round )
  {
    const QString root = QString::fromStdString( dir ) + QStringLiteral( "/root-%1" ).arg( round );
    sicnu::data::ArtifactObjectPool poolA;
    sicnu::data::ArtifactObjectPool poolB;
    QString err;
    REQUIRE( poolA.enable( root, &err ) );
    REQUIRE( poolB.enable( root, &err ) );

    // Distinct source files with IDENTICAL content (same digest, the
    // concurrent-publish shape; copying first keeps the race at the rename).
    const QString srcB = QString::fromStdString( dir ) + QStringLiteral( "/payload-b-%1.bin" ).arg( round );
    QFile::copy( payload, srcB );

    std::atomic<int> successes{ 0 };
    auto putter = [&]( sicnu::data::ArtifactObjectPool *pool, const QString &path )
    {
      const auto object = pool->put( path, false );
      if ( object.has_value() && object->size == 4096 )
        successes.fetch_add( 1 );
    };
    std::thread ta( putter, &poolA, payload );
    std::thread tb( putter, &poolB, srcB );
    ta.join();
    tb.join();

    // Pre-fix: the loser's QFile::rename fails against the winner's object
    // and returns nullopt — a healthy pooled object reported as a miss.
    // Post-fix: both sides converge (dedup acceptance or re-stage+retry).
    INFO( "round " << round << " successes: " << successes.load() );
    CHECK( successes.load() == 2 );
  }
}

// ---------------------------------------------------------------------------
// #1702 — a failed source-asset link is reported, never silently dropped
// ---------------------------------------------------------------------------

namespace
{

struct PromotionFixture
{
  QTemporaryDir dir;
  sicnu::dataset::DatasetStore store;
  sicnu::dataset::DatasetId datasetId;
  sicnu::dataset::DatasetVersionId versionId;

  PromotionFixture()
  {
    REQUIRE( store.open( dir.filePath( QStringLiteral( "ds.db" ) ) ) );
    datasetId = sicnu::dataset::DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "r22" ) ).has_value() );
    versionId = sicnu::dataset::DatasetVersionId::generate();
    sicnu::dataset::DatasetManifest manifest;
    manifest.setDatasetId( datasetId.toString() );
    manifest.setVersionId( versionId.toString() );
    manifest.setName( QStringLiteral( "r22" ) );
    manifest.setCreatedAtUtc( QDateTime::fromString(
      QStringLiteral( "2026-10-25T00:00:00.000Z" ), Qt::ISODateWithMs ) );
    REQUIRE( store.createDraftVersion( manifest ).has_value() );
  }

  sicnu::dataset::PromotionSource source() const
  {
    sicnu::dataset::PromotionSource s;
    s.assetId = QStringLiteral( "aaaaaaaa-0000-4000-8000-000000000009" );
    s.revision = 1;
    // Model-assisted annotations must record their model identity
    // (dataset.annotation_invalid otherwise — see promoteClassification).
    s.sourceDetail = QJsonObject{
      { QStringLiteral( "model_id" ), QStringLiteral( "r22-repro" ) },
      { QStringLiteral( "model_digest" ), QStringLiteral( "cafe1234" ) },
    };
    return s;
  }
};

/// Rewrites the draft row's manifest_json to text DatasetManifest::fromJson
/// rejects. requireDraftVersion() only reads STATUS, so promotion passes its
/// entry gate and the failure lands exactly on addSourceAssetsToDraft — the
/// previously-ignored Result of #1702.
void corruptDraftManifest( const QString &dbPath, const sicnu::dataset::DatasetVersionId &version )
{
  sqlite3 *db = nullptr;
  REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr ) ==
           SQLITE_OK );
  const std::string sql = "UPDATE dataset_versions SET manifest_json='{not-json' WHERE id='" +
                          version.toString().toStdString() + "'";
  char *err = nullptr;
  REQUIRE( sqlite3_exec( db, sql.c_str(), nullptr, nullptr, &err ) == SQLITE_OK );
  sqlite3_free( err );
  sqlite3_close( db );
}

} // namespace

TEST_CASE( "promotion reports a failed source-asset link instead of silent success (#1702)",
           "[r22][data][dataset][promotion][issue1702]" )
{
  PromotionFixture fx;
  sicnu::dataset::SamplePromoter promoter( fx.store );

  sicnu::dataset::LabelSchema schema;
  schema.setSchemaId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
  schema.setVersion( 1 );
  sicnu::dataset::LabelClass water;
  water.setStableId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
  water.setCode( QStringLiteral( "water" ) );
  schema.classes().append( water );
  REQUIRE( schema.validate().has_value() );

  QVector<sicnu::dataset::ClassifiedRegion> regions;
  sicnu::dataset::ClassifiedRegion region;
  region.rawValue = 1;
  region.geometryWkt = QStringLiteral( "POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))" );
  regions.append( region );

  // With a healthy manifest the promotion succeeds and links provenance.
  {
    const auto ok = promoter.promoteClassification(
      fx.versionId, fx.source(), { { 1, QStringLiteral( "water" ) } }, regions, &schema );
    if ( !ok.has_value() )
    {
      QString joined;
      for ( const auto &d : ok.diagnostics() )
        joined += d.code + QStringLiteral( ": " ) + d.message + QStringLiteral( "\n" );
      std::fprintf( stderr, "healthy-path diagnostics (%zu):\n%s",
                    static_cast<std::size_t>( ok.diagnostics().size() ), joined.toStdString().c_str() );
    }
    REQUIRE( ok.has_value() );
    CHECK( ok.value().samplesWritten == 1 );
  }

  // Now the same shape with a manifest the store cannot parse: the sample
  // write succeeds, the source-asset link fails. Pre-fix master still
  // returned success here — the dataset's provenance silently incomplete.
  PromotionFixture fx2;
  sicnu::dataset::SamplePromoter promoter2( fx2.store );
  corruptDraftManifest( fx2.dir.filePath( QStringLiteral( "ds.db" ) ), fx2.versionId );
  const auto broken = promoter2.promoteClassification(
    fx2.versionId, fx2.source(), { { 1, QStringLiteral( "water" ) } }, regions, &schema );
  REQUIRE( !broken.has_value() );
  bool sawPartial = false;
  for ( const auto &diag : broken.diagnostics() )
    if ( diag.code == QStringLiteral( "dataset.promotion_partial" ) )
      sawPartial = true;
  CHECK( sawPartial ); // truthful: what landed, and what did not
}
