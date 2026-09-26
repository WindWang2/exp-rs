/***************************************************************************
  tests/test_portability_source_contract.cpp — static-evidence portability
  contract (Cross-Platform R2).

  The portability hazards that POSIX cannot observe at runtime (Windows ACP
  narrow opens, the UTF-8→path boundary's wide branch, the durability ordering
  inside a publish) still need a tripwire. Following the established
  source-contract precedent (test_cli_command_surface reads cli_commands.cpp;
  test_diagnostics_contract_9 reads platform sources), this suite reads the
  repo's own sources via CMAKE_SOURCE_DIR and pins:

    C1  platform/portable.h's pathFromUtf8 must decode UTF-8 through the
        wide API on Windows — a regression to the narrow fs::path(std::string)
        constructor would re-encode every non-ASCII path into the ANSI code
        page and pass every POSIX-lane test silently.
    C2  the durability publishers (stage ledger, finalize manifest, mirror
        lock, range block store) open through the UTF-8 boundary, never a
        narrow std::string render.
    C3  the session journal publish sequence stays
        stage-write → file sync → atomic rename (no remove-before-rename
        window, no unsynced publish).
    C4  path-valued environment reads go through envUtf8 at the boundaries
        this track converged (workspace sandbox root, curriculum root).

  Nothing here links QGIS/Qt/GDAL — the whole suite builds in the lightest
  lane, next to test_platform_portability.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string repoSource( const char *cmakeSourceDir, const std::string &relative )
{
  const std::string path = std::string( cmakeSourceDir ) + "/" + relative;
  std::ifstream in( path, std::ios::binary );
  if ( !in )
  {
    WARN( "cannot read repo source (deployed-test layout?): " << path );
    return std::string();
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

#ifndef SICNU_TEST_CMAKE_SOURCE_DIR
#define SICNU_TEST_CMAKE_SOURCE_DIR "."
#endif

/// Skips a contract when the sources are unavailable (test binary deployed
/// away from the repo — the m2 dialog stress suite's detection pattern).
bool sourcesAvailable()
{
  std::ifstream probe( std::string( SICNU_TEST_CMAKE_SOURCE_DIR )
                         + "/src/platform/portable.h",
                       std::ios::binary );
  return static_cast<bool>( probe );
}

} // namespace

TEST_CASE( "portable.h: the Windows branch of pathFromUtf8 rides the wide API",
           "[portability][contract][static]" )
{
  if ( !sourcesAvailable() )
    return;
  const std::string header =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/platform/portable.h" );
  REQUIRE_FALSE( header.empty() );

  // pathFromUtf8's _WIN32 branch must call wideFromUtf8 (the mutation this
  // pins — swapping it for the narrow fs::path(std::string) constructor —
  // compiled and passed the whole POSIX lane while corrupting every
  // non-ASCII path on Windows).
  const std::size_t signature = header.find( "inline std::filesystem::path pathFromUtf8(" );
  REQUIRE( signature != std::string::npos );
  const std::size_t bodyEnd = header.find( "\n}", signature );
  REQUIRE( bodyEnd != std::string::npos );
  const std::string body = header.substr( signature, bodyEnd - signature );
  INFO( "pathFromUtf8 body:\n" << body );
  REQUIRE( body.find( "wideFromUtf8" ) != std::string::npos );
  REQUIRE( body.find( "_WIN32" ) != std::string::npos );
}

TEST_CASE( "durability publishers open through the UTF-8 path boundary",
           "[portability][contract][static][fs]" )
{
  if ( !sourcesAvailable() )
    return;

  struct Publisher
  {
    const char *file;
    const char *forbiddenNarrowOpen;
    const char *requiredBoundary;
  };
  const Publisher publishers[] = {
    // The staged-file callback inside atomic_fs::writeFileAtomic — a narrow
    // ofstream over the UTF-8 staged path string.
    { "src/geospatial/io/stage_ledger.cpp", "std::ofstream file( stagedPath",
      "sicnu::portable::pathFromUtf8( stagedPath" },
    { "src/geospatial/io/finalize_manifest.cpp", "std::ofstream file( stagedPath",
      "sicnu::portable::pathFromUtf8( stagedPath" },
    { "src/geospatial/io/finalize_manifest.cpp", "std::ifstream file( manifestPath",
      "sicnu::portable::pathFromUtf8( manifestPath" },
    { "src/geospatial/fabric/mirror.cpp", "std::ifstream in( mLockPath )",
      "sicnu::portable::pathFromUtf8( mLockPath" },
    // The block store's C file API over a UTF-8 path value: narrow fopen is
    // the ACP decode; fileOpenUtf8 widens on Windows.
    { "src/geospatial/remote/range_cache_disk.cpp", "std::fopen( tempPath.c_str()",
      "sicnu::portable::fileOpenUtf8( tempPath" },
    { "src/geospatial/remote/range_cache_disk.cpp", "std::fopen( path.c_str()",
      "sicnu::portable::fileOpenUtf8( path" },
  };
  for ( const Publisher &publisher : publishers )
  {
    const std::string source = repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, publisher.file );
    REQUIRE_FALSE( source.empty() );
    INFO( "file: " << publisher.file );
    REQUIRE( source.find( publisher.requiredBoundary ) != std::string::npos );
    REQUIRE( source.find( publisher.forbiddenNarrowOpen ) == std::string::npos );
  }
}

TEST_CASE( "journal publish stays stage-write → file sync → atomic rename",
           "[portability][contract][static][durability]" )
{
  if ( !sourcesAvailable() )
    return;
  const std::string source =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/agent_loop/session_journal.cpp" );
  REQUIRE_FALSE( source.empty() );

  // Ordering contract inside writeFileAtomic: the file-sync durability gate
  // sits between the staging write and the publishing rename.
  const std::size_t stagingWrite = source.find( "std::ofstream out( temp" );
  const std::size_t syncGate = source.find( "sicnu::portable::syncFileUtf8" );
  const std::size_t publish = source.find( "fs::rename( temp, target, ec )" );
  INFO( "stagingWrite=" << stagingWrite << " syncGate=" << syncGate
                        << " publish=" << publish );
  REQUIRE( stagingWrite != std::string::npos );
  REQUIRE( syncGate != std::string::npos );
  REQUIRE( publish != std::string::npos );
  REQUIRE( stagingWrite < syncGate );
  REQUIRE( syncGate < publish );

  // The publish is a single atomic replace: no remove-before-rename window
  // (a crash there destroyed the audit trail the journal exists to keep).
  REQUIRE( source.find( "fs::remove( target, ec );\n    fs::rename(" ) == std::string::npos );
  // Staging names are unique (pid/counter), never the shared "<name>.tmp".
  REQUIRE( source.find( "target.filename().string() + \".tmp\"" ) == std::string::npos );
  REQUIRE( source.find( "stagingCounter" ) != std::string::npos );
}

TEST_CASE( "path-valued environment reads enter through envUtf8",
           "[portability][contract][static][env]" )
{
  if ( !sourcesAvailable() )
    return;

  // Each pair is (file, boundary call). The workspace root is a security
  // input: an ACP getenv there corrupts the sandbox root on Windows.
  const std::pair<const char *, const char *> boundaries[] = {
    { "src/sdk/exprs/path_policy.cpp", "sicnu::portable::envUtf8( \"SICNU_MCP_WORKSPACE\" )" },
    { "src/agent/harness/curriculum_catalog.cpp",
      "sicnu::portable::envUtf8( name )" },
    { "src/lab/session_store.cpp",
      "sicnu::portable::envUtf8( \"SICNU_LAB_SESSION_DIR\" )" },
    { "src/recipes/recipe_registry.cpp",
      "sicnu::portable::envUtf8( \"SICNU_SCIENTIFIC_RECIPES_DIR\" )" },
  };
  for ( const auto &boundary : boundaries )
  {
    const std::string source = repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, boundary.first );
    REQUIRE_FALSE( source.empty() );
    INFO( "file: " << boundary.first );
    REQUIRE( source.find( boundary.second ) != std::string::npos );
  }
  // The narrow getenv read of the workspace root must stay gone.
  const std::string policy =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/sdk/exprs/path_policy.cpp" );
  REQUIRE( policy.find( "std::getenv( \"SICNU_MCP_WORKSPACE\" )" ) == std::string::npos );
}


TEST_CASE( "portable.h: the staging/durability helpers ride the wide, exclusive API",
           "[portability][contract][static][fs]" )
{
  if ( !sourcesAvailable() )
    return;
  const std::string header =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/platform/portable.h" );
  REQUIRE_FALSE( header.empty() );

  // claimExclusiveUtf8 must CLAIM through CREATE_NEW on Windows (the
  // check-then-use antidote). A mutation to OPEN_ALWAYS (or a plain
  // CreateFileW without CREATE_NEW) compiles and passes every POSIX lane
  // while reopening the staging-collision race the helper exists to close.
  {
    const std::size_t signature = header.find( "inline bool claimExclusiveUtf8(" );
    REQUIRE( signature != std::string::npos );
    const std::size_t bodyEnd = header.find( "\n}", signature );
    REQUIRE( bodyEnd != std::string::npos );
    const std::string body = header.substr( signature, bodyEnd - signature );
    INFO( "claimExclusiveUtf8 body:\n" << body );
    REQUIRE( body.find( "CREATE_NEW" ) != std::string::npos );
    REQUIRE( body.find( "O_EXCL" ) != std::string::npos );
  }

  // syncFileUtf8 must flush through FlushFileBuffers on Windows and fsync(2)
  // on POSIX — a silent drop of the flush would reopen the crash window the
  // publish contract closes.
  {
    const std::size_t signature = header.find( "inline bool syncFileUtf8(" );
    REQUIRE( signature != std::string::npos );
    const std::size_t bodyEnd = header.find( "\n}", signature );
    REQUIRE( bodyEnd != std::string::npos );
    const std::string body = header.substr( signature, bodyEnd - signature );
    INFO( "syncFileUtf8 body:\n" << body );
    REQUIRE( body.find( "FlushFileBuffers" ) != std::string::npos );
    REQUIRE( body.find( "::fsync" ) != std::string::npos );
  }

  // fileOpenUtf8 must widen on Windows (the narrow fopen decodes with the
  // process ANSI code page and misses non-ASCII cache directories).
  {
    const std::size_t signature = header.find( "inline std::FILE *fileOpenUtf8(" );
    REQUIRE( signature != std::string::npos );
    const std::size_t bodyEnd = header.find( "\n}", signature );
    REQUIRE( bodyEnd != std::string::npos );
    const std::string body = header.substr( signature, bodyEnd - signature );
    INFO( "fileOpenUtf8 body:\n" << body );
    REQUIRE( body.find( "_wfopen" ) != std::string::npos );
    REQUIRE( body.find( "wideFromUtf8" ) != std::string::npos );
  }
}

TEST_CASE( "atomic publish callers never hand-roll the staging claim",
           "[portability][contract][static][staging]" )
{
  if ( !sourcesAvailable() )
    return;

  // Hand-rolled check-then-use staging (exists() then open) is the TOCTOU
  // race the exclusive claim closes: each publisher in the table must go
  // through the helper, never a hand-rolled two-step.
  struct Claim
  {
    const char *file;
    const char *required;
  };
  const Claim claims[] = {
    // The journal's staging claim goes through the portable helper.
    { "src/agent_loop/session_journal.cpp",
      "sicnu::portable::claimExclusiveUtf8( sicnu::portable::pathToUtf8( temp ) )" },
  };
  for ( const Claim &claim : claims )
  {
    const std::string source = repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, claim.file );
    REQUIRE_FALSE( source.empty() );
    INFO( "file: " << claim.file );
    REQUIRE( source.find( claim.required ) != std::string::npos );
  }
}
