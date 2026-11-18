// tests/test_r33_t05_sidecar_recovery.cpp — R33-T05 WP08/WP09 gates.
//
// The mission authority's file channels must be bounded and evidence-bound:
//
//   - an oversized sidecar (authority, last-good or legacy timeline) is
//     refused BEFORE an unbounded readAll, and never overwrites a good
//     last-known-good copy (WP08);
//   - the last-good rotation copies the exact bytes the probe decoded —
//     the sidecar on disk and the "good" snapshot cannot diverge through
//     an open-validate-then-reopen-copy race (WP09).
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_context_store.h"
#include "app/workbench/mission_runtime_store.h"
#include "app/workbench/mission_stage.h"

#include <QByteArray>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace sicnu::app;

namespace
{

QString sidecarPath( const QString &projectPath )
{
    return missionSidecarPathForProject( projectPath );
}

QString lastGoodPath( const QString &projectPath )
{
    return missionRuntimeLastGoodPathForProject( projectPath );
}

/// A small valid runtime state with one task, saved to @p project.
MissionRuntimeState saveSampleRuntime( const QString &project )
{
    MissionRuntimeState state;
    state.context.missionId = QStringLiteral( "mission-r33t05" );
    state.context.projectRef = project;
    MissionTask task;
    task.id = QStringLiteral( "task-1" );
    task.stage = MissionStage::Import;
    task.title = QStringLiteral( "导入数据" );
    task.capabilityId = QStringLiteral( "project.importLayer" );
    REQUIRE( state.timeline.addTask( task ).applied );

    QDomDocument doc;
    QString error;
    REQUIRE( saveMissionRuntime( project, doc, state, &error ) );
    return state;
}

void writeBytes( const QString &path, const QByteArray &bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
    file.close();
}

} // namespace

TEST_CASE( "An oversized authority sidecar is refused without slurping and the last-known-good "
           "still recovers it",
           "[mission][r33t05][sidecar][oversize]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "oversized.qgs" ) );

    (void) saveSampleRuntime( project );
    QFile sideFile( sidecarPath( project ) );
    REQUIRE( sideFile.open( QIODevice::ReadOnly ) );
    const QByteArray goodBytes = sideFile.readAll();
    sideFile.close();
    REQUIRE( !goodBytes.isEmpty() );
    QFile lgProbe( lastGoodPath( project ) );
    REQUIRE( lgProbe.open( QIODevice::ReadOnly ) );
    REQUIRE( lgProbe.readAll() == goodBytes );
    lgProbe.close();

    // One byte past the sidecar write authority's ceiling (32 MiB): a file
    // the write side can never publish, so hostile or torn input.
    QByteArray huge( static_cast< qsizetype >( 32u * 1024u * 1024u ) + 1, 'z' );
    writeBytes( sidecarPath( project ), huge );

    // The oversized file is refused BEFORE any read; the good last-known-good
    // still recovers the authority with the full task space intact.
    MissionRuntimeState state;
    QDomDocument doc;
    QString error;
    REQUIRE( loadMissionRuntime( project, doc, state, &error ) );
    REQUIRE( state.authorityLoaded );
    REQUIRE( state.recoveredFromLastGood );
    REQUIRE( state.context.missionId == QStringLiteral( "mission-r33t05" ) );
    REQUIRE( state.timeline.tasks().size() == 1 );
    REQUIRE( state.problems.isEmpty() );

    // A recovered (non-poisoned) state saves cleanly again: the authority is
    // republished at normal size and the rotation re-binds the same bytes.
    QDomDocument saveDoc;
    QString saveError;
    REQUIRE( saveMissionRuntime( project, saveDoc, state, &saveError ) );
    QFile side2( sidecarPath( project ) );
    QFile lg2( lastGoodPath( project ) );
    REQUIRE( side2.open( QIODevice::ReadOnly ) );
    REQUIRE( lg2.open( QIODevice::ReadOnly ) );
    const QByteArray republished = side2.readAll();
    const QByteArray rotated = lg2.readAll();
    REQUIRE( republished.size() < huge.size() );
    REQUIRE( rotated == republished );

    SECTION( "no last-good to recover: the open refuses, poisoned, and the save never "
             "overwrites the residue" )
    {
        writeBytes( sidecarPath( project ), huge );
        QFile::remove( lastGoodPath( project ) );

        MissionRuntimeState poisoned;
        QDomDocument doc2;
        QString loadErr;
        REQUIRE_FALSE( loadMissionRuntime( project, doc2, poisoned, &loadErr ) );
        REQUIRE( poisoned.authorityCorrupt );
        REQUIRE( loadErr.contains( QLatin1String( "authority_sidecar_unreadable" ) ) );
        REQUIRE( loadErr.contains( QLatin1String( "bound" ) ) );

        // The poisoned state can never save over the artifact.
        QDomDocument saveDoc2;
        QString saveErr2;
        REQUIRE_FALSE( saveMissionRuntime( project, saveDoc2, poisoned, &saveErr2 ) );
        QFile still( sidecarPath( project ) );
        REQUIRE( still.open( QIODevice::ReadOnly ) );
        REQUIRE( still.size() == huge.size() ); // untouched for explicit recovery
    }
}

TEST_CASE( "The last-good copy is the exact validated byte set, and recovery prefers it",
           "[mission][r33t05][sidecar][lastgood]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "lastgood.qgs" ) );

    (void) saveSampleRuntime( project );

    // WP09: the rotation validated and copied the SAME read — the snapshot
    // equals the sidecar byte-for-byte (a reopen-copy could race a replace).
    QFile side( sidecarPath( project ) );
    REQUIRE( side.open( QIODevice::ReadOnly ) );
    const QByteArray sideBytes = side.readAll();
    side.close();
    QFile lg( lastGoodPath( project ) );
    REQUIRE( lg.open( QIODevice::ReadOnly ) );
    const QByteArray lgBytes = lg.readAll();
    lg.close();
    REQUIRE( lgBytes == sideBytes );

    // Corrupt the primary; the load recovers from last-good with the SAME
    // mission identity and task space, not an empty project.
    writeBytes( sidecarPath( project ), QByteArrayLiteral( "{ torn" ) );
    MissionRuntimeState state;
    QDomDocument doc;
    QString error;
    REQUIRE( loadMissionRuntime( project, doc, state, &error ) );
    REQUIRE( state.authorityLoaded );
    REQUIRE( state.recoveredFromLastGood );
    REQUIRE( state.context.missionId == QStringLiteral( "mission-r33t05" ) );
    REQUIRE( state.timeline.tasks().size() == 1 );
    REQUIRE( state.timeline.tasks().first().id == QStringLiteral( "task-1" ) );

    // A recovered state saves cleanly again (the authority is republished
    // and a fresh rotation binds the new validated bytes).
    QDomDocument saveDoc;
    QString saveError;
    REQUIRE( saveMissionRuntime( project, saveDoc, state, &saveError ) );
    QFile side2( sidecarPath( project ) );
    QFile lg3( lastGoodPath( project ) );
    REQUIRE( side2.open( QIODevice::ReadOnly ) );
    REQUIRE( lg3.open( QIODevice::ReadOnly ) );
    REQUIRE( lg3.readAll() == side2.readAll() );
}

TEST_CASE( "An oversized legacy timeline residue is reported, not slurped, and the open "
           "still succeeds",
           "[mission][r33t05][sidecar][legacy]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( QStringLiteral( "legacy.qgs" ) );

    // No authority anywhere; only a giant legacy timeline sidecar.
    QByteArray huge( static_cast< qsizetype >( 32u * 1024u * 1024u ) + 1, 'l' );
    writeBytes( missionTimelineLegacySidecarPathForProject( project ), huge );

    MissionRuntimeState state;
    QDomDocument doc;
    QString error;
    REQUIRE( loadMissionRuntime( project, doc, state, &error ) );
    REQUIRE_FALSE( state.authorityLoaded ); // fresh project, not an error
    REQUIRE( state.problems.size() == 1 );
    REQUIRE( state.problems.first().contains( QLatin1String( "legacy_timeline_unreadable" ) ) );
    REQUIRE( state.timeline.tasks().isEmpty() );
}

TEST_CASE( "readMissionSidecarBytes refuses an oversized file before reading it",
           "[mission][r33t05][sidecar][bound]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "direct.bin" ) );

    QByteArray huge( static_cast< qsizetype >( 32u * 1024u * 1024u ) + 1, 'x' );
    writeBytes( path, huge );

    QByteArray bytes;
    QString error;
    REQUIRE_FALSE( readMissionSidecarBytes( path, bytes, &error ) );
    REQUIRE( error.contains( QLatin1String( "bound" ) ) );
    REQUIRE( bytes.isEmpty() );

    // A document below the ceiling is read to the end, byte-exact.
    QByteArray small( static_cast< qsizetype >( 1024 ), 'y' );
    writeBytes( path, small );
    REQUIRE( readMissionSidecarBytes( path, bytes, &error ) );
    REQUIRE( bytes == small );
}
