// src/operators/runtime/python_worker_provider.cpp — Python worker provider
// (out-of-process QProcess speaking newline-delimited JSON).
#include "operators/runtime/python_worker_provider.h"

#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provider_wire.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace sicnu::operators::runtime {

namespace {

/// One worker process per session. Weights stay wherever the worker keeps
/// them; the local artifact (digest anchor) is announced to the worker.
///
/// Sharing contract (#1056): the registry hands ONE shared session to any
/// number of TaskCenter threads. inferNamed serializes the whole
/// request/response exchange on m_inferMutex (the worker protocol is
/// strictly request/response per process); capabilities/health/
/// providerDetails are concurrent snapshots — m_negotiated under
/// m_stateMutex, m_loaded/m_cancelRequested/m_forwards/m_failures atomic.
/// Lock order is m_inferMutex → m_stateMutex, never the reverse.
class PythonWorkerSession final : public IModelRuntime
{
  public:
    PythonWorkerSession( std::string interpreter, std::string workerScript,
                         std::string workingDir, std::string artifact, std::string digest,
                         int timeoutMs, int resolvedCudaIndex = -1, long maxBodyMb = 0 )
        : m_interpreter( std::move( interpreter ) )
        , m_workerScript( std::move( workerScript ) )
        , m_workingDir( std::move( workingDir ) )
        , m_artifact( std::move( artifact ) )
        , m_digest( std::move( digest ) )
        , m_timeoutMs( timeoutMs > 0 ? timeoutMs : 30000 )
        , m_resolvedCudaIndex( resolvedCudaIndex )
        , m_maxReadBytes( maxBodyMb > 0 ? static_cast<qint64>( maxBodyMb ) * 1024 * 1024
                                        : static_cast<qint64>( 256 ) * 1024 * 1024 )
    {
    }

    ~PythonWorkerSession() override { stopWorker(); }

    bool startWorker( std::string *errorMessage )
    {
      const QFileInfo scriptInfo( QString::fromStdString( m_workerScript ) );
      if ( !scriptInfo.exists() || !scriptInfo.isFile() )
      {
        if ( errorMessage )
          *errorMessage = "python worker script not found: " + m_workerScript;
        return false;
      }
      m_process = std::make_unique<QProcess>();
      if ( !m_workingDir.empty() )
        m_process->setWorkingDirectory( QString::fromStdString( m_workingDir ) );
      // Platform 9.0 (M1): the worker sees EXACTLY the device the registry's
      // resolution picked — CUDA_VISIBLE_DEVICES renumbers it to 0 inside the
      // process, so a worker that asks ORT for cuda:0 cannot land on another
      // card. CPU-resolved sessions leave the environment untouched. An
      // INHERITED mask (deployment-side device restriction) is respected and
      // never clobbered — the worker keeps the deployment's visibility.
      if ( m_resolvedCudaIndex >= 0
           && !QProcessEnvironment::systemEnvironment().contains(
                QStringLiteral( "CUDA_VISIBLE_DEVICES" ) ) )
      {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert( QStringLiteral( "CUDA_VISIBLE_DEVICES" ),
                    QString::number( m_resolvedCudaIndex ) );
        m_process->setProcessEnvironment( env );
      }
      m_process->setProgram( QString::fromStdString( m_interpreter ) );
      m_process->setArguments( { QString::fromStdString( m_workerScript ) } );
      m_process->start();
      if ( !m_process->waitForStarted( 10000 ) )
      {
        if ( errorMessage )
          *errorMessage = "failed to start python worker ('" + m_interpreter + " "
                            + m_workerScript + "'): " + m_process->errorString().toStdString();
        stopWorker();
        return false;
      }
      // Handshake: one ready line within the timeout (an oversized or
      // unparseable line fails the handshake exactly like a timeout — the
      // document is meaningless either way).
      QJsonObject ready;
      if ( readLine( ready, m_timeoutMs ) != ReadOutcome::Ok )
      {
        if ( errorMessage )
          *errorMessage = "python worker did not report ready (timed out or exited); "
                          "stderr: " + drainStderr();
        stopWorker();
        return false;
      }
      try
      {
        checkWireProtocol( ready );
      }
      catch ( const std::exception &e )
      {
        if ( errorMessage )
          *errorMessage = e.what();
        stopWorker();
        return false;
      }
      if ( ready.value( QStringLiteral( "event" ) ).toString()
           != QStringLiteral( "ready" ) )
      {
        if ( errorMessage )
          *errorMessage = "python worker handshake failed (expected event=ready)";
        stopWorker();
        return false;
      }
      // Platform 8.0 WP-F capability negotiation: the worker MAY declare its
      // surface in the ready event; declared values replace the historical
      // defaults so consumers negotiate against what THIS worker actually
      // supports (unknown fields keep defaults; lying is the worker's bug).
      // Reset first: a RESTARTED worker must not inherit the dead process's
      // declarations when it declares nothing itself.
      // #1056: capabilities()/providerDetails() snapshot m_negotiated from
      // other threads — publish/reset it under m_stateMutex so a concurrent
      // restart inside inferNamed cannot tear the JSON object.
      {
        std::lock_guard<std::mutex> stateLock( m_stateMutex );
        m_negotiated = QJsonObject();
      }
      const QJsonObject negotiated = ready.value( QStringLiteral( "capabilities" ) ).toObject();
      if ( !negotiated.isEmpty() )
      {
        std::lock_guard<std::mutex> stateLock( m_stateMutex );
        m_negotiated = negotiated;
      }
      m_loaded.store( true, std::memory_order_release );
      return true;
    }

    std::string framework() const override { return "python"; }
    std::string backendName() const override { return "python_worker"; }
    std::string deviceName() const override { return m_interpreter; }
    std::string artifactPath() const override { return m_artifact; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      // The registry hands the same shared session to any thread; the worker
      // protocol is strictly request/response per process — serialize the
      // whole exchange like the ORT provider serializes Run.
      std::lock_guard<std::mutex> lock( m_inferMutex );
      if ( !m_loaded.load( std::memory_order_acquire ) || !m_process )
        throw std::runtime_error( "runtime session is not loaded" );
      if ( m_cancelRequested.load( std::memory_order_relaxed ) )
        throw std::runtime_error( "inference canceled before the forward pass" );

      // Platform 8.0 WP-F bounded restart: a worker that DIED mid-exchange
      // (crash between two forwards) gets ONE respawn + replay for the whole
      // SESSION lifetime — never a loop. An exhausted restart budget, or a
      // restart that fails to hand-shake, surfaces as a typed provider-crash
      // diagnostic. The request document is deterministic, so replaying it
      // is safe.
      const QJsonObject request = encodeInferRequest( inputs, outputNames, m_artifact, m_digest );
      for ( int attempt = 0; attempt < 2; ++attempt )
      {
        if ( attempt > 0 )
        {
          if ( m_restartsLeft <= 0 )
          {
            // Mark the session dead-for-good: stopWorker() clears m_loaded,
            // so permanentlyUnavailable() reads true and the registry
            // recycles the cached session on the next acquire instead of
            // handing out a corpse whose every forward throws.
            stopWorker();
            recordFailure( "worker crashed and the session restart budget is exhausted" );
            throw std::runtime_error( "provider crashed: python worker crashed and the "
                                      "session restart budget is exhausted" );
          }
          --m_restartsLeft;
          stopWorker();
          std::string restartError;
          if ( !startWorker( &restartError ) )
          {
            recordFailure( "worker crashed and the restart failed: " + restartError );
            throw std::runtime_error( "provider crashed: python worker crashed and the "
                                      "restart failed: " + restartError );
          }
          recordFailure( "worker crashed mid-run; restarted and replayed the forward" );
        }
        const QByteArray line = QJsonDocument( request ).toJson( QJsonDocument::Compact ) + '\n';
        m_process->write( line );
        if ( !m_process->waitForBytesWritten( m_timeoutMs ) )
        {
          // Only a DEAD worker is restartable — a live-but-stuck worker is a
          // hang, and replaying into it could double-execute the forward.
          if ( attempt == 0 && m_process->state() != QProcess::Running )
            continue; // dead worker → the bounded restart above
          recordFailure( "python worker stopped accepting requests (broken pipe)" );
          throw std::runtime_error( "python worker stopped accepting requests (broken pipe)" );
        }

        QJsonObject response;
        const ReadOutcome outcome = readLine( response, m_timeoutMs );
        if ( outcome != ReadOutcome::Ok )
        {
          if ( outcome == ReadOutcome::Oversized )
          {
            // The worker is alive but its answer blew the same max_body_mb
            // guard the HTTP transport enforces — fail the forward with the
            // shared output-invalid classification (no truncation into the
            // parser, no replay: the request would produce the same
            // oversized answer). The worker is stopped because the exchange
            // is left mid-stream; an abandoned tail must never be served to
            // the next request.
            recordFailure( "python worker response exceeds the runtime.provider.max_body_mb "
                           "guard (output invalid); stderr: " + drainStderr() );
            stopWorker();
            throw std::runtime_error( "python worker response exceeds the "
                                      "runtime.provider.max_body_mb guard (output invalid)" );
          }
          if ( attempt == 0 && m_process->state() != QProcess::Running )
            continue; // worker EXITED (crash) → restart + replay
          recordFailure( "python worker exited unexpectedly before responding; stderr: "
                         + drainStderr() );
          throw std::runtime_error( "python worker exited unexpectedly before responding" );
        }
        if ( response.contains( QStringLiteral( "error" ) ) )
        {
          const std::string error =
            response.value( QStringLiteral( "error" ) ).toString().toStdString();
          recordFailure( error );
          throw std::runtime_error( error );
        }
        try
        {
          auto outputs = decodeInferOutputs( response.value( QStringLiteral( "outputs" ) ).toArray() );
          m_forwards.fetch_add( 1, std::memory_order_relaxed );
          return outputs;
        }
        catch ( const std::exception &e )
        {
          recordFailure( e.what() );
          throw;
        }
      }
      throw std::runtime_error( "provider crashed: python worker exchange failed (unreachable)" );
    }

    bool supportsMultiInput() const override { return true; }

    /// Historical cv::Mat fast path (bridges through the named surface).
    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      auto outs =
        inferNamed( { NamedTensor{ std::string(), TensorBlob::fromMat( nchwBlob ) } }, {} );
      if ( outs.empty() || outs.front().second.rank() != 4 )
        throw std::runtime_error( "worker output is not rank-4 (the cv::Mat path carries "
                                  "rank-4; use inferNamed)" );
      return outs.front().second.toMat();
    }

    ProviderCapabilities capabilities() const override
    {
      ProviderCapabilities caps;
      caps.multiInput = true;
      caps.namedBind = true;
      caps.maxRank = 6;
      caps.batch = true;
      caps.cancelInForward = false;
      caps.inputDtypes = { "float32", "float64", "int32", "int64", "uint8", "int8" };
      caps.outputDtypes = { "float32", "float64", "int32", "int64", "uint8", "int8" };
      // Platform 8.0 WP-F: worker-declared capabilities override the defaults
      // (handshake negotiation; unknown/absent fields keep the defaults).
      // #1056: work on a COPY taken under m_stateMutex — this method runs on
      // foreign threads concurrently with a restart that rewrites the
      // negotiated block.
      QJsonObject negotiated;
      {
        std::lock_guard<std::mutex> stateLock( m_stateMutex );
        negotiated = m_negotiated;
      }
      if ( !negotiated.isEmpty() )
      {
        const int maxRank = negotiated.value( QStringLiteral( "max_rank" ) ).toInt( caps.maxRank );
        if ( maxRank >= 1 && maxRank <= 8 )
          caps.maxRank = maxRank;
        const bool multiInput =
          negotiated.value( QStringLiteral( "multi_input" ) ).toBool( caps.multiInput );
        caps.multiInput = multiInput;
        const auto dtypeList = [ & ]( const char *key, std::vector<std::string> *out ) {
          const QJsonArray arr = negotiated.value( key ).toArray();
          if ( !arr.isEmpty() )
          {
            out->clear();
            for ( const auto &v : arr )
              out->push_back( v.toString().toStdString() );
          }
        };
        dtypeList( "input_dtypes", &caps.inputDtypes );
        dtypeList( "output_dtypes", &caps.outputDtypes );
      }
      return caps;
    }

    void requestCancel() override { m_cancelRequested.store( true, std::memory_order_relaxed ); }
    void clearCancel() override { m_cancelRequested.store( false, std::memory_order_relaxed ); }

    SessionHealth health() const override
    {
      SessionHealth health;
      health.ok = m_loaded.load( std::memory_order_acquire )
                    && !m_cancelRequested.load( std::memory_order_relaxed );
      health.forwardsCompleted = m_forwards.load( std::memory_order_relaxed );
      health.failures = m_failures.load( std::memory_order_relaxed );
      std::lock_guard<std::mutex> lock( m_healthMutex );
      health.lastError = m_lastError;
      return health;
    }

    // Track 13 crash recovery: a session whose worker died with the restart
    // budget exhausted (or whose restart failed to hand-shake) is finished.
    // m_loaded flips false on stopWorker(); m_restartsLeft is atomic so the
    // registry can probe without holding m_inferMutex.
    bool permanentlyUnavailable() const override
    {
      return !m_loaded.load( std::memory_order_acquire )
             && m_restartsLeft.load( std::memory_order_acquire ) <= 0;
    }

    SessionMemoryEstimate memoryEstimate() const override
    {
      SessionMemoryEstimate estimate;
      const QFileInfo info( QString::fromStdString( m_artifact ) );
      if ( info.exists() && info.isFile() )
        estimate.weightsMb = static_cast<int>( ( info.size() + ( 1 << 20 ) - 1 ) >> 20 );
      return estimate;
    }

    ProviderRuntimeDetails providerDetails() const override
    {
      // Platform 9.0 (M1): the worker may declare its execution providers and
      // runtime version in the handshake capabilities block; when it does not,
      // the resolved device still tells the truth about what was requested.
      ProviderRuntimeDetails details;
      // Honesty contract (model_runtime.h): report only what the WORKER
      // declared in its handshake — a resolved CUDA index is a request, not
      // evidence of the EP the worker actually engaged.
      // #1056: negotiated snapshot under m_stateMutex (see capabilities()).
      QJsonObject negotiated;
      {
        std::lock_guard<std::mutex> stateLock( m_stateMutex );
        negotiated = m_negotiated;
      }
      const QStringList providers =
        negotiated.value( QStringLiteral( "providers" ) ).toVariant().toStringList();
      if ( !providers.isEmpty() )
        details.executionProvider = providers.join( QStringLiteral( "," ) ).toStdString();
      details.runtimeVersion =
        negotiated.value( QStringLiteral( "runtime_version" ) ).toString().toStdString();
      return details;
    }

  private:
    /// Outcome of reading one wire document.
    enum class ReadOutcome
    {
      Ok,
      Failed,   // timeout or worker exit; the document is meaningless
      Oversized // response exceeded the max_body_mb read guard
    };

    /// Reads ONE newline-terminated JSON document. Failed on timeout or
    /// worker exit; Oversized once the accumulated bytes pass the same
    /// runtime.provider.max_body_mb guard the HTTP transport enforces —
    /// a worker streaming an unbounded stdout must hit a typed failure,
    /// never silently truncate (or OOM the host while the timeout runs).
    ReadOutcome readLine( QJsonObject &document, int timeoutMs )
    {
      QByteArray line;
      const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
      while ( !line.contains( '\n' ) )
      {
        // Platform 10.0 security audit: drain stderr on the same cadence and
        // keep only a bounded tail — a worker spamming stderr must not grow
        // QProcess-internal buffers without bound while a stdout exchange
        // waits (the stdout side is already bounded by max_body_mb).
        drainStderr();
        if ( m_process->bytesAvailable() > 0
             || m_process->waitForReadyRead( 100 ) )
        {
          line += m_process->readAll();
          if ( static_cast<qint64>( line.size() ) > m_maxReadBytes )
            return ReadOutcome::Oversized;
          continue;
        }
        if ( m_process->state() != QProcess::Running )
          return ReadOutcome::Failed;
        if ( QDateTime::currentMSecsSinceEpoch() > deadline )
          return ReadOutcome::Failed;
      }
      const int newline = line.indexOf( '\n' );
      QJsonParseError parseError{};
      const QJsonDocument doc =
        QJsonDocument::fromJson( line.left( newline ), &parseError );
      if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
        return ReadOutcome::Failed;
      document = doc.object();
      return ReadOutcome::Ok;
    }

    /// Bounded stderr drain: QProcess-internal stderr buffers are drained on
    /// every poll and only the LAST m_maxStderrBytes bytes are kept (with an
    /// explicit truncation marker) — a chatty worker surfaces its tail in
    /// diagnostics without ever being able to balloon host memory.
    std::string drainStderr()
    {
      if ( !m_process )
        return {};
      const QByteArray fresh = m_process->readAllStandardError();
      if ( !fresh.isEmpty() )
      {
        m_stderrTail.append( fresh );
        const qint64 cap = m_maxStderrBytes;
        if ( m_stderrTail.size() > cap )
        {
          m_stderrTail.remove( 0, m_stderrTail.size() - cap );
          m_stderrTruncated = true;
        }
      }
      std::string out;
      if ( m_stderrTruncated )
        out = "...[stderr truncated]...";
      out += QString::fromUtf8( m_stderrTail ).toStdString();
      return out;
    }

    void stopWorker()
    {
      if ( !m_process )
        return;
      if ( m_process->state() == QProcess::Running )
      {
        m_process->kill();
        m_process->waitForFinished( 5000 );
      }
      m_process.reset();
      m_loaded.store( false, std::memory_order_release );
    }

    void recordFailure( const std::string &what )
    {
      m_failures.fetch_add( 1, std::memory_order_relaxed );
      std::lock_guard<std::mutex> lock( m_healthMutex );
      m_lastError = what;
    }

    std::string m_interpreter;
    std::string m_workerScript;
    std::string m_workingDir;
    std::string m_artifact;
    std::string m_digest;
    int m_timeoutMs = 30000;
    int m_resolvedCudaIndex = -1; // Platform 9.0: -1 = cpu / no device pinning
    /// Wire-read guard (same bound and manifest knob as the HTTP transport's
    /// response guard — runtime.provider.max_body_mb, 256 MiB default).
    qint64 m_maxReadBytes = 256 * 1024 * 1024;
    /// Platform 10.0: bounded stderr tail (see drainStderr).
    qint64 m_maxStderrBytes = 64 * 1024;
    QByteArray m_stderrTail;
    bool m_stderrTruncated = false;
    // #1056: read by health() on foreign threads while a restart inside
    // inferNamed flips it — atomic, acquire/release against the handshake.
    std::atomic<bool> m_loaded{ false };
    std::mutex m_inferMutex;   // one request/response exchange at a time
    mutable std::mutex m_stateMutex; // guards m_negotiated (snapshot reads)
    std::unique_ptr<QProcess> m_process;
    std::atomic<bool> m_cancelRequested{ false };
    std::atomic<std::uint64_t> m_forwards{ 0 };
    std::atomic<std::uint64_t> m_failures{ 0 };
    mutable std::mutex m_healthMutex;
    std::string m_lastError;
    QJsonObject m_negotiated; // WP-F: capabilities declared in the ready handshake
                              // (guarded by m_stateMutex)
    // WP-F: ONE bounded restart for the session lifetime. Atomic so the
    // registry's permanentlyUnavailable() probe can read it on a foreign
    // thread (all mutations happen under m_inferMutex; relaxed is enough —
    // the flag is advisory, the next forward re-checks under the lock).
    std::atomic<int> m_restartsLeft{ 1 };
};

ModelRuntimePtr makePythonWorkerRuntime( const ModelInfo &model,
                                         const ModelHardwareCapabilities &,
                                         std::string *errorMessage )
{
  const std::string interpreter =
    model.runtime.provider.interpreter.empty() ? "python3" : model.runtime.provider.interpreter;
  // The worker script resolves against the manifest directory (the same
  // relative-path rule as artifact paths).
  QString script = QString::fromStdString( model.runtime.provider.workerScript );
  if ( !model.sourceManifest.empty() && QFileInfo( script ).isRelative() )
    script = QFileInfo( QString::fromStdString( model.sourceManifest ) ).absoluteDir()
               .filePath( script );
  auto session = std::make_shared<PythonWorkerSession>(
    interpreter, script.toStdString(),
    QFileInfo( script ).absolutePath().toStdString(), model.resolvedArtifactPath,
    model.contentDigest, model.runtime.provider.timeoutMs,
    model.runtime.gpu ? model.runtime.resolvedCudaIndex : -1,
    model.runtime.provider.maxBodyMb );
  if ( !session->startWorker( errorMessage ) )
    return nullptr;
  return session;
}

} // namespace

void registerPythonWorkerProvider( ModelRuntimeRegistry &registry )
{
  // Platform 9.0 (M2): the worker runs its own runtime (e.g. onnxruntime)
  // inside the subprocess and can address any CUDA device through the
  // CUDA_VISIBLE_DEVICES pin — so its traits are a DIRECT-CUDA provider and
  // the real-driver (NVML) gate promotes GPU resolution for it, exactly like
  // the onnxruntime provider.
  registry.registerProvider( "python", makePythonWorkerRuntime,
                             ProviderTraits{ /*maxAddressableCudaIndex*/ 63 } );
}

} // namespace sicnu::operators::runtime
