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
    C5  a forward declaration keeps the definition's elaborated type kind
        (class/struct) — MSVC decorates the FIRST-seen kind into the mangled
        name, so a mismatch is a Windows-only LNK2019 (see C5's body).

  Nothing here links QGIS/Qt/GDAL — the whole suite builds in the lightest
  lane, next to test_platform_portability.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

std::string trimCopy( const std::string &text )
{
  const std::size_t begin = text.find_first_not_of( " \t\r\n" );
  if ( begin == std::string::npos )
    return std::string();
  const std::size_t end = text.find_last_not_of( " \t\r\n" );
  return text.substr( begin, end - begin + 1 );
}

/// The line with its trailing `// ...` comment removed.
std::string codeOf( std::string line )
{
  const std::size_t comment = line.find( "//" );
  if ( comment != std::string::npos )
    line.erase( comment );
  return trimCopy( line );
}

/// Elaborated type specifier (`struct`/`class`) that `text` uses for the
/// first declaration of `type`, or an empty string when there is none.
/// `forwardDeclaration` selects between `<kind> <type>;` and a definition
/// (`<kind> <type>` followed by `final`, a base list, the body's `{`, or
/// nothing because the brace sits on the next line).
std::string elaboratedKindOf( const std::string &text, const std::string &type,
                              bool forwardDeclaration )
{
  std::istringstream in( text );
  std::string line;
  while ( std::getline( in, line ) )
  {
    const std::string code = codeOf( std::move( line ) );
    for ( const char *kind : { "struct", "class" } )
    {
      const std::string prefix = std::string( kind ) + " " + type;
      if ( code.rfind( prefix, 0 ) != 0 )
        continue;
      const std::string rest = trimCopy( code.substr( prefix.size() ) );
      if ( forwardDeclaration )
      {
        if ( rest == ";" )
          return kind;
      }
      else if ( rest.empty() || rest.rfind( "final", 0 ) == 0
                || rest.rfind( ":", 0 ) == 0 || rest.rfind( "{", 0 ) == 0 )
      {
        return kind;
      }
    }
  }
  return std::string();
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

TEST_CASE( "durable sidecar authority stays stage-write -> file sync -> atomic publish",
           "[portability][contract][static][durability]" )
{
  if ( !sourcesAvailable() )
    return;
  // R6: the single sidecar write authority owns the ordering contract the
  // journal implementation used to pin directly. The durability gate sits
  // between the staging write and the publishing rename.
  const std::string source =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/platform/durable_sidecar.cpp" );
  REQUIRE_FALSE( source.empty() );

  const std::size_t stagingWrite = source.find( "writeTempFile( staged" );
  const std::size_t syncGate = source.find( "portable::syncFileUtf8( staged" );
  const std::size_t publish = source.find( "publishStaged( staged" );
  INFO( "stagingWrite=" << stagingWrite << " syncGate=" << syncGate
                        << " publish=" << publish );
  REQUIRE( stagingWrite != std::string::npos );
  REQUIRE( syncGate != std::string::npos );
  REQUIRE( publish != std::string::npos );
  REQUIRE( stagingWrite < syncGate );
  REQUIRE( syncGate < publish );

  // The publish is a single atomic replace: no remove-before-rename window
  // (a crash there destroyed the audit trail the journal exists to keep).
  REQUIRE( source.find( "removeQuiet( request.targetPath" ) == std::string::npos );
  // Staging names are unique (pid/counter/rng), never the shared "<name>.tmp".
  REQUIRE( source.find( "filename() + \".tmp\"" ) == std::string::npos );
  REQUIRE( source.find( "stagingCounter" ) != std::string::npos );

  // Every sidecar consumer routes through the authority: the journal keeps
  // no private write path of its own (R6 convergence).
  const std::string journal =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/agent_loop/session_journal.cpp" );
  REQUIRE_FALSE( journal.empty() );
  REQUIRE( journal.find( "sicnu::platform::sidecar::write(" ) != std::string::npos );

  // ADR 0166 / issue #1394 item 2: ONE sidecar write API. Every state-sidecar
  // writer in the repo routes here; the remaining runtime-chunk writers named
  // in the issue (tile_run_contract's checkpoint, scratch_registry's digest
  // sidecar, resumable_tile_run's published marker) must not keep a private
  // temp/publish lane, and neither may the two R6 track-5 lanes (the D17
  // pipeline checkpoint/provenance writer and the lab session store).
  // Call-shaped needles: a comment mentioning the API is not enough
  // (mutation: delete the call, keep the comment → red).
  const char *const consumers[] = {
    "src/runtime/chunk/tile_checkpoint.cpp",
    "src/runtime/chunk/scratch_registry.cpp",
    "src/runtime/chunk/resumable_tile_run.cpp",
    "src/workflow/pipeline_run_coordinator.cpp",
    "src/lab/session_store.cpp",
  };
  for ( const char *consumer : consumers )
  {
    const std::string source = repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, consumer );
    REQUIRE_FALSE( source.empty() );
    INFO( "consumer: " << consumer );
    REQUIRE( source.find( "sicnu::platform::sidecar::write(" ) != std::string::npos );
    // No private publish lane survives in these writers: neither a
    // hand-rolled temp name for the target nor a raw std::filesystem::rename
    // onto it.
    REQUIRE( source.find( "path + \".tmp.\"" ) == std::string::npos );
    REQUIRE( source.find( "+ \".tmp\";" ) == std::string::npos );
    REQUIRE( source.find( "::rename( sicnu::portable::pathFromUtf8( tmp )" )
             == std::string::npos );
  }

  // Track-5 lane needles the generic patterns above cannot spell: the Qt
  // lane's pid/counter temp naming and its platform publish syscalls, and
  // the session store's retired writeFileSync staging helper (mutations:
  // restore either hand-rolled lane → red).
  const std::string coordinator =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/workflow/pipeline_run_coordinator.cpp" );
  REQUIRE( coordinator.find( ".tmp.%1.%2" ) == std::string::npos );
  REQUIRE( coordinator.find( "::rename( QFile::encodeName(" ) == std::string::npos );
  REQUIRE( coordinator.find( "MoveFileExW" ) == std::string::npos );
  const std::string sessionStore =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/lab/session_store.cpp" );
  REQUIRE( sessionStore.find( "finalPath + \".tmp.\"" ) == std::string::npos );
  REQUIRE( sessionStore.find( "writeFileSync" ) == std::string::npos );
  REQUIRE( sessionStore.find( "std::filesystem::rename(" ) == std::string::npos );
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
    // R6: the sidecar authority owns the O_EXCL staging claim; the journal
    // consumer no longer stages names of its own.
    { "src/platform/durable_sidecar.cpp",
      "portable::claimExclusiveUtf8( staged" },
  };
  for ( const Claim &claim : claims )
  {
    const std::string source = repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, claim.file );
    REQUIRE_FALSE( source.empty() );
    INFO( "file: " << claim.file );
    REQUIRE( source.find( claim.required ) != std::string::npos );
  }
}

TEST_CASE( "atomic_fs rides portable.h instead of hand-rolling the claim/flush syscalls",
           "[portability][contract][static][authority]" )
{
  if ( !sourcesAvailable() )
    return;
  const std::string source =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/geospatial/util/atomic_fs.cpp" );
  REQUIRE_FALSE( source.empty() );

  // platform/portable.h is the repo's single authority for the staging
  // claim (O_EXCL / CREATE_NEW) and the durability flush (fsync(2) /
  // FlushFileBuffers). atomic_fs layers retry + typed GeoError ON TOP of it.
  // Reintroducing a second copy of either syscall branch here is the drift
  // the R5 convergence closed — this pin kills that mutation on the POSIX
  // lane where the Windows branches cannot be observed at runtime.
  // Call-shaped needles: a comment MENTIONING the helper must not satisfy
  // the pin — only the real call does (mutation: delete the call, keep the
  // comment → red).
  REQUIRE( source.find( "sicnu::portable::claimExclusiveUtf8( staged" ) != std::string::npos );
  REQUIRE( source.find( "sicnu::portable::syncFileUtf8( path" ) != std::string::npos );
  REQUIRE( source.find( "sicnu::portable::syncDirectoryBestEffortUtf8( targetPath" ) != std::string::npos );
  REQUIRE( source.find( "O_EXCL" ) == std::string::npos );
  REQUIRE( source.find( "CREATE_NEW" ) == std::string::npos );
  REQUIRE( source.find( "FlushFileBuffers" ) == std::string::npos );
}

TEST_CASE( "chunk durability rides portable.h",
           "[portability][contract][static][authority]" )
{
  if ( !sourcesAvailable() )
    return;
  const std::string source =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/runtime/chunk/fsync_compat.h" );
  REQUIRE_FALSE( source.empty() );

  // The chunk family keeps its own throw/best-effort ERROR contract, but the
  // per-platform syscall branches must stay in portable.h — the third copy
  // that used to live here also disagreed on the open mode (O_RDONLY fsync
  // vs the XSI-strict O_WRONLY the atomic lane uses).
  REQUIRE( source.find( "sicnu::portable::syncFileUtf8( path" ) != std::string::npos );
  REQUIRE( source.find( "sicnu::portable::syncDirectoryBestEffortUtf8( path" ) != std::string::npos );
  REQUIRE( source.find( "FlushFileBuffers" ) == std::string::npos );
  REQUIRE( source.find( "MultiByteToWideChar" ) == std::string::npos );
}

TEST_CASE( "publish clears a stale read-only attribute on Windows before replacing",
           "[portability][contract][static][fs]" )
{
  if ( !sourcesAvailable() )
    return;
  const std::string source =
    repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, "src/geospatial/util/atomic_fs.cpp" );
  REQUIRE_FALSE( source.empty() );

  // POSIX rename(2) replaces a read-only TARGET (the gate is the directory's
  // write permission) — the runtime oracle lives in test_io_paths. Windows'
  // ReplaceFileW/MoveFileExW refuse a READONLY target, so the Windows branch
  // must clear FILE_ATTRIBUTE_READONLY inside publishStagedFile BEFORE the
  // first replace call, or the same publish fails there with
  // ERROR_ACCESS_DENIED (static evidence: this lane cannot run Windows).
  const std::size_t publish = source.find( "void publishStagedFile(" );
  REQUIRE( publish != std::string::npos );
  const std::size_t readonlyClear = source.find( "FILE_ATTRIBUTE_READONLY", publish );
  const std::size_t replaceCall = source.find( "ReplaceFileW(", publish );
  REQUIRE( readonlyClear != std::string::npos );
  REQUIRE( replaceCall != std::string::npos );
  INFO( "readonlyClear=" << readonlyClear << " replaceCall=" << replaceCall );
  REQUIRE( readonlyClear < replaceCall );
  REQUIRE( source.find( "SetFileAttributesW", publish ) != std::string::npos );
}

TEST_CASE( "cross-TU forward declarations keep the definition's elaborated type kind",
           "[portability][contract][static][mangle]" )
{
  if ( !sourcesAvailable() )
    return;

  struct CrossTUBoundary
  {
    const char *forwardHeader;
    const char *definitionHeader;
    const char *type;
    const char *kind;
  };

  // MSVC decorates the elaborated type kind of the FIRST declaration it saw
  // (U = struct, V = class) into a function's mangled name. A forward
  // declaration that disagrees with the definition therefore hands two
  // translation units two DIFFERENT mangled names for the SAME type, and a
  // symbol that is plainly defined and already on the link line fails with
  // LNK2019. GCC and Clang never decorate the kind, so every lane except
  // Tier 3 Windows links it happily and sees nothing.
  //
  // Tier 3 hit exactly this on sicnu_geo_rs.exe:
  // src/explain/adapters/workflow_projection.h forward-declared
  // `class sicnu::workflow::WorkflowDocument` while src/workflow/workflow_ir_v2.h
  // defines it as `struct`, so the definition mangled V while the caller
  // (src/app/workbench/step_explanation_section.cpp, which sees the struct
  // first) expected U — C4099 "first seen using 'class' now seen using
  // 'struct'" in one vcxproj and the mirror-image warning in the other was
  // the whole root cause. The three explain source interfaces carry the same
  // disagreement: their C4099 warnings were already in the Tier 3 log
  // without (yet) costing a link, and each is one by-value parameter away
  // from doing so.
  //
  // The definition's own kind is the authority: the forward declaration has
  // to match it, not the other way round.
  const CrossTUBoundary boundaries[] = {
    { "src/explain/adapters/workflow_projection.h", "src/workflow/workflow_ir_v2.h",
      "WorkflowDocument", "struct" },
    { "src/app/workbench/step_explanation_panel.h", "src/explain/explanation_sources.h",
      "IOperatorKnowledge", "struct" },
    { "src/app/workbench/step_explanation_panel.h", "src/explain/authored_guidance.h",
      "IAuthoredGuidance", "struct" },
    { "src/app/workbench/step_explanation_panel.h", "src/explain/explanation_sources.h",
      "IExecutionEvidence", "struct" },
  };

  for ( const CrossTUBoundary &boundary : boundaries )
  {
    const std::string forward =
      repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, boundary.forwardHeader );
    const std::string definition =
      repoSource( SICNU_TEST_CMAKE_SOURCE_DIR, boundary.definitionHeader );
    REQUIRE_FALSE( forward.empty() );
    REQUIRE_FALSE( definition.empty() );

    const std::string forwardKind =
      elaboratedKindOf( forward, boundary.type, true );
    INFO( boundary.forwardHeader << " forward-declares " << boundary.type );
    REQUIRE_FALSE( forwardKind.empty() );
    REQUIRE( forwardKind == boundary.kind );

    const std::string definitionKind =
      elaboratedKindOf( definition, boundary.type, false );
    INFO( boundary.definitionHeader << " defines " << boundary.type );
    REQUIRE_FALSE( definitionKind.empty() );
    REQUIRE( definitionKind == boundary.kind );
  }
}
