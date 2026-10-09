// tests/test_workflow_run_lock_r4.cpp — Track 10 lock-semantics and
// checkpoint-durability lane (light: the checkpoint/run/lock sources are
// compiled directly, mirroring the test_workflow_durability_13 pattern; the
// runtime fault registry is linked so the PRODUCTION fault points
// workflow_checkpoint.write / .publish route through their real failure
// branches).
//
// Contracts under test (workflow_run_lock.h header is the truth source):
//   * flock is per open-file-description: a second acquirer — including the
//     SAME process on a second descriptor — sees HeldByLiveOwner.
//   * liveness is ALWAYS the lock primitive, never the recorded pid, so
//     garbage/empty/truncated owner metadata never widens ownership.
//   * release() is idempotent and destructor-backed.
//   * probeOwner: NoHolder (no file / no holder), LiveOwner (holder);
//     Unknown is the QLockFile (non-Unix) path's state and is unreachable
//     on Q_OS_UNIX by construction.
//   * checkpoint publish faults leave the previous checkpoint intact and no
//     tmp residue (the atomic-write contract, #1323).
#include <catch2/catch_test_macros.hpp>

#include "runtime/observability/fault_registry.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_lock.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <json/json.h>

#include <memory>
// POSIX-only include kept for the non-Windows lanes; nothing in this TU
// needs it on Windows (pid assertions go through QCoreApplication).
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string>

using namespace sicnu::workflow;

namespace {

WorkflowDefinition oneStepDefinition( const std::string &id )
{
    WorkflowDefinition def;
    def.id = id;
    def.title = id;
    StepDef step;
    step.id = "s1";
    step.operatorId = "rs:test";
    def.steps.push_back( step );
    return def;
}

/// A Running run with @p runId persisted into @p dir; returns its checkpoint path.
QString saveRunningRun( const QString &dir, const std::string &runId )
{
    auto run = WorkflowRun::createFromDefinition( oneStepDefinition( "wf-" + runId ), runId );
    REQUIRE( run );
    run->transitionTo( WorkflowRunState::Planning );
    run->transitionTo( WorkflowRunState::Ready );
    run->transitionTo( WorkflowRunState::Running );
    const QString path = WorkflowCheckpointManager().saveCheckpoint( *run, dir );
    REQUIRE_FALSE( path.isEmpty() );
    return path;
}

/// The production tmp naming (workflow_checkpoint.cpp::saveCheckpoint).
QString tmpPathFor( const QString &dir, const std::string &runId )
{
    return QDir( dir ).filePath( QStringLiteral( "checkpoint_%1.json.tmp.999999.7" )
                                     .arg( runId.c_str() ) );
}

int tmpResidue( const QString &dir )
{
    return QDir( dir )
        .entryList( QStringList{ QStringLiteral( "checkpoint_*.json.tmp.*" ) }, QDir::Files )
        .size();
}

} // namespace

TEST_CASE( "WorkflowRunLock: second descriptor in the SAME process is a live owner (flock per-ofd)",
           "[workflow][r4][run_lock]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString lockPath = WorkflowRunLock::lockPathForRun( dir.path(), "r4_lock_ofd" );

    WorkflowRunLock first( lockPath );
    REQUIRE( first.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
    REQUIRE( first.isHeld() );

    // flock conflicts per open file description — the same effect a real
    // second process sees, exercised on a second descriptor in-process.
    WorkflowRunLock second( lockPath );
    QString heldByPid;
    REQUIRE( second.tryAcquire( &heldByPid ) == WorkflowRunLock::TryResult::HeldByLiveOwner );
    REQUIRE( heldByPid.toLongLong() == QCoreApplication::applicationPid() );

    // Metadata is diagnostics-only JSON carrying the holder pid.
    const QString info = first.ownerInfoLine();
    REQUIRE( info.contains( QStringLiteral( "\"pid\":%1" ).arg( QCoreApplication::applicationPid() ) ) );

    first.release();
    REQUIRE_FALSE( first.isHeld() );
    // Idempotence: a second release is a no-op, not a corruption.
    first.release();
    REQUIRE_FALSE( first.isHeld() );

    // The released lock is really free again (release is not just internal).
    REQUIRE( second.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
    second.release();
}

TEST_CASE( "WorkflowRunLock: garbage owner metadata never widens or narrows ownership",
           "[workflow][r4][run_lock]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString lockPath = WorkflowRunLock::lockPathForRun( dir.path(), "r4_lock_meta" );

    // No file at all: NoHolder, pid 0.
    {
        const WorkflowRunLock::OwnerProbe probe = WorkflowRunLock::probeOwner( lockPath );
        REQUIRE( probe.state == WorkflowRunLock::OwnerProbe::State::NoHolder );
        REQUIRE( probe.pid == 0 );
    }

    // A LOCKED holder with deliberately corrupt metadata: still a live
    // owner (liveness is the flock, never the recorded pid), still NOT
    // stealable by tryAcquire.
    WorkflowRunLock holder( lockPath );
    REQUIRE( holder.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
    {
        QFile trash( lockPath );
        REQUIRE( trash.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        trash.write( "not-json-at-all\n" );
        trash.close();
    }
    {
        const WorkflowRunLock::OwnerProbe probe = WorkflowRunLock::probeOwner( lockPath );
        REQUIRE( probe.state == WorkflowRunLock::OwnerProbe::State::LiveOwner ); // flock decides
        REQUIRE( probe.pid == 0 );                              // metadata unreadable
        WorkflowRunLock thief( lockPath );
        REQUIRE( thief.tryAcquire() != WorkflowRunLock::TryResult::Acquired );
    }

    // Truncated-to-empty metadata: same verdict.
    {
        QFile empty( lockPath );
        REQUIRE( empty.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        empty.close();
        REQUIRE( WorkflowRunLock::probeOwner( lockPath ).state == WorkflowRunLock::OwnerProbe::State::LiveOwner );
    }

    holder.release();
    // Unlocked file with garbage content: NoHolder again (the file itself
    // is not a lock; flock is), and the next owner overwrites the metadata.
    {
        const WorkflowRunLock::OwnerProbe probe = WorkflowRunLock::probeOwner( lockPath );
        REQUIRE( probe.state == WorkflowRunLock::OwnerProbe::State::NoHolder );
        WorkflowRunLock next( lockPath );
        REQUIRE( next.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
        REQUIRE( next.ownerInfoLine().contains( QStringLiteral( "\"pid\":" ) ) );
    }
}

TEST_CASE( "checkpoint publish faults leave the previous checkpoint intact with no tmp residue",
           "[workflow][r4][checkpoint][fault]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString path = saveRunningRun( dir.path(), "r4_publish_fault" );
    const QByteArray intact = [&] {
        QFile f( path );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        return f.readAll();
    }();

    auto run = [&] {
        QString err;
        auto loaded = WorkflowCheckpointManager().loadCheckpoint( path, &err );
        REQUIRE( loaded );
        return loaded;
    }();

    for ( const char *faultName : { "workflow_checkpoint.write", "workflow_checkpoint.publish" } )
    {
        const sicnu::runtime::observability::fault::ArmedFault armed{
            sicnu::runtime::observability::fault::FaultAction{
                faultName, sicnu::runtime::observability::fault::Mode::Always, 1, "" }
        };
        // The save ROLLS BACK like a real failure: empty path, previous
        // checkpoint byte-identical, no staged tmp left behind.
        REQUIRE( WorkflowCheckpointManager().saveCheckpoint( *run, dir.path() ).isEmpty() );
        QFile after( path );
        REQUIRE( after.open( QIODevice::ReadOnly ) );
        REQUIRE( after.readAll() == intact );
        after.close();
        REQUIRE( tmpResidue( dir.path() ) == 0 );
    }

    // The durable publish the fault refuses is exactly what recovery would
    // load: the previous version is still the parseable truth.
    QString err;
    REQUIRE( WorkflowCheckpointManager().loadCheckpoint( path, &err ) != nullptr );
}

TEST_CASE( "recovery sweeps the tmp orphan of an unowned run and skips a live writer's",
           "[workflow][r4][checkpoint][recovery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // An unowned run with a crashed-save tmp: swept by the recovery pass.
    const std::string orphanId = "r4_tmp_orphan";
    saveRunningRun( dir.path(), orphanId );
    {
        QFile tmp( tmpPathFor( dir.path(), orphanId ) );
        REQUIRE( tmp.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        tmp.write( "{ partial" );
        tmp.close();
    }

    // A second tmp whose run is LOCKED by a live writer (this process):
    // a save in flight, NOT a crashed one — deleting it would break the
    // writer's final rename. Recovery must leave it alone.
    const std::string inFlightId = "r4_tmp_live";
    saveRunningRun( dir.path(), inFlightId );
    WorkflowRunLock liveWriter(
        WorkflowRunLock::lockPathForRun( dir.path(), inFlightId ) );
    REQUIRE( liveWriter.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
    {
        QFile tmp( tmpPathFor( dir.path(), inFlightId ) );
        REQUIRE( tmp.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        tmp.write( "{ in-flight" );
        tmp.close();
    }

    const auto recovered = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( tmpResidue( dir.path() ) == 1 ); // exactly the live writer's tmp survives
    REQUIRE( QFile::exists( tmpPathFor( dir.path(), inFlightId ) ) );
    REQUIRE_FALSE( QFile::exists( tmpPathFor( dir.path(), orphanId ) ) );

    // Both runs were active and unowned-locked... except the one WE hold:
    // the orphan is reconciled Interrupted, the live-owned one untouched
    // (still Running on disk, never reported).
    REQUIRE( recovered.size() == 1 );
    REQUIRE( recovered.front()->runId() == orphanId );
    REQUIRE( recovered.front()->state() == WorkflowRunState::Interrupted );

    {
        QString err;
        const auto untouched = WorkflowCheckpointManager().loadCheckpoint(
            QDir( dir.path() ).filePath( QStringLiteral( "checkpoint_%1.json" ).arg( inFlightId.c_str() ) ),
            &err );
        REQUIRE( untouched );
        REQUIRE( untouched->state() == WorkflowRunState::Running );
    }

    // A CANCELLING run (cancel reached the checkpoint, then death) is a
    // recovery candidate too: reconciled to Interrupted, steps stuck in
    // Running/Cancelling back to Pending — the sticky-cancel contract's
    // resumable side.
    liveWriter.release();
    {
        QString err;
        const QString cancellingPath = QDir( dir.path() ).filePath(
            QStringLiteral( "checkpoint_%1.json" ).arg( inFlightId.c_str() ) );
        auto run = WorkflowCheckpointManager().loadCheckpoint( cancellingPath, &err );
        REQUIRE( run );
        REQUIRE( run->transitionTo( WorkflowRunState::Cancelling ) );
        StepPlan stuck = run->stepPlan( "s1" ).value_or( StepPlan{} );
        stuck.status = "Running";
        REQUIRE( run->updateStepPlan( stuck ) );
        REQUIRE( WorkflowCheckpointManager().saveCheckpoint( *run, dir.path() ).size() > 0 );
    }
    const auto afterCancel = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( afterCancel.size() == 1 );
    REQUIRE( afterCancel.front()->runId() == inFlightId );
    REQUIRE( afterCancel.front()->state() == WorkflowRunState::Interrupted );
    const auto stuckPlan = afterCancel.front()->stepPlan( "s1" );
    REQUIRE( stuckPlan.has_value() );
    REQUIRE( stuckPlan->status == "Pending" ); // resumable, not resurrected-running
}

TEST_CASE( "a corrupt checkpoint is a structured skip, never a crash or a silent serve",
           "[workflow][r4][checkpoint][edge]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString goodPath = saveRunningRun( dir.path(), "r4_corrupt_good" );
    const QString badPath = QDir( dir.path() )
                                .filePath( QStringLiteral( "checkpoint_r4_corrupt_bad.json" ) );
    {
        QFile bad( badPath );
        REQUIRE( bad.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        bad.write( "{ \"version\": 2, \"runId\": \"r4_corrupt_bad\", TRUNCATED" );
        bad.close();
    }

    QString err;
    REQUIRE( WorkflowCheckpointManager().loadCheckpoint( badPath, &err ) == nullptr );
    REQUIRE_FALSE( err.isEmpty() ); // the rejection is explained, not silent

    // Recovery skips the corrupt file and still recovers the good one.
    const auto recovered = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( recovered.size() == 1 );
    REQUIRE( recovered.front()->runId() == "r4_corrupt_good" );
    REQUIRE( QFile::exists( goodPath ) ); // the corrupt file does not poison neighbors
}

// --- Track 10 WP-D: boundary second pass ------------------------------------

TEST_CASE( "the guarded transition table refuses impossible durability moves",
           "[workflow][r4][edge][state]" )
{
    auto run = WorkflowRun::createFromDefinition( oneStepDefinition( "wf-r4-edge" ), "r4_edge" );
    REQUIRE( run );
    // Created → Canceled is legal (cancel before planning); Created → Running is not.
    REQUIRE( run->transitionTo( WorkflowRunState::Canceled ) );
    auto done = WorkflowRun::createFromDefinition( oneStepDefinition( "wf-r4-edge2" ), "r4_edge2" );
    REQUIRE( done );
    REQUIRE_FALSE( done->transitionTo( WorkflowRunState::Running ) );
    done->transitionTo( WorkflowRunState::Planning );
    done->transitionTo( WorkflowRunState::Ready );
    done->transitionTo( WorkflowRunState::Running );
    done->transitionTo( WorkflowRunState::Completed );
    // A terminal run refuses every lifecycle move — the anti-resurrection wall.
    REQUIRE_FALSE( done->transitionTo( WorkflowRunState::Cancelling ) );
    REQUIRE_FALSE( done->transitionTo( WorkflowRunState::Running ) );
    REQUIRE_FALSE( done->transitionTo( WorkflowRunState::Interrupted ) );
    REQUIRE( done->state() == WorkflowRunState::Completed );
}

TEST_CASE( "lock acquisition in a nonexistent directory creates it; a read-only directory is a "
           "structured Error",
           "[workflow][r4][run_lock][edge]" )
{
    QTemporaryDir parent;
    REQUIRE( parent.isValid() );

    // Missing directory: acquisition creates it (the first checkpoint may not
    // exist yet when the lock is taken).
    const QString missing = parent.path() + QStringLiteral( "/deep/nested/dir" );
    {
        WorkflowRunLock lock( WorkflowRunLock::lockPathForRun( missing, "r4_edge_mkpath" ) );
        REQUIRE( lock.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
        REQUIRE( QDir( missing ).exists() );
    }

    // Read-only directory: refusal is the typed Error branch, not a crash.
    // (Skipped for root, which bypasses directory permissions — a POSIX
    // concept; Windows has no uid-0 bypass.)
#ifndef _WIN32
    if ( ::geteuid() == 0 )
        return;
#endif
    const QString readonly = parent.path() + QStringLiteral( "/ro" );
    REQUIRE( QDir().mkpath( readonly ) );
    REQUIRE( QFile::setPermissions(
        readonly, QFileDevice::ReadOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                      | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther ) );
    WorkflowRunLock lock( WorkflowRunLock::lockPathForRun( readonly, "r4_edge_ro" ) );
    REQUIRE( lock.tryAcquire() == WorkflowRunLock::TryResult::Error );
    REQUIRE( lock.tryAcquire() == WorkflowRunLock::TryResult::Error ); // stable, not wedged
}

TEST_CASE( "SICNU_CHECKPOINT_DIR relocates the default checkpoint family for one session",
           "[workflow][r5][run_lock][isolation]" )
{
    // Session isolation knob (Track 10 R5, #1351 backlog): an MCP/CLI session
    // or a test relocates the WHOLE checkpoint family (checkpoints, run locks,
    // history) with one env var — two processes sharing $HOME no longer share
    // checkpoint state, and a test never writes the developer's real
    // ~/.rs_studio/checkpoints. Unset keeps the historical default.
    struct EnvGuard
    {
        const QByteArray saved = qgetenv( "SICNU_CHECKPOINT_DIR" );
        const bool had = qEnvironmentVariableIsSet( "SICNU_CHECKPOINT_DIR" );
        ~EnvGuard()
        {
            if ( had )
                qputenv( "SICNU_CHECKPOINT_DIR", saved );
            else
                qunsetenv( "SICNU_CHECKPOINT_DIR" );
        }
    } guard;

    QTemporaryDir sessionDir;
    REQUIRE( sessionDir.isValid() );

    qputenv( "SICNU_CHECKPOINT_DIR", sessionDir.path().toUtf8() );
    REQUIRE( WorkflowCheckpointManager::defaultCheckpointDirectory() == sessionDir.path() );

    // The default-directory save path lands inside the relocated family.
    const QString path = saveRunningRun( QString(), "r5_env_override" );
    REQUIRE( path.startsWith( sessionDir.path() ) );
    REQUIRE( QDir( sessionDir.path() )
                 .entryList( QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files )
                 .size() == 1 );

    // Unset restores the historical HOME-derived default.
    qunsetenv( "SICNU_CHECKPOINT_DIR" );
    REQUIRE( WorkflowCheckpointManager::defaultCheckpointDirectory()
             == QDir::homePath() + QStringLiteral( "/.rs_studio/checkpoints" ) );
}
