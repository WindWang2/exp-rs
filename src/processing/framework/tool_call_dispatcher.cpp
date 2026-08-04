// src/processing/framework/tool_call_dispatcher.cpp
#include "tool_call_dispatcher.h"

#include "atomic_algorithm_registry.h"
#include "json_params_converter.h"
#include "task_center.h"

#include "output_committer.h"
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>
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
    mOutputCommitterHandler = [dataManager]( const sicnu::AlgorithmTaskInfo &info,
                                             std::string &outCommittedPath,
                                             std::string &outCommitError ) -> bool {
      sicnu::OutputCommitter committer( dataManager );
      const QFileInfo outInfo( info.outputLayerPath );
      const QString suffix = outInfo.suffix().isEmpty() ? QStringLiteral( "tif" ) : outInfo.suffix();
      const QString stablePath = outInfo.absolutePath() + QStringLiteral( "/" )
                                 + outInfo.completeBaseName() + QStringLiteral( "_committed." ) + suffix;

      sicnu::AlgorithmOutputRequest request;
      request.kind = sicnu::data::AssetKind::Raster;
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
    if ( registry.findAdapter( altId ) )
      return altId;
  }
  return rawName;
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

  const std::string algorithmId = resolveAlgorithmId( parsed.name );
  const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
  if ( !adapter )
  {
    return QString( QStringLiteral( "Algorithm not registered: %1" ) )
      .arg( QString::fromStdString( parsed.name ) );
  }

  // Legacy AgentWorkflowExecutor::executeToolCall contract: reject before any
  // submission when a descriptor-required input is missing.
  const AlgorithmDescriptor descriptor = adapter->descriptor();
  for ( const auto &inputPort : descriptor.inputs )
  {
    if ( inputPort.required && !parsed.arguments.isMember( inputPort.name ) )
    {
      return QString( QStringLiteral( "Missing required parameter: %1" ) )
        .arg( QString::fromStdString( inputPort.name ) );
    }
  }
  return QString();
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
  const ParsedEnvelope parsed = parseEnvelope( envelope );

  const QString reason = rejectionReasonFor( parsed );
  if ( !reason.isEmpty() )
  {
    if ( errorOut )
      *errorOut = reason;
    return false;
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

  if ( mWatcher )
  {
    mWatcher( taskId, std::move( onComplete ) );
  }
  return true;
}

Json::Value ToolCallDispatcher::dispatchAndAwait( const Json::Value &envelope, std::chrono::milliseconds timeout )
{
  struct AwaitState
  {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    Json::Value captured;
  };

  auto state = std::make_shared<AwaitState>();

  QString error;
  const bool submitted = submit( envelope, [state]( const Json::Value &resultPayload ) {
    {
      std::lock_guard<std::mutex> lock( state->mutex );
      state->captured = resultPayload;
      state->done = true;
    }
    state->cv.notify_all();
  }, &error );

  if ( !submitted )
  {
    Json::Value errorResult( Json::objectValue );
    errorResult["status"] = "error";
    errorResult["errorMessage"] = error.toStdString();
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
    if ( !info.outputLayerPath.isEmpty() && payload.isObject() && !payload.isMember( "output" ) )
    {
      if ( committerHandler )
      {
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
      else
      {
        payload["output"] = info.outputLayerPath.toStdString();
      }
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
