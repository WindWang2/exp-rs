#include "agent_tool_host.h"
#include "python_approval_snapshot.h"

#include "agent/copilot_tool_gate.h"
#include "agent/harness/tool_manifest.h"
#include "agent/interaction_tool_registry.h"
#include "agent/output_verifier.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "agent/tool_catalog/meta_protocol_tools.h"
#include "data/data_manager.h"
#include "processing/framework/agent_workflow_executor.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/json_params_converter.h"
#include "processing/framework/provider_algorithm_adapter.h"
#include "processing/framework/task_center.h"
#include "workflow/workflow_definition.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQueue>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sicnu::agent::runtime
{
namespace
{
using processing::ToolCallDispatcher;
using tool_catalog::AgentTool;
constexpr int kMaxCallsPerRun = 32;
constexpr int kMaxRememberedRunIds = 4096;
constexpr int kMaxArgumentBytes = 1024 * 1024;
constexpr int kMaxResultBytes = 512 * 1024;
constexpr int kMaxCatalogBytes = 1024 * 1024;

QJsonObject toQt( const Json::Value &value )
{
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  return QJsonDocument::fromJson( QByteArray::fromStdString( Json::writeString( writer, value ) ) ).object();
}

Json::Value toCpp( const QJsonObject &value )
{
  return processing::jsonValueFromQJson( value );
}

QString digest( const QJsonObject &value )
{
  return QString::fromLatin1( QCryptographicHash::hash(
    QJsonDocument( value ).toJson( QJsonDocument::Compact ), QCryptographicHash::Sha256 ).toHex() );
}

QJsonObject errorResult( const QString &code, const QString &message )
{
  return { { QStringLiteral( "status" ), QStringLiteral( "error" ) },
           { QStringLiteral( "errorCode" ), code },
           { QStringLiteral( "errorMessage" ), message },
           { QStringLiteral( "retryable" ), false } };
}

bool safeView( const QString &id )
{
  // Exact IDs: a new tool in an interaction namespace is never privileged.
  static const QSet<QString> ids = {
    QStringLiteral( "view:get_state" ), QStringLiteral( "view:set_extent" ),
    QStringLiteral( "view:zoom_to_layer" ), QStringLiteral( "view:zoom_to_asset" ),
    QStringLiteral( "view:fit_all" ), QStringLiteral( "view:set_scale" ),
    QStringLiteral( "canvas:zoom_to_extent" )
  };
  return ids.contains( id );
}

bool readDiscovery( const QString &id )
{
  static const QSet<QString> ids = {
    QStringLiteral( "data:list_layers" ), QStringLiteral( "data:describe_dataset" ),
    QStringLiteral( "data:get_lineage" ), QStringLiteral( "raster:get_display" ),
    QStringLiteral( "spatial:workspace_summary" ), QStringLiteral( "spatial:layer_summary" ),
    QStringLiteral( "spatial:raster_inspect" ), QStringLiteral( "spatial:vector_inspect" ),
    QStringLiteral( "spatial:list_models" ), QStringLiteral( "spatial:understand" ),
    QStringLiteral( "spatial:search_capabilities" ), QStringLiteral( "spatial:describe_capability" ),
    QStringLiteral( "list_tools" ), QStringLiteral( "search_tools" ),
    QStringLiteral( "get_tool_schema" ), QStringLiteral( "get_algorithm_schema" )
  };
  return ids.contains( id );
}

bool metaDiscovery( const QString &id )
{
  return id == QLatin1String( "list_tools" ) || id == QLatin1String( "search_tools" )
      || id == QLatin1String( "get_tool_schema" ) || id == QLatin1String( "get_algorithm_schema" );
}

bool supportedMeta( const QString &id )
{
  return metaDiscovery( id ) || id == QLatin1String( "run_workflow" );
}

int catalogPriority( const QString &id )
{
  if ( id.startsWith( QLatin1String( "python:" ) ) ) return 0;
  if ( readDiscovery( id ) || safeView( id ) || id == QLatin1String( "run_workflow" ) ) return 1;
  static const QSet<QString> processingIds = {
    QStringLiteral( "rs:spectral_index" ), QStringLiteral( "rs:ndvi" ),
    QStringLiteral( "rs:band_math" ), QStringLiteral( "rs:threshold_raster" ),
    QStringLiteral( "rs:resample" ), QStringLiteral( "rs:reproject" )
  };
  if ( processingIds.contains( id ) ) return 2;
  if ( InteractionToolRegistry::instance().hasTool( id.toStdString() ) ) return 3;
  return 4;
}

struct Catalog
{
  ToolCatalogSnapshot snapshot;
  QMap<QString, AgentTool> tools;
  QMap<QString, QString> mappedIds;
};

Catalog currentCatalog()
{
  Catalog result;
  // listTools is the live registry projection. Calling initializeDefaults
  // here would erase custom providers and registrations.
  auto tools = tool_catalog::AgentToolCatalog::instance().listTools();
  for ( const auto &def : tool_catalog::meta_protocol::defs() )
  {
    if ( !supportedMeta( QString::fromUtf8( def.name ) ) ) continue;
    AgentTool tool;
    tool.name = def.name;
    tool.description = def.description;
    tool.inputSchema = tool_catalog::meta_protocol::schema( def );
    tools.push_back( std::move( tool ) );
  }
  std::sort( tools.begin(), tools.end(), []( const AgentTool &a, const AgentTool &b ) {
    const QString left = QString::fromStdString( a.name ), right = QString::fromStdString( b.name );
    const int lp = catalogPriority( left ), rp = catalogPriority( right );
    return lp == rp ? left < right : lp < rp;
  } );
  QSet<QString> providerNames;
  QMap<QString, QJsonObject> definitions;
  for ( auto tool : tools )
  {
    const QString id = QString::fromStdString( tool.name );
    const auto algorithm = processing::AtomicAlgorithmRegistry::instance().findAdapter( tool.name );
    // Provider schemas contain translated labels and enum values. Keep this
    // product session surface stable across language restarts; the complete
    // provider catalog remains available to Processing UI and standalone MCP.
    if ( dynamic_cast<processing::ProviderAlgorithmAdapter *>( algorithm.get() ) )
      continue;
    const auto interaction = InteractionToolRegistry::instance().findTool( tool.name );
    const auto spatial = spatial_tools::SpatialToolRegistry::instance().find( tool.name );
    if ( !algorithm && !interaction && !( spatial && readDiscovery( id ) ) && !supportedMeta( id ) )
      continue;
    if ( result.tools.size() >= AgentToolHost::kMaxCatalogTools && !result.tools.contains( id ) )
      continue;
    try
    {
      // Schema comes from the actual executable registration, rather than
      // a stale provider-side description.
      if ( algorithm ) tool.inputSchema = algorithm->descriptor().toInputSchema();
      else if ( interaction ) tool.inputSchema = interaction->inputSchema;
      else if ( spatial ) tool.inputSchema = ( *spatial )->inputSchema();
      QString name = id;
      name.replace( QRegularExpression( QStringLiteral( "[^A-Za-z0-9_-]" ) ), QStringLiteral( "_" ) );
      name = name.left( 64 );
      if ( name.isEmpty() || id.isEmpty() || id.size() > 128 )
        throw std::invalid_argument( "Invalid tool identity" );
      const QJsonObject definition{
        { QStringLiteral( "name" ), name }, { QStringLiteral( "canonicalId" ), id },
        { QStringLiteral( "description" ), QString::fromStdString( tool.description ).left( 4096 ) },
        { QStringLiteral( "parameters" ), toQt( tool.normalizedInputSchema() ) }
      };
      if ( definitions.contains( id ) )
      {
        if ( definitions.value( id ) != definition )
          throw std::invalid_argument( "Conflicting canonical tool registrations" );
        continue; // Same authority exposed by two providers, e.g. data tools.
      }
      if ( providerNames.contains( name ) )
        throw std::invalid_argument( "Provider-safe tool names collide" );
      definitions.insert( id, definition );
      providerNames.insert( name );
      result.tools.insert( id, tool );
      result.mappedIds.insert( id, id );
      result.mappedIds.insert( name, id );
      result.snapshot.tools.append( definition );
      if ( QJsonDocument( result.snapshot.tools ).toJson( QJsonDocument::Compact ).size() > kMaxCatalogBytes )
        throw std::invalid_argument( "Tool catalog exceeds the product budget" );
    }
    catch ( const std::exception & )
    {
      result.snapshot = {};
      result.snapshot.error = QStringLiteral( "invalid_or_colliding_tool_catalog" );
      result.tools.clear();
      result.mappedIds.clear();
      return result;
    }
  }
  result.snapshot.revision = QString::fromLatin1( QCryptographicHash::hash(
    QJsonDocument( result.snapshot.tools ).toJson( QJsonDocument::Compact ),
    QCryptographicHash::Sha256 ).toHex() );
  return result;
}

// Interaction handlers historically validated their own JSON. The host must
// reject malformed requests before approval too; this bounded structural
// walk covers the declared schemas, while algorithm-specific validation
// remains ToolCallDispatcher::validateCall's responsibility.
QString schemaError( const Json::Value &value, const Json::Value &schema, int depth = 0 )
{
  if ( depth > 32 ) return QStringLiteral( "argument nesting exceeds the host budget" );
  if ( schema.isBool() ) return schema.asBool() ? QString() : QStringLiteral( "schema forbids this value" );
  if ( schema.isMember( "$ref" ) || schema.isMember( "if" ) || schema.isMember( "patternProperties" ) )
    return QStringLiteral( "schema construct is not supported by the native host" );
  if ( schema.isMember( "const" ) && value != schema["const"] ) return QStringLiteral( "argument differs from declared constant" );
  if ( schema["allOf"].isArray() )
    for ( const auto &part : schema["allOf"] )
      if ( const auto error = schemaError( value, part, depth + 1 ); !error.isEmpty() ) return error;
  if ( schema["oneOf"].isArray() )
  {
    int matches = 0;
    for ( const auto &part : schema["oneOf"] ) matches += schemaError( value, part, depth + 1 ).isEmpty();
    if ( matches != 1 ) return QStringLiteral( "argument must match exactly one declared shape" );
  }
  if ( schema.isMember( "not" ) && schemaError( value, schema["not"], depth + 1 ).isEmpty() )
    return QStringLiteral( "argument matches a forbidden shape" );
  if ( schema["type"].isArray() )
  {
    bool matches = false;
    for ( const auto &type : schema["type"] )
    {
      Json::Value candidate = schema;
      candidate["type"] = type;
      matches |= schemaError( value, candidate, depth + 1 ).isEmpty();
    }
    return matches ? QString() : QStringLiteral( "argument does not match a declared type" );
  }
  if ( schema["anyOf"].isArray() )
  {
    bool matches = false;
    for ( const auto &choice : schema["anyOf"] )
      matches |= schemaError( value, choice, depth + 1 ).isEmpty();
    if ( !matches ) return QStringLiteral( "argument does not match any declared type" );
  }
  if ( schema["enum"].isArray() )
  {
    bool match = false;
    for ( const auto &allowed : schema["enum"] ) match |= value == allowed;
    if ( !match ) return QStringLiteral( "argument is not a declared enum value" );
  }
  const std::string type = schema.get( "type", "" ).isString() ? schema.get( "type", "" ).asString() : "";
  if ( ( type == "object" && !value.isObject() ) || ( type == "array" && !value.isArray() )
       || ( type == "null" && !value.isNull() )
       || ( type == "string" && !value.isString() ) || ( type == "boolean" && !value.isBool() )
       || ( type == "number" && !value.isNumeric() )
       || ( type == "integer" && !( value.isIntegral() || ( value.isDouble() && std::isfinite( value.asDouble() )
                                                           && std::floor( value.asDouble() ) == value.asDouble() ) ) ) )
    return QStringLiteral( "argument type does not match the registered schema" );
  if ( value.isNumeric() )
  {
    if ( !std::isfinite( value.asDouble() )
         || ( schema["minimum"].isNumeric() && value.asDouble() < schema["minimum"].asDouble() )
         || ( schema["maximum"].isNumeric() && value.asDouble() > schema["maximum"].asDouble() ) )
      return QStringLiteral( "argument is outside the declared numeric range" );
  }
  if ( value.isString() )
  {
    const QString text = QString::fromStdString( value.asString() );
    if ( ( schema["minLength"].isNumeric() && text.size() < schema["minLength"].asInt64() )
         || ( schema["maxLength"].isNumeric() && text.size() > schema["maxLength"].asInt64() ) )
      return QStringLiteral( "argument is outside the declared string length" );
    if ( schema["pattern"].isString() && !QRegularExpression( QString::fromStdString( schema["pattern"].asString() ) ).match( text ).hasMatch() )
      return QStringLiteral( "argument does not match the declared pattern" );
  }
  if ( value.isArray() && ( ( schema["minItems"].isNumeric() && value.size() < schema["minItems"].asUInt64() )
                           || ( schema["maxItems"].isNumeric() && value.size() > schema["maxItems"].asUInt64() ) ) )
    return QStringLiteral( "argument is outside the declared array length" );
  if ( value.isObject() )
  {
    if ( schema["required"].isArray() )
      for ( const auto &required : schema["required"] )
        if ( required.isString() && !value.isMember( required.asString() ) )
          return QStringLiteral( "required argument is missing" );
    for ( const auto &key : value.getMemberNames() )
    {
      if ( schema["properties"].isObject() && schema["properties"].isMember( key ) )
      {
        const QString error = schemaError( value[key], schema["properties"][key], depth + 1 );
        if ( !error.isEmpty() ) return QString::fromStdString( key ) + QLatin1String( ": " ) + error;
      }
      else if ( schema["additionalProperties"].isBool() && !schema["additionalProperties"].asBool() )
        return QStringLiteral( "undeclared argument" );
      else if ( schema["additionalProperties"].isObject() )
      {
        const QString error = schemaError( value[key], schema["additionalProperties"], depth + 1 );
        if ( !error.isEmpty() ) return error;
      }
    }
  }
  if ( value.isArray() && schema["items"].isObject() )
    for ( const auto &item : value )
    {
      const QString error = schemaError( item, schema["items"], depth + 1 );
      if ( !error.isEmpty() ) return error;
    }
  return {};
}

struct WriterLease { QPointer<QObject> owner; QString runId; };
QMutex &leaseMutex() { static QMutex mutex; return mutex; }
QMap<QString, WriterLease> &leases() { static QMap<QString, WriterLease> values; return values; }
}

struct AgentToolHost::Impl : std::enable_shared_from_this<Impl>
{
  struct Call
  {
    QString id, fingerprint, state = QStringLiteral( "queued" );
    QJsonObject envelope, result, plan;
    QString approvalToken, approvalContext;
    bool terminal = false;
    bool resultReady = false;
    bool submitting = false;
    long taskId = -1, pipelineId = -1;
    QSet<long> nativeTasks;
    PythonApprovalSnapshot pythonApproval;
  };
  struct Run
  {
    QString id, revision, context, leaseKey, active, cancelReason;
    Catalog catalog;
    QMap<QString, std::shared_ptr<Call>> calls;
    QQueue<QString> queue;
    QSet<long> tasks, pipelines;
    QMap<long, sicnu::AlgorithmTaskInfo> taskSnapshots;
    QElapsedTimer clock;
    qint64 approvalStarted = -1, pausedMs = 0;
    bool conversationFinished = false, cancelling = false, idle = false;
    std::shared_ptr<std::atomic_bool> cancelFlag = std::make_shared<std::atomic_bool>( false );
  };

  QPointer<AgentToolHost> host;
  QPointer<sicnu::TaskCenter> center;
  QPointer<data::DataManager> manager;
  QPointer<QObject> keeper;
  std::unique_ptr<ToolCallDispatcher> dispatcher;
  QPointer<processing::AgentWorkflowExecutor> workflow;
  QMap<QString, std::shared_ptr<Run>> runs;
  QSet<QString> retiredRunIds;
  QQueue<QString> retiredRunOrder;
  QMap<QString, std::pair<QString, QString>> approvals;
  QString managerIdentity;
  quint64 contextRevision = 0;
  QList<QMetaObject::Connection> managerConnections;
  std::shared_ptr<Impl> detachedRetention;
  std::shared_ptr<std::atomic_bool> contextValid = std::make_shared<std::atomic_bool>( true );

  Impl( AgentToolHost *owner, sicnu::TaskCenter &authority ) : host( owner ), center( &authority )
  {
    keeper = new QObject( &authority );
    // The host owns the terminal/commit observation. A second default watcher
    // would snapshot a committer and publish before this ledger reconciles a
    // worker which is still exiting after a Task Center cancel watchdog.
    dispatcher = std::make_unique<ToolCallDispatcher>(
      []( const QString &algorithmId, const QVariantMap &params ) {
        processing::ExecutionRequest request;
        request.algorithmId = algorithmId;
        request.params = params;
        request.autoLoad = false;
        request.source = QStringLiteral( "agent" );
        return processing::ExecutionPlane::instance().submit( request ).taskId();
      }, ToolCallDispatcher::CompletionWatcher{} );
    dispatcher->setSourceTag( QStringLiteral( "agent" ) );
    workflow = new processing::AgentWorkflowExecutor( nullptr, keeper );
  }

  void initialize()
  {
    const std::weak_ptr<Impl> weak = shared_from_this();
    QObject::connect( center, &sicnu::TaskCenter::taskUpdated, keeper, [weak]( const sicnu::AlgorithmTaskInfo &info ) {
      if ( const auto self = weak.lock() ) self->taskUpdated( info );
    } );
    auto *timer = new QTimer( keeper );
    timer->setInterval( 100 );
    QObject::connect( timer, &QTimer::timeout, keeper, [weak] {
      if ( const auto self = weak.lock() ) self->tick();
    } );
    timer->start();
  }

  bool ownerThread() const { return keeper && QThread::currentThread() == keeper->thread(); }
  QString context() const
  {
    return managerIdentity + QLatin1Char( ':' ) + QString::number( contextRevision ) + QLatin1Char( ':' )
      + tool_catalog::containment::canonicalWorkspaceRoot( tool_catalog::containment::effectiveMcpWorkspaceRoot() );
  }
  bool busy() const
  {
    for ( const auto &run : runs ) if ( !run->idle ) return true;
    return false;
  }
  void update( const std::shared_ptr<Run> &run, const std::shared_ptr<Call> &call,
               const QString &state, const QString &detail = {} )
  {
    call->state = state;
    if ( host ) emit host->toolUpdated( run->id, call->id, state, detail );
  }
  void resumeClock( const std::shared_ptr<Run> &run )
  {
    if ( run->approvalStarted >= 0 )
    {
      run->pausedMs += run->clock.elapsed() - run->approvalStarted;
      run->approvalStarted = -1;
    }
  }
  void releaseLease( const std::shared_ptr<Run> &run )
  {
    QMutexLocker lock( &leaseMutex() );
    auto it = leases().find( run->leaseKey );
    if ( it != leases().end() && it->owner == keeper && it->runId == run->id ) leases().erase( it );
  }
  void maybeIdle( const std::shared_ptr<Run> &run )
  {
    if ( run->idle || !run->conversationFinished || !run->active.isEmpty() || !run->queue.isEmpty() ) return;
    for ( const auto &call : run->calls ) if ( !call->terminal ) return;
    run->idle = true;
    releaseLease( run );
    if ( host ) emit host->runIdle( run->id );
    if ( !host && !busy() )
    {
      if ( keeper ) keeper->deleteLater();
      detachedRetention.reset();
    }
  }
  void complete( const std::shared_ptr<Run> &run, const std::shared_ptr<Call> &call, QJsonObject result )
  {
    if ( call->terminal ) return;
    if ( !call->approvalToken.isEmpty() ) approvals.remove( call->approvalToken );
    call->approvalToken.clear();
    resumeClock( run );
    if ( QJsonDocument( result ).toJson( QJsonDocument::Compact ).size() > kMaxResultBytes )
    {
      QJsonObject bounded;
      for ( const QString &key : { QStringLiteral( "status" ), QStringLiteral( "taskId" ), QStringLiteral( "pipelineId" ),
              QStringLiteral( "execution_id" ), QStringLiteral( "assetId" ), QStringLiteral( "output" ),
              QStringLiteral( "verified" ), QStringLiteral( "script_sha256" ), QStringLiteral( "environment_sha256" ),
              QStringLiteral( "lock_sha256" ), QStringLiteral( "profile_id" ), QStringLiteral( "python_version" ),
              QStringLiteral( "lineage" ), QStringLiteral( "provenance" ), QStringLiteral( "errorCode" ), QStringLiteral( "errorMessage" ) } )
      {
        if ( !result.contains( key ) ) continue;
        const QJsonObject candidate{ { key, result.value( key ) } };
        if ( QJsonDocument( candidate ).toJson( QJsonDocument::Compact ).size() <= 16384 )
          bounded.insert( key, result.value( key ) );
      }
      if ( !bounded.contains( QStringLiteral( "status" ) ) ) bounded.insert( QStringLiteral( "status" ), QStringLiteral( "error" ) );
      bounded.insert( QStringLiteral( "resultTruncated" ), true );
      bounded.insert( QStringLiteral( "fullResultSha256" ), digest( result ) );
      result = bounded;
    }
    call->result = result;
    call->terminal = true;
    update( run, call, result.value( QStringLiteral( "status" ) ) == QLatin1String( "success" )
                          ? QStringLiteral( "completed" ) : ( run->cancelling ? QStringLiteral( "cancelled" )
                            : result.value( QStringLiteral( "errorCode" ) ) == QLatin1String( "approval_rejected" )
                              ? QStringLiteral( "rejected" ) : QStringLiteral( "failed" ) ) );
    if ( run->active == call->id ) run->active.clear();
    if ( host ) emit host->toolCompleted( run->id, call->id, call->result );
    const auto self = shared_from_this();
    if ( keeper ) QTimer::singleShot( 0, keeper, [self, run] { self->drain( run ); } );
    maybeIdle( run );
  }

  bool nativeTerminal( const sicnu::AlgorithmTaskInfo &info ) const
  {
    if ( !sicnu::isTerminalStatus( info.status ) ) return false;
    return center && center->isExecutionQuiescent( info.jobId );
  }

  void observeTask( const std::shared_ptr<Run> &run, const sicnu::AlgorithmTaskInfo &info )
  {
    if ( info.taskId <= 0 ) return;
    const auto previous = run->taskSnapshots.constFind( info.taskId );
    // Queued signals may arrive after a newer terminal snapshot was polled.
    if ( previous != run->taskSnapshots.cend() && sicnu::isTerminalStatus( previous->status )
         && !sicnu::isTerminalStatus( info.status ) ) return;
    run->taskSnapshots.insert( info.taskId, info );
  }

  void reconcile( const std::shared_ptr<Run> &run, const std::shared_ptr<Call> &call )
  {
    if ( call->terminal || call->submitting || !center ) return;
    if ( call->pipelineId > 0 )
    {
      const auto pipeline = center->getPipelineInfo( call->pipelineId );
      for ( auto it = pipeline.stepToTaskId.cbegin(); it != pipeline.stepToTaskId.cend(); ++it )
      {
        call->nativeTasks.insert( it.value() );
        run->tasks.insert( it.value() );
        if ( run->cancelling ) center->cancelTask( it.value() );
      }
    }
    for ( const long id : std::as_const( call->nativeTasks ) )
    {
      const auto current = center->getTaskInfo( id );
      if ( current.taskId == id ) observeTask( run, current );
      const auto observed = run->taskSnapshots.constFind( id );
      // Clearing Task Center history does not erase an observed native
      // terminal. Keep checking its job, since the worker may still be exiting.
      if ( observed == run->taskSnapshots.cend() || !nativeTerminal( observed.value() ) ) return;
    }
    if ( call->taskId > 0 && !call->resultReady )
    {
      update( run, call, QStringLiteral( "awaiting_commit" ) );
      // Existing ExecutionPlane publication gate: exactly-once commit,
      // verification and rollback, on the DataManager's owning thread.
      call->result = toQt( dispatcher->buildCommittedResultPayload( run->taskSnapshots.value( call->taskId ) ) );
      call->resultReady = true;
    }
    if ( !call->resultReady ) return;
    QJsonObject result = call->result;
    // Preserve a legitimately committed output when cancellation raced its
    // delivery. A canceled run still cannot dispatch another queued call.
    if ( run->cancelling && result.value( QStringLiteral( "status" ) ) != QLatin1String( "success" ) )
    {
      result.insert( QStringLiteral( "errorCode" ), run->cancelReason );
      result.insert( QStringLiteral( "cancelled" ), true );
    }
    complete( run, call, result );
  }

  void taskUpdated( const sicnu::AlgorithmTaskInfo &info )
  {
    for ( const auto &run : runs )
    {
      if ( run->active.isEmpty() ) continue;
      const auto call = run->calls.value( run->active );
      if ( !call || call->terminal ) continue;
      if ( call->nativeTasks.contains( info.taskId ) )
      {
        observeTask( run, info );
        update( run, call, sicnu::isTerminalStatus( info.status ) ? QStringLiteral( "awaiting_commit" )
                                                               : ( run->cancelling ? QStringLiteral( "cancelling" ) : QStringLiteral( "running" ) ),
                QStringLiteral( "task-%1" ).arg( info.taskId ) );
        reconcile( run, call );
      }
    }
  }

  void cancel( const std::shared_ptr<Run> &run, const QString &reason )
  {
    if ( run->idle ) return;
    const bool first = !run->cancelling;
    run->cancelling = true;
    run->cancelFlag->store( true );
    run->conversationFinished = true;
    if ( run->cancelReason.isEmpty() ) run->cancelReason = reason.isEmpty() ? QStringLiteral( "cancelled" ) : reason;
    resumeClock( run );
    const auto queued = run->queue;
    run->queue.clear();
    for ( const QString &id : queued )
      complete( run, run->calls.value( id ), errorResult( run->cancelReason, AgentToolHost::tr( "Tool request cancelled before dispatch." ) ) );
    if ( !run->active.isEmpty() )
    {
      const auto call = run->calls.value( run->active );
      if ( call && !call->terminal )
      {
        if ( !call->approvalToken.isEmpty() || ( call->taskId < 0 && call->pipelineId < 0 && !call->submitting ) )
          complete( run, call, errorResult( run->cancelReason, AgentToolHost::tr( "Tool request cancelled before execution." ) ) );
        else
        {
          update( run, call, QStringLiteral( "cancelling" ) );
          if ( center )
          {
            if ( call->pipelineId > 0 ) center->cancelPipeline( call->pipelineId );
            for ( const long id : std::as_const( call->nativeTasks ) ) center->cancelTask( id );
          }
          reconcile( run, call );
        }
      }
    }
    if ( first && host ) emit host->cancelConversationRequested( run->id );
    maybeIdle( run );
  }

  void tick()
  {
    const auto snapshot = runs;
    for ( const auto &run : snapshot )
    {
      if ( run->idle ) continue;
      const qint64 paused = run->pausedMs + ( run->approvalStarted < 0 ? 0 : run->clock.elapsed() - run->approvalStarted );
      if ( !run->cancelling && run->clock.elapsed() - paused >= AgentToolHost::kExecutionBudgetMs )
        cancel( run, QStringLiteral( "execution_budget_exceeded" ) );
      if ( !run->active.isEmpty() ) reconcile( run, run->calls.value( run->active ) );
      maybeIdle( run );
    }
  }

  QString validate( const std::shared_ptr<Run> &run, const std::shared_ptr<Call> &call, bool approvalRecheck = false )
  {
    if ( !manager || !center ) return QStringLiteral( "workspace_unavailable" );
    const Catalog live = currentCatalog();
    if ( !live.snapshot.error.isEmpty() || live.snapshot.revision != run->revision ) return QStringLiteral( "tool_catalog_changed" );
    if ( approvalRecheck && call->approvalContext != context() ) return QStringLiteral( "approval_context_changed" );
    const Json::Value envelope = toCpp( call->envelope );
    const Json::Value args = ToolCallDispatcher::argumentsFor( envelope );
    const auto gate = copilot_gate::gateCallArguments( args );
    if ( !gate.refusalReason.isEmpty() ) return QStringLiteral( "workspace_policy_refused: " ) + gate.refusalReason;
    if ( !call->plan.isEmpty() )
    {
      sicnu::workflow::WorkflowDefinition definition;
      std::string parseError;
      if ( !sicnu::workflow::workflowDefinitionFromJson( toCpp( call->plan ), definition, parseError )
           || definition.steps.empty() || definition.steps.size() > kMaxCallsPerRun )
        return QStringLiteral( "invalid_plan_steps" );
      for ( const auto &step : definition.steps )
      {
        if ( step.kind != sicnu::workflow::StepKind::Operator ) return QStringLiteral( "unsupported_plan_step" );
        const QString id = run->catalog.mappedIds.value( QString::fromStdString( step.operatorId ) );
        if ( id.isEmpty() || !processing::AtomicAlgorithmRegistry::instance().findAdapter( id.toStdString() ) )
          return QStringLiteral( "plan_tool_not_in_catalog" );
        if ( id.startsWith( QLatin1String( "python:" ) ) ) return QStringLiteral( "submit_python_separately" );
        const QJsonObject normalized{ { QStringLiteral( "name" ), id },
          { QStringLiteral( "arguments" ), toQt( step.params ) } };
        const auto validation = dispatcher->validateCall( toCpp( normalized ) );
        if ( !validation.get( "valid", false ).asBool() ) return QStringLiteral( "invalid_plan_parameters" );
      }
      return {};
    }
    const QString id = call->envelope.value( QStringLiteral( "function" ) ).toObject().value( QStringLiteral( "name" ) ).toString();
    const auto tool = live.tools.constFind( id );
    if ( tool == live.tools.cend() ) return QStringLiteral( "tool_not_in_catalog" );
    const QString structural = schemaError( args, tool->normalizedInputSchema() );
    if ( !structural.isEmpty() ) return QStringLiteral( "invalid_parameters: " ) + structural;
    if ( processing::AtomicAlgorithmRegistry::instance().findAdapter( id.toStdString() ) )
    {
      const auto validation = dispatcher->validateCall( envelope );
      if ( !validation.get( "valid", false ).asBool() ) return QStringLiteral( "invalid_algorithm_parameters" );
    }
    return {};
  }

  bool needsApproval( const std::shared_ptr<Call> &call ) const
  {
    if ( !call->plan.isEmpty() ) return true;
    const QString id = call->envelope.value( QStringLiteral( "function" ) ).toObject().value( QStringLiteral( "name" ) ).toString();
    if ( safeView( id ) || readDiscovery( id ) )
      return copilot_gate::gateCallArguments( ToolCallDispatcher::argumentsFor( toCpp( call->envelope ) ) ).needsApproval;
    // Unknown risk never inherits the harness fallback's read_only value.
    // Python environment preparation and ROI/project changes are approvals.
    return true;
  }

  QJsonObject approvalPlan( const std::shared_ptr<Call> &call ) const
  {
    if ( !call->plan.isEmpty() ) return call->plan;
    const auto function = call->envelope.value( QStringLiteral( "function" ) ).toObject();
    QJsonObject plan{ { QStringLiteral( "steps" ), QJsonArray{ QJsonObject{
      { QStringLiteral( "id" ), QStringLiteral( "approved_single_call" ) },
      { QStringLiteral( "name" ), function.value( QStringLiteral( "name" ) ) },
      { QStringLiteral( "params" ), function.value( QStringLiteral( "arguments" ) ) } } } } };
    if ( !call->pythonApproval.identity.isEmpty() ) plan.insert( QStringLiteral( "python_identity" ), call->pythonApproval.identity );
    return plan;
  }

  void publishApproval( const std::shared_ptr<Run> &run, const std::shared_ptr<Call> &call )
  {
    if ( call->terminal || run->cancelling ) return;
    call->approvalToken = QUuid::createUuid().toString( QUuid::WithoutBraces );
    call->approvalContext = context();
    approvals.insert( call->approvalToken, { run->id, call->id } );
    run->approvalStarted = run->clock.elapsed();
    update( run, call, QStringLiteral( "approval" ) );
    if ( host ) emit host->approvalRequested( run->id, call->approvalToken, approvalPlan( call ), call->envelope );
  }

  void prepareApproval( const std::shared_ptr<Run> &run, const std::shared_ptr<Call> &call )
  {
    const auto function = call->envelope.value( QStringLiteral( "function" ) ).toObject();
    const QString id = function.value( QStringLiteral( "name" ) ).toString();
    if ( id != QLatin1String( "python:run_script" ) && id != QLatin1String( "python:environment" ) )
    { publishApproval( run, call ); return; }
    update( run, call, QStringLiteral( "preparing_approval" ) );
    const auto self = shared_from_this();
    auto *watcher = new QFutureWatcher<PythonApprovalSnapshot>( keeper );
    QObject::connect( watcher, &QFutureWatcher<PythonApprovalSnapshot>::finished, keeper, [self, run, call, watcher] {
      const auto snapshot = watcher->result();
      watcher->deleteLater();
      if ( call->terminal || run->cancelling ) return;
      if ( !snapshot.error.isEmpty() )
      { self->complete( run, call, errorResult( snapshot.error, AgentToolHost::tr( "Python approval identity could not be captured; prepare the managed environment before running scripts." ) ) ); return; }
      call->pythonApproval = snapshot;
      auto normalized = call->envelope.value( QStringLiteral( "function" ) ).toObject();
      normalized.insert( QStringLiteral( "arguments" ), snapshot.args );
      call->envelope.insert( QStringLiteral( "function" ), normalized );
      const QString validation = self->validate( run, call );
      if ( !validation.isEmpty() )
      { self->complete( run, call, errorResult( validation, AgentToolHost::tr( "Python approval no longer matches the native context." ) ) ); return; }
      self->publishApproval( run, call );
    } );
    const auto args = function.value( QStringLiteral( "arguments" ) ).toObject();
    watcher->setFuture( QtConcurrent::run( [id, args, cancel = run->cancelFlag] {
      return capturePythonApprovalSnapshot( id, args, [cancel] { return cancel->load(); } );
    } ) );
  }

  void executeApproved( const std::shared_ptr<Run> &run, const std::shared_ptr<Call> &call )
  {
    if ( call->pythonApproval.canonicalToolId.isEmpty() ) { execute( run, call ); return; }
    update( run, call, QStringLiteral( "verifying_approval" ) );
    const auto self = shared_from_this();
    auto *watcher = new QFutureWatcher<QString>( keeper );
    QObject::connect( watcher, &QFutureWatcher<QString>::finished, keeper, [self, run, call, watcher] {
      const QString error = watcher->result();
      watcher->deleteLater();
      if ( call->terminal || run->cancelling ) return;
      const QString validation = self->validate( run, call, true );
      if ( !error.isEmpty() || !validation.isEmpty() )
      { self->complete( run, call, errorResult( error.isEmpty() ? validation : error, AgentToolHost::tr( "The approved Python script or environment changed; review the operation again." ) ) ); return; }
      self->execute( run, call );
    } );
    watcher->setFuture( QtConcurrent::run( [snapshot = call->pythonApproval, cancel = run->cancelFlag] {
      return recheckPythonApprovalSnapshot( snapshot, [cancel] { return cancel->load(); } );
    } ) );
  }

  QJsonObject discovery( const QString &id, const QJsonObject &args, const std::shared_ptr<Run> &run ) const
  {
    if ( id == QLatin1String( "get_tool_schema" ) || id == QLatin1String( "get_algorithm_schema" ) )
    {
      const QString wanted = args.value( id == QLatin1String( "get_tool_schema" ) ? QStringLiteral( "tool_id" )
                                                                                 : QStringLiteral( "algorithm_id" ) ).toString();
      const QString canonical = run->catalog.mappedIds.value( wanted );
      if ( canonical.isEmpty() ) return errorResult( QStringLiteral( "tool_not_in_catalog" ), AgentToolHost::tr( "Tool is not in this session's bounded catalog." ) );
      for ( const auto &value : run->catalog.snapshot.tools )
        if ( value.toObject().value( QStringLiteral( "canonicalId" ) ) == canonical )
          return { { QStringLiteral( "status" ), QStringLiteral( "success" ) }, { QStringLiteral( "tool" ), value } };
    }
    tool_catalog::SearchQuery query;
    query.text = args.value( QStringLiteral( "query" ) ).toString().toStdString();
    query.group = args.value( QStringLiteral( "group" ) ).toString().toStdString();
    query.tag = args.value( QStringLiteral( "tag" ) ).toString().toStdString();
    query.inputType = args.value( QStringLiteral( "input_type" ) ).toString().toStdString();
    query.outputType = args.value( QStringLiteral( "output_type" ) ).toString().toStdString();
    query.taskFamily = args.value( QStringLiteral( "task" ) ).toString().toStdString();
    query.modality = args.value( QStringLiteral( "modality" ) ).toString().toStdString();
    query.bandRoles = args.value( QStringLiteral( "band_roles" ) ).toString().toStdString();
    query.memoryPolicy = args.value( QStringLiteral( "memory_policy" ) ).toString().toStdString();
    query.costClass = args.value( QStringLiteral( "cost_class" ) ).toString().toStdString();
    query.largeRasterSafeOnly = args.value( QStringLiteral( "large_raster_safe" ) ).toBool();
    if ( args.contains( QStringLiteral( "gpu" ) ) ) query.gpu = args.value( QStringLiteral( "gpu" ) ).toBool();
    if ( args.contains( QStringLiteral( "temporal" ) ) ) query.temporal = args.value( QStringLiteral( "temporal" ) ).toBool();
    if ( args.contains( QStringLiteral( "deterministic" ) ) ) query.deterministic = args.value( QStringLiteral( "deterministic" ) ).toBool();
    if ( args.contains( QStringLiteral( "category" ) ) )
    {
      query.category = tool_catalog::tryParseToolCategory( args.value( QStringLiteral( "category" ) ).toString().toStdString() );
      if ( !query.category ) return errorResult( QStringLiteral( "invalid_category" ), AgentToolHost::tr( "Unknown tool category." ) );
    }
    const int cursor = args.value( QStringLiteral( "cursor" ) ).toInt( 0 );
    const int limit = args.value( QStringLiteral( "limit" ) ).toInt( 0 );
    if ( cursor < 0 || limit < 0 ) return errorResult( QStringLiteral( "invalid_page" ), AgentToolHost::tr( "Page bounds must be nonnegative." ) );
    QJsonArray selected;
    for ( const auto &tool : tool_catalog::AgentToolCatalog::instance().searchTools( query ) )
    {
      const QString canonical = QString::fromStdString( tool.name );
      if ( !run->catalog.tools.contains( canonical ) ) continue;
      for ( const auto &value : run->catalog.snapshot.tools )
      {
        auto entry = value.toObject();
        if ( entry.value( QStringLiteral( "canonicalId" ) ) != canonical ) continue;
        entry.insert( QStringLiteral( "category" ), QString::fromStdString( tool_catalog::toolCategoryToString( tool.category ) ) );
        if ( args.value( QStringLiteral( "compact" ) ).toBool( true ) ) entry.remove( QStringLiteral( "parameters" ) );
        selected.append( entry );
        break;
      }
    }
    QJsonArray tools;
    const int end = limit == 0 ? static_cast<int>( selected.size() )
                              : static_cast<int>( std::min<qint64>( selected.size(), static_cast<qint64>( cursor ) + limit ) );
    for ( int index = cursor; index < end; ++index ) tools.append( selected[index] );
    return { { QStringLiteral( "status" ), QStringLiteral( "success" ) }, { QStringLiteral( "tools" ), tools },
             { QStringLiteral( "total" ), selected.size() }, { QStringLiteral( "nextCursor" ), end < selected.size() ? end : -1 },
             { QStringLiteral( "toolRevision" ), run->revision } };
  }

  void execute( const std::shared_ptr<Run> &run, const std::shared_ptr<Call> &call )
  {
    if ( run->cancelling || call->terminal ) return;
    const QString validation = validate( run, call );
    if ( !validation.isEmpty() ) { complete( run, call, errorResult( validation, AgentToolHost::tr( "Tool request no longer matches current native authority." ) ) ); return; }
    update( run, call, QStringLiteral( "running" ) );
    const auto self = shared_from_this();
    const auto function = call->envelope.value( QStringLiteral( "function" ) ).toObject();
    const QString id = function.value( QStringLiteral( "name" ) ).toString();
    const Json::Value args = ToolCallDispatcher::argumentsFor( toCpp( call->envelope ) );
    try
    {
      if ( !call->plan.isEmpty() )
      {
        call->submitting = true;
        call->pipelineId = workflow->executeAgentPlanAsync( toCpp( call->plan ), [self, run, call]( const Json::Value &result ) {
          call->result = toQt( result );
          call->resultReady = true;
          self->update( run, call, QStringLiteral( "awaiting_commit" ) );
          self->reconcile( run, call );
        }, keeper );
        call->submitting = false;
        if ( call->pipelineId > 0 ) run->pipelines.insert( call->pipelineId );
        else if ( !call->resultReady ) { call->result = errorResult( QStringLiteral( "pipeline_submission_failed" ), AgentToolHost::tr( "Native workflow submission failed." ) ); call->resultReady = true; }
        reconcile( run, call );
        return;
      }
      if ( metaDiscovery( id ) ) { complete( run, call, discovery( id, toQt( args ), run ) ); return; }
      if ( const auto spatial = spatial_tools::SpatialToolRegistry::instance().find( id.toStdString() ); spatial && readDiscovery( id ) )
      {
        const auto outcome = ( *spatial )->execute( args );
        QJsonObject result = toQt( outcome.toJson() );
        result.insert( QStringLiteral( "status" ), outcome.success ? QStringLiteral( "success" ) : QStringLiteral( "error" ) );
        if ( !outcome.success ) result.insert( QStringLiteral( "errorMessage" ), QString::fromStdString( outcome.error ) );
        complete( run, call, result );
        return;
      }
      QString error;
      long nativeId = -1;
      call->submitting = true;
      const bool submitted = dispatcher->submit( toCpp( call->envelope ), [self, run, call]( const Json::Value &result ) {
        call->result = toQt( result );
        call->resultReady = true;
        self->update( run, call, QStringLiteral( "awaiting_commit" ) );
        self->reconcile( run, call );
      }, &error, &nativeId );
      call->submitting = false;
      if ( !submitted ) { complete( run, call, errorResult( QStringLiteral( "native_submission_failed" ), error ) ); return; }
      if ( nativeId > 0 && nativeId != 9000001 )
      {
        call->taskId = nativeId;
        call->nativeTasks.insert( nativeId );
        run->tasks.insert( nativeId );
        update( run, call, run->cancelling ? QStringLiteral( "cancelling" ) : QStringLiteral( "running" ), QStringLiteral( "task-%1" ).arg( nativeId ) );
        if ( run->cancelling && center ) center->cancelTask( nativeId );
      }
      reconcile( run, call );
    }
    catch ( const std::exception & )
    {
      call->submitting = false;
      if ( call->taskId > 0 || call->pipelineId > 0 ) cancel( run, QStringLiteral( "native_dispatch_error" ) );
      else complete( run, call, errorResult( QStringLiteral( "native_dispatch_error" ), AgentToolHost::tr( "Native tool dispatch failed." ) ) );
    }
  }

  void drain( const std::shared_ptr<Run> &run )
  {
    if ( run->idle || run->cancelling || !run->active.isEmpty() ) { maybeIdle( run ); return; }
    if ( run->queue.isEmpty() ) { maybeIdle( run ); return; }
    run->active = run->queue.dequeue();
    const auto call = run->calls.value( run->active );
    const QString validation = validate( run, call );
    if ( !validation.isEmpty() ) { complete( run, call, errorResult( validation, AgentToolHost::tr( "Native tool validation refused the request." ) ) ); return; }
    if ( needsApproval( call ) )
    {
      prepareApproval( run, call );
      return;
    }
    execute( run, call );
  }
};

AgentToolHost::AgentToolHost( sicnu::TaskCenter &taskCenter, QObject *parent ) : QObject( parent )
{
  if ( !sicnu::TaskCenter::bindSharedInstance( taskCenter ) )
    throw std::logic_error( "AgentToolHost requires the GUI TaskCenter authority" );
  m_impl = std::make_shared<Impl>( this, taskCenter );
  m_impl->initialize();
}

AgentToolHost::~AgentToolHost()
{
  const auto self = m_impl;
  self->host.clear();
  if ( self->busy() ) self->detachedRetention = self;
  for ( const auto &run : self->runs ) self->cancel( run, QStringLiteral( "host_closed" ) );
  if ( !self->busy() && self->keeper ) self->keeper->deleteLater();
}

bool AgentToolHost::setContext( data::DataManager *manager, ToolCallDispatcher::CanvasActionHandler canvasHandler,
                                ToolCallDispatcher::InteractionActionHandler interactionHandler )
{
  const auto self = m_impl;
  if ( !self->ownerThread() || ( manager && manager->thread() != thread() ) ) return false;
  if ( self->busy() ) return false;
  for ( const auto &connection : self->managerConnections ) QObject::disconnect( connection );
  self->managerConnections.clear();
  self->manager = manager;
  self->contextValid->store( false );
  self->contextValid = std::make_shared<std::atomic_bool>( true );
  self->managerIdentity.clear();
  ++self->contextRevision;
  self->dispatcher->setDataManager( manager );
  const auto nativeCommitter = self->dispatcher->outputCommitterHandler();
  self->dispatcher->setOutputCommitterHandler(
    [valid = self->contextValid, nativeCommitter]( const sicnu::AlgorithmTaskInfo &info,
      std::string &path, std::string &error, std::string &asset ) {
      if ( !valid->load() || !nativeCommitter )
      {
        ToolCallDispatcher::discardUncommittedPythonOutput( info );
        error = "The project changed before output publication";
        return false;
      }
      return nativeCommitter( info, path, error, asset );
    } );
  self->workflow->setDataManager( manager );
  self->dispatcher->setCanvasActionHandler( std::move( canvasHandler ) );
  self->dispatcher->setInteractionActionHandler( interactionHandler ? std::move( interactionHandler )
    : ToolCallDispatcher::InteractionActionHandler( []( const std::string &id, const Json::Value &args ) { return InteractionToolRegistry::instance().execute( id, args ); } ) );
  self->dispatcher->setOutputVerificationHandler( []( const QString &path, const QString &kind ) {
    const auto verification = OutputVerifier().verify( path, kind );
    Json::Value result( Json::objectValue );
    result["ok"] = verification.ok; result["kind"] = verification.kind.toStdString(); result["summary"] = verification.summary;
    result["issues"] = Json::Value( Json::arrayValue ); result["warnings"] = Json::Value( Json::arrayValue );
    for ( const auto &issue : verification.issues ) result["issues"].append( issue.toStdString() );
    for ( const auto &warning : verification.warnings ) result["warnings"].append( warning.toStdString() );
    return result;
  } );
  if ( manager )
  {
    const char *identityProperty = "_sicnu_agent_writer_identity";
    self->managerIdentity = manager->property( identityProperty ).toString();
    if ( self->managerIdentity.isEmpty() )
    {
      self->managerIdentity = QUuid::createUuid().toString( QUuid::WithoutBraces );
      manager->setProperty( identityProperty, self->managerIdentity );
    }
    const std::weak_ptr<Impl> weak = self;
    const auto changed = [weak] { if ( const auto current = weak.lock() ) ++current->contextRevision; };
    self->managerConnections.append( connect( manager, &data::DataManager::assetAdded, self->keeper, changed ) );
    self->managerConnections.append( connect( manager, &data::DataManager::assetChanged, self->keeper, changed ) );
    self->managerConnections.append( connect( manager, &data::DataManager::assetRemoved, self->keeper, changed ) );
    self->managerConnections.append( connect( manager, &QObject::destroyed, self->keeper, [weak] {
      if ( const auto current = weak.lock() )
      {
        current->manager.clear();
        current->contextValid->store( false );
        current->dispatcher->setDataManager( nullptr );
        current->workflow->setDataManager( nullptr );
        ++current->contextRevision;
        for ( const auto &run : current->runs ) current->cancel( run, QStringLiteral( "workspace_closed" ) );
      }
    } ) );
  }
  return true;
}

ToolCatalogSnapshot AgentToolHost::catalogSnapshot() const
{
  if ( !m_impl->ownerThread() ) { ToolCatalogSnapshot result; result.error = QStringLiteral( "wrong_host_thread" ); return result; }
  return currentCatalog().snapshot;
}

void AgentToolHost::invalidateProject()
{
  const auto self = m_impl;
  if ( !self->ownerThread() ) return;
  self->contextValid->store( false );
  ++self->contextRevision;
  self->workflow->setDataManager( nullptr );
  for ( const auto &run : self->runs ) self->cancel( run, QStringLiteral( "project_changed" ) );
}

bool AgentToolHost::beginRun( const QString &id, const QString &revision )
{
  const auto self = m_impl;
  if ( !self->ownerThread() || !self->manager || !self->contextValid->load() || id.isEmpty() || id.size() > 128 || self->runs.contains( id )
       || self->retiredRunIds.contains( id ) || self->busy() ) return false;
  const Catalog catalog = currentCatalog();
  if ( !catalog.snapshot.error.isEmpty() || ( !revision.isEmpty() && revision != catalog.snapshot.revision ) ) return false;
  for ( auto it = self->runs.begin(); it != self->runs.end(); )
  {
    if ( !it.value()->idle ) { ++it; continue; }
    self->retiredRunIds.insert( it.key() );
    self->retiredRunOrder.enqueue( it.key() );
    it = self->runs.erase( it );
  }
  // Retain identities, rather than every call payload, across completed runs.
  // Old tool frames still fail as unknown runs after the tombstone bound.
  while ( self->retiredRunOrder.size() > kMaxRememberedRunIds )
    self->retiredRunIds.remove( self->retiredRunOrder.dequeue() );
  {
    QMutexLocker lock( &leaseMutex() );
    const auto existing = leases().constFind( self->managerIdentity );
    if ( existing != leases().cend() && existing->owner ) return false;
    leases().insert( self->managerIdentity, { self->keeper, id } );
  }
  auto run = std::make_shared<Impl::Run>();
  run->id = id; run->revision = catalog.snapshot.revision; run->catalog = catalog;
  run->context = self->context(); run->leaseKey = self->managerIdentity; run->clock.start();
  self->runs.insert( id, run );
  return true;
}

void AgentToolHost::requestTool( const QString &runId, const QJsonObject &envelope, const QString &revision )
{
  const auto self = m_impl;
  if ( !self->ownerThread() )
  {
    if ( self->keeper ) QMetaObject::invokeMethod( self->keeper, [guard = QPointer<AgentToolHost>( this ), runId, envelope, revision] {
      if ( guard ) guard->requestTool( runId, envelope, revision );
    }, Qt::QueuedConnection );
    return;
  }
  const QString callId = envelope.value( QStringLiteral( "id" ) ).toString();
  const auto run = self->runs.value( runId );
  const auto reject = [&]( const QString &code ) { emit toolCompleted( runId, callId, errorResult( code, AgentToolHost::tr( "Native tool host refused the request." ) ) ); };
  if ( !run || callId.isEmpty() || callId.size() > 128 ) { reject( QStringLiteral( "unknown_run_or_call" ) ); return; }
  QJsonObject normalized = envelope;
  Json::Value parsed = toCpp( envelope );
  const Json::Value args = ToolCallDispatcher::argumentsFor( parsed );
  const QJsonObject function = envelope.value( QStringLiteral( "function" ) ).toObject();
  const QString name = function.value( QStringLiteral( "name" ) ).toString( envelope.value( QStringLiteral( "name" ) ).toString() );
  const QString canonical = run->catalog.mappedIds.value( name );
  normalized = { { QStringLiteral( "id" ), callId }, { QStringLiteral( "type" ), QStringLiteral( "function" ) },
                 { QStringLiteral( "function" ), QJsonObject{ { QStringLiteral( "name" ), canonical },
                                                             { QStringLiteral( "arguments" ), toQt( args ) } } } };
  const QString fingerprint = digest( normalized );
  if ( const auto existing = run->calls.value( callId ) )
  {
    if ( existing->fingerprint != fingerprint ) { self->cancel( run, QStringLiteral( "call_id_conflict" ) ); return; }
    if ( existing->terminal ) emit toolCompleted( runId, callId, existing->result );
    else emit toolUpdated( runId, callId, existing->state, AgentToolHost::tr( "Existing request retained; no replay." ) );
    return;
  }
  if ( run->cancelling || run->conversationFinished || run->idle ) { reject( QStringLiteral( "run_closed" ) ); return; }
  if ( revision != run->revision ) { reject( QStringLiteral( "tool_catalog_changed" ) ); return; }
  if ( canonical.isEmpty() || !args.isObject() ) { reject( QStringLiteral( "tool_not_in_catalog_or_invalid_arguments" ) ); return; }
  if ( QJsonDocument( normalized ).toJson( QJsonDocument::Compact ).size() > kMaxArgumentBytes ) { reject( QStringLiteral( "tool_arguments_too_large" ) ); return; }
  if ( run->queue.size() >= kMaxQueuedCalls || run->calls.size() >= kMaxCallsPerRun ) { reject( QStringLiteral( "tool_queue_full" ) ); return; }
  auto call = std::make_shared<Impl::Call>();
  call->id = callId; call->envelope = normalized; call->fingerprint = fingerprint;
  if ( canonical == QLatin1String( "run_workflow" ) )
  {
    if ( !args["pipeline"].isObject() || args.size() != 1 )
    { reject( QStringLiteral( "workflow_requires_pipeline_only" ) ); return; }
    sicnu::workflow::WorkflowDefinition definition;
    std::string parseError;
    if ( !sicnu::workflow::workflowDefinitionFromJson( args["pipeline"], definition, parseError ) )
    { reject( QStringLiteral( "invalid_plan_steps" ) ); return; }
    for ( auto &step : definition.steps )
    {
      const QString stepId = run->catalog.mappedIds.value( QString::fromStdString( step.operatorId ) );
      if ( stepId.isEmpty() ) { reject( QStringLiteral( "plan_tool_not_in_catalog" ) ); return; }
      step.operatorId = stepId.toStdString();
    }
    call->plan = toQt( sicnu::workflow::workflowDefinitionToJson( definition ) );
  }
  else if ( args.isMember( "steps" ) )
  { reject( QStringLiteral( "workflow_requires_run_workflow" ) ); return; }
  run->calls.insert( callId, call ); run->queue.enqueue( callId );
  self->update( run, call, QStringLiteral( "queued" ) );
  self->drain( run );
}

void AgentToolHost::resolveApproval( const QString &token, bool approved )
{
  const auto self = m_impl;
  if ( !self->ownerThread() )
  {
    if ( self->keeper ) QMetaObject::invokeMethod( self->keeper, [guard = QPointer<AgentToolHost>( this ), token, approved] { if ( guard ) guard->resolveApproval( token, approved ); }, Qt::QueuedConnection );
    return;
  }
  const auto it = self->approvals.find( token );
  if ( it == self->approvals.end() ) return;
  const auto identity = it.value(); self->approvals.erase( it );
  const auto run = self->runs.value( identity.first );
  const auto call = run ? run->calls.value( identity.second ) : nullptr;
  if ( !call || call->terminal || run->cancelling || call->approvalToken != token ) return;
  const QString validation = self->validate( run, call, true );
  call->approvalToken.clear(); self->resumeClock( run );
  if ( !approved ) { self->complete( run, call, errorResult( QStringLiteral( "approval_rejected" ), AgentToolHost::tr( "The operator rejected this tool request." ) ) ); return; }
  if ( !validation.isEmpty() ) { self->complete( run, call, errorResult( validation, AgentToolHost::tr( "Approval expired because its native context or schema changed." ) ) ); return; }
  self->executeApproved( run, call );
}

void AgentToolHost::cancelRun( const QString &runId, const QString &reason )
{
  const auto self = m_impl;
  if ( !self->ownerThread() )
  {
    if ( self->keeper ) QMetaObject::invokeMethod( self->keeper, [guard = QPointer<AgentToolHost>( this ), runId, reason] { if ( guard ) guard->cancelRun( runId, reason ); }, Qt::QueuedConnection );
    return;
  }
  if ( const auto run = self->runs.value( runId ) ) self->cancel( run, reason );
}

void AgentToolHost::finishConversation( const QString &runId )
{
  const auto self = m_impl;
  if ( !self->ownerThread() )
  {
    if ( self->keeper ) QMetaObject::invokeMethod( self->keeper, [guard = QPointer<AgentToolHost>( this ), runId] { if ( guard ) guard->finishConversation( runId ); }, Qt::QueuedConnection );
    return;
  }
  if ( const auto run = self->runs.value( runId ) ) { run->conversationFinished = true; self->maybeIdle( run ); }
}

bool AgentToolHost::isRunIdle( const QString &runId ) const
{
  const auto run = m_impl->runs.value( runId );
  return !run || run->idle;
}
bool AgentToolHost::hasOwnedWork() const { return m_impl->busy(); }
QSet<long> AgentToolHost::taskIds( const QString &runId ) const
{
  const auto run = m_impl->runs.value( runId ); return run ? run->tasks : QSet<long>{};
}
QSet<long> AgentToolHost::pipelineIds( const QString &runId ) const
{
  const auto run = m_impl->runs.value( runId ); return run ? run->pipelines : QSet<long>{};
}
QString AgentToolHost::callState( const QString &runId, const QString &callId ) const
{
  const auto run = m_impl->runs.value( runId );
  const auto call = run ? run->calls.value( callId ) : nullptr;
  return call ? call->state : QString();
}

} // namespace sicnu::agent::runtime
