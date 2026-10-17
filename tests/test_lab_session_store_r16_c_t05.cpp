/***************************************************************************
  tests/test_lab_session_store_r16_c_t05.cpp — session store lifecycle pins.

  r16-c-t05 hardening round, student lab session track:
   * an unreadable lab directory must not truncate the WHOLE session
     listing (Listing::warnings documents "skipped, not fatal");
   * a hand-edited session whose session_id disagrees with its recorded
     lab_id/student_id must be refused (strict-envelope identity);
   * Unicode store roots and read-only-directory save failures are pinned
     as verification (fail-closed, no half products).
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "lab/session_state.h"
#include "lab/session_store.h"

#include <unistd.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

/// Unique temp root per run (no shared state between tests).
std::string makeRoot( const std::string &tag )
{
  const std::string root = "/tmp/r16_c_t05_" + tag + "_" +
                           std::to_string( ::getpid() ) + "_" +
                           std::to_string( ::clock() );
  std::filesystem::remove_all( root );
  std::filesystem::create_directories( root );
  return root;
}

/// A minimal meta for a lab whose plan needs no seed; the store itself does
/// not consult plan contents for save/load/list.
sicnu::lab::SessionMeta metaFor( const std::string &labId )
{
  sicnu::lab::SessionMeta meta;
  meta.labId = labId;
  meta.studentId = "student1";
  meta.labSpecVersion = 3;
  meta.planSource = "authored_v3";
  return meta;
}

sicnu::lab::LabSession makeSavedSession( sicnu::lab::LabSessionStore &store,
                                         const std::string &labId )
{
  const sicnu::lab::LabRuntimePlan plan; // empty plan: startSession mirrors stages only
  auto created = store.create( plan, metaFor( labId ), "fp-" + labId );
  REQUIRE( created.ok );
  REQUIRE( store.save( created.value ).ok );
  return created.value;
}

/// RAII: a directory held at mode 000 for the test body, restored on exit.
class DeniedDir
{
  public:
    explicit DeniedDir( std::string path ) : m_path( std::move( path ) )
    {
      std::filesystem::create_directories( m_path );
      std::filesystem::permissions( m_path, std::filesystem::perms::none );
    }
    ~DeniedDir() { restore(); }
    void restore()
    {
      if ( m_path.empty() ) return;
      std::error_code ec;
      std::filesystem::permissions( m_path, std::filesystem::perms::owner_all, ec );
      std::filesystem::remove_all( m_path, ec );
      m_path.clear();
    }

  private:
    std::string m_path;
};

} // namespace

TEST_CASE( "r16-c-t05: an unreadable lab directory skips without truncating the listing",
           "[lab_runtime][store][r16_c_t05]" )
{
  if ( ::geteuid() == 0 )
    SKIP( "running as root: directory permissions do not deny reads" );
  const std::string root = makeRoot( "listing" );

  // Denied directory FIRST in creation order: whatever the readdir order is
  // on this filesystem, every other lab directory must still be listed.
  DeniedDir denied( root + "/lab_m_denied" );
  sicnu::lab::LabSessionStore store( root );
  const auto sessionA = makeSavedSession( store, "lab_a_ok" );
  const auto sessionZ = makeSavedSession( store, "lab_z_ok" );

  const sicnu::lab::LabSessionStore::Listing listing = store.listSessions();
  bool sawA = false, sawZ = false;
  for ( const auto &summary : listing.summaries ) {
    if ( summary.sessionId == sessionA.sessionId ) sawA = true;
    if ( summary.sessionId == sessionZ.sessionId ) sawZ = true;
  }
  CHECK( sawA );
  CHECK( sawZ );
  // "Skipped, not fatal" must be OBSERVABLE: the denied directory yields a
  // warning diag (readdir order is filesystem-controlled, so the warning —
  // not the truncation order — is the deterministic oracle here).
  REQUIRE_FALSE( listing.warnings.empty() );
  bool warnedAboutDenied = false;
  for ( const auto &diag : listing.warnings )
    if ( diag.message.find( "lab_m_denied" ) != std::string::npos ) warnedAboutDenied = true;
  REQUIRE( warnedAboutDenied );

  denied.restore();
  std::filesystem::remove_all( root );
}

TEST_CASE( "r16-c-t05: session identity must stay coherent across a load",
           "[lab_runtime][session][r16_c_t05]" )
{
  const sicnu::lab::LabRuntimePlan plan;
  auto started = sicnu::lab::startSession( plan, metaFor( "lab90_id_probe" ), 7 );
  REQUIRE( started.ok );
  const Json::Value canonical = sicnu::lab::sessionToJson( started.value );

  SECTION( "untouched round-trip keeps loading" )
  {
    const auto reloaded = sicnu::lab::sessionFromJson( canonical );
    REQUIRE( reloaded.ok );
    REQUIRE( reloaded.value.sessionId == started.value.sessionId );
  }

  SECTION( "lab_id swapped in a hand-edit is refused" )
  {
    Json::Value forged = canonical;
    forged["lab_id"] = "lab_other";
    const auto refused = sicnu::lab::sessionFromJson( forged );
    REQUIRE_FALSE( refused.ok );
    REQUIRE( refused.diagnostics.front().code == "lab.session.identity" );
  }

  SECTION( "student_id swapped in a hand-edit is refused" )
  {
    Json::Value forged = canonical;
    forged["student_id"] = "someone_else";
    const auto refused = sicnu::lab::sessionFromJson( forged );
    REQUIRE_FALSE( refused.ok );
    REQUIRE( refused.diagnostics.front().code == "lab.session.identity" );
  }

  SECTION( "session_id suffix must be the recorded sequence, not free text" )
  {
    Json::Value forged = canonical;
    forged["session_id"] = "lab90_id_probe/student1/not-a-number";
    const auto refused = sicnu::lab::sessionFromJson( forged );
    REQUIRE_FALSE( refused.ok );
    REQUIRE( refused.diagnostics.front().code == "lab.session.identity" );
  }
}

TEST_CASE( "r16-c-t05: unicode store root round-trips and read-only roots fail typed",
           "[lab_runtime][store][r16_c_t05][verification]" )
{
  // Unicode root: the student-facing store root may contain non-ASCII path
  // components (locale homes); save/load/list must be byte-stable there.
  const std::string root = makeRoot( "unicode_实验室根目录" );
  {
    sicnu::lab::LabSessionStore store( root );
    const auto session = makeSavedSession( store, "lab90_unicode" );
    const auto loaded = store.load( session.sessionId, "fp-lab90_unicode" );
    REQUIRE( loaded.ok );
    REQUIRE( loaded.value.sessionId == session.sessionId );
    REQUIRE( store.listSessionIds().size() == 1 );

    // Read-only LAB directory (the session file's own directory): a save
    // must fail typed, leave the previous generation intact, and never
    // leave a half-written file behind. The root itself stays traversable
    // so loads keep working.
    const std::string labDir = root + "/lab90_unicode";
    std::filesystem::permissions( labDir, std::filesystem::perms::owner_read |
                                             std::filesystem::perms::owner_exec |
                                             std::filesystem::perms::group_read |
                                             std::filesystem::perms::group_exec |
                                             std::filesystem::perms::others_read |
                                             std::filesystem::perms::others_exec );
    auto advanced = loaded.value;
    REQUIRE( sicnu::lab::recordExecutionRef( advanced, "artifact", "ref-2" ).ok );
    const auto refused = store.save( advanced );
    REQUIRE_FALSE( refused.ok );
    REQUIRE( refused.diagnostics.front().code == "lab.session.io" );

    const auto reloaded = store.load( session.sessionId, "fp-lab90_unicode" );
    REQUIRE( reloaded.ok );
    REQUIRE( reloaded.value.lastSeq == loaded.value.lastSeq );
    std::error_code permEc;
    std::filesystem::permissions( labDir, std::filesystem::perms::owner_all, permEc );
  }
  std::filesystem::remove_all( root );
}
