// local_worker_host.cpp — see local_worker_host.h for the contract.
#include "local_worker_host.h"

#include "runtime/worker/worker_protocol.h"
#include "worker_process_guard.h"
#include "worker_process_io.h"

#include <QProcess>
#include <QString>

#include <atomic>
#include <thread>

namespace sicnu::processing
{
namespace
{
constexpr int kProtocolVersion = 1;

// Process-unique job id: two hosts starting in the same clock tick used to
// collide on the timestamp-only id.
std::atomic<long> g_hostJobSeq{ 0 };

// Cancellation verdict for error frames lives in the shared protocol header
// (sicnu::runtime::worker::frameErrorMeansCancelled) so the one-shot host and
// the pool cannot drift; it type-checks the legacy "message" fallback there.

} // namespace

Json::Value runInLocalWorker( const QString &workerProgram,
                              const std::string &algorithmId,
                              const Json::Value &params,
                              const std::function<bool()> &isCancelled,
                              std::chrono::milliseconds timeout,
                              std::chrono::milliseconds cancelGraceMs,
                              const std::function<void( double, const std::string & )> &onProgress,
                              LocalWorkerRunReport *report )
{
    const std::string jobId = "w-" + std::to_string( ++g_hostJobSeq ) + "-"
                              + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() );

    QProcess process;
    process.setProgram( workerProgram );
    process.setArguments( { QStringLiteral( "--protocol" ),
                            QString::fromLatin1( sicnu::runtime::worker::kWorkerProtocolVersion ) } );
    process.setProcessChannelMode( QProcess::SeparateChannels ); // stderr = diagnostics
    // 8.0 WP-C: bind the one-shot worker to OS-level containment (POSIX
    // new session / process group; Windows kill-on-close Job Object armed
    // after start). The guard's destructor closes the job handle, so the
    // whole tree dies even if this host throws or the process dies.
    WorkerProcessGuard guard;
    guard.attach( process );
    WorkerDiagnosticsRing diagnostics;
    const auto startedAt = std::chrono::steady_clock::now();
    process.start( QIODevice::ReadWrite );
    if ( !process.waitForStarted( 5000 ) )
        throw std::runtime_error( "worker protocol: cannot start " + workerProgram.toStdString() );
    guard.armAfterStart( process );

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    // Handshake: first frame must be ready/v1; "caps" (7.0) is optional.
    Json::Value frame;
    bool crashed = false;
    bool softTimedOut = false;
    std::string badFrame;
    if ( !workerReadFrame( process, deadline, frame, crashed, deadline, softTimedOut, &diagnostics,
                           &badFrame )
         || frame["op"].asString() != "ready" )
    {
        if ( !badFrame.empty() )
            throw std::runtime_error( "worker protocol: malformed handshake frame: " + badFrame );
        throw std::runtime_error( crashed ? "worker crashed: no ready handshake"
                                          : "worker protocol: bad handshake" );
    }
    if ( report )
        report->handshakeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()
                                                                   - startedAt )
                .count();
    for ( const auto &cap : { sicnu::runtime::worker::kWorkerCapProgress,
                              sicnu::runtime::worker::kWorkerCapCancelAck,
                              sicnu::runtime::worker::kWorkerCapStructuredErrors,
                              sicnu::runtime::worker::kWorkerCapOutputIdentity } )
    {
        if ( report && sicnu::runtime::worker::frameHasCapability( frame, cap ) )
            report->capabilities << QString::fromLatin1( cap );
    }

    if ( !workerWriteLine( process,
                           sicnu::runtime::worker::makeRunRequest( jobId, algorithmId, params ) ) )
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
            workerWriteLine( process, sicnu::runtime::worker::makeCancelRequest( jobId ) );
        }
        if ( cancelRequested && std::chrono::steady_clock::now() >= cancelDeadline )
        {
            // Escalation ladder: the worker had its full grace window to ack
            // and exit cooperatively. The group/job-wide terminate → kill
            // ladder follows (terminate() is NOT sent at cancel-request
            // time: on POSIX it SIGTERMs and on Windows it WM_CLOSEs the
            // worker before it ever reads the cancel frame, defeating the
            // cooperative ack and diagnostics the protocol promises).
            guard.terminateTree( process );
            diagnostics.drain( process );
            if ( report )
                report->stderrTail = diagnostics.tail();
            throw std::runtime_error( "worker cancelled" );
        }
        frame = Json::Value();
        badFrame.clear();
        const auto soft = std::chrono::steady_clock::now() + std::chrono::milliseconds( 250 );
        if ( !workerReadFrame( process, deadline, frame, crashed, soft, softTimedOut, &diagnostics,
                               &badFrame ) )
        {
            if ( softTimedOut )
                continue; // re-check cancellation, keep waiting
            if ( report )
                report->stderrTail = diagnostics.tail();
            if ( !badFrame.empty() )
            {
                // A desynchronized stream means the worker can never be
                // trusted again — tear the tree down before reporting.
                guard.terminateTree( process, 0, 3000 );
                throw std::runtime_error( "worker protocol: malformed frame: " + badFrame );
            }
            if ( crashed )
                throw std::runtime_error( "worker crashed: process died without a reply" );
            if ( cancelRequested )
                throw std::runtime_error( "worker cancelled" );
            // No reply within the deadline: a hung worker must not outlive
            // this host (8.0 WP-C: group/job-wide kill, not just the leader).
            guard.terminateTree( process, 0, 3000 );
            throw std::runtime_error( "worker timeout: no reply within the deadline" );
        }
        const std::string op = frame["op"].asString();
        if ( op == "progress" )
        {
            if ( onProgress )
                onProgress( frame["value"].asDouble(), frame["message"].asString() );
            if ( report )
                ++report->progressUpdates;
            continue;
        }
        if ( op == "ack" )
        {
            // 7.0 cancel receipt: evidence the worker accepted the cancel
            // (the escalation ladder below stays the enforcement mechanism).
            if ( cancelRequested && frame["kind"].asString() == "cancel" && report )
                report->cancelAcked = true;
            continue;
        }
        if ( op == "error" && frame["jobId"].asString() == jobId )
        {
            if ( cancelRequested && sicnu::runtime::worker::frameErrorMeansCancelled( frame ) )
                throw std::runtime_error( "worker cancelled" );
            std::string message = "worker error: " + frame["message"].asString();
            if ( report )
                report->stderrTail = diagnostics.tail();
            const std::string tail = diagnostics.tail().toStdString();
            if ( !tail.empty() )
                message += " [worker stderr: " + tail + "]";
            throw std::runtime_error( message );
        }
        if ( op == "result" && frame["jobId"].asString() == jobId )
        {
            const Json::Value payload = frame["payload"];
            if ( !workerWriteLine( process, sicnu::runtime::worker::makeShutdownRequest() ) )
                process.kill();
            process.waitForFinished( 5000 );
            if ( report )
                report->stderrTail = diagnostics.tail();
            return payload;
        }
        // Unknown op (7.0 extension rule): keep waiting, bounded by the
        // deadline — never a protocol violation.
    }
}

} // namespace sicnu::processing
