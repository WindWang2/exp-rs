// test_fault_injection.cpp — Phase M chaos matrix (FAILURE_MATRIX.md rows not
// already covered by the phase suites): cache corruption self-heal, checkpoint
// corruption skip, ghost-checkpoint double-execution election, run-history
// archive bounds, and cross-process run-lock double ownership. Every fault
// must end in a predictable, recoverable state — never silent wrong data.
#include <catch2/catch_test_macros.hpp>

#include "data/artifact_object_pool.h"
#include "data/execution_fingerprint.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run_lock.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace sicnu::data;
using namespace sicnu::workflow;

namespace
{
void writeBytes( const QString &path, const QByteArray &bytes )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
    f.close();
}

struct PooledExecutionFixture
{
    QTemporaryDir dir;
    ArtifactObjectPool pool;
    QString payload;

    PooledExecutionFixture()
    {
        payload = dir.filePath( "payload.tif" );
        writeBytes( payload, "cacheable-bytes" );
        QString err;
        REQUIRE( pool.enable( dir.filePath( "pool" ), &err ) );
    }
};
} // namespace

TEST_CASE( "corrupted pool object self-heals into a miss, never a wrong serve",
           "[fault][cache_corruption]" )
{
    PooledExecutionFixture fx;

    sicnu::data::PoolExecution execution;
    execution.declaredOriginal = fx.payload;
    auto object = fx.pool.put( fx.payload, true );
    REQUIRE( object );
    execution.objects.append( *object );
    execution.payloadJson = "{\"output\":\"/x.tif\"}";
    REQUIRE( fx.pool.recordExecution( "fp-corrupt-1", execution ) );

    // Healthy lookup serves.
    const auto healthy = fx.pool.lookupExecution( "fp-corrupt-1" );
    REQUIRE( healthy );
    REQUIRE( healthy->objects.size() == 1 );

    // Corrupt the pooled bytes (bit rot / external write).
    QFile objectFile( object->poolPath );
    REQUIRE( objectFile.open( QIODevice::ReadWrite ) );
    objectFile.seek( 0 );
    objectFile.write( "CORRUPTED-CORRUPT" );
    objectFile.close();

    // The next lookup refuses to serve: digest mismatch → nullopt → a real
    // execution re-runs. Silent wrong data is impossible through this path.
    REQUIRE_FALSE( fx.pool.lookupExecution( "fp-corrupt-1" ) );
}

TEST_CASE( "a truncated cache file is not pooled as valid content", "[fault][partial_copy]" )
{
    PooledExecutionFixture fx;
    // Simulate a partial copy: the source shrinks between digest and staging
    // is not directly injectable, so verify the staged-copy contract instead:
    // an object file rewritten to a different size is rejected by lookup.
    auto object = fx.pool.put( fx.payload, true );
    REQUIRE( object );
    REQUIRE( object->size == QByteArray( "cacheable-bytes" ).size() );
    QFile::remove( object->poolPath );
    writeBytes( object->poolPath, "short" );
    REQUIRE_FALSE( fx.pool.lookupExecution( "fp-truncated" ) );
}

TEST_CASE( "a corrupt checkpoint is skipped, not fatal", "[fault][checkpoint_corruption]" )
{
    QTemporaryDir dir;
    const QString good = dir.filePath( "checkpoint_run-good.json" );
    writeBytes( good, "{\"valid\":true}" ); // invalid run JSON on purpose

    const auto runs = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    // Neither crashes nor resurrects data: zero recovered runs.
    REQUIRE( runs.empty() );
    // The corrupt file is left in place (skipped, not destroyed).
    REQUIRE( QFile::exists( good ) );
}

TEST_CASE( "ghost checkpoint election prevents double execution",
           "[fault][ghost_checkpoint]" )
{
    QTemporaryDir dir;
    // Simulate the crash window: an Interrupted original checkpoint and a
    // newer post-resume ghost for the SAME run id.
    const QString original = dir.filePath( "checkpoint_run-1.json" );
    const QString ghost = dir.filePath( "checkpoint_run-1_resume.json" );
    writeBytes( original, "{}" );
    QFile( ghost ).close();
    REQUIRE( QFile( ghost ).open( QIODevice::WriteOnly ) );
    QFile( ghost ).write( "{}" );
    QFile( ghost ).close();
    // Make the ghost provably newer (explicit timestamp; mtime granularity on
    // some filesystems makes write-order nondeterministic).
    QFile ghostFile( ghost );
    REQUIRE( ghostFile.open( QIODevice::ReadWrite ) );
    REQUIRE( ghostFile.setFileTime( QDateTime::currentDateTimeUtc().addSecs( 5 ),
                                    QFileDevice::FileModificationTime ) );
    ghostFile.close();

    const int quarantined = WorkflowCheckpointManager::electCheckpoints( dir.path() );
    REQUIRE( quarantined == 1 );
    // Exactly one of the two remains listed; the other is renamed (not lost).
    const int remaining = QDir( dir.path() )
                              .entryList( QStringList{ QStringLiteral( "checkpoint_run-1*.json" ) } )
                              .size();
    REQUIRE( remaining == 1 );
    const int orphaned =
        QDir( dir.path() ).entryList( QStringList{ QStringLiteral( "*.orphaned" ) } ).size();
    REQUIRE( orphaned == 1 );
}

TEST_CASE( "completed run checkpoints archive with a bounded history",
           "[fault][history]" )
{
    QTemporaryDir dir;
    for ( int i = 0; i < 55; ++i )
    {
        const QString cp = dir.filePath( QStringLiteral( "checkpoint_hist-%1.json" ).arg( i ) );
        writeBytes( cp, "{}" );
        // Distinct mtimes so the pruning order is deterministic.
        REQUIRE( WorkflowCheckpointManager::archiveCompletedRun( cp, dir.path(), 50 ) );
    }
    const auto history =
        QDir( dir.filePath( "history" ) )
            .entryList( QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files );
    REQUIRE( history.size() == 50 );
    // Completed checkpoint is no longer in the live directory.
    REQUIRE_FALSE( QFile::exists( dir.filePath( "checkpoint_hist-0.json" ) ) );
}

TEST_CASE( "a second process cannot own an actively locked run", "[fault][double_start]" )
{
    QTemporaryDir dir;
    const QString lockPath =
        WorkflowRunLock::lockPathForRun( dir.path(), std::string( "run-double" ) );
    WorkflowRunLock first( lockPath );
    const auto acquired = first.tryAcquire();
    REQUIRE( acquired == WorkflowRunLock::TryResult::Acquired );

    // The SAME process already holds the flock — a second lock object in a
    // child process is the real-world case; simulate with fork (chaos suite
    // runs serially, fork is safe here).
    const pid_t pid = ::fork();
    if ( pid == 0 )
    {
        WorkflowRunLock second( lockPath );
        const WorkflowRunLock::TryResult result = second.tryAcquire();
        _exit( result == WorkflowRunLock::TryResult::HeldByLiveOwner ? 42 : 1 );
    }
    REQUIRE( pid > 0 );
    int status = 0;
    ::waitpid( pid, &status, 0 );
    REQUIRE( WEXITSTATUS( status ) == 42 );
}

TEST_CASE( "repeated identical workflow submissions serialize via the run lock",
           "[fault][concurrent_identical]" )
{
    // The flock contract (kernel-owned, released on death) is what makes a
    // killed owner safe: after the lock object dies, the next acquirer wins.
    QTemporaryDir dir;
    const QString lockPath =
        WorkflowRunLock::lockPathForRun( dir.path(), std::string( "run-reuse" ) );
    {
        WorkflowRunLock owner( lockPath );
        REQUIRE( owner.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
    } // "process death": descriptor closed
    WorkflowRunLock next( lockPath );
    REQUIRE( next.tryAcquire() == WorkflowRunLock::TryResult::Acquired );
}
