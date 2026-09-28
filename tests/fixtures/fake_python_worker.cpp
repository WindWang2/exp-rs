// tests/fixtures/fake_python_worker.cpp — a REAL worker process for the
// python-channel recovery oracles. Speaks the same wire protocol as
// worker_daemon.py (newline-framed JSON-RPC over a QLocalSocket unix socket)
// so PythonWorkerProcessPool exercises its true restart path — real process
// death, real reconnect, real replay — with zero Python dependency.
//
// Launch contract (identical to a real worker): argv[1] is an ignored script
// path, then `--socket <name>`.
//
// Environment knobs (all optional, read once at startup):
//   SICNU_FAKE_WORKER_PIDFILE — own pid written on startup (lets the test
//                               SIGKILL exactly this process);
//   SICNU_FAKE_WORKER_LOG     — append-only activity log ("connected",
//                               "got:<method>") the test polls for
//                               deterministic synchronization;
//   SICNU_FAKE_WORKER_MODE    — serve (default): answer requests;
//                               never_connect: stay alive without ever
//                               connecting (restart-watchdog oracle);
//                               die_on_start: exit(42) immediately, before
//                               connecting (pre-connect death streak — the
//                               budget-retirement oracle).
//
// Request methods:
//   test.job               -> result {"job_id": <echo>, "pid": <worker pid>}
//                             params.hold=true holds the answer until a
//                             test.flush arrives (the test then knows the
//                             request is still UNANSWERED at kill/shrink/
//                             shutdown time — no answer race);
//   test.flush             -> answers every held job, then {"ok": true};
//   test.die_before_answer -> answers NOTHING; exits with params.exit_code
//                             (negative code = SIGKILL self-kill)
//   test.answer_then_exit  -> result {"ok": true}, then exits params.code —
//                             deterministic clean-exit-with-inflight-work
//   test.answer_then_abort -> result {"ok": true}, then socket abort while
//                             the PROCESS stays alive — the answer and the
//                             EOF land in the same event batch on the
//                             console side (buffered-answer race contract);
//   test.abort_socket      -> closes the socket, keeps the PROCESS alive —
//                             deterministic socket-EOF-with-live-worker
//   test.hang              -> answers nothing (drain handshake exit target)
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include <unistd.h>

namespace
{
QFile *g_log = nullptr;

void logLine( const char *what )
{
  if ( !g_log )
    return;
  g_log->write( what );
  g_log->write( "\n" );
  g_log->flush();
}

void appendResult( QLocalSocket *socket, int id, const QJsonObject &result )
{
  QJsonObject response;
  response[QStringLiteral( "jsonrpc" )] = QStringLiteral( "2.0" );
  response[QStringLiteral( "id" )] = id;
  response[QStringLiteral( "result" )] = result;
  socket->write( QJsonDocument( response ).toJson( QJsonDocument::Compact ) + '\n' );
  socket->flush();
}

/// Jobs held by params.hold=true until test.flush (see file comment).
std::vector<std::pair<int, QJsonObject>> g_heldJobs;

/// Set by test.answer_then_exit: the next answered request completes the
/// deterministic drain handshake — the exit fires only after the console
/// side demonstrably received an answer written EARLIER on the same ordered
/// stream (no fixed-delay guesswork).
int g_pendingExitCode = -1;

void handleLine( QLocalSocket *socket, const QByteArray &line )
{
  const QJsonDocument doc = QJsonDocument::fromJson( line );
  if ( !doc.isObject() )
    return;
  const QJsonObject request = doc.object();
  const QString method = request.value( QStringLiteral( "method" ) ).toString();
  const int id = request.value( QStringLiteral( "id" ) ).toInt();
  const QJsonObject params = request.value( QStringLiteral( "params" ) ).toObject();
  logLine( ( "got:" + method ).toUtf8().constData() );

  if ( method == QLatin1String( "test.job" ) )
  {
    if ( params.value( QStringLiteral( "hold" ) ).toBool() )
    {
      g_heldJobs.emplace_back( id, params );
      logLine( "held" );
      return;
    }
    QJsonObject result;
    result[QStringLiteral( "job_id" )] = params.value( QStringLiteral( "job_id" ) );
    result[QStringLiteral( "pid" )] = static_cast<qint64>( ::getpid() );
    appendResult( socket, id, result );
    return;
  }
  if ( method == QLatin1String( "test.flush" ) )
  {
    for ( const auto &held : g_heldJobs )
    {
      QJsonObject result;
      result[QStringLiteral( "job_id" )] = held.second.value( QStringLiteral( "job_id" ) );
      result[QStringLiteral( "pid" )] = static_cast<qint64>( ::getpid() );
      appendResult( socket, held.first, result );
    }
    g_heldJobs.clear();
    logLine( "flushed" );
    appendResult( socket, id, QJsonObject{ { QStringLiteral( "ok" ), true } } );
    return;
  }
  if ( method == QLatin1String( "test.die_before_answer" ) )
  {
    // Answer nothing: the request and everything sent before it stay
    // in-flight on the server while this process dies.
    const int code = params.value( QStringLiteral( "exit_code" ) ).toInt( 42 );
    logLine( "dying" );
    if ( code < 0 )
      ::kill( ::getpid(), SIGKILL );
    else
      ::exit( code );
    return;
  }
  if ( method == QLatin1String( "test.answer_then_exit" ) )
  {
    appendResult( socket, id, QJsonObject{ { QStringLiteral( "ok" ), true } } );
    logLine( "answering-then-exiting" );
    // The exit itself waits for the drain handshake: the test answers back
    // with test.hang once it saw the control answer — when the hang's own
    // answer is written, the earlier control bytes were long since delivered
    // (ordered stream), so the console side observed "answered, then exited".
    g_pendingExitCode = params.value( QStringLiteral( "code" ) ).toInt( 0 );
    return;
  }
  if ( method == QLatin1String( "test.hang" ) )
  {
    if ( g_pendingExitCode >= 0 )
    {
      const int code = std::exchange( g_pendingExitCode, -1 );
      appendResult( socket, id, QJsonObject{ { QStringLiteral( "ok" ), true } } );
      logLine( "drained-exiting" );
      QTimer::singleShot( 0, socket, [code] { ::exit( code ); } );
      return;
    }
    return; // unanswered, socket stays open
  }
  if ( method == QLatin1String( "test.answer_then_abort" ) )
  {
    appendResult( socket, id, QJsonObject{ { QStringLiteral( "ok" ), true } } );
    logLine( "answering-then-aborting" );
    socket->abort(); // EOF immediately after the answer: same-batch race
    return;
  }
  if ( method == QLatin1String( "test.abort_socket" ) )
  {
    logLine( "aborting-socket" );
    socket->abort(); // socket EOF; the process itself stays alive
    return;
  }
}
} // namespace

int main( int argc, char *argv[] )
{
  QCoreApplication app( argc, argv );

  // Optional startup stderr marker: lets the crash-reason oracle assert that
  // the captured stderr tail flows through the pool's loss classification.
  if ( const QString stderrMarker = qEnvironmentVariable( "SICNU_FAKE_WORKER_STDERR" ); !stderrMarker.isEmpty() )
  {
    std::fputs( stderrMarker.toUtf8().constData(), stderr );
    std::fflush( stderr );
  }

  const QString pidFile = qEnvironmentVariable( "SICNU_FAKE_WORKER_PIDFILE" );
  const QString logPath = qEnvironmentVariable( "SICNU_FAKE_WORKER_LOG" );
  if ( !logPath.isEmpty() )
  {
    g_log = new QFile( logPath );
    g_log->open( QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text );
  }

  if ( qEnvironmentVariable( "SICNU_FAKE_WORKER_MODE" ) == QLatin1String( "never_connect" ) )
  {
    // Alive but never attaches: the pool's restart watchdog must answer the
    // recovered requests after its deadline.
    if ( !pidFile.isEmpty() )
    {
      QFile f( pidFile );
      f.open( QIODevice::WriteOnly | QIODevice::Truncate );
      f.write( QByteArray::number( static_cast<qint64>( ::getpid() ) ) );
    }
    logLine( "started-never-connect" );
    QTimer::singleShot( 120000, &app, &QCoreApplication::quit );
    return QCoreApplication::exec();
  }
  if ( qEnvironmentVariable( "SICNU_FAKE_WORKER_MODE" ) == QLatin1String( "die_on_start" ) )
  {
    // Spawn succeeds, then the worker dies BEFORE ever connecting: every
    // replacement worker's server stays client-less, so the recovered
    // requests can only be answered by the retirement path.
    logLine( "started-die-on-start" );
    QTimer::singleShot( 0, &app, [] { ::exit( 42 ); } );
    return QCoreApplication::exec();
  }

  QString socketName;
  for ( int i = 1; i < argc; ++i )
  {
    if ( QByteArray( argv[i] ) == "--socket" && i + 1 < argc )
    {
      socketName = QString::fromUtf8( argv[i + 1] );
      ++i;
    }
  }
  if ( socketName.isEmpty() )
    return 2;

  auto *socket = new QLocalSocket( &app );
  QObject::connect( socket, &QLocalSocket::connected, [socket] {
    logLine( "connected" );
  } );
  QObject::connect( socket, &QLocalSocket::readyRead, [socket] {
    while ( socket->canReadLine() )
    {
      const QByteArray line = socket->readLine().trimmed();
      if ( !line.isEmpty() )
        handleLine( socket, line );
    }
  } );

  if ( !pidFile.isEmpty() )
  {
    QFile f( pidFile );
    f.open( QIODevice::WriteOnly | QIODevice::Truncate );
    f.write( QByteArray::number( static_cast<qint64>( ::getpid() ) ) );
  }
  logLine( "started" );
  socket->connectToServer( socketName );
  // Bounded lifetime: a fixture worker that outlives its test cannot wedge
  // the suite (ctest reaps the pool, but defense in depth is free here).
  QTimer::singleShot( 120000, &app, &QCoreApplication::quit );
  return QCoreApplication::exec();
}
