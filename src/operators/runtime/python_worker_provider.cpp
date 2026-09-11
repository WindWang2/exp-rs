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
class PythonWorkerSession final : public IModelRuntime
{
  public:
    PythonWorkerSession( std::string interpreter, std::string workerScript,
                         std::string workingDir, std::string artifact, std::string digest,
                         int timeoutMs, int resolvedCudaIndex = -1 )
        : m_interpreter( std::move( interpreter ) )
        , m_workerScript( std::move( workerScript ) )
        , m_workingDir( std::move( workingDir ) )
        , m_artifact( std::move( artifact ) )
        , m_digest( std::move( digest ) )
        , m_timeoutMs( timeoutMs > 0 ? timeoutMs : 30000 )
        , m_resolvedCudaIndex( resolvedCudaIndex )
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
      // card. CPU-resolved sessions leave the environment untouched.
      if ( m_resolvedCudaIndex >= 0 )
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
      // Handshake: one ready line within the timeout.
      QJsonObject ready;
      if ( !readLine( ready, m_timeoutMs ) )
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
      m_negotiated = QJsonObject();
      const QJsonObject negotiated = ready.value( QStringLiteral( "capabilities" ) ).toObject();
      if ( !negotiated.isEmpty() )
      {
        m_negotiated = negotiated;
      }
      m_loaded = true;
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
      if ( !m_loaded || !m_process )
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
        if ( !readLine( response, m_timeoutMs ) )
        {
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
      if ( !m_negotiated.isEmpty() )
      {
        const int maxRank = m_negotiated.value( QStringLiteral( "max_rank" ) ).toInt( caps.maxRank );
        if ( maxRank >= 1 && maxRank <= 8 )
          caps.maxRank = maxRank;
        const bool multiInput =
          m_negotiated.value( QStringLiteral( "multi_input" ) ).toBool( caps.multiInput );
        caps.multiInput = multiInput;
        const auto dtypeList = [ & ]( const char *key, std::vector<std::string> *out ) {
          const QJsonArray arr = m_negotiated.value( key ).toArray();
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
      health.ok = m_loaded && !m_cancelRequested.load( std::memory_order_relaxed );
      health.forwardsCompleted = m_forwards.load( std::memory_order_relaxed );
      health.failures = m_failures.load( std::memory_order_relaxed );
      std::lock_guard<std::mutex> lock( m_healthMutex );
      health.lastError = m_lastError;
      return health;
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
      const QStringList providers =
        m_negotiated.value( QStringLiteral( "providers" ) ).toVariant().toStringList();
      if ( !providers.isEmpty() )
        details.executionProvider = providers.join( QStringLiteral( "," ) ).toStdString();
      else if ( m_resolvedCudaIndex >= 0 )
        details.executionProvider = "CUDAExecutionProvider";
      details.runtimeVersion =
        m_negotiated.value( QStringLiteral( "runtime_version" ) ).toString().toStdString();
      return details;
    }

  private:
    /// Reads ONE newline-terminated JSON document. False on timeout or worker
    /// exit (the response document is then meaningless).
    bool readLine( QJsonObject &document, int timeoutMs )
    {
      QByteArray line;
      const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
      while ( !line.contains( '\n' ) )
      {
        if ( m_process->bytesAvailable() > 0
             || m_process->waitForReadyRead( 100 ) )
        {
          line += m_process->readAll();
          continue;
        }
        if ( m_process->state() != QProcess::Running )
          return false;
        if ( QDateTime::currentMSecsSinceEpoch() > deadline )
          return false;
      }
      const int newline = line.indexOf( '\n' );
      QJsonParseError parseError{};
      const QJsonDocument doc =
        QJsonDocument::fromJson( line.left( newline ), &parseError );
      if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
        return false;
      document = doc.object();
      return true;
    }

    std::string drainStderr()
    {
      if ( !m_process )
        return {};
      return QString::fromUtf8( m_process->readAllStandardError() ).toStdString();
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
      m_loaded = false;
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
    bool m_loaded = false;
    std::mutex m_inferMutex; // one request/response exchange at a time
    std::unique_ptr<QProcess> m_process;
    std::atomic<bool> m_cancelRequested{ false };
    std::atomic<std::uint64_t> m_forwards{ 0 };
    std::atomic<std::uint64_t> m_failures{ 0 };
    mutable std::mutex m_healthMutex;
    std::string m_lastError;
    QJsonObject m_negotiated; // WP-F: capabilities declared in the ready handshake
    int m_restartsLeft = 1;   // WP-F: ONE bounded restart for the session lifetime
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
    model.runtime.gpu ? model.runtime.resolvedCudaIndex : -1 );
  if ( !session->startWorker( errorMessage ) )
    return nullptr;
  return session;
}

} // namespace

void registerPythonWorkerProvider( ModelRuntimeRegistry &registry )
{
  registry.registerProvider( "python", makePythonWorkerRuntime, ProviderTraits{} );
}

} // namespace sicnu::operators::runtime
