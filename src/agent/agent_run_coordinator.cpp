#include "agent_run_coordinator.h"

#include "data/data_manager.h"
#include "processing/framework/algorithm_preflight.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/output_committer.h"
#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"

#include <QFileInfo>
#include <QPointer>
#include <QUuid>

#include <QJsonDocument>
#include <QJsonObject>

namespace sicnu::agent
{

namespace
{

constexpr int kMaxRepairAttempts = 2;

QString kindHintFromPath( const QString &path )
{
  const QString suffix = QFileInfo( path ).suffix().toLower();
  if ( suffix == QStringLiteral( "shp" ) || suffix == QStringLiteral( "geojson" )
       || suffix == QStringLiteral( "gpkg" ) || suffix == QStringLiteral( "kml" )
       || suffix == QStringLiteral( "csv" ) || suffix == QStringLiteral( "tsv" )
       || suffix == QStringLiteral( "json" ) || suffix == QStringLiteral( "xml" ) )
  {
    return QStringLiteral( "vector" );
  }
  return QStringLiteral( "raster" );
}

sicnu::data::AssetKind assetKindForSuffix( const QString &suffix )
{
  const QString s = suffix.toLower();
  if ( s == QStringLiteral( "shp" ) || s == QStringLiteral( "geojson" )
       || s == QStringLiteral( "gpkg" ) || s == QStringLiteral( "kml" )
       || s == QStringLiteral( "csv" ) || s == QStringLiteral( "tsv" )
       || s == QStringLiteral( "json" ) || s == QStringLiteral( "xml" ) )
  {
    return sicnu::data::AssetKind::Vector;
  }
  return sicnu::data::AssetKind::Raster;
}

} // namespace

QString agentRunStageName( AgentRunStage stage )
{
  switch ( stage )
  {
    case AgentRunStage::Understanding: return QStringLiteral( "understanding" );
    case AgentRunStage::Planning: return QStringLiteral( "planning" );
    case AgentRunStage::Preflight: return QStringLiteral( "preflight" );
    case AgentRunStage::Queued: return QStringLiteral( "queued" );
    case AgentRunStage::Running: return QStringLiteral( "running" );
    case AgentRunStage::Verifying: return QStringLiteral( "verifying" );
    case AgentRunStage::Repairing: return QStringLiteral( "repairing" );
    case AgentRunStage::Presenting: return QStringLiteral( "presenting" );
    case AgentRunStage::Completed: return QStringLiteral( "completed" );
    case AgentRunStage::Failed: return QStringLiteral( "failed" );
    case AgentRunStage::Canceled: return QStringLiteral( "canceled" );
  }
  return QStringLiteral( "unknown" );
}

AgentRunCoordinator::AgentRunCoordinator( sicnu::data::DataManager *dataManager, QObject *parent )
  : QObject( parent )
  , m_dataManager( dataManager )
{
  qRegisterMetaType<sicnu::agent::AgentRun>();
  qRegisterMetaType<sicnu::agent::AgentRunStage>();
  installDefaultFunctions();
}

void AgentRunCoordinator::setPreflightFunction( PreflightFunction function )
{
  m_preflightFunction = std::move( function );
}

void AgentRunCoordinator::setExecuteFunction( ExecuteFunction function )
{
  m_executeFunction = std::move( function );
}

void AgentRunCoordinator::setVerifyFunction( VerifyFunction function )
{
  m_verifyFunction = std::move( function );
}

void AgentRunCoordinator::setRepairFunction( RepairFunction function )
{
  m_repairFunction = std::move( function );
}

void AgentRunCoordinator::setPresentFunction( PresentFunction function )
{
  m_presentFunction = std::move( function );
}

void AgentRunCoordinator::installDefaultFunctions()
{
  m_preflightFunction = [this]( const QString &algorithmId, const QVariantMap &params ) -> Json::Value {
    return sicnu::processing::preflightAlgorithm( algorithmId.toStdString(),
                                                  sicnu::TaskCenter::variantMapToJsonParams( params ) );
  };

  m_executeFunction = [this]( const QString &algorithmId, const QVariantMap &params,
                              long &outTaskId, std::function<void()> &outCancel ) -> Json::Value {
    if ( !m_dataManager )
    {
      outTaskId = -1;
      Json::Value err( Json::objectValue );
      err["status"] = "error";
      err["errorMessage"] = "AgentRunCoordinator has no DataManager";
      return err;
    }

    sicnu::processing::ExecutionRequest request;
    request.algorithmId = algorithmId;
    request.params = params;
    request.source = QStringLiteral( "agent" );
    request.autoLoad = false;
    request.resourceEstimateMb = sicnu::processing::ExecutionPlane::estimateFromPreflight(
      algorithmId.toStdString(), sicnu::TaskCenter::variantMapToJsonParams( params ) );

    const sicnu::processing::ExecutionHandle handle =
      sicnu::processing::ExecutionPlane::instance().submit( request );
    outTaskId = handle.taskId();
    outCancel = [handle]() mutable { handle.cancel(); };

    QPointer<sicnu::data::DataManager> managerGuard( m_dataManager );
    const sicnu::processing::ExecutionPlane::OutputCommitterHandler committerHandler =
      [managerGuard]( const sicnu::AlgorithmTaskInfo &info, std::string &outCommittedPath,
                      std::string &outCommitError, std::string &outAssetId ) -> bool {
        if ( !managerGuard )
        {
          outCommitError = "DataManager was destroyed before output commit";
          return false;
        }
        const QFileInfo outInfo( info.outputLayerPath );
        const QString suffix = outInfo.suffix().isEmpty() ? QStringLiteral( "tif" ) : outInfo.suffix();
        const QString stablePath = outInfo.absolutePath() + QStringLiteral( "/" )
                                   + outInfo.completeBaseName() + QStringLiteral( "_committed." ) + suffix;

        sicnu::AlgorithmOutputRequest commitRequest;
        commitRequest.kind = assetKindForSuffix( suffix );
        commitRequest.tempPath = info.outputLayerPath;
        commitRequest.stablePath = stablePath;
        commitRequest.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
        commitRequest.autoLoad = false;
        commitRequest.derivation.algorithmId = info.algorithmId;
        commitRequest.derivation.parameters = QJsonObject::fromVariantMap( info.parameterMap );
        commitRequest.derivation.taskReference = QString::number( info.taskId );
        commitRequest.derivation.completedAtUtc = QDateTime::currentDateTimeUtc();

        sicnu::OutputCommitter committer( managerGuard );
        const auto result = committer.commit( commitRequest );
        if ( result )
        {
          outCommittedPath = stablePath.toStdString();
          outAssetId = result.value().toString().toStdString();
          return true;
        }
        outCommitError = result.diagnostics().isEmpty()
                           ? "OutputCommitter refused the tool-call output."
                           : result.diagnostics().first().message.toStdString();
        return false;
      };

    const sicnu::processing::ExecutionPlane::OutputVerificationHandler verificationHandler =
      [this]( const QString &committedPath, const QString &kindHint ) -> Json::Value {
        const OutputVerification report = m_verifyFunction( committedPath, kindHint );
        Json::Value v( Json::objectValue );
        v["ok"] = report.ok;
        v["kind"] = report.kind.toStdString();
        v["summary"] = report.summary;
        Json::Value issues( Json::arrayValue );
        for ( const QString &issue : report.issues )
          issues.append( issue.toStdString() );
        v["issues"] = issues;
        Json::Value warnings( Json::arrayValue );
        for ( const QString &warning : report.warnings )
          warnings.append( warning.toStdString() );
        v["warnings"] = warnings;
        return v;
      };

    return sicnu::processing::ExecutionPlane::instance().awaitResult(
      handle.taskId(), std::chrono::minutes( 30 ), committerHandler, nullptr,
      /*cancelOnTimeout=*/true, verificationHandler );
  };

  m_verifyFunction = []( const QString &committedPath, const QString &kindHint ) -> OutputVerification {
    return OutputVerifier().verify( committedPath, kindHint );
  };

  m_repairFunction = []( const AgentRun &, const Json::Value & ) -> std::optional<QVariantMap> {
    // No generic repair strategy by default; callers inject domain knowledge.
    return std::nullopt;
  };

  m_presentFunction = []( const AgentRun & ) {
    // No-op default: the application layer consumes runCompleted and decides
    // whether to zoom / add layers.
  };
}

QString AgentRunCoordinator::startRun( const AgentRunRequest &request )
{
  AgentRun run;
  run.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
  run.request = request.userRequest;
  run.algorithmId = request.algorithmId;
  run.params = request.params;
  run.startedAtUtc = QDateTime::currentDateTimeUtc();

  {
    QMutexLocker lock( &m_mutex );
    m_runs[run.id] = run;
    m_activeRunId = run.id;
    m_cancelRequested.store( false );
    m_currentCancel = nullptr;
  }

  executeRun( std::move( run ) );
  return run.id;
}

AgentRun AgentRunCoordinator::runSynchronously( const AgentRunRequest &request )
{
  AgentRun run;
  run.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
  run.request = request.userRequest;
  run.algorithmId = request.algorithmId;
  run.params = request.params;
  run.startedAtUtc = QDateTime::currentDateTimeUtc();

  {
    QMutexLocker lock( &m_mutex );
    m_runs[run.id] = run;
    m_activeRunId = run.id;
    m_cancelRequested.store( false );
    m_currentCancel = nullptr;
  }

  return executeRun( std::move( run ) );
}

AgentRun AgentRunCoordinator::executeRun( AgentRun run )
{
  auto finalize = [this]( AgentRun &r ) {
    r.completedAtUtc = QDateTime::currentDateTimeUtc();
    {
      QMutexLocker lock( &m_mutex );
      m_runs[r.id] = r;
      m_currentCancel = nullptr;
    }
    emitTerminalSignal( r );
  };

  transitionStage( run, AgentRunStage::Understanding );
  transitionStage( run, AgentRunStage::Planning );
  transitionStage( run, AgentRunStage::Preflight );

  run.preflightReport = m_preflightFunction( run.algorithmId, run.params );
  const bool preflightValid = run.preflightReport.isObject()
                              && run.preflightReport.isMember( "valid" )
                              && run.preflightReport["valid"].asBool();

  if ( !preflightValid )
  {
    run.errors.append( QStringLiteral( "Preflight failed" ) );
    if ( auto repaired = m_repairFunction( run, run.preflightReport ) )
    {
      run.params = std::move( *repaired );
      run.repairAttempts = 1;
      transitionStage( run, AgentRunStage::Repairing );
      run.preflightReport = m_preflightFunction( run.algorithmId, run.params );
      if ( !( run.preflightReport.isObject()
              && run.preflightReport.isMember( "valid" )
              && run.preflightReport["valid"].asBool() ) )
      {
        run.errors.append( QStringLiteral( "Preflight still invalid after repair" ) );
        transitionStage( run, AgentRunStage::Failed );
        finalize( run );
        return run;
      }
    }
    else
    {
      transitionStage( run, AgentRunStage::Failed );
      finalize( run );
      return run;
    }
  }

  while ( true )
  {
    if ( m_cancelRequested.load() )
    {
      transitionStage( run, AgentRunStage::Canceled );
      finalize( run );
      return run;
    }

    transitionStage( run, AgentRunStage::Running );
    std::function<void()> cancelFn;
    run.executionPayload = m_executeFunction( run.algorithmId, run.params, run.taskId, cancelFn );
    {
      QMutexLocker lock( &m_mutex );
      m_currentCancel = cancelFn;
      m_runs[run.id] = run;
    }

    if ( m_cancelRequested.load() )
    {
      transitionStage( run, AgentRunStage::Canceled );
      finalize( run );
      return run;
    }

    const bool executionOk = run.executionPayload.isObject()
                             && run.executionPayload.isMember( "status" )
                             && run.executionPayload["status"].asString() == "success";

    if ( !executionOk )
    {
      const QString msg = QString::fromStdString(
        run.executionPayload.isMember( "errorMessage" )
          ? run.executionPayload["errorMessage"].asString()
          : "Execution failed" );
      run.errors.append( msg );

      if ( run.repairAttempts >= kMaxRepairAttempts )
      {
        transitionStage( run, AgentRunStage::Failed );
        finalize( run );
        return run;
      }

      if ( auto repaired = m_repairFunction( run, run.executionPayload ) )
      {
        run.params = std::move( *repaired );
        ++run.repairAttempts;
        transitionStage( run, AgentRunStage::Repairing );
        continue;
      }

      transitionStage( run, AgentRunStage::Failed );
      finalize( run );
      return run;
    }

    // Verification gate: no Completed before the committed output is verified.
    transitionStage( run, AgentRunStage::Verifying );

    QString committedPath;
    if ( run.executionPayload.isMember( "output" ) )
      committedPath = QString::fromStdString( run.executionPayload["output"].asString() );

    if ( !committedPath.isEmpty() )
    {
      run.verification = m_verifyFunction( committedPath, kindHintFromPath( committedPath ) );
      if ( !run.verification.ok )
      {
        const QString issue = run.verification.issues.isEmpty()
                              ? QStringLiteral( "Output verification failed" )
                              : run.verification.issues.first();
        run.errors.append( issue );

        if ( run.repairAttempts < kMaxRepairAttempts )
        {
          Json::Value failurePayload( Json::objectValue );
          failurePayload["status"] = "verification_failed";
          failurePayload["errorMessage"] = issue.toStdString();
          failurePayload["verification"] = Json::Value( Json::objectValue );
          if ( auto repaired = m_repairFunction( run, failurePayload ) )
          {
            run.params = std::move( *repaired );
            ++run.repairAttempts;
            transitionStage( run, AgentRunStage::Repairing );
            // Re-run execution after repair.
            continue;
          }
        }

        transitionStage( run, AgentRunStage::Failed );
        finalize( run );
        return run;
      }
    }

    break;
  }

  transitionStage( run, AgentRunStage::Presenting );
  if ( m_presentFunction )
    m_presentFunction( run );

  transitionStage( run, AgentRunStage::Completed );
  finalize( run );
  return run;
}

void AgentRunCoordinator::transitionStage( AgentRun &run, AgentRunStage stage )
{
  run.stage = stage;
  {
    QMutexLocker lock( &m_mutex );
    m_runs[run.id] = run;
  }
  emit runStageChanged( run );
}

void AgentRunCoordinator::emitTerminalSignal( const AgentRun &run )
{
  switch ( run.stage )
  {
    case AgentRunStage::Completed:
      emit runCompleted( run );
      break;
    case AgentRunStage::Canceled:
      emit runCanceled( run );
      break;
    default:
      emit runFailed( run );
      break;
  }
}

void AgentRunCoordinator::cancelRun( const QString &runId )
{
  QMutexLocker lock( &m_mutex );
  m_cancelRequested.store( true );
  if ( m_currentCancel )
    m_currentCancel();

  auto it = m_runs.find( runId );
  if ( it != m_runs.end() && it->stage != AgentRunStage::Completed && it->stage != AgentRunStage::Failed )
  {
    it->stage = AgentRunStage::Canceled;
    it->completedAtUtc = QDateTime::currentDateTimeUtc();
  }
}

AgentRun AgentRunCoordinator::runState( const QString &runId ) const
{
  QMutexLocker lock( &m_mutex );
  return m_runs.value( runId );
}

} // namespace sicnu::agent
