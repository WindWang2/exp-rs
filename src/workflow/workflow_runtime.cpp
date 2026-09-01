#include "workflow_runtime.h"

#include "builtin_definitions.h"
#include "workflow_gate.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <optional>
#include <future>
#include <memory>

#include <chrono>
#include <sstream>
#include <stdexcept>

#include "agent/output_verifier.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/json_params_converter.h"
#include "processing/framework/output_committer.h"
#include "processing/framework/task_center.h"

namespace sicnu::workflow {

namespace {

// Thrown by runStepViaExecutionPlane once a task has been dispatched to the
// plane. runStep must never fall back to the synchronous operator path for
// these: the operator already ran (or is being cancelled), so re-running it
// would duplicate side effects on the same outputs.
class PlaneTaskFailure : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

std::string jsonValueToArtifactString( const Json::Value &v )
{
  if ( v.isString() )
    return v.asString();
  if ( v.isNull() )
    return {};
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString( builder, v );
}

std::string joinHints( const std::vector<std::string> &hints )
{
  std::ostringstream oss;
  for ( size_t i = 0; i < hints.size(); ++i )
  {
    if ( i > 0 )
      oss << "; ";
    oss << hints[i];
  }
  return oss.str();
}

QString assetKindLabelForPath( const QString &path, const std::string &verificationPolicy )
{
  if ( !verificationPolicy.empty() )
  {
    QString vp = QString::fromStdString( verificationPolicy ).toLower();
    if ( vp == QStringLiteral( "vector" ) || vp == QStringLiteral( "ogr" ) )
      return QStringLiteral( "vector" );
    if ( vp == QStringLiteral( "raster" ) || vp == QStringLiteral( "gdal" ) )
      return QStringLiteral( "raster" );
    if ( vp == QStringLiteral( "skip" ) || vp == QStringLiteral( "none" ) )
      return QStringLiteral( "skip" );
  }
  const QString suffix = QFileInfo( path ).suffix().toLower();
  if ( suffix == QStringLiteral( "shp" ) || suffix == QStringLiteral( "geojson" )
       || suffix == QStringLiteral( "gpkg" ) || suffix == QStringLiteral( "kml" )
       || suffix == QStringLiteral( "csv" ) || suffix == QStringLiteral( "tsv" )
       || suffix == QStringLiteral( "json" ) || suffix == QStringLiteral( "xml" ) )
    return QStringLiteral( "vector" );
  return QStringLiteral( "raster" );
}

sicnu::data::AssetKind assetKindEnum( const QString &label )
{
  return label == QStringLiteral( "vector" ) ? sicnu::data::AssetKind::Vector
                                             : sicnu::data::AssetKind::Raster;
}

} // namespace

WorkflowRuntime::WorkflowRuntime( bool loadBuiltins )
{
  if ( loadBuiltins )
  {
    registerBuiltinWorkflows( *this );
  }
}

void WorkflowRuntime::registerDefinition( WorkflowDefinition def )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const std::string id = def.id;
  m_defs.insert_or_assign( id, std::make_shared<WorkflowDefinition>( std::move( def ) ) );
}

bool WorkflowRuntime::hasDefinition( const std::string &id ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_defs.find( id ) != m_defs.end();
}

const WorkflowDefinition *WorkflowRuntime::findDefinition( const std::string &id ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_defs.find( id );
  if ( it == m_defs.end() || !it->second )
    return nullptr;
  return it->second.get();
}

std::shared_ptr<const WorkflowDefinition> WorkflowRuntime::findDefinitionShared( const std::string &id ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_defs.find( id );
  if ( it == m_defs.end() )
    return nullptr;
  return it->second;
}

std::vector<std::string> WorkflowRuntime::registeredDefinitionIds() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  std::vector<std::string> out;
  out.reserve( m_defs.size() );
  for ( const auto &kv : m_defs )
    out.push_back( kv.first );
  return out;
}

std::string WorkflowRuntime::open( const std::string &definitionId )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_defs.find( definitionId );
  if ( it == m_defs.end() || !it->second )
    return {};

  const std::string sessionId = "wf-" + std::to_string( m_nextId++ );
  m_sessions[sessionId] = std::make_shared<WorkflowSession>( *it->second, sessionId );
  m_cancelFlags[sessionId] = std::make_shared<std::atomic<bool>>( false );
  return sessionId;
}

SessionSnapshot WorkflowRuntime::state( const std::string &sessionId ) const
{
  auto s = session( sessionId );
  if ( !s )
    throw std::runtime_error( "Session not found: " + sessionId );
  return s->snapshot();
}

bool WorkflowRuntime::gotoStep( const std::string &sessionId, const std::string &stepId )
{
  auto s = session( sessionId );
  if ( !s )
    return false;
  return s->gotoStep( stepId );
}

void WorkflowRuntime::setParams( const std::string &sessionId, const std::string &stepId, const Json::Value &params )
{
  auto s = session( sessionId );
  if ( !s )
    throw std::runtime_error( "Session not found: " + sessionId );
  s->setParams( stepId, params );
}

CanRunResult WorkflowRuntime::canRun( const std::string &sessionId, const std::string &stepId ) const
{
  CanRunResult result;
  auto s = session( sessionId );
  if ( !s )
  {
    result.ok = false;
    result.hints.push_back( "Session not found: " + sessionId );
    return result;
  }

  const StepDef *step = s->stepById( stepId );
  if ( !step )
  {
    result.ok = false;
    result.hints.push_back( "Step not found: " + stepId );
    return result;
  }

  return evaluateGates( *s, step->gates );
}

Json::Value WorkflowRuntime::runStep( const std::string &sessionId, const std::string &stepId )
{
  // Capture session and cancel flag with shared_ptr to keep them alive even if
  // close() erases the map entry concurrently (concurrent keep-alive).
  auto sessionPtr = session( sessionId );
  if ( !sessionPtr )
    throw std::runtime_error( "Session not found: " + sessionId );

  const StepDef *step = sessionPtr->stepById( stepId );
  if ( !step )
    throw std::runtime_error( "Step not found: " + stepId );

  const CanRunResult can = canRun( sessionId, stepId );
  if ( !can.ok )
  {
    const std::string msg = can.hints.empty()
                              ? std::string( "Step cannot run: gates failed" )
                              : joinHints( can.hints );
    throw std::runtime_error( msg );
  }

  if ( step->kind != StepKind::Operator )
  {
    throw std::runtime_error( "Step is not an Operator step: " + stepId );
  }

  if ( step->operatorId.empty() )
  {
    throw std::runtime_error( "Step has empty operatorId: " + stepId );
  }

  const Json::Value params = sessionPtr->resolveParams( stepId );
  auto cancelFlagPtr = cancelFlag( sessionId );

  // Default path: async via ExecutionPlane/TaskCenter with transactional commit
  // and OutputVerifier. Keep the original synchronous RSOperator path as a
  // fallback only when the plane was unavailable BEFORE any task was
  // dispatched; once a task ran, PlaneTaskFailure propagates (re-running the
  // operator would duplicate side effects).
  bool useExecutionPlane;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    useExecutionPlane = m_useExecutionPlane;
  }
  if ( useExecutionPlane )
  {
    try
    {
      return runStepViaExecutionPlane( sessionId, stepId, step, params, sessionPtr, cancelFlagPtr );
    }
    catch ( const PlaneTaskFailure & )
    {
      // A dispatched task failed, was cancelled, timed out, or failed
      // commit/verification — never silently re-run it synchronously.
      throw;
    }
    catch ( const std::exception &e )
    {
      const std::string msg = e.what();
      // Fallback only for pre-dispatch plane infrastructure failures
      // (submission rejected / invalid handle). Everything after submission
      // is typed as PlaneTaskFailure above.
      if ( msg.find( "ExecutionPlane submit failed" ) != std::string::npos )
      {
        // fall through to sync
      }
      else
      {
        throw;
      }
    }
  }

  return runStepSync( sessionId, stepId, step, params, sessionPtr, cancelFlagPtr );
}

Json::Value WorkflowRuntime::runStepSync( const std::string &sessionId, const std::string &stepId,
                                          const StepDef *step, const Json::Value &params,
                                          std::shared_ptr<WorkflowSession> sessionPtr,
                                          std::shared_ptr<std::atomic<bool>> cancelFlagPtr )
{
  auto op = sicnu::operators::RSOperatorRegistry::instance().create( step->operatorId );
  if ( !op )
  {
    throw std::runtime_error( "Operator not found: " + step->operatorId );
  }

  sicnu::operators::RSOperatorContext context;
  // Wire the session's cooperative cancellation flag into the operator context
  // so requestCancel() aborts a long-running operator step mid-run. The
  // shared_ptr local keeps the flag alive for the whole step even if close()
  // erases the session's map entry concurrently (no use-after-free).
  if ( cancelFlagPtr )
  {
    context.setCancelFlag( cancelFlagPtr.get() );
  }
  Json::Value result;
  try
  {
    result = op->execute( params, context );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    throw std::runtime_error( e.message() );
  }

  // Artifact side-effects from operator result — capture sessionPtr to keep
  // the session alive even if close() raced.
  if ( result.isMember( "output" ) && result["output"].isString() )
  {
    const std::string name = step->artifactOnSuccess.empty() ? "output" : step->artifactOnSuccess;
    sessionPtr->setArtifact( name, result["output"].asString() );
  }
  else if ( result.isMember( "result" ) )
  {
    const std::string name =
      ( step->artifactOnSuccess.empty() || step->artifactOnSuccess == "output" )
        ? "result"
        : step->artifactOnSuccess;
    sessionPtr->setArtifact( name, jsonValueToArtifactString( result["result"] ) );
  }
  else if ( !step->artifactOnSuccess.empty() )
  {
    sessionPtr->setArtifact( step->artifactOnSuccess, jsonValueToArtifactString( result ) );
  }

  sessionPtr->markStepComplete( stepId );
  return result;
}

Json::Value WorkflowRuntime::runStepViaExecutionPlane( const std::string &sessionId, const std::string &stepId,
                                                       const StepDef *step, const Json::Value &params,
                                                       std::shared_ptr<WorkflowSession> sessionPtr,
                                                       std::shared_ptr<std::atomic<bool>> cancelFlagPtr )
{
  // Derive resource estimate: explicit step field wins, otherwise use
  // ExecutionPlane::estimateFromPreflight which accounts for tiled inference
  // (rs:infer tile/halo/band geometry) via Operator::estimateExecution.
  unsigned int estimateMb = step->resourceEstimateMb;
  if ( estimateMb == 0 )
  {
    try
    {
      estimateMb = sicnu::processing::ExecutionPlane::estimateFromPreflight( step->operatorId, params );
    }
    catch ( ... )
    {
      estimateMb = 0;
    }
  }

  sicnu::processing::ExecutionRequest request;
  request.algorithmId = QString::fromStdString( step->operatorId );
  request.params = sicnu::processing::jsonParamsToVariantMap( params );
  request.resourceEstimateMb = estimateMb;
  request.source = QStringLiteral( "workflow" );
  request.correlationId = QString::fromStdString( sessionId + ":" + stepId );
  request.autoLoad = false;
  request.autoDispatch = true;

  // Submit via ExecutionPlane (TaskCenter admission, resource-aware).
  sicnu::processing::ExecutionHandle handle;
  try
  {
    handle = sicnu::processing::ExecutionPlane::instance().submit( request );
  }
  catch ( const std::exception &e )
  {
    throw std::runtime_error( std::string( "ExecutionPlane submit failed: " ) + e.what() );
  }

  if ( !handle.valid() || handle.taskId() <= 0 )
  {
    throw std::runtime_error( "ExecutionPlane submit failed: invalid handle" );
  }

  const long taskId = handle.taskId();
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    m_activeTaskIds[sessionId] = taskId;
  }

  // Ensure the active task id is cleared on exit (success, failure, or
  // cancellation) so a subsequent runStep in the same session starts fresh.
  struct ClearGuard
  {
    WorkflowRuntime *rt;
    std::string sid;
    ~ClearGuard()
    {
      std::lock_guard<std::mutex> lock( rt->m_mutex );
      rt->m_activeTaskIds.erase( sid );
    }
  } clearGuard{ this, sessionId };

  // Observe cooperative cancellation: if the session flag is set before or
  // during await, cancel the plane handle (TaskCenter -> JobEngine).
  // The shared_ptr keep-alive ensures the flag remains valid.
  auto cancelCheckAndWait = [&]() -> bool {
    // If already cancelled before wait, propagate immediately.
    if ( cancelFlagPtr && cancelFlagPtr->load( std::memory_order_acquire ) )
    {
      handle.cancel();
    }
    // Event-loop-free wait for terminal state; respects TaskCenter shutdown.
    const bool terminal = handle.await( std::chrono::minutes( 30 ) );
    if ( !terminal )
    {
      // Timeout — treat as failure; the task may still be running but the
      // caller cannot wait forever. Request cancel best-effort.
      handle.cancel();
      throw PlaneTaskFailure( "ExecutionPlane await timed out for task " + std::to_string( taskId ) );
    }
    return true;
  };

  // Also spawn a lightweight poller that watches the session flag and
  // cancels the handle promptly (sub-second) while awaiting, without
  // blocking the await itself. This keeps cancellation latency low.
  std::atomic<bool> done{ false };
  std::thread cancelWatcher;
  if ( cancelFlagPtr )
  {
    cancelWatcher = std::thread( [&, cancelFlagPtr, handle, &done]() mutable {
      while ( !done.load( std::memory_order_acquire ) )
      {
        if ( cancelFlagPtr->load( std::memory_order_acquire ) )
        {
          handle.cancel();
          break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
      }
    } );
  }

  bool awaitOk = false;
  std::string awaitError;
  try
  {
    awaitOk = cancelCheckAndWait();
  }
  catch ( const std::exception &e )
  {
    awaitError = e.what();
  }
  done.store( true, std::memory_order_release );
  if ( cancelWatcher.joinable() )
    cancelWatcher.join();

  if ( !awaitError.empty() )
    throw PlaneTaskFailure( awaitError );
  if ( !awaitOk )
    throw PlaneTaskFailure( "ExecutionPlane await failed" );

  // Fetch terminal task info (thread-safe).
  const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo( taskId );

  if ( info.taskId != taskId )
    throw PlaneTaskFailure( "ExecutionPlane Unknown task id: " + std::to_string( taskId ) );

  if ( info.status == sicnu::TaskStatus::Canceled
       || info.status == sicnu::TaskStatus::Cancelling )
  {
    throw PlaneTaskFailure( "Operator cancelled: " + step->operatorId );
  }
  if ( info.status == sicnu::TaskStatus::Failed )
  {
    std::string msg = info.errorMessage.toStdString();
    if ( msg.empty() )
      msg = "Operator failed: " + step->operatorId;
    throw PlaneTaskFailure( msg );
  }
  if ( info.status != sicnu::TaskStatus::Completed )
  {
    throw PlaneTaskFailure( "Task did not complete successfully, status=" + std::to_string( static_cast<int>( info.status ) ) );
  }

  Json::Value result = info.resultPayload;
  // Fallback: some adapters only fill outputLayerPath, not resultPayload["output"]
  if ( ( result.isNull() || !result.isMember( "output" ) ) && !info.outputLayerPath.isEmpty() )
  {
    if ( result.isNull() || !result.isObject() )
      result = Json::Value( Json::objectValue );
    result["output"] = info.outputLayerPath.toStdString();
  }

  // Snapshot the manager under the lock; setDataManager may race from
  // another thread (setUseExecutionPlane/setDataManager hold m_mutex).
  sicnu::data::DataManager *dataManager;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    dataManager = m_dataManager;
  }

  // Transactional commit + verification for file outputs.
  std::string committedPath;
  std::string committedAssetId;
  std::string outputTempPath;
  if ( result.isMember( "output" ) && result["output"].isString() )
    outputTempPath = result["output"].asString();
  else if ( !info.outputLayerPath.isEmpty() )
    outputTempPath = info.outputLayerPath.toStdString();

  const bool hasFileOutput = !outputTempPath.empty() && QFile::exists( QString::fromStdString( outputTempPath ) );

  if ( hasFileOutput )
  {
    const QString qTemp = QString::fromStdString( outputTempPath );
    const QString kindLabel = assetKindLabelForPath( qTemp, step->verificationPolicy );
    const bool skipVerify = kindLabel == QStringLiteral( "skip" );

    if ( dataManager )
    {
      // Transactional commit via OutputCommitter (temp -> stable asset)
      const QFileInfo tempInfo( qTemp );
      const QString suffix = tempInfo.suffix().isEmpty() ? QStringLiteral( "tif" ) : tempInfo.suffix();
      const QString stablePath = tempInfo.absolutePath() + QDir::separator()
                                 + tempInfo.completeBaseName() + QStringLiteral( "_committed." ) + suffix;

      sicnu::AlgorithmOutputRequest commitReq;
      commitReq.kind = assetKindEnum( kindLabel == QStringLiteral( "skip" ) ? QStringLiteral( "raster" ) : kindLabel );
      commitReq.tempPath = qTemp;
      commitReq.stablePath = stablePath;
      commitReq.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
      commitReq.autoLoad = false;
      commitReq.derivation.algorithmId = info.algorithmId;
      commitReq.derivation.parameters = QJsonObject::fromVariantMap( info.parameterMap );
      commitReq.derivation.taskReference = QString::number( taskId );
      commitReq.derivation.completedAtUtc = QDateTime::currentDateTimeUtc();

      // DataManager mutations must run on its own (affinity) thread: it has
      // no internal locking, and this workflow caller can be any worker
      // thread (#702). Marshal the commit like ExecutionPlane does; on a
      // starved affinity thread fail closed instead of committing from the
      // wrong thread. Same-thread callers (GUI) commit directly.
      std::optional<sicnu::CommitResult> commitResult;
      bool commitRan = false;
      if ( QCoreApplication::instance() && dataManager->thread() != QThread::currentThread() )
      {
        auto promise = std::make_shared<std::promise<sicnu::CommitResult>>();
        auto cancelled = std::make_shared<std::atomic<bool>>( false );
        auto future = promise->get_future();
        QMetaObject::invokeMethod(
          dataManager,
          [dataManager, commitReq, promise, cancelled]() {
            if ( cancelled->load() )
              return; // the caller already failed closed on its timeout
            sicnu::OutputCommitter committer( dataManager );
            promise->set_value( committer.commit( commitReq ) );
          },
          Qt::QueuedConnection );
        if ( future.wait_for( std::chrono::milliseconds( 10000 ) ) == std::future_status::ready )
        {
          commitResult = future.get();
          commitRan = true;
        }
        else
        {
          // Review P2: mark the late delivery dead so a starved affinity
          // thread cannot commit an asset AFTER this step already failed.
          cancelled->store( true );
        }
      }
      else
      {
        sicnu::OutputCommitter committer( dataManager );
        commitResult = committer.commit( commitReq );
        commitRan = true;
      }
      if ( !commitRan )
        throw PlaneTaskFailure( std::string( "Output commit timed out on the DataManager thread (" )
                                + outputTempPath + ")" );
      if ( !commitResult.has_value() || !*commitResult )
      {
        const QString diag = commitResult->diagnostics().isEmpty()
                               ? QStringLiteral( "OutputCommitter commit failed" )
                               : commitResult->diagnostics().first().message;
        throw PlaneTaskFailure( std::string( "Output commit failed: " ) + diag.toStdString() + " (" + outputTempPath + ")" );
      }
      committedPath = stablePath.toStdString();
      committedAssetId = commitResult->value().toString().toStdString();
      // Patch result to point at the stable asset
      result["output"] = committedPath;
      result["assetId"] = committedAssetId;
    }
    else
    {
      // No DataManager: use temp path directly, no stable promotion. Write
      // the provenance sidecar anyway (#698) — the README's "every derived
      // raster records source, operator, parameters, and time" held
      // nowhere on this path. Best-effort: a failed sidecar write logs and
      // continues (the output itself already exists).
      committedPath = outputTempPath;
      sicnu::data::DerivationRecord sidecar = sicnu::data::makeTaskDerivation(
        info.algorithmId, QJsonObject::fromVariantMap( info.parameterMap ),
        QString::number( taskId ) );
      const QString sidecarPath = QString::fromStdString( outputTempPath )
                                    + QStringLiteral( ".provenance.json" );
      QSaveFile sidecarFile( sidecarPath );
      if ( sidecarFile.open( QIODevice::WriteOnly ) )
      {
        const QJsonDocument doc( sidecar.toJson() );
        sidecarFile.write( doc.toJson( QJsonDocument::Compact ) );
        if ( !sidecarFile.commit() )
          qWarning() << "[workflow] provenance sidecar write failed:" << sidecarPath;
      }
      else
      {
        qWarning() << "[workflow] provenance sidecar open failed:" << sidecarPath;
      }
    }

    // Closed-loop verification via OutputVerifier
    if ( !skipVerify )
    {
      sicnu::agent::OutputVerifier verifier;
      const QString verifyPath = QString::fromStdString( committedPath.empty() ? outputTempPath : committedPath );
      const QString hint = kindLabel == QStringLiteral( "skip" ) ? QString() : kindLabel;
      const sicnu::agent::OutputVerification report = verifier.verify( verifyPath, hint );
      if ( !report.ok )
      {
        // Verification failed -> rollback committed asset so no invalid
        // layer remains in the catalog (closed-loop insulator).
        if ( !committedAssetId.empty() )
          rollbackCommittedAsset( committedAssetId );
        else if ( dataManager && !committedPath.empty() && QFile::exists( QString::fromStdString( committedPath ) ) )
          QFile::remove( QString::fromStdString( committedPath ) );

        std::string issues;
        for ( const auto &iss : report.issues )
          issues += iss.toStdString() + "; ";
        if ( issues.empty() )
          issues = "Output verification failed for " + committedPath;
        throw PlaneTaskFailure( std::string( "Output verification failed: " ) + issues );
      }
    }
  }

  // Artifact side-effects — use sessionPtr keep-alive, not a fresh lookup.
  if ( result.isMember( "output" ) && result["output"].isString() )
  {
    const std::string outStr = result["output"].asString();
    const std::string name = step->artifactOnSuccess.empty() ? "output" : step->artifactOnSuccess;
    // On successful commit, prefer the stable asset path / assetId.
    if ( !committedAssetId.empty() )
      sessionPtr->setArtifact( name, committedAssetId );
    else if ( !committedPath.empty() )
      sessionPtr->setArtifact( name, committedPath );
    else
      sessionPtr->setArtifact( name, outStr );

    // Also keep a path artifact for consumers that expect a filesystem path
    // even when we promoted to an assetId (e.g. downstream placeholder $step.output).
    if ( !committedAssetId.empty() && !committedPath.empty() )
      sessionPtr->setArtifact( name + "_path", committedPath );
  }
  else if ( result.isMember( "result" ) )
  {
    const std::string name =
      ( step->artifactOnSuccess.empty() || step->artifactOnSuccess == "output" )
        ? "result"
        : step->artifactOnSuccess;
    sessionPtr->setArtifact( name, jsonValueToArtifactString( result["result"] ) );
  }
  else if ( !step->artifactOnSuccess.empty() )
  {
    sessionPtr->setArtifact( step->artifactOnSuccess, jsonValueToArtifactString( result ) );
  }

  sessionPtr->markStepComplete( stepId );
  return result;
}

bool WorkflowRuntime::rollbackCommittedAsset( const std::string &assetIdStr )
{
  if ( !m_dataManager || assetIdStr.empty() )
    return false;
  const auto idOpt = sicnu::data::AssetId::fromString( QString::fromStdString( assetIdStr ) );
  if ( !idOpt || idOpt->isNull() )
    return false;
  if ( !m_dataManager->asset( *idOpt ).has_value() )
    return false;
  const auto reapRes = m_dataManager->reap( sicnu::data::ReapRequest{ *idOpt } );
  if ( !reapRes.unloaded )
  {
    const auto plan = m_dataManager->planUnload( *idOpt ).confirmedCascade();
    (void)m_dataManager->unload( plan );
  }
  return reapRes.unloaded;
}

void WorkflowRuntime::markStepComplete( const std::string &sessionId, const std::string &stepId )
{
  auto s = session( sessionId );
  if ( !s )
    return;
  s->markStepComplete( stepId );
}

void WorkflowRuntime::setArtifact( const std::string &sessionId, const std::string &name, const std::string &value )
{
  auto s = session( sessionId );
  if ( !s )
    return;
  s->setArtifact( name, value );
}

void WorkflowRuntime::requestCancel( const std::string &sessionId )
{
  auto flag = cancelFlag( sessionId );
  if ( flag )
    flag->store( true, std::memory_order_release );

  long taskId = -1;
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    auto it = m_activeTaskIds.find( sessionId );
    if ( it != m_activeTaskIds.end() )
      taskId = it->second;
  }
  if ( taskId > 0 )
    sicnu::TaskCenter::instance().cancelTask( taskId );
}

void WorkflowRuntime::close( const std::string &sessionId )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_sessions.erase( sessionId );
  m_cancelFlags.erase( sessionId );
  m_activeTaskIds.erase( sessionId );
}

void WorkflowRuntime::setDataManager( sicnu::data::DataManager *dataManager )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_dataManager = dataManager;
}

sicnu::data::DataManager *WorkflowRuntime::dataManager() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_dataManager;
}

void WorkflowRuntime::setUseExecutionPlane( bool use )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_useExecutionPlane = use;
}

bool WorkflowRuntime::useExecutionPlane() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_useExecutionPlane;
}

std::shared_ptr<std::atomic<bool>> WorkflowRuntime::cancelFlag( const std::string &sessionId )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_cancelFlags.find( sessionId );
  if ( it == m_cancelFlags.end() )
    return nullptr;
  return it->second;
}

std::shared_ptr<WorkflowSession> WorkflowRuntime::session( const std::string &sessionId ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_sessions.find( sessionId );
  if ( it == m_sessions.end() )
    return nullptr;
  return it->second;
}

} // namespace sicnu::workflow
