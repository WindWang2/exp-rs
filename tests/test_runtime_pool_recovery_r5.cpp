// test_runtime_pool_recovery_r5.cpp — R5 Track 07: REAL worker/pool restart
// recovery oracles for the python worker channel (src/python/isolated).
//
// The #1353 residual this suite closes: the R4 in-flight recovery case drove
// the failure path by INVOKING PENDING CALLBACKS BY HAND. Every case here
// drives the pool's true recovery machinery against a real worker process
// (tests/fixtures/fake_python_worker — same wire protocol as worker_daemon.py,
// zero Python): real process death, real backoff timer, real reconnect, real
// replay on the restarted worker. The worker's pid — not a mock — is the
// identity asserted across each restart.
//
// Determinism: requests are sent with params.hold=true, so the worker logs
// "held" and stays UNANSWERED until the test sends test.flush. A held request
// is provably in flight at kill/shrink/shutdown time — no answer race — and
// the replayed copy announces itself on the restarted worker with a second
// "held" line before the flush fires.
//
// Covered contracts:
//   1. SIGKILL mid-request  -> crash classification (real exit axis + stderr
//      tail) -> restart -> in-flight request ANSWERED on the new pid;
//   2. clean self-exit with in-flight work (protocol EOF) -> same recovery;
//   3. socket EOF with a LIVE worker (daemon dropped the connection) ->
//      recovery; the death-triggering control request itself is answered
//      with a typed error once its replay budget runs out (no crash loop);
//   4. crash-restart budget exhaustion retires the node and answers typed;
//   5. released jobs earn the budget back (dead-worker recycling);
//   6. pool shutdown answers in-flight requests instead of leaking them;
//   7. pool resize away a node with in-flight work answers typed;
//   8. replayRecoveredRequest: the silent sendRequest(-1) drop is closed;
//   9. a restarted worker that never connects hits the typed watchdog;
//  10. repeated recovery cycles leak no fds / zombies / workers.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "python/isolated/python_ipc_server.h"
#include "python/isolated/python_worker_process_pool.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <thread>

using namespace sicnu::python::isolated;

namespace
{
#ifndef FAKE_WORKER_BIN
#define FAKE_WORKER_BIN "sicnu_fake_python_worker-not-built"
#endif

struct FixtureEnv
{
    QTemporaryDir dir;
    QString logPath;

    explicit FixtureEnv( const char *mode = nullptr, const char *stderrMarker = nullptr )
    {
        logPath = dir.filePath( QStringLiteral( "worker.log" ) );
        qputenv( "SICNU_FAKE_WORKER_PIDFILE", dir.filePath( QStringLiteral( "worker.pid" ) ).toUtf8() );
        qputenv( "SICNU_FAKE_WORKER_LOG", logPath.toUtf8() );
        if ( mode )
            qputenv( "SICNU_FAKE_WORKER_MODE", QByteArray( mode ) );
        else
            qunsetenv( "SICNU_FAKE_WORKER_MODE" );
        if ( stderrMarker )
            qputenv( "SICNU_FAKE_WORKER_STDERR", QByteArray( stderrMarker ) );
        else
            qunsetenv( "SICNU_FAKE_WORKER_STDERR" );
    }
};

bool waitOn( std::atomic<bool> &flag, int timeoutMs = 45'000 )
{
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot( true );
    QObject::connect( &deadline, &QTimer::timeout, &loop, &QEventLoop::quit );
    deadline.start( timeoutMs );
    while ( !flag.load() && deadline.isActive() )
    {
        loop.processEvents( QEventLoop::AllEvents, 20 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return flag.load();
}

int logCount( const QString &logPath, const QByteArray &needle )
{
    QFile log( logPath );
    if ( !log.open( QIODevice::ReadOnly ) )
        return 0;
    const QByteArray all = log.readAll();
    int count = 0;
    for ( int pos = all.indexOf( needle ); pos >= 0; pos = all.indexOf( needle, pos + 1 ) )
        ++count;
    return count;
}

bool waitLogCount( const QString &logPath, const QByteArray &needle, int minimum, int timeoutMs = 30'000 )
{
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot( true );
    QObject::connect( &deadline, &QTimer::timeout, &loop, &QEventLoop::quit );
    deadline.start( timeoutMs );
    while ( logCount( logPath, needle ) < minimum && deadline.isActive() )
    {
        loop.processEvents( QEventLoop::AllEvents, 20 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    return logCount( logPath, needle ) >= minimum;
}

std::size_t openFdCount()
{
    std::error_code ec;
    return static_cast<std::size_t>(
        std::distance( std::filesystem::directory_iterator( "/proc/self/fd", ec ),
                       std::filesystem::directory_iterator() ) );
}

bool processAlive( qint64 pid )
{
    if ( pid <= 0 )
        return false;
    std::error_code ec;
    return std::filesystem::exists( std::filesystem::path( "/proc" ) / std::to_string( pid ), ec ) && !ec;
}

struct Probe
{
    std::atomic<bool> answered{ false };
    std::atomic<bool> isError{ true };
    QJsonObject result;
    QString message;
    void record( const QJsonObject &payload, bool err )
    {
        result = payload;
        message = payload.value( QStringLiteral( "message" ) ).toString();
        isError.store( err );
        answered.store( true );
    }
};

WorkerNode *acquireOne( PythonWorkerProcessPool &pool )
{
    for ( int i = 0; i < 500; ++i )
    {
        if ( WorkerNode *node = pool.acquireWorker() )
            return node;
        QEventLoop loop;
        QTimer::singleShot( 20, &loop, &QEventLoop::quit );
        loop.exec();
    }
    return nullptr;
}

/// Waits until the node's restarted worker is running AND attached.
bool workerReady( PythonWorkerProcessPool &pool, WorkerNode *node )
{
    for ( int i = 0; i < 1000; ++i )
    {
        if ( pool.poolHealth().active == 1 && node->server && node->server->hasClient() )
            return true;
        QEventLoop loop;
        QTimer::singleShot( 20, &loop, &QEventLoop::quit );
        loop.exec();
    }
    return false;
}

/// Sends a HELD test.job (unanswered until a test.flush arrives); the
/// eventual answer lands in @p probe.
void sendHeldJob( WorkerNode *node, const QString &jobId, int retriesLeft, Probe &probe )
{
    QJsonObject params;
    params[QStringLiteral( "job_id" )] = jobId;
    params[QStringLiteral( "hold" )] = true;
    node->server->sendRequest(
        QStringLiteral( "test.job" ), params,
        [ &probe ]( const QJsonObject &payload, bool err ) { probe.record( payload, err ); },
        retriesLeft );
}

void flushJobs( WorkerNode *node, Probe &flushProbe )
{
    node->server->sendRequest(
        QStringLiteral( "test.flush" ), QJsonObject(),
        [ &flushProbe ]( const QJsonObject &payload, bool err ) { flushProbe.record( payload, err ); },
        /*retriesLeft=*/1 );
}
} // namespace

int main( int argc, char *argv[] )
{
    QCoreApplication app( argc, argv );
    QCoreApplication::setApplicationName( QLatin1String( "test-runtime-pool-recovery-r5" ) );
    return Catch::Session().run( argc, argv );
}

TEST_CASE( "SIGKILL mid-request replays the in-flight job on the REALLY restarted worker",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env( nullptr, "R5-CRASH-STDERR-MARKER" );
    PythonWorkerProcessPool pool( 1 );

    std::atomic<bool> crashed{ false };
    QString crashReason;
    std::atomic<int> restarts{ 0 };
    QObject::connect( &pool, &PythonWorkerProcessPool::workerCrashed,
                      [ &crashReason, &crashed ]( int, const QString &reason ) {
                          crashReason = reason;
                          crashed.store( true );
                      } );
    QObject::connect( &pool, &PythonWorkerProcessPool::workerRestarted,
                      [&]( int ) { restarts.fetch_add( 1 ); } );

    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );
    REQUIRE( waitLogCount( env.logPath, "connected", 1 ) );

    const qint64 firstPid = node->worker->processId();
    REQUIRE( firstPid > 0 );

    // The job is HELD: provably unanswered while the worker is killed.
    Probe job;
    sendHeldJob( node, QStringLiteral( "A" ), /*retriesLeft=*/2, job );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) );

    ::kill( static_cast<pid_t>( firstPid ), SIGKILL ); // REAL process death

    // The restarted worker announces the replayed job (second "held"), then
    // the flush lets it answer.
    REQUIRE( waitLogCount( env.logPath, "held", 2 ) );
    Probe flush;
    flushJobs( node, flush );
    REQUIRE( waitOn( job.answered ) );

    CHECK( crashed.load() );
    INFO( "crash reason: " << crashReason.toStdString() );
    CHECK( crashReason.contains( QStringLiteral( "crashExit=1" ) ) );
    CHECK( crashReason.contains( QStringLiteral( "R5-CRASH-STDERR-MARKER" ) ) );
    CHECK( restarts.load() == 1 );
    CHECK( !job.isError.load() ); // replayed to completion — not failed off
    CHECK( job.result.value( QStringLiteral( "job_id" ) ).toString() == QLatin1String( "A" ) );
    // The oracle of a REAL restart: the answering worker is a NEW process.
    CHECK( job.result.value( QStringLiteral( "pid" ) ).toVariant().toLongLong() != firstPid );
    CHECK( pool.poolHealth().totalRestarts == 1 );

    pool.releaseWorker( node );
    pool.shutdown();
}

TEST_CASE( "A clean self-exit with in-flight work recovers over a real restart (protocol EOF)",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env;
    PythonWorkerProcessPool pool( 1 );
    std::atomic<bool> crashed{ false };
    QString crashReason;
    QObject::connect( &pool, &PythonWorkerProcessPool::workerCrashed,
                      [ &crashReason, &crashed ]( int, const QString &reason ) {
                          crashReason = reason;
                          crashed.store( true );
                      } );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );
    REQUIRE( waitLogCount( env.logPath, "connected", 1 ) );

    const qint64 firstPid = node->worker->processId();
    Probe job;
    sendHeldJob( node, QStringLiteral( "B" ), /*retriesLeft=*/2, job );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) );

    // The control request IS answered; the worker then exits 0 while the job
    // is still held/unanswered — the historical clean-exit whitelist emits no
    // crash, so only the EOF-with-in-flight contract can recover this.
    Probe control;
    node->server->sendRequest(
        QStringLiteral( "test.answer_then_exit" ), QJsonObject{ { QStringLiteral( "code" ), 0 } },
        [ &control ]( const QJsonObject &payload, bool err ) { control.record( payload, err ); },
        /*retriesLeft=*/2 );

    REQUIRE( waitOn( control.answered ) );
    CHECK( !control.isError.load() );

    // Deterministic drain handshake: the fixture exits only after answering
    // one more request — when the hang's answer arrives, the control answer
    // was provably delivered on the same ordered stream. The drain answer
    // and the exit land in the same event batch, which additionally pins the
    // recovery's buffered-answer drain (drainBufferedResponses): the loss
    // notification and the data notification are unordered, and the answer
    // must win over the callback teardown (no phantom "crashed on a request
    // that actually completed").
    Probe drain;
    node->server->sendRequest(
        QStringLiteral( "test.hang" ), QJsonObject(),
        [ &drain ]( const QJsonObject &payload, bool err ) { drain.record( payload, err ); },
        /*retriesLeft=*/2 );
    REQUIRE( waitOn( drain.answered ) );
    CHECK( !drain.isError.load() );

    // Recovery restarted the worker and replayed the held job (second
    // "held"); the flush lets the restarted worker answer it.
    REQUIRE( waitLogCount( env.logPath, "held", 2 ) );
    Probe flush;
    flushJobs( node, flush );
    REQUIRE( waitOn( job.answered ) );
    INFO( "loss reason: " << crashReason.toStdString() );
    CHECK( crashed.load() );
    CHECK( crashReason.contains( QStringLiteral( "exitCode=0" ) ) );
    CHECK( crashReason.contains( QStringLiteral( "protocol EOF" ) ) );
    CHECK( !job.isError.load() );
    CHECK( job.result.value( QStringLiteral( "job_id" ) ).toString() == QLatin1String( "B" ) );
    CHECK( job.result.value( QStringLiteral( "pid" ) ).toVariant().toLongLong() != firstPid );
    CHECK( pool.poolHealth().totalRestarts == 1 );

    pool.releaseWorker( node );
    pool.shutdown();
}

TEST_CASE( "Socket EOF with a live worker recovers; the death-triggering control request ends typed",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env;
    PythonWorkerProcessPool pool( 1 );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );
    REQUIRE( waitLogCount( env.logPath, "connected", 1 ) );

    const qint64 firstPid = node->worker->processId();
    Probe job;
    sendHeldJob( node, QStringLiteral( "C" ), /*retriesLeft=*/3, job );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) );

    // abort_socket drops the socket and KEEPS the process alive: pure
    // protocol EOF. Its replay would kill every successor — after its budget
    // is exhausted the recovery must ANSWER it typed instead of looping.
    Probe control;
    node->server->sendRequest(
        QStringLiteral( "test.abort_socket" ), QJsonObject(),
        [ &control ]( const QJsonObject &payload, bool err ) { control.record( payload, err ); },
        /*retriesLeft=*/1 );

    REQUIRE( waitOn( control.answered ) ); // two recovery cycles
    REQUIRE( waitLogCount( env.logPath, "held", 3 ) ); // replayed onto worker 3
    Probe flush;
    flushJobs( node, flush );
    REQUIRE( waitOn( job.answered ) );
    CHECK( control.isError.load() ); // exactly the no-crash-loop guarantee
    CHECK( control.message == QLatin1String( "Worker crashed; replay budget exhausted" ) );
    CHECK( !job.isError.load() );
    CHECK( job.result.value( QStringLiteral( "job_id" ) ).toString() == QLatin1String( "C" ) );
    CHECK( job.result.value( QStringLiteral( "pid" ) ).toVariant().toLongLong() != firstPid );
    CHECK( pool.poolHealth().totalRestarts == 2 );

    pool.releaseWorker( node );
    pool.shutdown();
}

TEST_CASE( "Exhausting the crash budget retires the node and answers typed (no crash loop)",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env;
    PythonWorkerProcessPool pool( 1 );
    std::atomic<int> restarts{ 0 };
    QObject::connect( &pool, &PythonWorkerProcessPool::workerRestarted, [&]( int ) { restarts.fetch_add( 1 ); } );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );

    // Five crashes burn the whole budget.
    auto crashOnce = [ & ]( int cycle ) {
        Probe die;
        QJsonObject params;
        params[QStringLiteral( "exit_code" )] = 42;
        node->server->sendRequest(
            QStringLiteral( "test.die_before_answer" ), params,
            [ &die ]( const QJsonObject &payload, bool err ) { die.record( payload, err ); },
            /*retriesLeft=*/0 );
        REQUIRE( waitLogCount( env.logPath, "dying", cycle + 1 ) );
        REQUIRE( waitOn( die.answered ) );
        CHECK( die.isError.load() );
        INFO( "cycle " << cycle );
        REQUIRE( workerReady( pool, node ) );
    };
    crashOnce( 0 );
    crashOnce( 1 );
    crashOnce( 2 );
    crashOnce( 3 );
    crashOnce( 4 );
    CHECK( restarts.load() == 5 );

    // Sixth loss: the budget is gone — the node is RETIRED. The request is
    // answered typed and no sixth worker ever appears.
    Probe die6;
    QJsonObject params;
    params[QStringLiteral( "exit_code" )] = 42;
    node->server->sendRequest(
        QStringLiteral( "test.die_before_answer" ), params,
        [ &die6 ]( const QJsonObject &payload, bool err ) { die6.record( payload, err ); },
        /*retriesLeft=*/0 );
    REQUIRE( waitLogCount( env.logPath, "dying", 6 ) );
    REQUIRE( waitOn( die6.answered ) );
    CHECK( die6.isError.load() );
    CHECK( die6.message == QLatin1String( "Worker crash restart budget exhausted" ) );
    CHECK( restarts.load() == 5 ); // the sixth death restarted NOTHING
    CHECK( pool.poolHealth().active == 0 ); // retired: no worker left
    CHECK( pool.poolHealth().totalRestarts == 5 );
    pool.releaseWorker( node ); // a retired node earns nothing back
    CHECK( pool.poolHealth().active == 0 );
    pool.shutdown();
}

TEST_CASE( "Workers that keep serving earn crash budget back (dead-worker recycling)",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env;
    PythonWorkerProcessPool pool( 1 );
    std::atomic<int> restarts{ 0 };
    QObject::connect( &pool, &PythonWorkerProcessPool::workerRestarted, [&]( int ) { restarts.fetch_add( 1 ); } );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );

    // Three crashes (budget 5 -> 2), then a SERVED job whose release earns
    // one step back: without that step the sequence below would retire the
    // node at the seventh crash.
    auto crashOnce = [ & ]( int cycle ) {
        Probe die;
        QJsonObject params;
        params[QStringLiteral( "exit_code" )] = 42;
        node->server->sendRequest(
            QStringLiteral( "test.die_before_answer" ), params,
            [ &die ]( const QJsonObject &payload, bool err ) { die.record( payload, err ); },
            /*retriesLeft=*/0 );
        REQUIRE( waitLogCount( env.logPath, "dying", cycle + 1 ) );
        REQUIRE( waitOn( die.answered ) );
        REQUIRE( workerReady( pool, node ) );
    };
    crashOnce( 0 );
    crashOnce( 1 );
    crashOnce( 2 );
    REQUIRE( pool.poolHealth().active == 1 );

    Probe served;
    sendHeldJob( node, QStringLiteral( "serve" ), /*retriesLeft=*/1, served );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) );
    Probe flush;
    flushJobs( node, flush );
    REQUIRE( waitOn( served.answered ) );
    CHECK( !served.isError.load() );
    pool.releaseWorker( node ); // budget 2 -> 3

    crashOnce( 3 );
    crashOnce( 4 );
    crashOnce( 5 ); // budget 3 -> 0 across these three
    CHECK( restarts.load() == 6 );
    CHECK( pool.poolHealth().active == 1 ); // alive where a no-recycling pool retires

    Probe served2;
    sendHeldJob( node, QStringLiteral( "serve2" ), /*retriesLeft=*/1, served2 );
    REQUIRE( waitLogCount( env.logPath, "held", 2 ) );
    Probe flush2;
    flushJobs( node, flush2 );
    REQUIRE( waitOn( served2.answered ) );
    pool.releaseWorker( node ); // budget 0 -> 1
    crashOnce( 6 ); // the crash that would retire a no-recycling node
    CHECK( restarts.load() == 7 );
    CHECK( pool.poolHealth().active == 1 );
    CHECK( pool.poolHealth().totalRestarts == 7 );

    pool.shutdown();
}

TEST_CASE( "Pool shutdown answers in-flight requests instead of leaking them",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env;
    PythonWorkerProcessPool pool( 1 );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );
    REQUIRE( waitLogCount( env.logPath, "connected", 1 ) );

    Probe job;
    sendHeldJob( node, QStringLiteral( "S" ), /*retriesLeft=*/2, job );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) ); // provably unanswered

    pool.shutdown();
    // The server the request was sent on is gone; the caller must have been
    // answered, not left hanging on a deleted object.
    CHECK( waitOn( job.answered, 5'000 ) );
    CHECK( job.isError.load() );
    CHECK( job.message == QLatin1String( "Worker pool is shutting down" ) );
}

TEST_CASE( "Resizing away a node with in-flight work answers its requests typed",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env;
    PythonWorkerProcessPool pool( 2 );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *first = acquireOne( pool );
    REQUIRE( first != nullptr );
    WorkerNode *second = nullptr;
    for ( int i = 0; i < 500 && !second; ++i )
    {
        second = pool.acquireWorker(); // first is busy-held; only the other node can come back
        if ( !second )
        {
            QEventLoop loop;
            QTimer::singleShot( 20, &loop, &QEventLoop::quit );
            loop.exec();
        }
    }
    REQUIRE( second != nullptr );

    // acquireWorker walks m_nodes in order: the SECOND-held node is the pool
    // tail the shrink removes.
    WorkerNode *tail = second->id > first->id ? second : first;

    Probe job;
    sendHeldJob( tail, QStringLiteral( "R" ), /*retriesLeft=*/2, job );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) ); // provably unanswered

    pool.releaseWorker( tail ); // idle again — but its request is still open
    REQUIRE( pool.setPoolSize( 1 ) ); // the shrink removes the idle TAIL node

    CHECK( waitOn( job.answered, 10'000 ) );
    CHECK( job.isError.load() );
    CHECK( job.message == QLatin1String( "Worker removed during pool resize" ) );
    CHECK( pool.poolHealth().total == 1 ); // exactly the tail node went away
    CHECK( pool.poolHealth().active == 1 ); // the head node is untouched and running

    pool.shutdown();
}

TEST_CASE( "Retiring a node answers requests recovered by EARLIER cycles (pre-connect death streak)",
           "[runtime][python][pool][r5]" )
{
    // Regression oracle for the orphaned-recovery-entry window: replacement
    // workers die BEFORE connecting, so every cycle's fresh in-flight list is
    // empty and the original entry survives in m_pendingRecovery. Each
    // cycle's watchdog dies with its server at the next loss (backoffs of
    // 1s..8s all fire before any 5s watchdog), so only the RETIREMENT can
    // answer the original caller — silence here would be the bug.
    FixtureEnv env;
    PythonWorkerProcessPool pool( 1 );
    std::atomic<int> restarts{ 0 };
    QObject::connect( &pool, &PythonWorkerProcessPool::workerRestarted, [&]( int ) { restarts.fetch_add( 1 ); } );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );
    REQUIRE( waitLogCount( env.logPath, "connected", 1 ) );

    Probe job;
    QJsonObject params;
    params[QStringLiteral( "job_id" )] = QStringLiteral( "ORPHAN" );
    params[QStringLiteral( "hold" )] = true;
    node->server->sendRequest(
        QStringLiteral( "test.job" ), params,
        [ &job ]( const QJsonObject &payload, bool err ) { job.record( payload, err ); },
        /*retriesLeft=*/5 );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) );
    const qint64 firstPid = node->worker->processId();
    ::kill( static_cast<pid_t>( firstPid ), SIGKILL ); // entry P1 born

    // Every replacement worker now dies pre-connect, burning the budget
    // without ever consuming P1.
    qputenv( "SICNU_FAKE_WORKER_MODE", "die_on_start" );
    struct Restore
    {
        ~Restore() { qunsetenv( "SICNU_FAKE_WORKER_MODE" ); }
    } restore;

    REQUIRE( waitOn( job.answered, 60'000 ) ); // backoffs 1+2+4+8 s plus spawns
    CHECK( job.isError.load() );
    CHECK( job.message == QLatin1String( "Worker crash restart budget exhausted" ) );
    CHECK( restarts.load() == 5 ); // W2..W6 spawned and died pre-connect (budget 5 -> 0)
    CHECK( pool.poolHealth().active == 0 ); // retired on the sixth loss
    CHECK( pool.poolHealth().totalRestarts == 5 );
    pool.shutdown();
}

TEST_CASE( "An answer that races its own socket abort still reaches the caller",
           "[runtime][python][pool][r5]" )
{
    // The buffered-answer race contract: the fixture writes the response and
    // aborts the socket in the same breath — death/data notifications are
    // unordered, and whichever order the console observes, the answer must
    // be dispatched (normal readyRead OR the recovery's
    // drainBufferedResponses), never dropped by the callback teardown.
    // (Which branch wins is kernel-scheduling-dependent by construction; the
    // CONTRACT — answered, not dropped — is the deterministic part.)
    FixtureEnv env;
    PythonWorkerProcessPool pool( 1 );
    std::atomic<bool> crashed{ false };
    QString crashReason;
    QObject::connect( &pool, &PythonWorkerProcessPool::workerCrashed,
                      [ &crashReason, &crashed ]( int, const QString &reason ) {
                          crashReason = reason;
                          crashed.store( true );
                      } );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );
    REQUIRE( waitLogCount( env.logPath, "connected", 1 ) );
    const qint64 firstPid = node->worker->processId();

    Probe job;
    sendHeldJob( node, QStringLiteral( "RACE" ), /*retriesLeft=*/2, job );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) );

    Probe control;
    node->server->sendRequest(
        QStringLiteral( "test.answer_then_abort" ), QJsonObject(),
        [ &control ]( const QJsonObject &payload, bool err ) { control.record( payload, err ); },
        /*retriesLeft=*/1 );

    // The control answer MUST arrive despite the abort race…
    REQUIRE( waitOn( control.answered ) );
    CHECK( !control.isError.load() );
    // …and the held job must still be recovered onto a restarted worker.
    REQUIRE( waitLogCount( env.logPath, "held", 2 ) );
    Probe flush;
    flushJobs( node, flush );
    REQUIRE( waitOn( job.answered ) );
    INFO( "loss reason: " << crashReason.toStdString() );
    CHECK( crashed.load() );
    CHECK( crashReason.contains( QStringLiteral( "protocol EOF" ) ) );
    CHECK( !job.isError.load() );
    CHECK( job.result.value( QStringLiteral( "pid" ) ).toVariant().toLongLong() != firstPid );
    CHECK( pool.poolHealth().totalRestarts == 1 );

    pool.releaseWorker( node );
    pool.shutdown();
}

TEST_CASE( "replayRecoveredRequest answers recovered requests the server cannot re-send",
           "[runtime][python][pool][r5]" )
{
    // The silent-drop branch: sendRequest() refuses a server with no client.
    PythonIpcServer server;
    const QString socketName = QStringLiteral( "r5-replay-%1" ).arg( QCoreApplication::applicationPid() );
    REQUIRE( server.listen( socketName ) );

    Probe probe;
    PythonIpcServer::PendingRequest stranded;
    stranded.method = QStringLiteral( "iface.run" );
    stranded.params = QJsonObject();
    stranded.retriesLeft = 2;
    stranded.callback = [ &probe ]( const QJsonObject &payload, bool err ) { probe.record( payload, err ); };

    CHECK( replayRecoveredRequest( &server, stranded, QStringLiteral( "no client" ) ) == false );
    CHECK( probe.answered.load() ); // answered, not dropped
    CHECK( probe.isError.load() );
    CHECK( probe.message == QLatin1String( "no client" ) );

    // No budget: answered typed without touching the wire either.
    Probe noBudget;
    PythonIpcServer::PendingRequest budgetless;
    budgetless.method = QStringLiteral( "iface.run" );
    budgetless.retriesLeft = 0;
    budgetless.callback = [ &noBudget ]( const QJsonObject &payload, bool err ) { noBudget.record( payload, err ); };
    CHECK( replayRecoveredRequest( &server, budgetless, QStringLiteral( "no budget" ) ) == false );
    CHECK( noBudget.answered.load() );
    CHECK( noBudget.message == QLatin1String( "no budget" ) );
}

TEST_CASE( "A restarted worker that never connects hits the typed restart watchdog",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env( nullptr );
    PythonWorkerProcessPool pool( 1 );
    REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
    WorkerNode *node = acquireOne( pool );
    REQUIRE( node != nullptr );
    REQUIRE( waitLogCount( env.logPath, "connected", 1 ) );

    // The REPLACEMENT worker never attaches: flip the fixture mode before the
    // restart spawns its process.
    qputenv( "SICNU_FAKE_WORKER_MODE", "never_connect" );
    struct Restore
    {
        ~Restore() { qunsetenv( "SICNU_FAKE_WORKER_MODE" ); }
    } restore;

    Probe job;
    sendHeldJob( node, QStringLiteral( "W" ), /*retriesLeft=*/2, job );
    REQUIRE( waitLogCount( env.logPath, "held", 1 ) );
    const qint64 firstPid = node->worker->processId();
    ::kill( static_cast<pid_t>( firstPid ), SIGKILL );

    REQUIRE( waitOn( job.answered, 30'000 ) );
    CHECK( job.isError.load() );
    CHECK( job.message == QLatin1String( "Worker restart timed out" ) );
    pool.shutdown();
}

TEST_CASE( "Repeated real recovery cycles leak no fds, zombies or workers",
           "[runtime][python][pool][r5]" )
{
    FixtureEnv env;
    const std::size_t fdBefore = openFdCount();
    qint64 lastWorkerPid = 0;
    {
        PythonWorkerProcessPool pool( 1 );
        REQUIRE( pool.initialize( QLatin1String( FAKE_WORKER_BIN ), QStringLiteral( "/dev/null" ) ) );
        WorkerNode *node = acquireOne( pool );
        REQUIRE( node != nullptr );
        REQUIRE( waitLogCount( env.logPath, "connected", 1 ) );

        for ( int cycle = 0; cycle < 3; ++cycle )
        {
            Probe job;
            sendHeldJob( node, QStringLiteral( "L%1" ).arg( cycle ), /*retriesLeft=*/2, job );
            REQUIRE( waitLogCount( env.logPath, "held", 2 * cycle + 1 ) );
            const qint64 pid = node->worker->processId();
            REQUIRE( pid > 0 );
            lastWorkerPid = pid;
            ::kill( static_cast<pid_t>( pid ), SIGKILL );
            // The restarted worker announces the replay (held #2N+2), then
            // the flush lets it answer.
            REQUIRE( waitLogCount( env.logPath, "held", 2 * cycle + 2 ) );
            Probe flush;
            flushJobs( node, flush );
            REQUIRE( waitOn( job.answered ) );
            CHECK( !job.isError.load() );
            CHECK( job.result.value( QStringLiteral( "job_id" ) ).toString()
                   == QStringLiteral( "L%1" ).arg( cycle ) );
            REQUIRE( workerReady( pool, node ) );
        }
        pool.shutdown();
    }

    // No zombie: the killed and reaped workers are all gone, including the
    // last one QProcess reaped during shutdown.
    bool reaped = !processAlive( lastWorkerPid );
    for ( int i = 0; i < 200 && !reaped; ++i )
    {
        reaped = !processAlive( lastWorkerPid );
        if ( !reaped )
        {
            QEventLoop loop;
            QTimer::singleShot( 25, &loop, &QEventLoop::quit );
            loop.exec();
        }
    }
    CHECK( reaped );

    // The fixture wrote nothing outside its own temporary directory.
    CHECK( QDir( env.dir.path() ).entryList( QDir::Files ).size() == 2 ); // pidfile + log

    // fd budget: per cycle at most one server socket + one connection is
    // created and destroyed; nothing may accumulate across the cycles.
    const std::size_t fdAfter = openFdCount();
    INFO( "fds before=" << fdBefore << " after=" << fdAfter );
    CHECK( fdAfter <= fdBefore + 4 );
}
