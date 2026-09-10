// local_worker_host.h — Local worker process host (Data Plane 3.0, Phase K).
//
// Isolation boundary: an operator run executed through a LocalWorkerHost
// executor happens in a separate `sicnu_worker` process speaking
// worker_protocol v1 over stdin/stdout. Consequences (the reason this exists):
//   - worker crash  != app crash   (SIGKILL/segfault → typed task failure)
//   - Python crash  != main app crash (same boundary)
//   - GPU OOM inside the worker != whole app crash
//
// One process per invocation (simplest safe isolation; pooling is a future
// opt-in). The executor is a TaskCenter JobExecutor, so any surface that can
// submit a custom executor (submitJob, fused head, worker-host registration)
// can route a job into a worker. Cancel cooperates while the worker lives and
// escalates to kill after a grace period.
//
// 7.0 extensions (all backward compatible, wire stays v1): capability
// report, worker progress forwarding, cancel-ack evidence, and a bounded
// stderr diagnostics tail attached to crash/timeout errors.
#pragma once

#include <json/json.h>

#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>
#include <memory>

namespace sicnu::processing
{

/// Optional per-run diagnostics produced by runInLocalWorker (7.0). Pass a
/// report pointer to observe the worker handshake/capabilities/diagnostics;
/// the run contract is unchanged when it is omitted.
struct LocalWorkerRunReport
{
    /// Capability tokens advertised by the worker's ready frame (may be
    /// empty for a legacy worker).
    QStringList capabilities;
    /// Wall time from process start to a verified ready handshake (cold
    /// start diagnostics).
    qint64 handshakeMs = 0;
    /// True when the worker acknowledged the cancel request ("cancelAck"
    /// capability); false when cancellation was inferred by escalation.
    bool cancelAcked = false;
    /// Number of progress frames received from the worker.
    qint64 progressUpdates = 0;
    /// Bounded single-line tail of the worker's stderr — the crash/hang
    /// diagnostics channel that was previously discarded.
    QString stderrTail;
};

/// Runs @p algorithmId in a fresh `sicnu_worker` process and returns its
/// result payload. Throws std::runtime_error with a typed prefix when the
/// worker fails:
///   "worker protocol: ..."  — handshake/version failures
///   "worker error: ..."     — the operator failed inside the worker
///   "worker crashed: ..."   — process died without a reply (SIGKILL, segfault)
///   "worker timeout: ..."   — no reply within @p timeout
///   "worker cancelled"      — cancel request honored (worker exited cleanly)
/// @p isCancelled is polled; when it flips, a cancel request is sent and the
/// process is killed after @p cancelGraceMs.
/// @p onProgress (optional) receives the worker's progress frames.
Json::Value runInLocalWorker( const QString &workerProgram,
                              const std::string &algorithmId,
                              const Json::Value &params,
                              const std::function<bool()> &isCancelled = {},
                              std::chrono::milliseconds timeout =
                                  std::chrono::minutes( 30 ),
                              std::chrono::milliseconds cancelGraceMs =
                                  std::chrono::milliseconds( 3000 ),
                              const std::function<void( double, const std::string & )> &onProgress = {},
                              LocalWorkerRunReport *report = nullptr );

} // namespace sicnu::processing
