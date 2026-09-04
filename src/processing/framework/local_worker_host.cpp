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
bool readFrame( QProcess &process, std::chrono::steady_clock::time_point deadline,
                Json::Value &frame, bool &workerCrashed )
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
        if ( std::chrono::steady_clock::now() >= deadline )
            return false;
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
    if ( !readFrame( process, deadline, frame, crashed ) || frame["op"].asString() != "ready" )
        throw std::runtime_error( crashed ? "worker crashed: no ready handshake"
                                          : "worker protocol: bad handshake" );

    if ( !writeLine( process, sicnu::runtime::worker::makeRunRequest( jobId, algorithmId, params ) ) )
        throw std::runtime_error( "worker protocol: cannot send run request" );

    bool cancelRequested = false;
    auto cancelDeadline = std::chrono::steady_clock::now() + cancelGraceMs;
    while ( true )
    {
        if ( isCancelled && isCancelled() && !cancelRequested )
        {
            cancelRequested = true;
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
        if ( !readFrame( process, deadline, frame, crashed ) )
        {
            if ( crashed )
                throw std::runtime_error( "worker crashed: process died without a reply" );
            throw std::runtime_error( "worker timeout: no reply within the deadline" );
        }
        const std::string op = frame["op"].asString();
        if ( op == "progress" )
            continue; // operators' progress callback bridge stays simple: ignored
        if ( op == "error" && frame["jobId"].asString() == jobId )
            throw std::runtime_error( "worker error: " + frame["message"].asString() );
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
