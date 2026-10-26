// tests/test_r22_agent_ipc.cpp — R22-04 (#1692) regression.
//
// RED first: PythonIpcServer::onReadyRead discarded any line above
// kMaxLineBytes (1 MiB) WITHOUT firing the pending request's callback and
// without dropInFlight() — a normal (non-malicious) large algorithm result
// (histograms, per-pixel stats, point lists) left the caller hanging until
// its full timeout (300 s on the execute_algorithm path), with an error
// that never mentioned the real cause; above 4 MiB the whole worker was
// even treated as crashed and the algorithm REPLAYED. The oversized line
// must now fail its pending request immediately with a typed error, and
// the parser must keep serving subsequent well-formed lines.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "python/isolated/python_ipc_server.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using sicnu::python::isolated::AwaitStatus;
using sicnu::python::isolated::PythonIpcServer;

namespace
{
template <typename T>
bool waitOn( std::atomic<T> &flag, int timeoutMs = 5'000 )
{
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot( true );
    QObject::connect( &deadline, &QTimer::timeout, &loop, &QEventLoop::quit );
    deadline.start( timeoutMs );
    while ( flag.load() == T( 0 ) && deadline.isActive() )
    {
        loop.processEvents( QEventLoop::AllEvents, 20 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return flag.load() != T( 0 );
}
} // namespace

int main( int argc, char *argv[] )
{
    QCoreApplication app( argc, argv );
    QCoreApplication::setApplicationName( QLatin1String( "test-r22-agent-ipc" ) );
    return Catch::Session().run( argc, argv );
}

TEST_CASE( "oversized IPC responses fail their pending request immediately (#1692)",
           "[python][ipc][r22]" )
{
    PythonIpcServer server;
    const QString socketName =
        QStringLiteral( "r22-agent-ipc-%1" ).arg( QCoreApplication::applicationPid() );
    REQUIRE( server.listen( socketName ) );

    std::atomic<bool> connected{ false };
    QObject::connect( &server, &PythonIpcServer::clientConnected,
                      [&] { connected.store( true ); } );
    QLocalSocket fakeWorker;
    fakeWorker.connectToServer( socketName );
    REQUIRE( waitOn( connected ) );
    REQUIRE( server.hasClient() );

    // One pending request whose answer will be oversized. (A second, small
    // request is sent only AFTER the in-flight check below —
    // takeInFlightRequests() retires EVERY pending request, including
    // unrelated ones, because it hands crash-recovery ownership to the
    // caller.)
    std::atomic<int> bigAnswered{ 0 };
    QString bigMessage;
    bool bigIsError = false;

    const int bigId = server.sendRequest(
        QStringLiteral( "processing.execute_algorithm" ), QJsonObject(),
        [&]( const QJsonObject &response, bool isError ) {
            bigIsError = isError;
            bigMessage = response.value( QStringLiteral( "message" ) ).toString();
            bigAnswered.fetch_add( 1 );
        } );
    REQUIRE( bigId >= 0 );

    // 1.5 MiB single-line response with "id" as the second JSON key — the
    // exact shape worker_daemon.py serializes.
    QByteArray big = QStringLiteral( R"({"jsonrpc":"2.0","id":%1,"result":{"blob":")" )
                         .arg( bigId )
                         .toUtf8();
    big += QByteArray( 1536 * 1024, 'A' );
    big += QByteArrayLiteral( "\"}}\n" );
    REQUIRE( big.size() > 1024 * 1024 );
    fakeWorker.write( big );
    fakeWorker.flush();

    // The typed error must arrive promptly — the RED behavior is a silent
    // drop and a caller-side 300 s hang, so a 5 s budget proves the fix
    // without stalling the suite.
    REQUIRE( waitOn( bigAnswered ) );
    REQUIRE( bigAnswered.load() == 1 );
    REQUIRE( bigIsError );
    REQUIRE( bigMessage.contains( QStringLiteral( "exceeds" ) ) );

    // The salvaged request is no longer in-flight: a later crash replay
    // must not resurrect it.
    const auto pending = server.takeInFlightRequests();
    const bool bigStillPending =
        std::any_of( pending.begin(), pending.end(),
                     [&]( const auto &p ) { return p.id == bigId; } );
    REQUIRE_FALSE( bigStillPending );

    // The parser survived: a request sent after the oversized-line salvage
    // still gets its well-formed short response delivered.
    std::atomic<int> smallAnswered{ 0 };
    const int smallId = server.sendRequest(
        QStringLiteral( "iface.ping" ), QJsonObject(),
        [&]( const QJsonObject &, bool ) { smallAnswered.fetch_add( 1 ); } );
    REQUIRE( smallId >= 0 );
    const QByteArray small = QStringLiteral(
                                 R"({"jsonrpc":"2.0","id":%1,"result":{"status":"ok"}})" )
                                 .arg( smallId )
                                 .toUtf8();
    fakeWorker.write( small + '\n' );
    fakeWorker.flush();
    REQUIRE( waitOn( smallAnswered ) );
    REQUIRE( smallAnswered.load() == 1 );
}

TEST_CASE( "oversized IPC line with unknown id is dropped silently (#1692)",
           "[python][ipc][r22]" )
{
    PythonIpcServer server;
    const QString socketName = QStringLiteral( "r22-agent-ipc-unknown-%1" )
                                   .arg( QCoreApplication::applicationPid() );
    REQUIRE( server.listen( socketName ) );

    std::atomic<bool> connected{ false };
    QObject::connect( &server, &PythonIpcServer::clientConnected,
                      [&] { connected.store( true ); } );
    QLocalSocket fakeWorker;
    fakeWorker.connectToServer( socketName );
    REQUIRE( waitOn( connected ) );

    // A pending request plus an oversized line whose salvaged id belongs to
    // nobody: nothing may fire for the junk line, but the real request must
    // still complete afterwards.
    std::atomic<int> answered{ 0 };
    const int id = server.sendRequest(
        QStringLiteral( "iface.ping" ), QJsonObject(),
        [&]( const QJsonObject &, bool ) { answered.fetch_add( 1 ); } );

    QByteArray junk = QByteArrayLiteral( R"({"jsonrpc":"2.0","id":9999,"result":[)" );
    junk += QByteArray( 1200 * 1024, 'B' );
    junk += QByteArrayLiteral( "]}\n" );
    fakeWorker.write( junk );
    fakeWorker.flush();

    QEventLoop cool;
    QTimer::singleShot( 150, &cool, &QEventLoop::quit );
    cool.exec();
    REQUIRE( answered.load() == 0 ); // junk line fired nothing

    const QByteArray ok = QStringLiteral(
                              R"({"jsonrpc":"2.0","id":%1,"result":{"status":"ok"}})" )
                              .arg( id )
                              .toUtf8();
    fakeWorker.write( ok + '\n' );
    fakeWorker.flush();
    REQUIRE( waitOn( answered ) );
}
