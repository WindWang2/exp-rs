// src/processing/framework/tool_call_dispatcher.cpp
#include "tool_call_dispatcher.h"

#include "atomic_algorithm_registry.h"
#include "json_params_converter.h"
#include "schema_validator.h"
#include "task_center.h"

#include "output_committer.h"
#include "data/data_manager.h"
#include <QCoreApplication>
#include <QPointer>
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonObject>
#include <QThread>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace sicnu::processing {

namespace {

/// Parses a JSON string into @a out; false on malformed input.
bool parseJsonString( const std::string &jsonStr, Json::Value &out )
{
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string errs;
  return reader->parse( jsonStr.c_str(), jsonStr.c_str() + jsonStr.size(), &out, &errs );
}

/// Maps an output file suffix to the asset kind the committer validates and
/// registers. Kept in sync with the descriptor outputs: statistics-only and
/// multi-output operators must not be committed as raster assets (their CSV /
/// vector outputs open structurally as OGR datasets).
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

ToolCallDispatcher::ToolCallDispatcher( SubmissionSink sink, CompletionWatcher watcher )
  : mSink( std::move( sink ) )
  , mWatcher( std::move( watcher ) )
{
}

void ToolCallDispatcher::setDataManager( sicnu::data::DataManager *dataManager )
{
  mDataManager = dataManager;
  if ( dataManager )
  {
    QPointer<sicnu::data::DataManager> managerGuard( dataManager );
    mOutputCommitterHandler = [managerGuard]( const sicnu::AlgorithmTaskInfo &info,
                                              std::string &outCommittedPath,
                                              std::string &outCommitError ) -> bool {
      if ( !managerGuard )
      {
        outCommitError = "DataManager was destroyed before the output could be committed";
        return false;
      }
      sicnu::OutputCommitter committer( managerGuard );
      const QFileInfo outInfo( info.outputLayerPath );
      const QString suffix = outInfo.suffix().isEmpty() ? QStringLiteral( "tif" ) : outInfo.suffix();
      const QString stablePath = outInfo.absolutePath() + QStringLiteral( "/" )
                                 + outInfo.completeBaseName() + QStringLiteral( "_committed." ) + suffix;

      sicnu::AlgorithmOutputRequest request;
      request.kind = assetKindForSuffix( suffix );
      request.tempPath = info.outputLayerPath;
      request.stablePath = stablePath;
      request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
      request.autoLoad = false;
      request.derivation.algorithmId = info.algorithmId;
      request.derivation.parameters = QJsonObject::fromVariantMap( info.parameterMap );
      request.derivation.taskReference = QString::number( info.taskId );
      request.derivation.completedAtUtc = QDateTime::currentDateTimeUtc();

      const auto commitResult = committer.commit( request );

      if ( commitResult )
      {
        outCommittedPath = stablePath.toStdString();
        return true;
      }
      else
      {
        outCommitError = commitResult.diagnostics().isEmpty()
                           ? "OutputCommitter refused the tool-call output."
                           : commitResult.diagnostics().first().message.toStdString();
        return false;
      }
    };
  }
  else
  {
    mOutputCommitterHandler = nullptr;
  }
}

ToolCallDispatcher::ParsedEnvelope ToolCallDispatcher::parseEnvelope( const Json::Value &envelope )
{
  ParsedEnvelope parsed;
  if ( !envelope.isObject() )
    return parsed;

  Json::Value argsNode;

  if ( envelope.isMember( "function" ) && envelope["function"].isObject() )
  {
    // Shape: {function:{name, arguments}} — arguments may be a JSON string or an object.
    const Json::Value &funcObj = envelope["function"];
    if ( !( funcObj.isMember( "name" ) && funcObj["name"].isString() ) )
      return parsed;

    parsed.name = funcObj["name"].asString();
    if ( funcObj.isMember( "arguments" ) )
      argsNode = funcObj["arguments"];
  }
  else
  {
    // Shapes: {name, parameters} / {name, arguments} / {name, params}.
    if ( !( envelope.isMember( "name" ) && envelope["name"].isString() ) )
      return parsed;

    parsed.name = envelope["name"].asString();
    if ( envelope.isMember( "parameters" ) )
      argsNode = envelope["parameters"];
    else if ( envelope.isMember( "arguments" ) )
      argsNode = envelope["arguments"];
    else if ( envelope.isMember( "params" ) )
      argsNode = envelope["params"];
  }

  if ( argsNode.isNull() )
  {
    // No arguments member: an envelope with just a name is a call with empty params.
    parsed.arguments = Json::Value( Json::objectValue );
  }
  else if ( argsNode.isObject() )
  {
    parsed.arguments = argsNode;
  }
  else if ( argsNode.isString() )
  {
    Json::Value parsedArgs;
    if ( !parseJsonString( argsNode.asString(), parsedArgs ) || !parsedArgs.isObject() )
      return ParsedEnvelope{}; // unparseable arguments → invalid
    parsed.arguments = parsedArgs;
  }
  else
  {
    // Arguments must be an object or a JSON string.
    return ParsedEnvelope{};
  }

  parsed.valid = true;
  return parsed;
}

Json::Value ToolCallDispatcher::argumentsFor( const Json::Value &envelope )
{
  return parseEnvelope( envelope ).arguments;
}

std::string ToolCallDispatcher::resolveAlgorithmId( const std::string &rawName )
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  if ( registry.findAdapter( rawName ) )
    return rawName;

  const auto underscorePos = rawName.find( '_' );
  if ( underscorePos != std::string::npos )
  {
    // LLMs emit rs_kmeans_classification for rs:kmeans_classification because
    // OpenAI tool names forbid ':'. Only the first underscore is rewritten.
    std::string altId = rawName;
    altId[underscorePos] = ':';
    if ( registry.findAdapter( altId ) || isInteractionAction( altId ) )
      return altId;
  }
  return rawName;
}

bool ToolCallDispatcher::isCanvasAction( const std::string &name )
{
  return ( name.size() > 7 && name.compare( 0, 7, "canvas:" ) == 0 ) ||
         ( name.size() > 7 && name.compare( 0, 7, "canvas_" ) == 0 );
}

bool ToolCallDispatcher::isInteractionAction( const std::string &name )
{
  if ( isCanvasAction( name ) )
    return true;
  if ( ( name.size() > 5 && name.compare( 0, 5, "view:" ) == 0 ) ||
       ( name.size() > 5 && name.compare( 0, 5, "view_" ) == 0 ) )
    return true;
  if ( ( name.size() > 4 && name.compare( 0, 4, "roi:" ) == 0 ) ||
       ( name.size() > 4 && name.compare( 0, 4, "roi_" ) == 0 ) )
    return true;
  if ( ( name.size() > 6 && name.compare( 0, 6, "layer:" ) == 0 ) ||
       ( name.size() > 6 && name.compare( 0, 6, "layer_" ) == 0 ) )
    return true;
  if ( ( name.size() > 7 && name.compare( 0, 7, "raster:" ) == 0 ) ||
       ( name.size() > 7 && name.compare( 0, 7, "raster_" ) == 0 ) )
    return true;
  if ( ( name.size() > 5 && name.compare( 0, 5, "data:" ) == 0 ) ||
       ( name.size() > 5 && name.compare( 0, 5, "data_" ) == 0 ) )
    return true;
  return false;
}

bool ToolCallDispatcher::isPlanRequest( const ParsedEnvelope &parsed )
{
  return parsed.arguments.isObject()
         && parsed.arguments.isMember( "steps" )
         && parsed.arguments["steps"].isArray();
}

QString ToolCallDispatcher::rejectionReasonFor( const ParsedEnvelope &parsed ) const
{
  if ( !parsed.valid )
  {
    return QStringLiteral(
      "Tool call envelope is malformed: expected {name, parameters}, "
      "{function:{name, arguments}} (arguments as object or JSON string), "
      "{name, arguments} or {name, params}." );
  }

  // Plan requests are never dispatched as single tool calls.
  if ( isPlanRequest( parsed ) )
  {
    return QStringLiteral(
      "Envelope contains a 'steps' array and must be routed through plan "
      "approval, not dispatched as a single tool call." );
  }

  // Interaction actions (view:, roi:, canvas:, layer:) bypass the algorithm registry
  // and Task Center entirely: they route to InteractionActionHandler or CanvasActionHandler.
  if ( isInteractionAction( parsed.name ) )
  {
    if ( !mInteractionActionHandler && !( isCanvasAction( parsed.name ) && mCanvasActionHandler ) )
    {
      if ( isCanvasAction( parsed.name ) )
      {
        return QString( QStringLiteral(
          "Canvas action '%1' has no handler wired (setCanvasActionHandler)." ) )
          .arg( QString::fromStdString( parsed.name ) );
      }
      return QString( QStringLiteral(
        "Interaction action '%1' has no handler wired (setInteractionActionHandler)." ) )
        .arg( QString::fromStdString( parsed.name ) );
    }
    return QString();
  }

  const std::string algorithmId = resolveAlgorithmId( parsed.name );
  const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
  if ( !adapter )
  {
    return QString( QStringLiteral( "Algorithm not registered: %1" ) )
      .arg( QString::fromStdString( parsed.name ) );
  }

  // Shared JSON-schema validation (type / enum / range / array / shape),
  // single implementation for every agent-facing surface. Unknown parameters
  // are reported as warnings (not hard errors) so legacy callers that pass
  // extra fields keep working; required/type/enum/range violations reject.
  const AlgorithmDescriptor descriptor = adapter->descriptor();
  const ParameterValidationResult validation =
    validateParameters( parsed.arguments, descriptor, UnknownParameterPolicy::Warn );
  if ( !validation.ok() )
  {
    return QString::fromStdString( validation.errors.front().message );
  }
  return QString();
}

Json::Value ToolCallDispatcher::validateCall( const Json::Value &envelope ) const
{
  Json::Value result( Json::objectValue );

  const ParsedEnvelope parsed = parseEnvelope( envelope );
  if ( !parsed.valid )
  {
    result["valid"] = false;
    Json::Value errs( Json::arrayValue );
    Json::Value err( Json::objectValue );
    err["code"] = "malformed_envelope";
    err["parameter"] = "";
    err["message"] = "Tool call envelope is malformed: expected {name, parameters}, "
                     "{function:{name, arguments}}, {name, arguments} or {name, params}.";
    errs.append( err );
    result["errors"] = errs;
    result["warnings"] = Json::Value( Json::arrayValue );
    return result;
  }

  if ( isPlanRequest( parsed ) )
  {
    result["valid"] = false;
    Json::Value errs( Json::arrayValue );
    Json::Value err( Json::objectValue );
    err["code"] = "plan_request";
    err["parameter"] = "steps";
    err["message"] = "Envelope contains a 'steps' array and must be routed through plan "
                     "approval, not dispatched as a single tool call.";
    errs.append( err );
    result["errors"] = errs;
    result["warnings"] = Json::Value( Json::arrayValue );
    return result;
  }

  // Interaction actions bypass algorithm validation — parameter shape is the
  // handler's responsibility.
  if ( isInteractionAction( parsed.name ) )
  {
    result["valid"] = true;
    result["errors"] = Json::Value( Json::arrayValue );
    result["warnings"] = Json::Value( Json::arrayValue );
    return result;
  }

  const std::string algorithmId = resolveAlgorithmId( parsed.name );
  const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
  if ( !adapter )
  {
    result["valid"] = false;
    Json::Value errs( Json::arrayValue );
    Json::Value err( Json::objectValue );
    err["code"] = "algorithm_not_registered";
    err["parameter"] = "";
    err["message"] = "Algorithm not registered: " + parsed.name;
    errs.append( err );
    result["errors"] = errs;
    result["warnings"] = Json::Value( Json::arrayValue );
    return result;
  }

  const ParameterValidationResult validation =
    validateParameters( parsed.arguments, adapter->descriptor(), UnknownParameterPolicy::Warn );
  result["valid"] = validation.ok();
  Json::Value errs( Json::arrayValue );
  for ( const auto &e : validation.errors ) errs.append( e.toJson() );
  result["errors"] = errs;
  Json::Value warns( Json::arrayValue );
  for ( const auto &w : validation.warnings ) warns.append( w.toJson() );
  result["warnings"] = warns;
  return result;
}

ToolCallClassification ToolCallDispatcher::classify( const Json::Value &envelope ) const
{
  const ParsedEnvelope parsed = parseEnvelope( envelope );
  if ( !parsed.valid )
    return ToolCallClassification::Invalid;

  // PlanRequest wins over name resolution: an envelope whose arguments carry a
  // top-level `steps` array is a multi-step plan, whatever the name says.
  if ( isPlanRequest( parsed ) )
    return ToolCallClassification::PlanRequest;

  // Interaction actions (view:, roi:, canvas:, layer:) classify as ToolCall when
  // an interaction action handler or canvas action handler is wired.
  if ( isInteractionAction( parsed.name ) )
  {
    if ( mInteractionActionHandler )
      return ToolCallClassification::ToolCall;
    if ( isCanvasAction( parsed.name ) && mCanvasActionHandler )
      return ToolCallClassification::ToolCall;
    return ToolCallClassification::Invalid;
  }

  if ( AtomicAlgorithmRegistry::instance().findAdapter( resolveAlgorithmId( parsed.name ) ) )
    return ToolCallClassification::ToolCall;

  return ToolCallClassification::Invalid;
}

QString ToolCallDispatcher::rejectionReason( const Json::Value &envelope ) const
{
  return rejectionReasonFor( parseEnvelope( envelope ) );
}

bool ToolCallDispatcher::submit( const Json::Value &envelope, CompletionCallback onComplete,
                                 QString *errorOut, long *taskIdOut )
{
  return submitParsed( parseEnvelope( envelope ), std::move( onComplete ), errorOut, taskIdOut,
                       /*allowWatcher=*/true );
}

bool ToolCallDispatcher::submitParsed( const ParsedEnvelope &parsed, CompletionCallback onComplete,
                                       QString *errorOut, long *taskIdOut, bool allowWatcher )
{
  const QString reason = rejectionReasonFor( parsed );
  if ( !reason.isEmpty() )
  {
    if ( errorOut )
      *errorOut = reason;
    return false;
  }

  // Interaction / canvas actions run synchronously in-process via their handler — no
  // task id, no Task Center submission, no completion watcher. The handler's result is
  // delivered through the same CompletionCallback so dispatchAndAwait()'s await
  // loop works uniformly. The caller (dispatchAndAwait) re-enters from the same
  // thread, so onComplete fires inline.
  if ( isInteractionAction( parsed.name ) )
  {
    Json::Value result;
    if ( mInteractionActionHandler )
    {
      result = mInteractionActionHandler( parsed.name, parsed.arguments );
    }
    else if ( isCanvasAction( parsed.name ) && mCanvasActionHandler )
    {
      const std::string action = parsed.name.substr( 7 ); // strip "canvas:"
      result = mCanvasActionHandler( action, parsed.arguments );
    }

    if ( onComplete )
      onComplete( result );

    if ( taskIdOut )
      *taskIdOut = 9000001; // reserved sentinel for interaction tools
    return true;
  }

  const std::string algorithmId = resolveAlgorithmId( parsed.name );
  const long taskId = mSink ? mSink( QString::fromStdString( algorithmId ), jsonParamsToVariantMap( parsed.arguments ) ) : -1;
  if ( taskId <= 0 )
  {
    if ( errorOut )
    {
      *errorOut = QStringLiteral( "Submission sink rejected the tool call (invalid task id)." );
    }
    return false;
  }

  if ( taskIdOut )
    *taskIdOut = taskId;

  if ( allowWatcher && mWatcher )
  {
    mWatcher( taskId, std::move( onComplete ) );
  }
  return true;
}

Json::Value ToolCallDispatcher::dispatchAndAwait( const Json::Value &envelope, std::chrono::milliseconds timeout )
{
  const ParsedEnvelope parsed = parseEnvelope( envelope );

  // Production wiring (Task Center flavor) provides an event-loop-free sync
  // await on the ExecutionPlane: the wakeup rides a thread-safe completion
  // channel and the payload commit runs on this (owning) thread, so blocking
  // here can never wedge a queued delivery (#559). The watcher is NOT
  // registered on this path — the commit is owned by the sync await, exactly
  // once.
  if ( mSyncAwait )
  {
    // Interaction actions deliver their payload inline through this callback;
    // algorithm tasks ignore it (taskId > 0 ≠ sentinel) and await below.
    auto inlineResult = std::make_shared<Json::Value>();
    long taskId = -1;
    QString error;
    if ( !submitParsed( parsed,
                        [inlineResult]( const Json::Value &payload ) { *inlineResult = payload; },
                        &error, &taskId, /*allowWatcher=*/false ) )
    {
      Json::Value errorResult( Json::objectValue );
      errorResult["status"] = "error";
      errorResult["errorMessage"] = error.toStdString();
      return errorResult;
    }
    if ( taskId == 9000001 )
      return *inlineResult; // interaction action: already delivered synchronously
    return mSyncAwait( taskId, timeout );
  }

  struct AwaitState
  {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    Json::Value captured;
  };

  auto state = std::make_shared<AwaitState>();

  QString error;
  const bool submitted = submitParsed( parsed, [state]( const Json::Value &resultPayload ) {
    {
      std::lock_guard<std::mutex> lock( state->mutex );
      state->captured = resultPayload;
      state->done = true;
    }
    state->cv.notify_all();
  }, &error, nullptr, /*allowWatcher=*/true );

  if ( !submitted )
  {
    Json::Value errorResult( Json::objectValue );
    errorResult["status"] = "error";
    errorResult["errorMessage"] = error.toStdString();
    return errorResult;
  }

  if ( QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread() )
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while ( true )
    {
      {
        std::lock_guard<std::mutex> lock( state->mutex );
        if ( state->done )
          return state->captured;
      }
      if ( std::chrono::steady_clock::now() >= deadline )
        break;
      QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
      std::unique_lock<std::mutex> lock( state->mutex );
      state->cv.wait_for( lock, std::chrono::milliseconds( 10 ), [state]() { return state->done; } );
      if ( state->done )
        return state->captured;
    }
    Json::Value errorResult( Json::objectValue );
    errorResult["status"] = "error";
    errorResult["errorMessage"] = "Tool call timed out";
    return errorResult;
  }

  std::unique_lock<std::mutex> lock( state->mutex );
  if ( !state->cv.wait_for( lock, timeout, [state]() { return state->done; } ) )
  {
    Json::Value errorResult( Json::objectValue );
    errorResult["status"] = "error";
    errorResult["errorMessage"] = "Tool call timed out";
    return errorResult;
  }
  return state->captured;
}

Json::Value ToolCallDispatcher::submitBlocking( const Json::Value &envelope, std::chrono::milliseconds timeout )
{
  return dispatchAndAwait( envelope, timeout );
}

Json::Value ToolCallDispatcher::buildTaskResultPayload( const sicnu::AlgorithmTaskInfo &info,
                                              const OutputCommitterHandler &committerHandler )
{
  Json::Value payload = info.resultPayload.isNull() ? Json::Value( Json::objectValue ) : info.resultPayload;
  payload["algorithmId"] = info.algorithmId.toStdString();
  payload["taskId"] = static_cast<Json::Int64>( info.taskId );

  if ( info.status == sicnu::TaskStatus::Completed )
  {
    payload["status"] = "success";
    if ( !info.outputLayerPath.isEmpty() && payload.isObject() && committerHandler )
    {
      // Always commit the produced output when a committer is wired — rs:
      // operators report their own "output" in the result payload, but the
      // task's output layer still needs transactional promotion to a stable
      // asset (asset_id / lineage). The committed stable path replaces any
      // operator-reported path so the result and the asset agree.
      std::string committedPath;
      std::string commitError;
      if ( committerHandler( info, committedPath, commitError ) )
      {
        payload["output"] = committedPath;
      }
      else
      {
        const std::string message = commitError.empty() ? "OutputCommitter refused the tool-call output." : commitError;
        payload["status"] = "error";
        payload["commitError"] = message;
        payload["errorMessage"] = message;
        payload["output"] = info.outputLayerPath.toStdString();
        qWarning() << "ToolCallDispatcher: output commit failed for task" << info.taskId
                   << "-" << QString::fromStdString( message );
      }
    }
    else if ( !info.outputLayerPath.isEmpty() && payload.isObject() && !payload.isMember( "output" ) )
    {
      payload["output"] = info.outputLayerPath.toStdString();
    }
  }
  else
  {
    payload["status"] = "error";
    payload["errorMessage"] = info.errorMessage.toStdString();
  }
  return payload;
}

} // namespace sicnu::processing
