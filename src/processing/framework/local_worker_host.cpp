// local_worker_host.cpp — see local_worker_host.h for the contract.
#include "local_worker_host.h"

#include "runtime/worker/worker_protocol.h"

#include <QProcess>
#include <QString>

#include <thread>

namespace sicnu::processing
{
namespace
{
constexpr int kProtocolVersion = 1;

bool writeLine( QProcess &process, const std::string &line )
{
    const QByteArray bytes = QByteArray::fromStdString( line + "\n" );
    process.write( bytes );
    return process.waitForBytesWritten( 5000 );
}

/// Reads one protocol frame with a deadline. Returns false on EOF/crash
/// (@p workerCrashed set) or timeout.
/// As readFrame, but gives up waiting after @p softDeadlineMs without
/// treating it as an error (@p softTimedOut set) so the caller can poll
/// cancellation even while the worker emits no frames.
bool readFrame( QProcess &process, std::chrono::steady_clock::time_point deadline,
                Json::Value &frame, bool &workerCrashed,
                std::chrono::steady_clock::time_point softDeadline,
                bool &softTimedOut )
{
    workerCrashed = false;
    while ( true )
    {
        while ( process.canReadLine() )
        {
            const QByteArray raw = process.readLine();
            const std::string line = QString::fromUtf8( raw ).trimmed().toStdString();
            if ( line.empty() )
                continue;
            if ( !sicnu::runtime::worker::parseFrame( line, frame ) )
                return false; // malformed or version mismatch — hard refusal
            return true;
        }
        if ( process.state() != QProcess::Running && !process.canReadLine() )
        {
            workerCrashed = true;
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        if ( now >= deadline )
            return false;
        if ( now >= softDeadline )
        {
            softTimedOut = true;
            return false;
        }
        if ( !process.waitForReadyRead( 100 ) && process.state() != QProcess::Running )
        {
            workerCrashed = true;
            return false;
        }
    }
}
} // namespace

Json::Value runInLocalWorker( const QString &workerProgram,
                              const std::string &algorithmId,
                              const Json::Value &params,
                              const std::function<bool()> &isCancelled,
                              std::chrono::milliseconds timeout,
                              std::chrono::milliseconds cancelGraceMs )
{
    const std::string jobId =
        "w-" + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() );

    QProcess process;
    process.setProgram( workerProgram );
    process.setArguments( { QStringLiteral( "--protocol" ),
                            QString::fromLatin1( sicnu::runtime::worker::kWorkerProtocolVersion ) } );
    process.setProcessChannelMode( QProcess::SeparateChannels ); // stderr = diagnostics
    process.start( QIODevice::ReadWrite );
    if ( !process.waitForStarted( 5000 ) )
        throw std::runtime_error( "worker protocol: cannot start " + workerProgram.toStdString() );

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    // Handshake: first frame must be ready/v1.
    Json::Value frame;
    bool crashed = false;
    bool softTimedOut = false;
    if ( !readFrame( process, deadline, frame, crashed, deadline, softTimedOut )
         || frame["op"].asString() != "ready" )
        throw std::runtime_error( crashed ? "worker crashed: no ready handshake"
                                          : "worker protocol: bad handshake" );

    if ( !writeLine( process, sicnu::runtime::worker::makeRunRequest( jobId, algorithmId, params ) ) )
        throw std::runtime_error( "worker protocol: cannot send run request" );

    bool cancelRequested = false;
    std::chrono::steady_clock::time_point cancelDeadline{};
    while ( true )
    {
        if ( isCancelled && isCancelled() && !cancelRequested )
        {
            cancelRequested = true;
            // The grace period starts when the cancel is REQUESTED, not when
            // the job started (any job longer than the grace must still get
            // its grace).
            cancelDeadline = std::chrono::steady_clock::now() + cancelGraceMs;
            writeLine( process, sicnu::runtime::worker::makeCancelRequest( jobId ) );
            process.terminate();
        }
        if ( cancelRequested && std::chrono::steady_clock::now() >= cancelDeadline )
        {
            process.kill();
            process.waitForFinished( 3000 );
            throw std::runtime_error( "worker cancelled" );
        }
        frame = Json::Value();
        const auto soft = std::chrono::steady_clock::now() + std::chrono::milliseconds( 250 );
        if ( !readFrame( process, deadline, frame, crashed, soft, softTimedOut ) )
        {
            if ( softTimedOut )
                continue; // re-check cancellation, keep waiting
            if ( crashed )
                throw std::runtime_error( "worker crashed: process died without a reply" );
            if ( cancelRequested )
                throw std::runtime_error( "worker cancelled" );
            throw std::runtime_error( "worker timeout: no reply within the deadline" );
        }
        const std::string op = frame["op"].asString();
        if ( op == "progress" )
            continue; // operators' progress callback bridge stays simple: ignored
        if ( op == "error" && frame["jobId"].asString() == jobId )
        {
            if ( cancelRequested && frame["message"].asString() == "cancelled" )
                throw std::runtime_error( "worker cancelled" );
            throw std::runtime_error( "worker error: " + frame["message"].asString() );
        }
        if ( op == "result" && frame["jobId"].asString() == jobId )
        {
            const Json::Value payload = frame["payload"];
            if ( !writeLine( process, sicnu::runtime::worker::makeShutdownRequest() ) )
                process.kill();
            process.waitForFinished( 5000 );
            return payload;
        }
    }
}

} // namespace sicnu::processing
