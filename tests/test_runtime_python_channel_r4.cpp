// test_runtime_python_channel_r4.cpp — Track 15 WP-F: python worker channel
// boundaries (src/python/isolated — the embedded-console ↔ worker channel).
// Real process boundaries, zero real Python (fixtures run under /bin/sh) and
// zero network (QLocalServer is a local unix socket):
//   1. exit-code classification axis: a worker exiting 42 is a crash, exiting
//      0/1 is not (the historical clean-exit whitelist, pinned);
//   2. a SIGKILLed worker is CrashExit-class AND its stderr is captured in
//      full (truncation of a small payload would be a defect);
//   3. stopWorker() removes the whole worker process TREE (a TERM-ignoring
//      worker with a grandchild leaves no residue — process enumeration is
//      the oracle);
//   4. channel errors surface as typed AwaitStatus values: NoClient when no
//      worker is connected, Disconnected when the worker dies mid-request;
//   5. in-flight request recovery: takeInFlightRequests hands back the
//      pending call once, then is empty (idempotent);
//   6. pool auto-heal drives the REAL recovery path end to end: crash →
//      pending takeover → backoff restart → replay onto the restarted
//      worker's fresh IPC server → correlated response (Round-2: replaces
//      the manual-callback proof for the recovery seam);
//   7. replay answers recovered requests whose retry budget is exhausted
//      with a typed error at replay time;
//   8. the recovery watchdog answers pending calls when the restarted
//      worker never gains a client (no leaked in-flight on a dead node).
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "python/isolated/python_ipc_server.h"
#include "python/isolated/python_worker_process.h"
#include "python/isolated/python_worker_process_pool.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

using namespace sicnu::python::isolated;

namespace
{
struct ScriptFixture
{
    QTemporaryDir dir;
    QString write( const QString &name, const std::string &body ) const
    {
        const QString path = dir.filePath( name );
        std::ofstream out( path.toStdString(), std::ios::binary | std::ios::trunc );
        out << body;
        out.close();
        return path;
    }
};

bool waitOn( std::atomic<bool> &flag, int timeoutMs = 5'000 )
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

bool waitUntil( const std::function<bool()> &predicate, int timeoutMs = 5'000 )
{
    // The predicate may carry side effects (e.g. acquireWorker hands out the
    // one node and marks it busy), so it must NEVER be re-evaluated after it
    // has succeeded — the result is captured, not recomputed at the end.
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot( true );
    QObject::connect( &deadline, &QTimer::timeout, &loop, &QEventLoop::quit );
    deadline.start( timeoutMs );
    while ( deadline.isActive() )
    {
        if ( predicate() )
            return true;
        loop.processEvents( QEventLoop::AllEvents, 20 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return false;
}

void sleepMs( int ms )
{
    QEventLoop loop;
    QTimer::singleShot( ms, &loop, &QEventLoop::quit );
    loop.exec();
}

bool fileHasPid( const std::string &pidPath, qint64 &pidOut )
{
    std::ifstream in( pidPath );
    if ( !( in >> pidOut ) )
        return false;
    return pidOut > 0;
}

bool processAlive( qint64 pid )
{
    if ( pid <= 0 )
        return false;
    std::error_code ec;
    const std::filesystem::path proc =
        std::filesystem::path( "/proc" ) / std::to_string( pid );
    return std::filesystem::exists( proc, ec ) && !ec;
}
} // namespace

int main( int argc, char *argv[] )
{
    QCoreApplication app( argc, argv );
    QCoreApplication::setApplicationName( QLatin1String( "test-runtime-python-channel-r4" ) );
    return Catch::Session().run( argc, argv );
}

TEST_CASE( "Worker exit-code classification follows the historical clean-exit axis",
           "[runtime][python][r4]" )
{
    struct Case
    {
        int exitCode;
        bool expectCrash;
    };
    // 0/1 are the historical benign self-exits, 137 the SIGKILL self-report;
    // 42 is outside the whitelist and must classify as a crash.
    const Case cases[] = { { 0, false }, { 1, false }, { 42, true }, { 137, false } };

    for ( const Case &item : cases )
    {
        ScriptFixture fixture;
        const QString script = fixture.write(
            "exit_code.sh", "#!/bin/sh\nexit " + std::to_string( item.exitCode ) + "\n" );
        PythonWorkerProcess worker;
        std::atomic<bool> crashed{ false };
        std::atomic<bool> finished{ false };
        int observedExit = -1;
        QObject::connect( &worker, &PythonWorkerProcess::workerCrashed, [&] {
            crashed.store( true );
        } );
        QObject::connect( &worker, &PythonWorkerProcess::workerFinished,
                          [ &, &observedExit = observedExit ]( int exitCode,
                                                               QProcess::ExitStatus ) {
                              if ( exitCode == item.exitCode )
                              {
                                  observedExit = exitCode;
                                  finished.store( true );
                              }
                          } );

        INFO( "exit code under test: " << item.exitCode );
        REQUIRE( worker.startWorker( "r4-classify", "/bin/sh", script ) );
        REQUIRE( waitOn( finished ) );
        REQUIRE( observedExit == item.exitCode );
        REQUIRE( crashed.load() == item.expectCrash );
    }
}

TEST_CASE( "A SIGKILLed worker is a crash and its stderr is captured in full",
           "[runtime][python][r4]" )
{
    ScriptFixture fixture;
    const QString script = fixture.write(
        "noisy.sh", "#!/bin/sh\necho R4-STDERR-MARKER-0123456789 >&2\nkill -9 $$\n" );
    PythonWorkerProcess worker;
    std::atomic<bool> crashed{ false };
    QObject::connect( &worker, &PythonWorkerProcess::workerCrashed, [&] {
        crashed.store( true );
    } );

    REQUIRE( worker.startWorker( "r4-noisy", "/bin/sh", script ) );
    REQUIRE( waitOn( crashed ) );
    // The classification is the crash axis; the captured stderr must contain
    // the marker VERBATIM (a truncated capture would be a defect).
    const QByteArray stderrTail = worker.capturedStderr();
    INFO( "captured stderr: " << stderrTail.constData() );
    REQUIRE( stderrTail.contains( "R4-STDERR-MARKER-0123456789" ) );
}

TEST_CASE( "stopWorker removes the whole worker process tree (no grandchild residue)",
           "[runtime][python][r4]" )
{
    ScriptFixture fixture;
    const QString pidFile = fixture.dir.filePath( "grandchild.pid" );
    // The worker ignores TERM (hostile-but-legal shutdown behavior) and
    // spawns a grandchild whose pid it reports.
    const QString script = fixture.write(
        "tree.sh", "#!/bin/sh\ntrap '' TERM INT\nsleep 300 &\necho $! > "
                       + pidFile.toStdString() + "\nwait\n" );

    PythonWorkerProcess worker;
    std::atomic<bool> running{ false };
    QObject::connect( &worker, &PythonWorkerProcess::workerStarted, [&] {
        running.store( true );
    } );
    REQUIRE( worker.startWorker( "r4-tree", "/bin/sh", script ) );
    REQUIRE( waitOn( running ) );

    qint64 grandchild = 0;
    bool gotPid = false;
    for ( int i = 0; i < 200 && !gotPid; ++i )
    {
        gotPid = fileHasPid( pidFile.toStdString(), grandchild );
        if ( !gotPid )
            sleepMs( 25 );
    }
    REQUIRE( gotPid );
    REQUIRE( processAlive( grandchild ) ); // the tree is real, not staged

    worker.stopWorker();

    // The oracle is process enumeration: the grandchild is gone within a
    // bounded window. (The worker itself is reaped by QProcess; the
    // grandchild only dies if the whole GROUP was killed.)
    bool grandchildGone = false;
    for ( int i = 0; i < 200 && !grandchildGone; ++i )
    {
        grandchildGone = !processAlive( grandchild );
        if ( !grandchildGone )
            sleepMs( 25 );
    }
    INFO( "grandchild pid: " << grandchild );
    REQUIRE( grandchildGone );
}

TEST_CASE( "Channel errors surface as typed AwaitStatus values",
           "[runtime][python][r4]" )
{
    PythonIpcServer server;
    const QString socketName =
        QStringLiteral( "r4-channel-%1" ).arg( QCoreApplication::applicationPid() );
    REQUIRE( server.listen( socketName ) );

    // NoClient: nothing was sent, the caller gets a typed answer at once.
    {
        QJsonObject result;
        bool isError = true;
        const AwaitStatus status =
            server.sendRequestAndAwait( "iface.ping", {}, result, isError, 1'000 );
        REQUIRE( status == AwaitStatus::NoClient );
    }

    // Disconnected: a fake worker connects, then dies mid-request — the
    // awaiting caller observes the typed disconnect, never a hang.
    {
        std::atomic<bool> connected{ false };
        QObject::connect( &server, &PythonIpcServer::clientConnected,
                          [&] { connected.store( true ); } );
        QLocalSocket fakeWorker;
        fakeWorker.connectToServer( socketName );
        REQUIRE( waitOn( connected ) );
        REQUIRE( fakeWorker.state() == QLocalSocket::ConnectedState );
        REQUIRE( server.hasClient() );

        QJsonObject result;
        bool isError = true;
        std::atomic<bool> callReturned{ false };
        AwaitStatus status = AwaitStatus::Timeout;
        // The request enters its wait, then a nested timer kills the worker
        // connection INSIDE the awaiting event loop: the await must observe
        // the typed Disconnected verdict, never a timeout hang.
        QTimer::singleShot( 0, [ & ] {
            status = server.sendRequestAndAwait( "iface.ping", {}, result, isError, 5'000 );
            callReturned.store( true );
        } );
        QTimer::singleShot( 150, [&] { fakeWorker.abort(); } );
        REQUIRE( waitOn( callReturned ) );
        REQUIRE( status == AwaitStatus::Disconnected );
    }
}

TEST_CASE( "Pool crash reports carry the real exit classification and the captured stderr",
           "[runtime][python][r4]" )
{
    ScriptFixture fixture;
    const QString script =
        fixture.write( "pool_crash.sh", "#!/bin/sh\necho POOL-STDERR-MARKER >&2\nexit 42\n" );

    PythonWorkerProcessPool pool( 1 );
    std::atomic<bool> crashed{ false };
    qint64 crashedId = 0;
    QString reason;
    QObject::connect( &pool, &PythonWorkerProcessPool::workerCrashed,
                      [ & ]( int id, const QString &crashReason ) {
                          crashedId = id;
                          reason = crashReason;
                          crashed.store( true );
                      } );
    REQUIRE( pool.initialize( "/bin/sh", script ) );
    REQUIRE( waitOn( crashed, 15'000 ) );

    // The console-side signal is typed AND informative: the historical
    // hardcoded "Process exited unexpectedly" string carried neither the
    // exit classification nor a single byte of the worker's stderr.
    INFO( "crash reason: " << reason.toStdString() );
    REQUIRE( crashedId > 0 );
    CHECK( reason.contains( QStringLiteral( "42" ) ) ); // real exit code
    CHECK( reason.contains( QStringLiteral( "POOL-STDERR-MARKER" ) ) ); // captured stderr
    pool.shutdown();
}

TEST_CASE( "In-flight request recovery hands back pending calls exactly once",
           "[runtime][python][r4]" )
{
    PythonIpcServer server;
    const QString socketName =
        QStringLiteral( "r4-inflight-%1" ).arg( QCoreApplication::applicationPid() );
    REQUIRE( server.listen( socketName ) );

    std::atomic<bool> connected{ false };
    QObject::connect( &server, &PythonIpcServer::clientConnected,
                      [&] { connected.store( true ); } );
    QLocalSocket fakeWorker;
    fakeWorker.connectToServer( socketName );
    REQUIRE( waitOn( connected ) );

    std::atomic<bool> answered{ false };
    server.sendRequest(
        "iface.run", {},
        [ & ]( const QJsonObject &, bool isError ) {
            REQUIRE( isError ); // the recovered request ends in a typed error
            answered.store( true );
        },
        /*retriesLeft=*/2 );

    auto pending = server.takeInFlightRequests();
    REQUIRE( pending.size() == 1 );
    REQUIRE( pending.front().method == QStringLiteral( "iface.run" ) );
    REQUIRE( pending.front().retriesLeft == 2 );

    // Idempotent: the second take is empty (ownership moved).
    REQUIRE( server.takeInFlightRequests().empty() );

    // The pool's failure path answers recovered requests so callers never
    // hang: replaying the callback with an error is observable.
    for ( const auto &request : pending )
    {
        QJsonObject error;
        error.insert( QStringLiteral( "message" ), QStringLiteral( "Worker crashed" ) );
        request.callback( error, true );
    }
    REQUIRE( answered.load() );
}

namespace
{
// Shared skeleton for the real-pool recovery cases: a one-worker pool whose
// fixture worker stays alive long enough for a request to go in flight and
// then dies with a real crash exit (every auto-restart re-runs the same
// script, so later crash cycles churn harmlessly with nothing pending).
struct PoolRecoveryRig
{
    ScriptFixture fixture;
    PythonWorkerProcessPool pool;
    QLocalSocket fakeWorker1;
    QLocalSocket fakeWorker2;
    WorkerNode *node = nullptr;
    std::atomic<bool> restarted{ false };
    std::atomic<bool> connected2{ false };
    std::atomic<bool> answered{ false };
    std::atomic<int> answeredCount{ 0 };
    std::atomic<bool> answeredIsError{ false };
    QString answeredMessage;
    QByteArray replayBytes;
    std::atomic<bool> replayed{ false };

    explicit PoolRecoveryRig( const std::string &scriptBody )
        : pool( 1 )
        , script( fixture.write( "pool_recovery.sh", scriptBody ) )
    {
        QObject::connect( &fakeWorker2, &QLocalSocket::connected,
                          [&] { connected2.store( true ); } );
        QObject::connect( &fakeWorker2, &QLocalSocket::readyRead, [&] {
            replayBytes += fakeWorker2.readAll();
            if ( replayBytes.contains( '\n' ) )
                replayed.store( true );
        } );
        QObject::connect( &pool, &PythonWorkerProcessPool::workerRestarted,
                          [&]( int ) {
                              restarted.store( true );
                              // Bind the second fake worker to the restarted
                              // node's fresh IPC server: the pool gates the
                              // replay on that server gaining a client, and
                              // node->server already points at the new server
                              // when workerRestarted fires.
                              if ( node && node->server &&
                                   fakeWorker2.state() == QLocalSocket::UnconnectedState )
                              {
                                  fakeWorker2.connectToServer( node->server->serverName() );
                              }
                          } );
        QObject::connect( &pool, &PythonWorkerProcessPool::workerCrashed,
                          [&]( int, const QString &reason ) { crashReason = reason; });

        REQUIRE( pool.initialize( "/bin/sh", script ) );

        // acquireWorker() only hands out nodes whose IPC server already has a
        // client, and the fixture worker never speaks the protocol: bind a
        // fake worker to the deterministic production socket name first.
        const QString firstServer =
            QStringLiteral( "sicnu_pool_%1_1" ).arg( QCoreApplication::applicationPid() );
        fakeWorker1.connectToServer( firstServer );
        REQUIRE( waitUntil( [&] { return ( node = pool.acquireWorker() ) != nullptr; }, 10'000 ) );
        REQUIRE( node );
        REQUIRE( node->server );
    }

    void send( int retriesLeft )
    {
        node->server->sendRequest(
            QStringLiteral( "iface.run" ), {},
            [&]( const QJsonObject &result, bool isError ) {
                answeredCount.fetch_add( 1 );
                answeredIsError.store( isError );
                answeredMessage = result[QStringLiteral( "message" )].toString();
                answered.store( true );
            },
            retriesLeft );
    }

    // Answers the replayed request frame with a real correlated result frame.
    void answerReplay()
    {
        REQUIRE( waitOn( replayed, 15'000 ) );
        const int newline = replayBytes.indexOf( '\n' );
        REQUIRE( newline > 0 );
        const QJsonDocument doc = QJsonDocument::fromJson( replayBytes.left( newline ) );
        REQUIRE( doc.isObject() );
        REQUIRE( doc.object()[QStringLiteral( "method" )].toString() ==
                 QStringLiteral( "iface.run" ) );
        QJsonObject resp;
        resp[QStringLiteral( "jsonrpc" )] = QStringLiteral( "2.0" );
        resp[QStringLiteral( "id" )] = doc.object()[QStringLiteral( "id" )];
        resp[QStringLiteral( "result" )] = QJsonObject{ { QStringLiteral( "replayed" ), true } };
        fakeWorker2.write( QJsonDocument( resp ).toJson( QJsonDocument::Compact ) + '\n' );
        fakeWorker2.flush();
    }

    QString script;
    QString crashReason;
};
} // namespace

TEST_CASE( "Pool auto-heal replays an in-flight request onto the restarted worker",
           "[runtime][python][r4]" )
{
    PoolRecoveryRig rig( "#!/bin/sh\nsleep 2\nexit 42\n" );
    rig.send( /*retriesLeft=*/1 );

    REQUIRE( waitOn( rig.restarted, 15'000 ) );
    REQUIRE( waitOn( rig.connected2, 10'000 ) );
    rig.answerReplay();
    REQUIRE( waitOn( rig.answered, 10'000 ) );
    REQUIRE( rig.answeredCount.load() == 1 );
    REQUIRE( !rig.answeredIsError.load() );

    // Exactly once: after the answer, later crash cycles of the fixture
    // script must not re-fire the consumed callback.
    sleepMs( 500 );
    REQUIRE( rig.answeredCount.load() == 1 );
    CHECK( rig.crashReason.contains( QStringLiteral( "42" ) ) );
    rig.pool.shutdown();
}

TEST_CASE( "Pool replay answers recovered requests whose retry budget is exhausted",
           "[runtime][python][r4]" )
{
    PoolRecoveryRig rig( "#!/bin/sh\nsleep 2\nexit 42\n" );
    rig.send( /*retriesLeft=*/0 );

    // The restarted worker's server must gain a client for the replay gate to
    // run; with a zeroed budget the replay answers typed instead of resending.
    REQUIRE( waitOn( rig.restarted, 15'000 ) );
    REQUIRE( waitOn( rig.connected2, 10'000 ) );
    REQUIRE( waitOn( rig.answered, 10'000 ) );
    REQUIRE( rig.answeredCount.load() == 1 );
    REQUIRE( rig.answeredIsError.load() );
    CHECK( rig.answeredMessage.contains( QStringLiteral( "retries exhausted" ) ) );
    rig.pool.shutdown();
}

TEST_CASE( "Recovery watchdog answers pending calls when the restarted worker never connects",
           "[runtime][python][r4]" )
{
    // The rig binds a second fake worker inside workerRestarted; for this case
    // the restarted node must stay clientless, so drive the raw pieces here.
    ScriptFixture fixture;
    const QString script =
        fixture.write( "pool_watchdog.sh", "#!/bin/sh\nsleep 2\nexit 42\n" );

    PythonWorkerProcessPool pool( 1 );
    std::atomic<bool> restarted{ false };
    std::atomic<bool> answered{ false };
    std::atomic<bool> answeredIsError{ false };
    QString answeredMessage;
    QObject::connect( &pool, &PythonWorkerProcessPool::workerRestarted,
                      [&]( int ) { restarted.store( true ); } );
    REQUIRE( pool.initialize( "/bin/sh", script ) );

    // Put the pool's IPC server into the acquirable state, then send.
    QLocalSocket fakeWorker1;
    const QString firstServer =
        QStringLiteral( "sicnu_pool_%1_1" ).arg( QCoreApplication::applicationPid() );
    fakeWorker1.connectToServer( firstServer );
    WorkerNode *node = nullptr;
    REQUIRE( waitUntil( [&] { return ( node = pool.acquireWorker() ) != nullptr; }, 10'000 ) );
    node->server->sendRequest(
        QStringLiteral( "iface.run" ), {},
        [&]( const QJsonObject &result, bool isError ) {
            answeredIsError.store( isError );
            answeredMessage = result[QStringLiteral( "message" )].toString();
            answered.store( true );
        },
        /*retriesLeft=*/1 );

    // No client ever binds to the restarted node: the watchdog must fail the
    // pending callback instead of leaking it.
    REQUIRE( waitOn( restarted, 15'000 ) );
    REQUIRE( waitOn( answered, 15'000 ) );
    REQUIRE( answeredIsError.load() );
    CHECK( answeredMessage.contains( QStringLiteral( "restart timed out" ) ) );
    pool.shutdown();
}
