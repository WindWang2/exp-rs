// test_project_session_boundary.cpp — the openProject transaction
//
// Covers the boundary the shell's openProject delegates to: probe → clear →
// story-boundary hook → store bind → read, and the ReadFailed rollback that
// keeps the phantom identity of a never-opened file out of the session
// (fileName, governance store). Real files + real QgsProject + real
// WorkspaceService; the mid-transaction read failure is driven through the
// injectable read edge (a well-formed file cannot reach it through the real
// parser, since the probe shares it).
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "workbench/project_session_boundary.h"

#include "project_context.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <qgsproject.h>

#include <cstdlib>
#include <cstdio>

// QgsProject::read/write touch the thread-local QgsProjContext, which
// crashes during glibc atexit cleanup after a Catch2 run; bypass it with
// std::_Exit once Catch has reported the final result (the
// test_dual_viewport_sync precedent).
namespace
{
  class FastExitListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
        // The console reporter may not have printed its summary yet when the
        // listener chain runs — emit the verdict ourselves so the transcript
        // survives std::_Exit.
        const bool ok = !stats.aborting && stats.totals.testCases.failed == 0;
        std::fprintf( stderr, "\n%s: %u/%u assertions, %u/%u test cases\n",
                      ok ? "ALL TESTS PASSED" : "TESTS FAILED",
                      static_cast<unsigned>( stats.totals.assertions.passed ),
                      static_cast<unsigned>( stats.totals.assertions.passed
                                             + stats.totals.assertions.failed ),
                      static_cast<unsigned>( stats.totals.testCases.passed ),
                      static_cast<unsigned>( stats.totals.testCases.passed
                                             + stats.totals.testCases.failed ) );
        std::fflush( stderr );
        std::_Exit( ok ? 0 : 1 );
      }
  };
}
CATCH_REGISTER_LISTENER( FastExitListener )

namespace
{
int fake_argc = 1;
char fake_argv0[] = "test_project_session_boundary";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
    if ( !QCoreApplication::instance() )
    {
        static QCoreApplication app( fake_argc, fake_argv );
        return &app;
    }
    return static_cast<QCoreApplication *>( QCoreApplication::instance() );
}

/// A minimal, well-formed project file the probe (and the real read) accept.
bool writeValidProject( const QString &path )
{
    QgsProject probe;
    return probe.write( path );
}

bool writeCorruptProject( const QString &path )
{
    QFile f( path );
    if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    return f.write( "this is not a zip or xml project document\n" ) > 0;
}
} // namespace

TEST_CASE( "Session boundary: probe failure leaves the live session untouched", "[app][session_boundary]" )
{
    ensureApp();
    QgsProject project;
    auto created = sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created );
    auto context = created.take();

    // A live session presenting an existing project.
    const QString live = QDir( QDir::tempPath() ).filePath( "session_boundary_live.qgs" );
    REQUIRE( writeValidProject( live ) );
    REQUIRE( project.read( live ) );
    REQUIRE( project.fileName() == live );

    int hookCalls = 0;
    const auto outcome = sicnu::app::openProjectSession(
        *context, project, QStringLiteral( "/nonexistent/path/really-missing.qgs" ),
        [&hookCalls] { ++hookCalls; } );

    REQUIRE( outcome.stage == sicnu::app::ProjectSessionOpenResult::Stage::ProbeFailed );
    REQUIRE( outcome.failedPath == QStringLiteral( "/nonexistent/path/really-missing.qgs" ) );
    // The story-boundary hook belongs to the EMPTIED session — a refused
    // probe never empties it.
    REQUIRE( hookCalls == 0 );
    // Identity untouched: still the live project.
    REQUIRE( project.fileName() == live );
    REQUIRE( !context->workspaceService().isStoreOpen() );
    QFile::remove( live );
}

TEST_CASE( "Session boundary: corrupt file refused by the probe", "[app][session_boundary]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString corrupt = dir.filePath( "corrupt.qgs" );
    REQUIRE( writeCorruptProject( corrupt ) );

    QgsProject project;
    auto created = sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created );
    auto context = created.take();

    int hookCalls = 0;
    const auto outcome = sicnu::app::openProjectSession(
        *context, project, corrupt, [&hookCalls] { ++hookCalls; } );

    REQUIRE( outcome.stage == sicnu::app::ProjectSessionOpenResult::Stage::ProbeFailed );
    REQUIRE( hookCalls == 0 );
    REQUIRE( project.fileName().isEmpty() );
}

TEST_CASE( "Session boundary: successful open binds identity and governance store", "[app][session_boundary]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString target = dir.filePath( "good.qgs" );
    REQUIRE( writeValidProject( target ) );

    QgsProject project;
    auto created = sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created );
    auto context = created.take();

    int hookCalls = 0;
    const auto outcome = sicnu::app::openProjectSession(
        *context, project, target, [&hookCalls] { ++hookCalls; } );

    REQUIRE( outcome.stage == sicnu::app::ProjectSessionOpenResult::Stage::Succeeded );
    REQUIRE( outcome.failedPath.isEmpty() );
    REQUIRE( outcome.governanceStoreOpened );
    REQUIRE( hookCalls == 1 );
    REQUIRE( project.fileName() == target );
    // The store lives next to the project file and is bound.
    REQUIRE( context->workspaceService().isStoreOpen() );
    REQUIRE( QFile::exists( dir.filePath( "good.governance.db" ) ) );
}

TEST_CASE( "Session boundary: read failure rolls back to the consistent empty session", "[app][session_boundary]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // A file the PROBE accepts — the failure below happens mid-transaction
    // (between store bind and read completion), the branch a real parser
    // cannot reach from a well-formed file.
    const QString target = dir.filePath( "midair.qgs" );
    REQUIRE( writeValidProject( target ) );

    QgsProject project;
    auto created = sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created );
    auto context = created.take();

    int hookCalls = 0;
    const auto outcome = sicnu::app::openProjectSession(
        *context, project, target, [&hookCalls] { ++hookCalls; },
        []( QgsProject &p, const QString &path ) {
            p.setFileName( path ); // real read semantics: identity first…
            return false;          // …then the failure
        } );

    REQUIRE( outcome.stage == sicnu::app::ProjectSessionOpenResult::Stage::ReadFailed );
    REQUIRE( outcome.failedPath == target );
    REQUIRE( !outcome.diagnostics.isEmpty() );
    // Story boundary ran exactly once (session emptied) and is NOT re-run
    // by the failure path — the empty session owns nothing.
    REQUIRE( hookCalls == 1 );
    // THE B2/#1269-remainder kill: the phantom identity is gone. Before the
    // rollback, QgsProject::read() had left fileName() pointing at the
    // target — updateWindowTitle() and saveProject() would adopt (and
    // overwrite!) a file that never opened.
    REQUIRE( project.fileName().isEmpty() );
    // The governance store that was just bound to the target is released.
    REQUIRE( !context->workspaceService().isStoreOpen() );
}

TEST_CASE( "Session boundary: failed open does not advance a later Save-As identity", "[app][session_boundary]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString target = dir.filePath( "phantom.qgs" );
    REQUIRE( writeValidProject( target ) );

    QgsProject project;
    auto created = sicnu::app::ProjectContext::createHeadless();
    REQUIRE( created );
    auto context = created.take();

    ( void ) sicnu::app::openProjectSession(
        *context, project, target, [] {},
        // Mirror QgsProject::read's real failure signature: the file name is
        // assigned BEFORE parsing and left behind on failure. Without this,
        // the rollback's setFileName reset would be untestable (the phantom
        // name would never exist to be rolled back).
        []( QgsProject &p, const QString &path ) {
            p.setFileName( path );
            return false;
        } );
    REQUIRE( project.fileName().isEmpty() );

    // After the failed open, saving under a NEW name must behave like a
    // first publication of the empty session: the phantom path plays no
    // role (write() to the new path succeeds and becomes the identity) and
    // the phantom file itself is never touched.
    const QString saveAs = dir.filePath( "rescue.qgs" );
    REQUIRE( project.write( saveAs ) );
    REQUIRE( project.fileName() == saveAs );
    REQUIRE( QFile::exists( target ) ); // the never-opened file was not overwritten
}
