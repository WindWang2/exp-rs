/***************************************************************************
  tests/test_io_mirror_maintenance.cpp — Offline Mirror 12.0 (WP E):
  maintenance surfaces that keep the mirror's PROOF true over time.

    * verifyMirror — read-only integrity audit: every manifest chunk entry
      must exist, match its declared size and (11.0 manifests) prove its
      payload with sha256; the chunk directory is scanned for orphans the
      manifest never named. A corrupt manifest is a REFUSAL, never a guess.
    * repairMirror — re-materialize broken entries from the source (12.0
      WP E; the pass records offline refusals instead of guessing).
    * pruneMirror — garbage-collect orphan files, dead entries, aged and
      over-quota entries; refuses to act against an unreadable manifest.

  Fixtures are plain local GTiffs mirrored through the normal chunk walk —
  no network, no Qt (the io-test contract).
 ***************************************************************************/

#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <filesystem>
#include <fstream>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <ctime>
#include <string>
#include <vector>

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_io_mirror_maint" /
                       name ).string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

/// A 32×32 tiled GTiff on a deterministic ramp (the fabric suites' fixture
/// discipline) with an explicit 4-chunk intent.
struct Fixture
{
  std::string dir;
  std::string scene;
  std::string mirrorDir;
  FabricPlan plan;

  explicit Fixture( const char *name )
    : dir( scratchDir( name ) ), scene( dir + "/s0.tif" ), mirrorDir( dir + "/mirror" ),
      plan( buildPlan() )
  {
  }

  FabricPlan buildPlan() const
  {
    {
      RasterWriter writer = RasterWriter::create(
        scene, 32, 32, { RasterBandSpec {} },
        { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
      writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
      writer.setGeotransform( { 0.0, 1.0, 0.0, 32.0, 0.0, -1.0 } );
      std::vector<double> raster( 32ull * 32 );
      for ( std::size_t i = 0; i < raster.size(); ++i )
        raster[i] = double( i % 251 );
      writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
      writer.finalize();
    }
    FabricIntent intent;
    AssetRecord record;
    record.id = "s0";
    record.path = scene;
    record.datetimeUtc = "2024-01-01T00:00:00Z";
    record.hasBbox = true;
    record.minX = 0.0;
    record.minY = 0.0;
    record.maxX = 32.0;
    record.maxY = 32.0;
    record.mediaType = "image/tiff";
    record.roles = { "data" };
    intent.records = { record };
    intent.sceneBudget = 2;
    intent.grid.explicitGrid = true;
    intent.grid.crs.valid = true;
    intent.grid.crs.authid = "EPSG:4326";
    intent.grid.scaleX = 1.0;
    intent.grid.scaleY = 1.0;
    intent.grid.minX = 0.0;
    intent.grid.minY = 0.0;
    intent.grid.maxX = 32.0;
    intent.grid.maxY = 32.0;
    intent.chunkShape = { 1, 16, 16, 1 };
    intent.hasWindow = true;
    intent.windowW = 16;
    intent.windowH = 16;
    return planFabric( intent );
  }

  MirrorReport materialize()
  {
    MirrorOptions options;
    options.mirrorDirectory = mirrorDir;
    return mirrorChunks( plan, options );
  }

  std::vector<std::string> chunkFiles() const
  {
    std::vector<std::string> files;
    for ( const auto &entry : std::filesystem::directory_iterator( mirrorDir + "/chunks" ) )
      files.push_back( entry.path().string() );
    return files;
  }
};

} // namespace

TEST_CASE( "verifyMirror passes a healthy mirror and refuses a corrupt manifest",
           "[io][fabric][mirror][verify][utc12]" )
{
  Fixture fix( "verify_healthy" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );

  const MirrorVerifyReport verify = verifyMirror( fix.mirrorDir );
  CHECK( verify.entriesChecked == 4 );
  CHECK( verify.ok == 4 );
  CHECK( verify.missingFiles == 0 );
  CHECK( verify.sizeMismatches == 0 );
  CHECK( verify.checksumMismatches == 0 );
  CHECK( verify.badEntries == 0 );
  CHECK( verify.unreferencedFiles == 0 );
  CHECK( verify.bytesChecked > 0 );
  CHECK( !verify.manifestUnreadable );

  // A corrupt manifest is a REFUSAL to conclude — maintenance must never
  // act against a manifest it cannot parse.
  const std::string manifestPath = fix.mirrorDir + "/manifest.json";
  {
    std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
    out << "{ not json at all";
  }
  const MirrorVerifyReport refused = verifyMirror( fix.mirrorDir );
  CHECK( refused.manifestUnreadable );
  CHECK( refused.entriesChecked == 0 );
}

TEST_CASE( "repairMirror re-materializes broken entries from the source",
           "[io][fabric][mirror][repair][utc12]" )
{
  Fixture fix( "repair" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );
  const std::vector<std::string> chunks = fix.chunkFiles();
  REQUIRE( chunks.size() == 4 );

  // Break two entries: delete one payload, tamper another (same size).
  std::filesystem::remove( chunks[0] );
  {
    std::fstream file( chunks[1], std::ios::binary | std::ios::in | std::ios::out );
    file.seekg( 0, std::ios::end );
    const auto size = file.tellg();
    file.seekp( size - 4 );
    char byte = 0;
    file.read( &byte, 1 );
    file.seekp( size - 4 );
    file.put( static_cast<char>( byte ^ 0xFF ) );
  }
  REQUIRE( verifyMirror( fix.mirrorDir ).ok == 2 );

  // The repair drops exactly the broken entries and re-materializes them;
  // the healthy pair is never re-read (already-present).
  const MirrorRepairReport repaired = repairMirror( fix.plan, { fix.mirrorDir } );
  CHECK( repaired.before.missingFiles == 1 );
  CHECK( repaired.before.checksumMismatches == 1 );
  CHECK( repaired.removedBadEntries == 2 );
  CHECK( repaired.remirror.mirrored == 2 );
  CHECK( repaired.remirror.alreadyPresent == 2 );

  // The mirror is healthy again — the full proof holds.
  const MirrorVerifyReport after = verifyMirror( fix.mirrorDir );
  CHECK( after.entriesChecked == 4 );
  CHECK( after.ok == 4 );
  CHECK( after.missingFiles == 0 );
  CHECK( after.checksumMismatches == 0 );

  // Repairing a healthy mirror changes nothing.
  const MirrorRepairReport idempotent = repairMirror( fix.plan, { fix.mirrorDir } );
  CHECK( idempotent.removedBadEntries == 0 );
  CHECK( idempotent.remirror.mirrored == 0 );
  CHECK( idempotent.remirror.alreadyPresent == 4 );
}

TEST_CASE( "repairMirror of LOCAL sources completes while forced offline",
           "[io][fabric][mirror][repair][offline][utc12]" )
{
  Fixture fix( "repair_offline" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );
  const std::vector<std::string> chunks = fix.chunkFiles();
  std::filesystem::remove( chunks[0] );
  std::filesystem::remove( chunks[1] );

  // The offline gate refuses REMOTE targets only — a local-source mirror is
  // exactly the out-of-core asset a network outage must not strand. The
  // repair of deleted chunks completes offline: cleanup (pure local) plus
  // re-materialization through the local read path. Remote-source repairs
  // inherit the per-chunk offline refusal through the same read path (the
  // chunk-walk failure isolation proven by the fabric-scale suite).
  offline::setEnabled( true );
  const MirrorRepairReport offlineReport = repairMirror( fix.plan, { fix.mirrorDir } );
  offline::setEnabled( false );
  CHECK( offlineReport.removedBadEntries == 2 );
  CHECK( offlineReport.remirror.mirrored == 2 );
  CHECK( offlineReport.remirror.failed == 0 );
  CHECK( verifyMirror( fix.mirrorDir ).ok == 4 );
}

TEST_CASE( "pruneMirror garbage-collects orphans, dead entries, expiry and quota",
           "[io][fabric][mirror][prune][utc12]" )
{
  Fixture fix( "prune" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );
  const std::string manifestPath = fix.mirrorDir + "/manifest.json";

  // 1) Orphan file: in chunks/, never named by the manifest.
  {
    std::ofstream out( fix.mirrorDir + "/chunks/orphan0000000000000000000000000f.tif",
                       std::ios::binary );
    out << "garbage";
  }
  MirrorPruneReport pruned = pruneMirror( fix.mirrorDir, {} );
  CHECK( pruned.orphanFilesRemoved == 1 );
  CHECK( pruned.keptEntries == 4 );
  CHECK( verifyMirror( fix.mirrorDir ).ok == 4 );

  // 2) Dead entry: drop a payload behind the manifest's back; prune drops
  //    the entry instead of leaving it unresolvable.
  const std::vector<std::string> chunks = fix.chunkFiles();
  REQUIRE( chunks.size() == 4 );
  std::filesystem::remove( chunks[0] );
  pruned = pruneMirror( fix.mirrorDir, {} );
  CHECK( pruned.deadEntriesRemoved == 1 );
  CHECK( pruned.keptEntries == 3 );
  CHECK( verifyMirror( fix.mirrorDir ).ok == 3 );

  // 3) Age expiry: backdate one entry's materialization stamp, prune with a
  //    1-hour horizon — the backdated entry (and only it) is removed with
  //    its file.
  Json::Value manifest;
  {
    std::ifstream in( manifestPath, std::ios::binary );
    std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( text.data(), text.data() + text.size(), &manifest, &errors ) );
  }
  REQUIRE( manifest.isMember( "index" ) );
  std::string backdatedToken;
  std::string backdatedKey;
  for ( const std::string &token : manifest.getMemberNames() )
  {
    if ( token == "index" )
      continue;
    const Json::Value &chunksJson = manifest[token];
    if ( chunksJson.isObject() && chunksJson.size() > 0 )
    {
      backdatedToken = token;
      backdatedKey = chunksJson.getMemberNames().front();
      break;
    }
  }
  REQUIRE( !backdatedToken.empty() );
  REQUIRE( manifest[backdatedToken][backdatedKey]["writtenUtc"].isString() );
  manifest[backdatedToken][backdatedKey]["writtenUtc"] = "2020-01-01T00:00:00Z";
  {
    std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
    out << Json::writeString( Json::StreamWriterBuilder(), manifest );
  }
  MirrorPruneOptions aged;
  aged.maxAgeSeconds = 3600;
  pruned = pruneMirror( fix.mirrorDir, aged );
  CHECK( pruned.expiredEntriesRemoved == 1 );
  CHECK( pruned.keptEntries == 2 );
  CHECK( verifyMirror( fix.mirrorDir ).ok == 2 );

  // 4) Quota: a maxBytes budget below the remaining bytes drops the oldest
  //    stamped entries first; entries without a stamp are un-evictable.
  //    Strip one survivor's stamp, then demand a 1-chunk budget.
  {
    Json::Value rewritten;
    std::ifstream in( manifestPath, std::ios::binary );
    std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( text.data(), text.data() + text.size(), &rewritten, &errors ) );
    for ( const std::string &token : rewritten.getMemberNames() )
    {
      if ( token == "index" )
        continue;
      Json::Value &chunksJson = rewritten[token];
      if ( chunksJson.isObject() )
      {
        for ( const std::string &key : chunksJson.getMemberNames() )
        {
          if ( chunksJson[key]["writtenUtc"].isString() )
          {
            chunksJson[key].removeMember( "writtenUtc" );
            break;
          }
        }
        break;
      }
    }
    std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
    out << Json::writeString( Json::StreamWriterBuilder(), rewritten );
  }
  MirrorPruneOptions quota;
  quota.maxBytes = 1024; // below any single chunk: only the unstamped survives
  pruned = pruneMirror( fix.mirrorDir, quota );
  // Exactly the stamped survivor is evictable (oldest-stamp-first); the
  // unstamped entry is un-evictable by design — absence is not evidence.
  CHECK( pruned.quotaEntriesRemoved == 1 );
  CHECK( pruned.keptEntries == 1 );
  CHECK( verifyMirror( fix.mirrorDir ).ok == 1 );
}

TEST_CASE( "a leftover writer.lock from a crashed writer is broken by pid or age (#1163)",
           "[io][fabric][mirror][lock][issue1163]" )
{
  Fixture fix( "stalelock" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );

  // 1) Empty pre-#1163 leftover with a BACKDATED mtime: age rule breaks it,
  //    the prune pass proceeds.
  {
    std::ofstream out( fix.mirrorDir + "/writer.lock", std::ios::binary );
    out << "";
  }
  {
    struct ::stat st {};
    REQUIRE( ::stat( ( fix.mirrorDir + "/writer.lock" ).c_str(), &st ) == 0 );
    std::filesystem::last_write_time(
        fix.mirrorDir + "/writer.lock",
        std::filesystem::last_write_time( fix.mirrorDir + "/writer.lock" ) - std::chrono::hours( 2 ) );
  }
  std::filesystem::copy_file( fix.chunkFiles()[0],
                              fix.mirrorDir + "/chunks/orphan_stale_lock.tif" );
  MirrorPruneReport pruned = pruneMirror( fix.mirrorDir, {} );
  CHECK( pruned.keptEntries == 4 );
  CHECK( std::filesystem::remove( fix.mirrorDir + "/chunks/orphan_stale_lock.tif" ) );
  pruned = pruneMirror( fix.mirrorDir, {} );
  CHECK( pruned.orphanFilesRemoved == 1 );
  CHECK( !std::filesystem::exists( fix.mirrorDir + "/writer.lock" ) );

  // 2) A lock naming a DEAD pid (the crash case with the #1163 stamp): the
  //    liveness rule breaks it even though the stamp is fresh.
  {
    std::ofstream out( fix.mirrorDir + "/writer.lock", std::ios::binary );
    out << "999999999 " << std::time( nullptr ) << "\n";
  }
  pruned = pruneMirror( fix.mirrorDir, {} );
  CHECK( pruned.keptEntries == 4 );
  CHECK( !std::filesystem::exists( fix.mirrorDir + "/writer.lock" ) );

  // 3) A lock naming THIS live pid is never stolen: the pass refuses with
  //    the typed single-writer error naming the lock file.
  {
    std::ofstream out( fix.mirrorDir + "/writer.lock", std::ios::binary );
    out << static_cast<long>( ::getpid() ) << " " << std::time( nullptr ) << "\n";
  }
  bool refused = false;
  try
  {
    static_cast<void>( pruneMirror( fix.mirrorDir, {} ) );
  }
  catch ( const GeoError &e )
  {
    refused = true;
    CHECK( std::string( e.what() ).find( "writer.lock" ) != std::string::npos );
  }
  CHECK( refused );
  CHECK( std::filesystem::exists( fix.mirrorDir + "/writer.lock" ) );
  std::filesystem::remove( fix.mirrorDir + "/writer.lock" );
}

TEST_CASE( "pruneMirror refuses against an unreadable manifest",
           "[io][fabric][mirror][prune][failclosed][utc12]" )
{
  Fixture fix( "prune_refusal" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );
  const std::string manifestPath = fix.mirrorDir + "/manifest.json";
  {
    std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
    out << "{{{ not json ]]>";
  }
  const MirrorPruneReport refused = pruneMirror( fix.mirrorDir, {} );
  CHECK( refused.manifestUnreadable );
  CHECK( refused.keptEntries == 0 );
  // Nothing was deleted: the chunk payloads all survive.
  CHECK( fix.chunkFiles().size() == 4 );
}

TEST_CASE( "repairMirror refuses against an unreadable manifest",
           "[io][fabric][mirror][repair][failclosed][utc12]" )
{
  Fixture fix( "repair_refusal" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );
  const std::string manifestPath = fix.mirrorDir + "/manifest.json";
  {
    std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
    out << "]]] garbage {{{";
  }
  MirrorOptions options;
  options.mirrorDirectory = fix.mirrorDir;
  const MirrorRepairReport refused = repairMirror( fix.plan, options );
  CHECK( refused.manifestUnreadable );
  CHECK( refused.removedBadEntries == 0 );
  CHECK( refused.remirror.mirrored == 0 );
}

TEST_CASE( "verifyMirror detects missing, tampered and orphan chunk files",
           "[io][fabric][mirror][verify][utc12]" )
{
  Fixture fix( "verify_defects" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );
  const std::vector<std::string> chunks = fix.chunkFiles();
  REQUIRE( chunks.size() == 4 );

  // 1) A deleted chunk file is a missing entry (the offline read path
  //    already treats it as a miss; verify makes the state INVENTORYABLE).
  std::filesystem::remove( chunks[0] );
  MirrorVerifyReport verify = verifyMirror( fix.mirrorDir );
  CHECK( verify.missingFiles == 1 );
  CHECK( verify.ok == 3 );

  // 2) A tampered payload (same size, flipped byte) is a checksum mismatch
  //    — the strong form of corruption the replay path refuses.
  const std::string tampered = chunks[1];
  {
    std::fstream file( tampered, std::ios::binary | std::ios::in | std::ios::out );
    file.seekg( 0, std::ios::end );
    const auto size = file.tellg();
    REQUIRE( size > 16 );
    file.seekp( size - 8 );
    char byte = 0;
    file.read( &byte, 1 );
    file.seekp( size - 8 );
    file.put( static_cast<char>( byte ^ 0xFF ) );
  }
  verify = verifyMirror( fix.mirrorDir );
  CHECK( verify.ok == 2 );
  CHECK( verify.checksumMismatches == 1 );

  // 3) An orphan file in chunks/ the manifest never names is inventories
  //    (prune's removal candidate), and the tampered file keeps its size
  //    (only the checksum catches it).
  const std::string orphan = fix.mirrorDir + "/chunks/deadbeef00000000000000000000000f.tif";
  {
    std::ofstream out( orphan, std::ios::binary );
    out << "orphan bytes";
  }
  verify = verifyMirror( fix.mirrorDir );
  CHECK( verify.unreferencedFiles == 1 );
  CHECK( verify.unreferencedBytes == std::string( "orphan bytes" ).size() );
  CHECK( verify.entriesChecked == 4 );
}

TEST_CASE( "pruneMirror removes UTF-8 orphans and refuses unsafe entries",
           "[io][fabric][mirror][prune][unicode][utc13]" )
{
  Fixture fix( "prune_unicode" );
  const MirrorReport report = fix.materialize();
  REQUIRE( report.mirrored == 4 );
  const std::string manifestPath = fix.mirrorDir + "/manifest.json";
  const std::string chunksDir = fix.mirrorDir + "/chunks";

  // 1) A manifest-REFERENCED non-ASCII name verifies healthy: the manifest
  //    domain is UTF-8, and both the stat (VSIStatL) and the sha256 proof
  //    (VSIFOpenL) resolve it to the native spelling on every platform.
  const std::vector<std::string> chunks = fix.chunkFiles();
  REQUIRE( chunks.size() == 4 );
  const std::string utf8Name = "块_碎片.tif"; // UTF-8 in the manifest domain
  {
    Json::Value manifest;
    {
      std::ifstream in( manifestPath, std::ios::binary );
      std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
      Json::CharReaderBuilder builder;
      std::string errors;
      std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
      REQUIRE( reader->parse( text.data(), text.data() + text.size(), &manifest, &errors ) );
    }
    const std::string oldName = std::filesystem::path( chunks[0] ).filename().string();
    bool renamedEntry = false;
    for ( const std::string &token : manifest.getMemberNames() )
    {
      if ( token == "index" )
        continue;
      Json::Value &chunksJson = manifest[token];
      if ( !chunksJson.isObject() )
        continue;
      for ( const std::string &key : chunksJson.getMemberNames() )
      {
        if ( chunksJson[key]["file"].asString() == oldName )
        {
          chunksJson[key]["file"] = utf8Name;
          renamedEntry = true;
        }
      }
    }
    REQUIRE( renamedEntry );
    {
      std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
      out << Json::writeString( Json::StreamWriterBuilder(), manifest );
    }
    // The rename itself stays inside the UTF-8 path discipline — a narrow
    // path would route the name through the active code page on Windows.
    std::filesystem::rename( std::filesystem::u8path( chunks[0] ),
                             std::filesystem::u8path( chunksDir + "/" + utf8Name ) );
  }
  {
    const MirrorVerifyReport verify = verifyMirror( fix.mirrorDir );
    CHECK( verify.ok == 4 );
    CHECK( verify.unreferencedFiles == 0 ); // a referenced name is no orphan
  }

  // 2) An UNREFERENCED non-ASCII file is inventoried and safely deleted
  //    inside the mirror root (12.0 counted it but never removed it).
  const std::string utf8Orphan = chunksDir + "/游离碎片.tif";
  {
    std::ofstream out( std::filesystem::u8path( utf8Orphan ), std::ios::binary );
    out << "orphan utf8";
  }
  CHECK( verifyMirror( fix.mirrorDir ).unreferencedFiles == 1 );
  MirrorPruneReport pruned = pruneMirror( fix.mirrorDir, {} );
  CHECK( pruned.orphanFilesRemoved == 1 );
  CHECK( pruned.orphanFilesRefused == 0 );
  CHECK( !std::filesystem::exists( std::filesystem::u8path( utf8Orphan ) ) );
  // The referenced UTF-8 file is untouched and the mirror stays healthy.
  CHECK( verifyMirror( fix.mirrorDir ).ok == 4 );

  // 3) A symlink inside chunks/ is never deleted through — it is a
  //    refusal, and neither link nor (out-of-mirror) target is touched.
  const std::string linkPath = chunksDir + "/escape_link.tif";
  std::error_code linkEc;
  std::filesystem::create_symlink( fix.scene, linkPath, linkEc );
  if ( !linkEc )
  {
    pruned = pruneMirror( fix.mirrorDir, {} );
    CHECK( pruned.orphanFilesRefused >= 1 );
    CHECK( std::filesystem::exists( linkPath ) );
    CHECK( std::filesystem::exists( fix.scene ) );
  }
  else
  {
    INFO( "symlink creation unavailable (privileges) — refusal arm skipped" );
  }

  // 4) An orphan that cannot be unlinked is a counted FAILURE, kept — the
  //    report distinguishes "refused" from "tried and failed".
#ifndef _WIN32
  const std::string lockedOrphan = chunksDir + "/locked_orphan.tif";
  {
    std::ofstream out( lockedOrphan, std::ios::binary );
    out << "cannot unlink me";
  }
  namespace fs = std::filesystem;
  std::error_code permEc;
  fs::permissions( chunksDir, fs::perms::owner_read | fs::perms::owner_exec,
                   fs::perm_options::replace, permEc );
  REQUIRE( !permEc );
  pruned = pruneMirror( fix.mirrorDir, {} );
  CHECK( pruned.orphanFilesFailed >= 1 );
  CHECK( fs::exists( lockedOrphan ) );
  fs::permissions( chunksDir, fs::perms::owner_all, fs::perm_options::replace, permEc );
  REQUIRE( !permEc );
  pruned = pruneMirror( fix.mirrorDir, {} );
  CHECK( pruned.orphanFilesRemoved >= 1 );
  CHECK( !fs::exists( lockedOrphan ) );
#endif

  CHECK( verifyMirror( fix.mirrorDir ).ok == 4 );
}
