/***************************************************************************
  tests/test_atomic_fs_caller_contract.cpp
  core-foundations-r4 (Track 5) — atomic_fs caller contract lane.
  ---------------------------
  Begin                : 2026-09-27
  Copyright            : (C) 2026 SICNU GEO RS

  Two kinds of truth, per the repo contract-test convention:

  1. Runtime contract cases (Part A) exercise sicnu::geo::atomic_fs itself
     against the contract stated in atomic_fs.h — byte-exact publication,
     no torn state on writer failure, staged residue always cleaned,
     rename-over-existing on every platform, typed GeoError propagation.
     The authority is the header contract, never the implementation.

  2. Source-contract cases (Part B/C) pin the caller-side unification this
     track landed: every staged publisher flushes staged bytes through
     atomic_fs::fsyncFile BEFORE the publish call (the crash window the
     runtime contract closes on the writeFileAtomic path must not reopen on
     hand-rolled publish sites), and every session-journal staging failure
     path cleans the claimed staged file. Same style as
     test_portability_source_contract.cpp (relative-order source pins).
 ***************************************************************************/

#include "geospatial/util/atomic_fs.h"
#include "platform/portable.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace
{

std::string u8( const fs::path &path )
{
  const std::u8string text = path.u8string();
  return std::string( text.begin(), text.end() );
}

/// Unique scratch directory that removes itself; paths keep UTF-8 bytes so a
/// non-ASCII component rides every API the cases touch.
class ScratchDir
{
public:
  ScratchDir()
  {
    static std::atomic<unsigned> counter{ 0 };
    std::random_device rd;
    const fs::path base = fs::temp_directory_path() /
                          fs::path( "sicnu_atomic_fs_contract_" + std::to_string( sicnu::portable::pid() ) +
                                    "_" + std::to_string( counter++ ) + "_" + std::to_string( rd() ) );
    fs::create_directories( base );
    mPath = base;
  }

  ~ScratchDir() { std::error_code ec; fs::remove_all( mPath, ec ); }

  ScratchDir( const ScratchDir & ) = delete;
  ScratchDir &operator=( const ScratchDir & ) = delete;

  const fs::path &path() const { return mPath; }

  std::string childUtf8( const std::string &name ) const { return u8( mPath / fs::path( name ) ); }

  /// Entries (UTF-8 names) created since the snapshot — used to detect staged
  /// residue without hard-coding the staging name pattern.
  std::vector<std::string> entries() const
  {
    std::vector<std::string> names;
    for ( const fs::directory_entry &entry : fs::directory_iterator( mPath ) )
      names.push_back( u8( entry.path().filename() ) );
    std::sort( names.begin(), names.end() );
    return names;
  }

private:
  fs::path mPath;
};

void writeBytes( const std::string &path, const std::string &bytes )
{
  std::ofstream out( sicnu::portable::pathFromUtf8( path ), std::ios::binary | std::ios::trunc );
  REQUIRE( static_cast<bool>( out ) );
  out.write( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
  REQUIRE( static_cast<bool>( out ) );
}

std::string readBytes( const std::string &path )
{
  std::ifstream in( sicnu::portable::pathFromUtf8( path ), std::ios::binary );
  REQUIRE( static_cast<bool>( in ) );
  return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}

bool containsTmpEntry( const std::vector<std::string> &names )
{
  return std::any_of( names.begin(), names.end(), []( const std::string &name ) {
    return name.find( ".tmp" ) != std::string::npos;
  } );
}

/// Repo source reader for the static contract cases (same lane as
/// test_portability_source_contract.cpp).
std::string repoSource( const std::string &sourceDir, const char *relative )
{
  std::ifstream in( sourceDir + "/" + relative, std::ios::binary );
  if ( !in )
    return std::string();
  return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}

bool sourcesAvailable()
{
  std::ifstream probe( std::string( CMAKE_SOURCE_DIR ) + "/src/platform/portable.h",
                       std::ios::binary );
  return static_cast<bool>( probe );
}

} // namespace

// ============================================================================
// Part A — runtime contract (authority: the atomic_fs.h header contract)
// ============================================================================

TEST_CASE( "writeFileAtomic publishes exactly the writer's bytes and leaves no staged residue",
           "[atomic_fs][contract]" )
{
  ScratchDir dir;
  const std::string target = dir.childUtf8( "out put.bin" );
  const std::string payload = std::string( "bin\x00ary \xe4\xb8\xad\xe6\x96\x87 bytes", 20 );

  sicnu::geo::atomic_fs::writeFileAtomic( target, [ & ]( const std::string &staged ) {
    writeBytes( staged, payload );
  } );

  REQUIRE( readBytes( target ) == payload );
  REQUIRE_FALSE( containsTmpEntry( dir.entries() ) );
}

TEST_CASE( "writeFileAtomic keeps the previous target bytes when the writer throws",
           "[atomic_fs][contract][torn-state]" )
{
  ScratchDir dir;
  const std::string target = dir.childUtf8( "journal.json" );
  const std::string previous = "{\"good\":true}";
  writeBytes( target, previous );
  const std::vector<std::string> before = dir.entries();

  try
  {
    sicnu::geo::atomic_fs::writeFileAtomic( target, [ & ]( const std::string &staged ) {
      // Torn write: partial bytes reach the staged file, then the writer dies.
      writeBytes( staged, "{\"partial" );
      throw std::runtime_error( "writer exploded mid-write" );
    } );
    FAIL( "the writer exception must propagate" );
  }
  catch ( const std::runtime_error &ex )
  {
    REQUIRE( std::string( ex.what() ) == "writer exploded mid-write" );
  }

  // No torn state: the target still holds the previous good bytes, exactly.
  REQUIRE( readBytes( target ) == previous );
  // No staged residue: the claimed staging file (and sidecars) is discarded.
  REQUIRE_FALSE( containsTmpEntry( dir.entries() ) );
  // The failure produced no other new entries either.
  REQUIRE( dir.entries() == before );
}

TEST_CASE( "stagedPathFor claims a unique O_EXCL name beside the target",
           "[atomic_fs][staging]" )
{
  ScratchDir dir;
  const std::string target = dir.childUtf8( "scene.tif" );

  const std::string first = sicnu::geo::atomic_fs::stagedPathFor( target );
  const std::string second = sicnu::geo::atomic_fs::stagedPathFor( target );

  REQUIRE( first != second );
  REQUIRE( first != target );
  // Claimed = the staging file already exists (the check-then-use antidote).
  REQUIRE( sicnu::geo::atomic_fs::fileExists( first ) );
  REQUIRE( sicnu::geo::atomic_fs::fileExists( second ) );
  // Same directory as the target (rename stays on one volume).
  REQUIRE( u8( sicnu::portable::pathFromUtf8( first ).parent_path() ) ==
           u8( sicnu::portable::pathFromUtf8( target ).parent_path() ) );
  // Staging keeps the final extension so extension-driven drivers recognize it.
  REQUIRE( first.find( ".tmp.tif" ) != std::string::npos );
  // The pid component formats identically across platforms.
  REQUIRE( first.find( "." + std::to_string( sicnu::portable::pid() ) + "." ) != std::string::npos );

  REQUIRE( sicnu::geo::atomic_fs::removeFileQuiet( first ) );
  REQUIRE( sicnu::geo::atomic_fs::removeFileQuiet( second ) );
}

TEST_CASE( "publishStagedFile replaces an existing target and consumes the staged file",
           "[atomic_fs][publish]" )
{
  ScratchDir dir;
  const std::string target = dir.childUtf8( "map.png" );
  const std::string staged = dir.childUtf8( "map.staged.png" );
  writeBytes( target, "previous good png" );
  writeBytes( staged, "new png bytes" );

  sicnu::geo::atomic_fs::publishStagedFile( staged, target );

  // rename-over-existing must succeed on every platform (Windows explicit).
  REQUIRE( readBytes( target ) == "new png bytes" );
  REQUIRE_FALSE( sicnu::geo::atomic_fs::fileExists( staged ) );
}

TEST_CASE( "publishStagedFile fails closed with a typed error when the staged file is missing",
           "[atomic_fs][publish][typed]" )
{
  ScratchDir dir;
  const std::string target = dir.childUtf8( "map.png" );
  writeBytes( target, "previous good png" );
  const std::string staged = dir.childUtf8( "never-written.png" );

  REQUIRE_THROWS_AS( sicnu::geo::atomic_fs::publishStagedFile( staged, target ),
                     sicnu::geo::GeoError );
  // The target is untouched by the failed publish.
  REQUIRE( readBytes( target ) == "previous good png" );
}

TEST_CASE( "renameReplaceQuiet replaces the destination; a missing source is a quiet false",
           "[atomic_fs][publish][soft-path]" )
{
  ScratchDir dir;
  const std::string from = dir.childUtf8( "gc-a.bin" );
  const std::string to = dir.childUtf8( "gc-b.bin" );
  writeBytes( from, "quarantine payload" );
  writeBytes( to, "old occupant" );

  REQUIRE( sicnu::geo::atomic_fs::renameReplaceQuiet( from, to ) );
  REQUIRE( readBytes( to ) == "quarantine payload" );
  REQUIRE_FALSE( sicnu::geo::atomic_fs::fileExists( from ) );
  // Missing source: false, and the destination is untouched.
  REQUIRE_FALSE( sicnu::geo::atomic_fs::renameReplaceQuiet( from, to ) );
  REQUIRE( readBytes( to ) == "quarantine payload" );
}

TEST_CASE( "removeFileQuiet and fileExists classify missing, file and directory inputs without throwing",
           "[atomic_fs][cleanup]" )
{
  ScratchDir dir;
  const std::string file = dir.childUtf8( "sweep.bin" );
  writeBytes( file, "x" );
  const std::string subDir = dir.childUtf8( "nested" );
  fs::create_directories( sicnu::portable::pathFromUtf8( subDir ) );

  // removeFileQuiet: existing file removed; missing is success (not an error).
  REQUIRE( sicnu::geo::atomic_fs::removeFileQuiet( file ) );
  REQUIRE_FALSE( sicnu::geo::atomic_fs::fileExists( file ) );
  REQUIRE( sicnu::geo::atomic_fs::removeFileQuiet( file ) );

  // fileExists: regular file yes, directory no, missing no; empty input is
  // a quiet false (no exception on weird input — header contract).
  REQUIRE( sicnu::geo::atomic_fs::fileExists( file ) == false );
  REQUIRE( sicnu::geo::atomic_fs::fileExists( subDir ) == false );
  REQUIRE_FALSE( sicnu::geo::atomic_fs::fileExists( "" ) );
}

TEST_CASE( "fsyncFile throws a typed GeoError for a missing file",
           "[atomic_fs][durability][typed]" )
{
  ScratchDir dir;
  REQUIRE_THROWS_AS( sicnu::geo::atomic_fs::fsyncFile( dir.childUtf8( "missing.bin" ) ),
                     sicnu::geo::GeoError );

  // The real gate: an existing file syncs without error.
  const std::string file = dir.childUtf8( "synced.bin" );
  writeBytes( file, "flush me" );
  REQUIRE_NOTHROW( sicnu::geo::atomic_fs::fsyncFile( file ) );
}

TEST_CASE( "publishStagedGroup publishes the sidecar-first group and clears backups",
           "[atomic_fs][group]" )
{
  ScratchDir dir;
  const std::string targetMain = dir.childUtf8( " parcels.shp" );
  const std::string targetShx = dir.childUtf8( " parcels.shx" );
  const std::string stagedMain = dir.childUtf8( " parcels.new.shp" );
  const std::string stagedShx = dir.childUtf8( " parcels.new.shx" );

  writeBytes( targetMain, "old main" );
  writeBytes( targetShx, "old shx" );
  writeBytes( stagedMain, "new main" );
  writeBytes( stagedShx, "new shx" );

  REQUIRE_NOTHROW(
    sicnu::geo::atomic_fs::publishStagedGroup( stagedMain, targetMain ) );

  REQUIRE( readBytes( targetMain ) == "new main" );
  REQUIRE( readBytes( targetShx ) == "new shx" );
  REQUIRE_FALSE( sicnu::geo::atomic_fs::fileExists( stagedMain ) );
  REQUIRE_FALSE( sicnu::geo::atomic_fs::fileExists( stagedShx ) );
  // Success drops the backup set: no ".bak" stragglers.
  REQUIRE_FALSE( sicnu::geo::atomic_fs::fileExists( targetMain + ".bak" ) );
  REQUIRE_FALSE( sicnu::geo::atomic_fs::fileExists( targetShx + ".bak" ) );
}

TEST_CASE( "non-ASCII and mixed-separator targets publish byte-exact through writeFileAtomic",
           "[atomic_fs][utf8]" )
{
  ScratchDir dir;
  // Chinese directory name + spaces in the file name + a trailing mixed
  // separator: the whole chain (staging name, fsync, rename) must treat the
  // bytes as opaque UTF-8.
  const std::string subDir = dir.childUtf8( "\xe6\x95\xb0\xe6\x8d\xae \xe7\x9b\xae\xe5\xbd\x95" ); // "数据 目录"
  fs::create_directories( sicnu::portable::pathFromUtf8( subDir ) );
  const std::string target = subDir + "/\xe5\x9c\xb0\xe5\x9b\xbe out.tif"; // "地图 out.tif"
  const std::string payload = "raster bytes \xf0\x9f\x8c\x8d";

  sicnu::geo::atomic_fs::writeFileAtomic( target, [ & ]( const std::string &staged ) {
    writeBytes( staged, payload );
  } );

  REQUIRE( readBytes( target ) == payload );
  // Round-trip invariant on the delivered name (pathFromUtf8 ∘ pathToUtf8).
  REQUIRE( sicnu::portable::pathToUtf8( sicnu::portable::pathFromUtf8( target ) ) == target );
}

// ============================================================================
// Part B — caller source contract: the fsync gate sits BEFORE every publish
// ============================================================================

TEST_CASE( "staged publishers flush through atomic_fs::fsyncFile before publishing",
           "[atomic_fs][caller][static][durability]" )
{
  if ( !sourcesAvailable() )
    return;

  // Each row: (file, required fsync call, publish anchor it must precede).
  // Truth: the atomic_fs.h contract — "staged files are fsynced before
  // publish (crash leaves old or new, not junk)". The single-file
  // publishers below used to rename page-cache-only bytes into place; the
  // group publishers flush every staged member (existence-guarded to keep
  // publishStagedGroup/Members' skip-missing semantics).
  struct Publisher
  {
    const char *file;
    const char *requiredFsync;
    const char *publishAnchor;
  };
  const Publisher publishers[] = {
    { "src/agent/cartography/export.cpp",
      "atomic_fs::fsyncFile( tempPath.toStdString() )",
      "atomic_fs::publishStagedFile( tempPath.toStdString()" },
    { "src/agent/cartography/export_manifest.cpp",
      "atomic_fs::fsyncFile( tempPath.toStdString() )",
      "atomic_fs::publishStagedFile( tempPath.toStdString()" },
    { "src/app/editing/rs_edit_persistence.cpp",
      "atomic_fs::fsyncFile( written.toStdString() )",
      "atomic_fs::publishStagedFile( written.toStdString()" },
    { "src/app/georeferencer/qgsimagewarper.cpp",
      "atomic_fs::fsyncFile( tmpOutput.toStdString() )",
      "atomic_fs::publishStagedFile( tmpOutput.toStdString()" },
    { "src/analysis/classification/rs_post_process.cpp",
      "atomic_fs::fsyncFile( tmpPath.toStdString() )",
      "atomic_fs::publishStagedFile( tmpPath.toStdString()" },
    { "src/analysis/segmentation/rs_class_raster.cpp",
      "atomic_fs::fsyncFile( tempPath.toStdString() )",
      "atomic_fs::publishStagedFile( tempPath.toStdString()" },
    { "src/workflow/workflow_run_coordinator.cpp",
      "atomic_fs::fsyncFile( tmp.toStdString() )",
      "atomic_fs::publishStagedFile( tmp.toStdString()" },
  };
  for ( const Publisher &publisher : publishers )
  {
    const std::string source = repoSource( CMAKE_SOURCE_DIR, publisher.file );
    REQUIRE_FALSE( source.empty() );
    INFO( "file: " << publisher.file );

    // Walk EVERY publish anchor in the file: each one needs its own fsync
    // occurrence between the previous anchor (or 0) and this anchor. A
    // first-occurrence-only find is blind to the second gated site
    // (export.cpp's atlas page loop) — review P1-3.
    std::size_t prevAnchor = 0;
    std::size_t anchor = source.find( publisher.publishAnchor );
    int sites = 0;
    while ( anchor != std::string::npos )
    {
      ++sites;
      const std::size_t gate = source.find( publisher.requiredFsync, prevAnchor );
      INFO( "site #" << sites << " anchor=" << anchor << " gate=" << gate
                    << " (must be in (" << prevAnchor << ", " << anchor << "))" );
      REQUIRE( gate != std::string::npos );
      REQUIRE( prevAnchor <= gate );
      REQUIRE( gate < anchor );
      prevAnchor = anchor + 1;
      anchor = source.find( publisher.publishAnchor,
                            anchor + std::string( publisher.publishAnchor ).size() );
    }
    INFO( "gated sites found: " << sites );
    REQUIRE( sites >= 1 );
  }
}

TEST_CASE( "group publishers flush every staged member before the group publish",
           "[atomic_fs][caller][static][durability][group]" )
{
  if ( !sourcesAvailable() )
    return;

  // rs_classification_pipeline (publishStagedMembers) must fsync each staged
  // member (existence-guarded: publishStagedMembers skips missing staged
  // files, and the flush must not narrow that contract).
  const std::string pipeline =
    repoSource( CMAKE_SOURCE_DIR, "src/analysis/classification/rs_classification_pipeline.cpp" );
  REQUIRE_FALSE( pipeline.empty() );
  const std::size_t memberFsync = pipeline.find( "atomic_fs::fsyncFile( member.first )" );
  const std::size_t membersPublish = pipeline.find( "atomic_fs::publishStagedMembers( members )" );
  INFO( "memberFsync=" << memberFsync << " membersPublish=" << membersPublish );
  REQUIRE( memberFsync != std::string::npos );
  REQUIRE( membersPublish != std::string::npos );
  REQUIRE( memberFsync < membersPublish );
  // The flush is existence-guarded so a listed-but-missing staged member
  // keeps its publishStagedMembers skip semantics (fail there would be a
  // third behavior, not a unification).
  REQUIRE( pipeline.find( "atomic_fs::fileExists( member.first )" ) != std::string::npos );

  // rs_class_raster (publishStagedGroup) must flush the staged main and the
  // staged sidecars that exist, before the group publish.
  const std::string raster =
    repoSource( CMAKE_SOURCE_DIR, "src/analysis/segmentation/rs_class_raster.cpp" );
  REQUIRE_FALSE( raster.empty() );
  const std::size_t groupFsync = raster.find( "atomic_fs::fsyncFile( stagedSidecar )" );
  const std::size_t mainFsync = raster.find( "atomic_fs::fsyncFile( tempVectorPath.toStdString() )" );
  const std::size_t groupPublish = raster.find( "atomic_fs::publishStagedGroup( tempVectorPath.toStdString()" );
  INFO( "groupFsync=" << groupFsync << " mainFsync=" << mainFsync << " groupPublish=" << groupPublish );
  REQUIRE( groupFsync != std::string::npos );
  REQUIRE( mainFsync != std::string::npos );
  REQUIRE( groupPublish != std::string::npos );
  REQUIRE( groupFsync < groupPublish );
  REQUIRE( mainFsync < groupPublish );

  // Same group-publish discipline for the operator-side producers. The
  // needles follow each file's own formatting (the polygonize operator is
  // compact-style; the tile engine is spaced).
  struct GroupPublisher
  {
    const char *file;
    const char *sidecarFsyncNeedle;
    const char *mainFsyncNeedle;
    const char *groupAnchorNeedle;
  };
  const GroupPublisher groupPublishers[] = {
    { "src/operators/gdal/gdal_polygonize_operator.cpp",
      "atomic_fs::fsyncFile(stagedSidecar)",
      "atomic_fs::fsyncFile(workPath)",
      "atomic_fs::publishStagedGroup(workPath" },
    { "src/operators/runtime/detection_tile_engine.cpp",
      "atomic_fs::fsyncFile( stagedSidecar )",
      "atomic_fs::fsyncFile( workPath.toStdString() )",
      "atomic_fs::publishStagedGroup( workPath.toStdString()" },
  };
  for ( const GroupPublisher &publisher : groupPublishers )
  {
    const std::string source = repoSource( CMAKE_SOURCE_DIR, publisher.file );
    REQUIRE_FALSE( source.empty() );
    INFO( "file: " << publisher.file );
    const std::size_t sidecarFsync = source.find( publisher.sidecarFsyncNeedle );
    const std::size_t mainFsyncGate = source.find( publisher.mainFsyncNeedle );
    const std::size_t groupAnchor = source.find( publisher.groupAnchorNeedle );
    INFO( "sidecarFsync=" << sidecarFsync << " mainFsyncGate=" << mainFsyncGate
                          << " groupAnchor=" << groupAnchor );
    REQUIRE( sidecarFsync != std::string::npos );
    REQUIRE( mainFsyncGate != std::string::npos );
    REQUIRE( groupAnchor != std::string::npos );
    REQUIRE( sidecarFsync < groupAnchor );
    REQUIRE( mainFsyncGate < groupAnchor );
  }
}

// ============================================================================
// Part C — session journal staging failure paths clean the claimed staged file
// ============================================================================

TEST_CASE( "session journal: every staging failure path removes the claimed staged file",
           "[atomic_fs][caller][static][durability][journal]" )
{
  if ( !sourcesAvailable() )
    return;
  const std::string source =
    repoSource( CMAKE_SOURCE_DIR, "src/agent_loop/session_journal.cpp" );
  REQUIRE_FALSE( source.empty() );

  // claimExclusiveUtf8 creates an EMPTY staged file up front. Every early
  // return after the claim must remove it, or a failed save leaks an orphan
  // staging file into the session directory (the exact residue class the
  // atomic_fs contract's discardStaged path exists to prevent).
  for ( const char *marker : { "cannot open temp file", "cannot write temp file" } )
  {
    const std::size_t at = source.find( marker );
    INFO( "marker: " << marker << " at=" << at );
    REQUIRE( at != std::string::npos );
    // The cleanup must sit in THIS branch: after the PREVIOUS branch's
    // "return false" (or the function start) and before this marker. A bare
    // rfind resolves to the previous branch's cleanup when this branch's is
    // deleted — review P1-2 mutation found exactly that blindness.
    const std::size_t prevGiveUp = source.rfind( "return false", at );
    const std::size_t branchStart = prevGiveUp == std::string::npos ? 0 : prevGiveUp;
    const std::size_t cleanup = source.find( "fs::remove( temp, cleanup", branchStart );
    const std::size_t giveUp = source.find( "return false", at );
    INFO( "branchStart=" << branchStart << " cleanup=" << cleanup
                         << " giveUp=" << giveUp );
    REQUIRE( cleanup != std::string::npos );
    REQUIRE( giveUp != std::string::npos );
    REQUIRE( branchStart <= cleanup );
    REQUIRE( cleanup < at );
    REQUIRE( at < giveUp );
  }
}
